/* Execute the production translators, allocator and x64 backend. Share the
   benchmark's RAM/code arena so correctness and timing exercise the same JIT,
   with explicit stack, fault and ABI assertions. */
#define CPU_FASTPATH_TEST
#define main cpu_microbench_main
#include "cpu_microbench.c"
#undef main

typedef void (*jit_fn)(void);
static unsigned checks;
int stack32;

static ir_data_t *begin_test(const char *name)
{
    active_case = name;
    bench_case_t empty = {0};
    reset_state(&empty, 0);
    test_fault_access = 0;
    helper_calls = 0;
    verifying = 1;
    next_chunk = first_codegen_chunk;
    memset(&bench_block, 0, sizeof(bench_block));
    bench_block.flags = CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP;
    start_code();
    codegen_reg_reset();
    codegen_flags_changed = 0;
    return codegen_ir_init();
}

static jit_fn finish_test(ir_data_t *ir)
{
    codegen_ir_compile(ir, &bench_block);
    flush_code();
    return (jit_fn) bench_block.data;
}

#ifdef _WIN64
static uint8_t sentinel[160], observed[160];

/* A generated caller avoids compiler/inline-asm assumptions about Win64
   shadow space and XMM clobbers. Raw emitters must still save the full ABI. */
static jit_fn abi_caller(jit_fn child)
{
    uint8_t *entry = start_code();
    codegen_backend_prologue(&bench_block);
    for (int r = 6; r <= 15; r++)
        host_x86_MOVDQU_XREG_ABS(&bench_block, r, sentinel + 16 * (r - 6));
    host_x86_CALL(&bench_block, child);
    for (int r = 6; r <= 15; r++)
        host_x86_MOVDQU_ABS_XREG(&bench_block, observed + 16 * (r - 6), r);
    codegen_backend_epilogue(&bench_block);
    flush_code();
    return (jit_fn) entry;
}

static void test_xmm_abi(void)
{
    for (unsigned i = 0; i < sizeof(sentinel); i++) sentinel[i] = i * 17 + 3;
    /* Include no XMM, read-only allocation, all 15 allocatable XMM registers,
       spilling, joins, C helpers, memory faults and the shared branch exit. */
    for (unsigned live = 0; live <= 16; live++)
        for (int path = 0; path < 6; path++) {
            ir_data_t *ir = begin_test("Win64 XMM preservation");
            for (unsigned r = 0; r < live; r++) {
                int reg = r < 8 ? IREG_XMM(r) : IREG_MM(r - 8);
                uop_PADDD(ir, reg, reg, reg);
            }
            if (path == 1 || path == 2 || path == 5) {
                uop_MOV_IMM(ir, IREG_eaaddr, 0x4000);
                /* An abort must also write back the old SIMD destination,
                   including one assigned beyond bit 15 of the host mask. */
                int dest = path == 5 ? (live > 8 ? IREG_MM(0) : IREG_XMM(0)) : IREG_EAX;
                uop_MEM_LOAD_REG(ir, dest, IREG_DS_base, IREG_eaaddr);
            }
            if (path == 3) uop_JMP(ir, codegen_exit_rout);
            if (path == 4) {
                int jump = uop_CMP_IMM_JNZ_DEST(ir, IREG_EAX, 0);
                uop_MOV_IMM(ir, IREG_ECX, 123);
                uop_set_jump_dest(ir, jump);
            }
            for (unsigned r = 0; r < live; r++) {
                int reg = r < 8 ? IREG_XMM(r) : IREG_MM(r - 8);
                uop_PADDD(ir, reg, reg, reg);
            }
            jit_fn child = finish_test(ir);
#ifdef CODEGEN_BACKEND_HAS_SELECTIVE_XMM
            uint16_t used = codegen_win64_xmm_used;
            CHECK(live || path == 5 || !used);
            if (live == 16) CHECK((used & 0xffc0) == 0xffc0);
#endif
            jit_fn entry = abi_caller(child);
            cpu_state_t before = cpu_state;
            int fault = path == 2 || path == 5;
            readlookup2[4] = path == 1 || fault ? (uintptr_t) -1 : (uintptr_t) memory;
            test_fault_access = fault ? 1 : 0;
            entry();
            CHECK(!memcmp(sentinel, observed, sizeof(sentinel)));
            CHECK(cpu_state.abrt == (fault ? 14 : 0));
            unsigned factor = fault || path == 3 ? 2 : 4;
            for (unsigned r = 0; r < live; r++)
                for (unsigned lane = 0; lane < (r < 8 ? 4u : 2u); lane++) {
                    uint32_t actual = r < 8 ? cpu_state.XMM[r].l[lane] : cpu_state.MM[r - 8].l[lane];
                    uint32_t expected = (r < 8 ? before.XMM[r].l[lane] : before.MM[r - 8].l[lane]) * factor;
                    if (actual != expected)
                        fatal("live=%u path=%d reg=%u lane=%u: %08x != %08x\n", live, path, r, lane, actual, expected);
                }
            readlookup2[4] = (uintptr_t) memory;
            checks++;
        }
    /* Read-only SIMD sources also clobber a host XMM when loaded. */
    ir_data_t *ir = begin_test("Win64 read-only XMM allocation");
    uop_MOV_IMM(ir, IREG_eaaddr, 0x4000);
    uop_MEM_STORE_REG(ir, IREG_DS_base, IREG_eaaddr, IREG_XMM(0));
    jit_fn child = finish_test(ir);
#ifdef CODEGEN_BACKEND_HAS_SELECTIVE_XMM
    CHECK(codegen_win64_xmm_used);
#endif
    abi_caller(child)();
    CHECK(!memcmp(sentinel, observed, sizeof(sentinel)));
    CHECK(!memcmp(memory + 0x4000, &cpu_state.XMM[0], 16));
    checks++;
    /* Force the body and restore sequence into later allocator chunks. */
    ir = begin_test("Win64 multi-chunk epilogue");
    for (unsigned i = 0; i < 192; i++) {
        uop_PADDD(ir, IREG_XMM(i % 8), IREG_XMM(i % 8), IREG_XMM(i % 8));
        uop_MEM_STORE_REG(ir, IREG_DS_base, IREG_EAX, IREG_XMM(i % 8));
    }
    child = finish_test(ir);
    CHECK(next_chunk > first_codegen_chunk + 1);
    EAX = 0x4000;
    abi_caller(child)();
    CHECK(!memcmp(sentinel, observed, sizeof(sentinel)));
    checks++;
}
#endif

static void test_ret_imm(void)
{
    const uint32_t stacks[] = {0x4000, 0xfffc, 0x7654fffc, 0x4fff};
    const uint16_t immediates[] = {0, 4, 0x8000, 0xffff};
    for (int wide = 0; wide <= 1; wide++)
        for (int dynamic = 0; dynamic <= 1; dynamic++)
            for (int split = 0; split <= 1; split++)
                for (unsigned s = 0; s < 4; s++)
                    for (unsigned n = 0; n < 4; n++) {
                        if (wide && s == 2) continue;
                        ir_data_t *ir = begin_test("RET imm16 / operand32");
                        uint32_t pc = split ? 0xfff : 0x101;
                        memcpy(instruction_bytes + pc, &immediates[n], 2);
                        if (dynamic) bench_block.flags |= CODEBLOCK_NO_IMMEDIATES;
                        stack32 = wide;
                        cpu_state.oldpc = pc - 1;
                        CHECK(ropRET_imm_32(&bench_block, ir, 0xc2, 0, 0x300, pc) == UINT32_MAX);
                        jit_fn entry = finish_test(ir);
                        for (int fault = 0; fault < 3; fault++) {
                            uint16_t imm = dynamic ? immediates[(n + 1) % 4] : immediates[n];
                            if (dynamic) memcpy(instruction_bytes + pc, &imm, 2);
                            ESP = stacks[s];
                            uint32_t address = wide ? ESP : SP;
                            uint32_t target = 0xdeadbeef;
                            memcpy(memory + address, &target, 4);
                            cpu_state.pc = 0x1234;
                            cpu_state.abrt = 0;
                            cpu_state.seg_ss.limit_high = fault == 1 ? address + 2 : UINT32_MAX;
                            readlookup2[address >> 12] = fault == 2 ? (uintptr_t) -1 : (uintptr_t) memory;
                            test_fault_access = fault == 2 ? 1 : 0;
                            entry();
                            uint32_t expected_sp = wide ? stacks[s] + 4 + imm
                                : (stacks[s] & 0xffff0000) | (uint16_t) (stacks[s] + 4 + imm);
                            CHECK(cpu_state.abrt == (fault == 1 ? 12 : fault == 2 ? 14 : 0));
                            CHECK(ESP == (fault ? stacks[s] : expected_sp));
                            CHECK(cpu_state.pc == (fault ? 0x1234 : target));
                            CHECK(cpu_state.oldpc == pc - 1);
                            readlookup2[address >> 12] = (uintptr_t) memory;
                            checks++;
                        }
                    }
}

int main(void)
{
#ifdef _WIN32
    code_memory = VirtualAlloc(NULL, CODE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    code_memory = mmap(NULL, CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(code_memory != MAP_FAILED);
#endif
    CHECK(code_memory);
    ram = memory;
    memset(readlookup2, 0xff, sizeof(readlookup2));
    memset(writelookup2, 0xff, sizeof(writelookup2));
    for (unsigned i = 0; i < RAM_SIZE / 4096; i++) readlookup2[i] = writelookup2[i] = (uintptr_t) memory;
    prepare_codegen();
    /* Exception dispatch is outside this fixture; retain the real checks and
       shared exit while recording the exception vector for assertions. */
    codegen_ss_rout = start_code();
    host_x86_MOV8_ABS_IMM(&bench_block, &cpu_state.abrt, 12);
    host_x86_JMP(&bench_block, codegen_exit_rout);
    first_codegen_chunk = next_chunk;
#ifdef _WIN64
    test_xmm_abi();
#endif
    test_ret_imm();
    printf("cpu_fastpath_test: %u executions passed\n", checks);
    return 0;
}
