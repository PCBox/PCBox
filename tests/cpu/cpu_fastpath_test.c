/* Execute the production translators, allocator and x64 backend. Share the
   benchmark's RAM/code arena so correctness and timing exercise the same JIT,
   but use independent arithmetic oracles and explicit fault/ABI assertions. */
#define CPU_FASTPATH_TEST
#define main cpu_microbench_main
#include "cpu_microbench.c"
#undef main

typedef void (*jit_fn)(void);
typedef uint32_t (*translator)(codeblock_t *, ir_data_t *, uint8_t, uint32_t, uint32_t, uint32_t);
static unsigned checks;
static uint32_t random_state = 0x9e3779b9;
int stack32;
void loadcsjmp(uint16_t seg, uint32_t old_pc) { (void) seg; (void) old_pc; fatal("unexpected far jump\n"); }

static uint32_t random_u32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

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

/* Do not derive expected carry using the same bit trick as the translator. */
static unsigned prepare_flags(int op, unsigned bits, uint32_t a, uint32_t b, unsigned carry)
{
    uint32_t mask = UINT32_MAX >> (32 - bits);
    a &= mask;
    b &= mask;
    cpu_state.flags_op = op;
    cpu_state.flags_op1 = a;
    cpu_state.flags_op2 = b;
    cpu_state.flags = 0x202 | carry;
    if (op == FLAGS_ADD8 || op == FLAGS_ADD16 || op == FLAGS_ADD32) {
        cpu_state.flags_res = (a + b) & mask;
        return ((uint64_t) a + b) > mask;
    }
    if (op == FLAGS_SUB8 || op == FLAGS_SUB16 || op == FLAGS_SUB32) {
        cpu_state.flags_res = (a - b) & mask;
        return a < b;
    }
    if (op == FLAGS_ADC32) {
        cpu_state.flags_res = a + b + carry;
        return (uint64_t) a + b + carry > UINT32_MAX;
    }
    if (op == FLAGS_SBC32) {
        cpu_state.flags_res = a - b - carry;
        return (uint64_t) a < (uint64_t) b + carry;
    }
    cpu_state.flags_res = a;
    return op == FLAGS_ZN8 || op == FLAGS_ZN16 || op == FLAGS_ZN32 ? 0 : carry;
}

static void test_conditions(void)
{
    const int ops[] = { FLAGS_ADD8, FLAGS_ADD16, FLAGS_ADD32, FLAGS_SUB8, FLAGS_SUB16, FLAGS_SUB32,
                       FLAGS_ZN8, FLAGS_ZN16, FLAGS_ZN32, FLAGS_INC8, FLAGS_INC16, FLAGS_INC32,
                       FLAGS_DEC8, FLAGS_DEC16, FLAGS_DEC32, FLAGS_ADC32, FLAGS_SBC32, FLAGS_UNKNOWN };
    const uint32_t edge[] = {0, 1, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff,
                            0x10000, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff};
    for (unsigned op = 0; op < sizeof(ops) / sizeof(ops[0]); op++)
        for (int known = 0; known <= 1; known++) {
            ir_data_t *ir = begin_test("carry/conditions");
            codegen_flags_changed = known;
            cpu_state.flags_op = ops[op];
            setcc_gen_B(ir, 0);
            uop_MOV(ir, IREG_EAX, IREG_temp0);
            setcc_gen_B(ir, 1);
            uop_MOV(ir, IREG_EBX, IREG_temp0);
            setcc_gen_BE(ir, 0);
            uop_MOV(ir, IREG_ECX, IREG_temp0);
            setcc_gen_BE(ir, 1);
            uop_MOV(ir, IREG_EDX, IREG_temp0);
            jit_fn entry = finish_test(ir);
            unsigned bits = op < 15 ? 8u << (op % 3) : 32;
            unsigned count = bits == 8 && op < 6 ? 65536 : 8192;
            for (unsigned i = 0; i < count; i++) {
                uint32_t a = bits == 8 && op < 6 ? i >> 8 : i < 196 ? edge[i / 14] : random_u32();
                uint32_t b = bits == 8 && op < 6 ? i & 255 : i < 196 ? edge[i % 14] : random_u32();
                unsigned carry = prepare_flags(ops[op], bits, a, b, i & 1);
                uint32_t old_a = cpu_state.flags_op1, old_b = cpu_state.flags_op2, old_r = cpu_state.flags_res;
                uint16_t old_flags = cpu_state.flags;
                unsigned zero = ops[op] == FLAGS_UNKNOWN ? !!(old_flags & Z_FLAG) : old_r == 0;
                entry();
                if (EAX != carry || EBX != !carry || ECX != (carry || zero) || EDX != !(carry || zero))
                    fatal("op=%d known=%d a=%08x b=%08x: got %u/%u/%u/%u, CF=%u ZF=%u\n",
                          ops[op], known, a, b, EAX, EBX, ECX, EDX, carry, zero);
                CHECK(cpu_state.flags_op == ops[op] && cpu_state.flags == old_flags);
                CHECK(cpu_state.flags_op1 == old_a && cpu_state.flags_op2 == old_b && cpu_state.flags_res == old_r);
                checks++;
            }
        }
}

static void test_adc_sbb(void)
{
    const translator reg_ops[2][3] = {{ropADC_b_rm, ropADC_w_rm, ropADC_l_rm},
                                      {ropSBB_b_rm, ropSBB_w_rm, ropSBB_l_rm}};
    const translator group_ops[] = {rop80, rop81_w, rop81_l};
    /* Every scratch-register arrangement: register/high byte, immediate,
       memory, and mutable immediate with a live memory operand. */
    for (int subtract = 0; subtract <= 1; subtract++)
        for (unsigned width = 0; width < 3; width++)
            for (int form = 0; form < 5; form++)
                for (int known = 0; known <= 1; known++) {
                    unsigned bits = 8u << width;
                    uint32_t mask = UINT32_MAX >> (32 - bits);
                    ir_data_t *ir = begin_test("carry/ADC-SBB operands");
                    int high = !width && form == 1;
                    int dynamic = form == 4;
                    int mem = form >= 3;
                    unsigned reg = high ? 7 : 3; /* BH or BL/BX/EBX */
                    if (dynamic) bench_block.flags |= CODEBLOCK_NO_IMMEDIATES;
                    cpu_state.flags_op = FLAGS_SUB32;
                    codegen_flags_changed = known;
                    uint32_t immediate = 0x87654321;
                    memcpy(instruction_bytes + 0x102, &immediate, 4);
                    if (form < 2)
                        reg_ops[subtract][width](&bench_block, ir, 0, 0xc0 | (reg << 3) | 2, 0x300, 0x101);
                    else
                        group_ops[width](&bench_block, ir, 0, (subtract ? 0x18 : 0x10) | (mem ? 0 : 0xc3), 0x300, 0x101);
                    jit_fn entry = finish_test(ir);
                    for (unsigned i = 0; i < 2048; i++) {
                        uint32_t a = random_u32(), b = form < 2 ? random_u32() : immediate;
                        if (dynamic && width) {
                            b = random_u32();
                            memcpy(instruction_bytes + 0x102, &b, 4);
                        }
                        a &= mask;
                        b &= mask;
                        unsigned carry = prepare_flags(FLAGS_SUB32, 32, i & 1 ? 0 : UINT32_MAX, 1, 0);
                        EAX = 0x4000;
                        EBX = high ? (0xa5b6005a | (a << 8)) : ((0xa5b65a00 & ~mask) | a);
                        EDX = b;
                        uint32_t old_ebx = EBX;
                        memcpy(memory + EAX, &a, bits / 8);
                        uint32_t expected = (subtract ? a - b - carry : a + b + carry) & mask;
                        entry();
                        CHECK(!cpu_state.abrt);
                        uint32_t actual = 0;
                        if (mem) memcpy(&actual, memory + EAX, bits / 8);
                        else actual = high ? BH : EBX & mask;
                        if (actual != expected)
                            fatal("sub=%d bits=%u form=%d known=%d a=%x b=%x cf=%u: %x != %x\n",
                                  subtract, bits, form, known, a, b, carry, actual, expected);
                        uint32_t changed = high ? 0xff00 : mask;
                        CHECK(mem ? EBX == old_ebx : (EBX & ~changed) == (old_ebx & ~changed));
                        CHECK(cpu_state.flags_op1 == a && cpu_state.flags_op2 == b && cpu_state.flags_res == expected);
                        CHECK(!!CF_SET() == (subtract ? (uint64_t) a < (uint64_t) b + carry : (uint64_t) a + b + carry > mask));
                        checks++;
                    }
                }
}

static void test_signed_conditions(void)
{
    const uint32_t edge[] = {0, 1, 0x7fffffff, 0x80000000, 0x80000001, 0xfffffffe, 0xffffffff};
    for (int decrement = 0; decrement < 2; decrement++)
        for (int known = 0; known < 2; known++)
            for (int cond = 12; cond < 16; cond++)
                for (int move = 0; move < 3; move++) {
                    ir_data_t *ir = begin_test("signed conditions");
                    cpu_state.flags_op = decrement ? FLAGS_DEC32 : FLAGS_SUB32;
                    codegen_flags_changed = known;
                    if (move) bench_cmov[move - 1][cond](&bench_block, ir, 0, 0xda, 0x300, 0x101);
                    else bench_setcc[cond](&bench_block, ir, 0, 0xc3, 0x300, 0x101);
                    jit_fn entry = finish_test(ir);
                    for (unsigned i = 0; i < 4145; i++) {
                        uint32_t a = i < 49 ? edge[i / 7] : random_u32();
                        uint32_t b = decrement ? 1 : i < 49 ? edge[i % 7] : random_u32();
                        prepare_flags(FLAGS_SUB32, 32, a, b, 1);
                        cpu_state.flags_op = decrement ? FLAGS_DEC32 : FLAGS_SUB32;
                        EDX = 0xabcdef12; EBX = 0x12345678;
                        unsigned condition = (cond < 14 ? (int32_t) a < (int32_t) b : (int32_t) a <= (int32_t) b) ^ (cond & 1);
                        uint32_t expected = move == 2 ? (condition ? EDX : EBX)
                            : move == 1 ? (condition ? (EBX & 0xffff0000) | (EDX & 0xffff) : EBX)
                            : (EBX & 0xffffff00) | condition;
                        entry();
                        CHECK(EBX == expected && EDX == 0xabcdef12);
                        CHECK(cpu_state.flags_op1 == a && cpu_state.flags_op2 == b && cpu_state.flags_res == a - b);
                        CHECK(cpu_state.flags & C_FLAG);
                        checks++;
                    }
                }
#ifdef CODEGEN_BACKEND_HAS_CMP_SLT
    for (int d = 0; d < CODEGEN_HOST_REGS; d++)
        for (int a = 0; a < CODEGEN_HOST_REGS; a++)
            for (int b = 0; b < CODEGEN_HOST_REGS; b++)
                for (int invert = 0; invert < 2; invert++) {
                    begin_test("signed compare aliases");
                    int dest = codegen_host_reg_list[d].reg;
                    int lhs = codegen_host_reg_list[a].reg, rhs = codegen_host_reg_list[b].reg;
                    codegen_backend_prologue(&bench_block);
                    host_x86_MOV32_REG_IMM(&bench_block, lhs, 0x80000000u);
                    host_x86_MOV32_REG_IMM(&bench_block, rhs, 0x7fffffffu);
                    uop_t op = {.dest_reg_a_real = dest | IREG_SIZE_L, .src_reg_a_real = lhs | IREG_SIZE_L,
                                .src_reg_b_real = rhs | IREG_SIZE_L, .imm_data = invert};
                    codegen_CMP_SLT(&bench_block, &op);
                    host_x86_MOV32_ABS_REG(&bench_block, &EAX, dest);
                    codegen_backend_epilogue(&bench_block);
                    flush_code();
                    ((jit_fn) bench_block.data)();
                    CHECK(EAX == (unsigned) ((lhs != rhs) ^ invert));
                    checks++;
                }
#endif
}

static void test_carry_faults(void)
{
    for (int subtract = 0; subtract <= 1; subtract++) {
        ir_data_t *ir = begin_test("carry/memory fault ordering");
        cpu_state.flags_op = FLAGS_SUB32;
        cpu_state.oldpc = 0x100;
        codegen_flags_changed = 1;
        uint32_t imm = 7;
        memcpy(instruction_bytes + 0x102, &imm, 4);
        rop81_l(&bench_block, ir, 0x81, subtract ? 0x18 : 0x10, 0x300, 0x101);
        jit_fn entry = finish_test(ir);
        for (int fault = 1; fault <= 2; fault++) {
            uint32_t operand = 0x12345678;
            memcpy(memory + 0x4000, &operand, 4);
            prepare_flags(FLAGS_SUB32, 32, 0, UINT32_MAX, 0);
            cpu_state.abrt = 0;
            EAX = 0x4000;
            readlookup2[4] = writelookup2[4] = (uintptr_t) -1;
            test_fault_access = fault;
            entry();
            CHECK(cpu_state.abrt == 14 && cpu_state.oldpc == 0x100);
            CHECK(cpu_state.flags_op == FLAGS_SUB32 && cpu_state.flags_op1 == 0);
            CHECK(cpu_state.flags_op2 == UINT32_MAX && cpu_state.flags_res == 1);
            CHECK(!memcmp(memory + 0x4000, &operand, 4));
            readlookup2[4] = writelookup2[4] = (uintptr_t) memory;
            checks++;
        }
    }
}

static void test_condition_version_limit(void)
{
    for (int add = 0; add <= 1; add++)
        for (int below_equal = 0; below_equal <= 1; below_equal++) {
            ir_data_t *ir = begin_test("carry/register version boundary");
            /* The decoder can enter an instruction with version 250. It
               only honors CPU_BLOCK_END after that instruction is emitted. */
            for (int i = 0; i < REG_VERSION_MAX; i++) {
                uop_MOV_IMM(ir, IREG_temp0, i);
                uop_MOV_IMM(ir, IREG_temp1, i);
            }
            cpu_block_end = 0;
            cpu_state.flags_op = add ? FLAGS_ADD32 : FLAGS_SUB32;
            codegen_flags_changed = 1;
            if (below_equal) setcc_gen_BE(ir, 0);
            else setcc_gen_B(ir, 1);
            CHECK(cpu_block_end && reg_last_version[IREG_temp0] >= REG_VERSION_MAX);
            CHECK(reg_last_version[IREG_temp1] >= REG_VERSION_MAX);
            uop_MOV(ir, IREG_EAX, IREG_temp0);
            jit_fn entry = finish_test(ir);
            prepare_flags(add ? FLAGS_ADD32 : FLAGS_SUB32, 32, add ? UINT32_MAX : 0, 1, 0);
            entry();
            CHECK(EAX == (unsigned) below_equal);
            checks++;
        }
}

#ifdef CODEGEN_BACKEND_HAS_CMP_ULT
static void test_native_compare_aliases(void)
{
    /* Exercise every allocator register, including REX byte encodings and
       destinations aliased with either comparison operand. */
    for (int d = 0; d < CODEGEN_HOST_REGS; d++)
        for (int a = 0; a < CODEGEN_HOST_REGS; a++)
            for (int b = 0; b < CODEGEN_HOST_REGS; b++)
                for (int invert = 0; invert <= 1; invert++) {
                    begin_test("carry/native compare aliases");
                    int dest = codegen_host_reg_list[d].reg;
                    int lhs = codegen_host_reg_list[a].reg, rhs = codegen_host_reg_list[b].reg;
                    codegen_backend_prologue(&bench_block);
                    host_x86_MOV32_REG_IMM(&bench_block, lhs, 0x80000000u);
                    host_x86_MOV32_REG_IMM(&bench_block, rhs, UINT32_MAX);
                    uop_t uop = { .dest_reg_a_real = dest | IREG_SIZE_L,
                                  .src_reg_a_real = lhs | IREG_SIZE_L,
                                  .src_reg_b_real = rhs | IREG_SIZE_L, .imm_data = invert };
                    codegen_CMP_ULT(&bench_block, &uop);
                    host_x86_MOV32_ABS_REG(&bench_block, &EAX, dest);
                    codegen_backend_epilogue(&bench_block);
                    flush_code();
                    ((jit_fn) bench_block.data)();
                    CHECK(EAX == (unsigned) ((lhs != rhs) ^ invert));
                    checks++;
                }
}
#endif

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

static void test_incdec_carry(void)
{
    const int producers[] = {FLAGS_ADD8, FLAGS_ADD16, FLAGS_ADD32, FLAGS_SUB8, FLAGS_SUB16, FLAGS_SUB32,
                            FLAGS_ZN8, FLAGS_ZN16, FLAGS_ZN32, FLAGS_INC32, FLAGS_DEC32, FLAGS_ADC32, FLAGS_SBC32, FLAGS_UNKNOWN};
    for (unsigned p = 0; p < sizeof(producers) / sizeof(producers[0]); p++)
        for (int known = 0; known < 2; known++)
            for (int width = 0; width < 3; width++)
                for (int form = 0; form < 3; form++)
                    for (int decrement = 0; decrement < 2; decrement++) {
                        ir_data_t *ir = begin_test("INC/DEC carry preservation");
                        cpu_state.flags_op = producers[p];
                        codegen_flags_changed = known;
                        int mem = form == 2, high = !width && form == 1;
                        uint32_t modrm = (decrement ? 8 : 0) | (mem ? 0 : high ? 0xc7 : 0xc3);
                        if (!width) ropINCDEC(&bench_block, ir, 0xfe, modrm, 0x300, 0x101);
                        else if (form) (width == 1 ? ropFF_16 : ropFF_32)(&bench_block, ir, 0xff, modrm, 0x300, 0x101);
                        else {
                            translator op = decrement ? (width == 1 ? ropDEC_r16 : ropDEC_r32) : (width == 1 ? ropINC_r16 : ropINC_r32);
                            op(&bench_block, ir, decrement ? 0x4b : 0x43, 0, 0x300, 0x101);
                        }
                        jit_fn entry = finish_test(ir);
                        unsigned bits = 8u << width, producer_bits = p < 9 ? 8u << (p % 3) : 32;
                        uint32_t mask = UINT32_MAX >> (32 - bits);
                        for (unsigned i = 0; i < 512; i++) {
                            uint32_t a = random_u32(), b = random_u32();
                            unsigned carry = prepare_flags(producers[p], producer_bits, a, b, i & 1);
                            uint16_t flags_before = cpu_state.flags;
                            uint32_t value = (i < 8 ? (i / 2 & 1 ? (1u << (bits - 1)) : 0) - (i & 1) : random_u32()) & mask;
                            EBX = high ? 0x12340078 | (value << 8) : (0x12345678 & ~mask) | value;
                            uint32_t saved_ebx = EBX;
                            EAX = 0x4000;
                            memcpy(memory + EAX, &value, bits / 8);
                            int fault = mem && i % 8 == 0 ? (i / 8 % 2) + 1 : 0;
                            readlookup2[EAX >> 12] = writelookup2[EAX >> 12] = fault ? (uintptr_t) -1 : (uintptr_t) memory;
                            test_fault_access = fault;
                            cpu_state.abrt = 0;
                            entry();
                            uint32_t expected = fault ? value : (value + (decrement ? -1 : 1)) & mask;
                            uint32_t actual = 0;
                            if (mem) memcpy(&actual, memory + EAX, bits / 8);
                            else actual = high ? BH : EBX & mask;
                            CHECK(actual == expected && cpu_state.abrt == (fault ? 14 : 0));
                            CHECK(!!CF_SET() == carry);
                            CHECK((cpu_state.flags & ~C_FLAG) == (flags_before & ~C_FLAG));
                            if (mem) CHECK(EBX == saved_ebx);
                            else CHECK((EBX & ~(high ? 0xff00 : mask)) == (saved_ebx & ~(high ? 0xff00 : mask)));
                            if (fault) CHECK(cpu_state.flags_op == producers[p]);
                            else CHECK(cpu_state.flags_res == expected);
                            checks++;
                        }
                        readlookup2[4] = writelookup2[4] = (uintptr_t) memory;
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
    test_conditions();
    test_adc_sbb();
    test_signed_conditions();
    test_incdec_carry();
    test_carry_faults();
    test_condition_version_limit();
#ifdef CODEGEN_BACKEND_HAS_CMP_ULT
    test_native_compare_aliases();
#endif
#ifdef _WIN64
    test_xmm_abi();
#endif
    test_ret_imm();
    printf("cpu_fastpath_test: %u executions passed\n", checks);
    return 0;
}
