/* Execute IR through the real allocator, memory emitters and helper ABI.
   Only guest RAM/device callbacks and executable allocation are supplied here. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/mman.h>
#endif

/* Keep the full backend table out of this standalone fixture's link. */
#define uop_handlers unused_uop_handlers
#include "../../src/codegen_new/codegen_backend_x86-64_uops.c"
#undef uop_handlers
#include "../../src/codegen_new/codegen_backend_x86-64.c"
#ifdef _WIN32
#    undef REG_DWORD
#    undef REG_QWORD
#endif
#include "../../src/codegen_new/codegen_reg.c"
extern const uOpFn uop_handlers[];
#include "../../src/codegen_new/codegen_ir.c"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "case %u, line %d: %s failed\n", cases, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

cpu_state_t cpu_state;
uintptr_t readlookup2[2097152], writelookup2[1048576];
uint8_t *ram, *block_write_data;
int block_pos, cpu_block_end;
static codeblock_t test_block;
codeblock_t *codeblock = &test_block;

enum { CODE_SIZE = 262144, CHUNK_SIZE = 4096 };
enum { FORM_REG, FORM_ABS, FORM_IMM, FORM_SINGLE, FORM_DOUBLE };
struct mem_block_t { uint8_t *data; };
static struct mem_block_t chunks[CODE_SIZE / CHUNK_SIZE];
static uint8_t *code_memory;
static uint8_t memory[8192];
static unsigned next_chunk, cases, helper_calls, fault_on_call, padding;
static uint32_t observed_eax, aborted, expected_oldpc;
#ifdef _WIN64
static const uint32_t xmm_sentinel[8] = {
    0x11223344, 0x55667788, 0x99aabbcc, 0xddeeff00,
    0xfedcba98, 0x76543210, 0x01234567, 0x89abcdef
};
static uint32_t saved_xmm[8];
#endif

void
fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

struct mem_block_t *
codegen_allocator_allocate(struct mem_block_t *parent, int nr)
{
    (void) parent;
    (void) nr;
    CHECK(next_chunk < CODE_SIZE / CHUNK_SIZE);
    chunks[next_chunk].data = code_memory + next_chunk * CHUNK_SIZE;
    return &chunks[next_chunk++];
}

uint8_t *codeblock_allocator_get_ptr(struct mem_block_t *block) { return block->data; }
void codegen_set_loop_start(ir_data_t *ir, int first) { (void) ir; (void) first; }

static uint8_t *
start_code(void)
{
    test_block.head_mem_block = codegen_allocator_allocate(NULL, 0);
    block_write_data = codeblock_allocator_get_ptr(test_block.head_mem_block);
    block_pos = 0;
    return block_write_data;
}

static uint64_t
access_memory(uint32_t addr, uint64_t value, unsigned size, int store)
{
    helper_calls++;
    CHECK(cpu_state.oldpc == expected_oldpc);
    CHECK(addr + size <= sizeof(memory));
    cycles -= 5;
    if (helper_calls == fault_on_call) {
        cpu_state.abrt = 1;
        return 0;
    }
    if (store)
        memcpy(memory + addr, &value, size);
    else {
        value = 0;
        memcpy(&value, memory + addr, size);
    }
    /* A real C callback may freely destroy these registers. */
    __asm__ volatile("pxor %%xmm1, %%xmm1\n\t"
                     "pxor %%xmm2, %%xmm2\n\t"
                     "pxor %%xmm3, %%xmm3\n\t"
                     "pxor %%xmm4, %%xmm4\n\t"
                     "pxor %%xmm5, %%xmm5"
                     : : : "xmm1", "xmm2", "xmm3", "xmm4", "xmm5");
#ifndef _WIN32
    __asm__ volatile("pxor %%xmm6, %%xmm6\n\tpxor %%xmm7, %%xmm7"
                     : : : "xmm6", "xmm7");
#endif
    return value;
}

uint8_t readmembl(uint32_t addr) { return access_memory(addr, 0, 1, 0); }
uint16_t readmemwl(uint32_t addr) { return access_memory(addr, 0, 2, 0); }
uint32_t readmemll(uint32_t addr) { return access_memory(addr, 0, 4, 0); }
uint64_t readmemql(uint32_t addr) { return access_memory(addr, 0, 8, 0); }
void writemembl(uint32_t addr, uint8_t value) { access_memory(addr, value, 1, 1); }
void writememwl(uint32_t addr, uint16_t value) { access_memory(addr, value, 2, 1); }
void writememll(uint32_t addr, uint32_t value) { access_memory(addr, value, 4, 1); }
void writememql(uint32_t addr, uint64_t value) { access_memory(addr, value, 8, 1); }

#define UOP_TEST_OBSERVE 0x1f
#define UOP_TEST_PADDING 0x1e

static int
observe_state(codeblock_t *block, uop_t *uop)
{
    (void) uop;
    /* Observe backing state without a barrier that would itself flush it. */
    host_x86_MOV32_REG_ABS(block, REG_ECX, &EAX);
    host_x86_MOV64_REG_IMM(block, REG_RDI, (uintptr_t) &observed_eax);
    host_x86_MOV32_BASE_OFFSET_REG(block, REG_RDI, 0, REG_ECX);
    return 0;
}

static int
pad_code(codeblock_t *block, uop_t *uop)
{
    for (uint32_t c = 0; c < uop->imm_data; c++)
        host_x86_NOP(block);
    return 0;
}

const uOpFn uop_handlers[UOP_MAX] = {
    [UOP_MOV & UOP_MASK] = codegen_MOV,
    [UOP_MOV_IMM & UOP_MASK] = codegen_MOV_IMM,
    [UOP_ADD_IMM & UOP_MASK] = codegen_ADD_IMM,
    [UOP_PADDD & UOP_MASK] = codegen_PADDD,
    [UOP_FADD & UOP_MASK] = codegen_FADD,
    [UOP_MEM_LOAD_REG & UOP_MASK] = codegen_MEM_LOAD_REG,
    [UOP_MEM_STORE_REG & UOP_MASK] = codegen_MEM_STORE_REG,
    [UOP_MEM_LOAD_ABS & UOP_MASK] = codegen_MEM_LOAD_ABS,
    [UOP_MEM_STORE_ABS & UOP_MASK] = codegen_MEM_STORE_ABS,
    [UOP_MEM_STORE_IMM_8 & UOP_MASK] = codegen_MEM_STORE_IMM_8,
    [UOP_MEM_STORE_IMM_16 & UOP_MASK] = codegen_MEM_STORE_IMM_16,
    [UOP_MEM_STORE_IMM_32 & UOP_MASK] = codegen_MEM_STORE_IMM_32,
    [UOP_MEM_LOAD_SINGLE & UOP_MASK] = codegen_MEM_LOAD_SINGLE,
    [UOP_MEM_LOAD_DOUBLE & UOP_MASK] = codegen_MEM_LOAD_DOUBLE,
    [UOP_MEM_STORE_SINGLE & UOP_MASK] = codegen_MEM_STORE_SINGLE,
    [UOP_MEM_STORE_DOUBLE & UOP_MASK] = codegen_MEM_STORE_DOUBLE,
    [UOP_TEST_OBSERVE] = observe_state,
    [UOP_TEST_PADDING] = pad_code,
};

static void
run_case(int store, int size, int mapped, int unaligned, unsigned fault, int high_byte, int alias, int dynamic_top, int form)
{
    cases++;
    memset(&cpu_state, 0, sizeof(cpu_state));
    memset(&test_block, 0, sizeof(test_block));
    memset(memory, 0xa5, sizeof(memory));
    memset(readlookup2, 0xff, sizeof(readlookup2));
    memset(writelookup2, 0xff, sizeof(writelookup2));
    next_chunk = helper_calls = aborted = 0;
    observed_eax = 0xffffffff;
    fault_on_call = fault;
    expected_oldpc = 0x1234;
    uint32_t address = (unaligned ? 4093 : 64);
    EAX = address - 16;
    EBX = 0x12345670;
    cycles = 1000;
    cpu_state.ST[0] = 1.5;
    cpu_state.ST[1] = 2.25;
    cpu_state.MM[0].q = UINT64_C(0x8877665544332211);
    if (form == FORM_SINGLE) {
        float value = 1.25f;
        memcpy(memory + address, &value, sizeof(value));
    } else if (form == FORM_DOUBLE) {
        double value = 1.25;
        memcpy(memory + address, &value, sizeof(value));
    }
    for (int i = 0; i < 8; i++)
        for (int lane = 0; lane < 4; lane++)
            cpu_state.XMM[i].l[lane] = 10 + i * 4 + lane;
    if (mapped) {
        for (int page = 0; page < 2; page++)
            readlookup2[page] = writelookup2[page] = (uintptr_t) memory;
    }

    start_code();
    build_loadstore_routines(&test_block);
    codegen_exit_rout = start_code();
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &aborted);
    host_x86_MOV32_BASE_OFFSET_IMM(&test_block, REG_RDI, 0, 1);
    codegen_backend_epilogue(&test_block);

    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    test_block.flags = CODEBLOCK_HAS_FPU | (dynamic_top ? 0 : CODEBLOCK_STATIC_TOP);
    test_block.TOP = 0;
    for (int i = 0; i < 4; i++)
        uop_PADDD(ir, IREG_XMM(i), IREG_XMM(i), IREG_XMM(i));
    uop_FADD(ir, IREG_ST(0), IREG_ST(0), IREG_ST(0));
    /* Keep a full-width temporary live through the helper and its C call. */
    uop_MOV(ir, IREG_temp0_DQ, IREG_XMM(4));
    uop_PADDD(ir, IREG_temp0_DQ, IREG_temp0_DQ, IREG_temp0_DQ);
    uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -3);
    uop_ADD_IMM(ir, IREG_EBX, IREG_EBX, 8);
    uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, 16);
    uop_MOV_IMM(ir, IREG_oldpc, expected_oldpc);
    uop_gen_imm(UOP_TEST_PADDING, ir, padding);
    int reg = size == 16 ? IREG_XMM(6) : size == 8 ? IREG_MM(0)
              : high_byte ? IREG_BH : size == 1 ? IREG_BL : size == 2 ? IREG_BX : IREG_EBX;
    if (alias)
        reg = IREG_EAX;
    if (form == FORM_ABS) {
        /* EAX supplies the base so it remains live through allocation. */
        if (store)
            uop_MEM_STORE_ABS(ir, IREG_EAX, 0, reg);
        else
            uop_MEM_LOAD_ABS(ir, reg, IREG_EAX, 0);
    } else if (form == FORM_IMM) {
        if (size == 1)
            uop_MEM_STORE_IMM_8(ir, IREG_DS_base, IREG_EAX, 0x76543210);
        else if (size == 2)
            uop_MEM_STORE_IMM_16(ir, IREG_DS_base, IREG_EAX, 0x76543210);
        else
            uop_MEM_STORE_IMM_32(ir, IREG_DS_base, IREG_EAX, 0x76543210);
    } else if (form == FORM_SINGLE) {
        if (store)
            uop_MEM_STORE_SINGLE(ir, IREG_DS_base, IREG_EAX, IREG_ST(1));
        else
            uop_MEM_LOAD_SINGLE(ir, IREG_ST(1), IREG_DS_base, IREG_EAX);
    } else if (form == FORM_DOUBLE) {
        if (store)
            uop_MEM_STORE_DOUBLE(ir, IREG_DS_base, IREG_EAX, IREG_ST(1));
        else
            uop_MEM_LOAD_DOUBLE(ir, IREG_ST(1), IREG_DS_base, IREG_EAX);
    } else if (store)
        uop_MEM_STORE_REG(ir, IREG_DS_base, IREG_EAX, reg);
    else
        uop_MEM_LOAD_REG(ir, reg, IREG_DS_base, IREG_EAX);
    uop_gen(UOP_TEST_OBSERVE, ir);
    uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, 1);
    uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -2);
    for (int i = 0; i < 4; i++)
        uop_PADDD(ir, IREG_XMM(i), IREG_XMM(i), IREG_XMM(i));
    uop_FADD(ir, IREG_ST(0), IREG_ST(0), IREG_ST(0));
    uop_MOV(ir, IREG_XMM(5), IREG_temp0_DQ);
    uop_PADDD(ir, IREG_XMM(5), IREG_XMM(5), IREG_XMM(5));
    uint8_t *entry = start_code();
    codegen_ir_compile(ir, &test_block);

#ifdef _WIN64
    /* The spill area must not overlap the caller's saved XMM6/XMM7. Check
       both normal returns and the common fault exit with known upper halves. */
    uint8_t *wrapper = start_code();
    test_block.flags = 0;
    codegen_backend_prologue(&test_block);
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) xmm_sentinel);
    host_x86_MOVDQU_XREG_BASE_OFFSET(&test_block, REG_XMM6, REG_RDI, 0);
    host_x86_MOVDQU_XREG_BASE_OFFSET(&test_block, REG_XMM7, REG_RDI, 16);
    host_x86_CALL(&test_block, entry);
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) saved_xmm);
    host_x86_MOVDQU_BASE_OFFSET_XREG(&test_block, REG_RDI, 0, REG_XMM6);
    host_x86_MOVDQU_BASE_OFFSET_XREG(&test_block, REG_RDI, 16, REG_XMM7);
    codegen_backend_epilogue(&test_block);
    entry = wrapper;
#endif

    if (dynamic_top) {
        cpu_state.ST[3] = cpu_state.ST[0];
        cpu_state.ST[4] = cpu_state.ST[1];
        cpu_state.TOP = 3;
    }
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, CODE_SIZE));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + CODE_SIZE);
#endif
    ((void (*)(void)) entry)();
#ifdef _WIN64
    CHECK(memcmp(saved_xmm, xmm_sentinel, sizeof(saved_xmm)) == 0);
#endif

    int slow = !mapped || (unaligned && size > 1);
    CHECK(aborted == !!fault);
    CHECK(helper_calls == (slow ? (fault ? fault : size == 16 ? 2u : 1u) : 0));
    CHECK(cycles == (fault ? 997 : 995) - (int) helper_calls * 5);
    if (fault) {
        CHECK(cpu_state.oldpc == expected_oldpc);
        CHECK(EAX == address);
        CHECK(EBX == 0x12345678);
        CHECK(observed_eax == 0xffffffff);
    } else {
        if (!alias)
            CHECK(observed_eax == (slow ? address : address - 16));
        CHECK(EAX == (alias ? 0xa5a5a5a6 : address + 1));
        if (!store && size <= 4 && !alias && form < FORM_SINGLE) {
            uint32_t expected = size == 4 ? 0xa5a5a5a5 : size == 2 ? 0x1234a5a5
                                : high_byte ? 0x1234a578 : 0x123456a5;
            CHECK(EBX == expected);
        }
        if (!store && size == 16)
            for (int lane = 0; lane < 4; lane++)
                CHECK(cpu_state.XMM[6].l[lane] == 0xa5a5a5a5);
        if (!store && size == 8 && form == FORM_REG)
            CHECK(cpu_state.MM[0].q == UINT64_C(0xa5a5a5a5a5a5a5a5));
    }
    if (form >= FORM_SINGLE)
        CHECK(cpu_state.ST[dynamic_top ? 4 : 1] == (!store && !fault ? 1.25 : 2.25));
    if (!store && fault && size == 16)
        for (int lane = 0; lane < 4; lane++)
            CHECK(cpu_state.XMM[6].l[lane] == (uint32_t) (34 + lane));
    if (store) {
        uint8_t expected[16];
        memset(expected, 0xa5, sizeof(expected));
        if (!fault || fault == 2) {
            uint64_t value = form == FORM_IMM ? 0x76543210 : size == 8 ? UINT64_C(0x8877665544332211)
                             : high_byte ? 0x56 : 0x12345678;
            if (form == FORM_SINGLE) {
                float single = 2.25f;
                memcpy(expected, &single, 4);
            } else if (form == FORM_DOUBLE) {
                double number = 2.25;
                memcpy(expected, &number, 8);
            } else if (size == 16) {
                uint32_t lanes[] = { 34, 35, 36, 37 };
                memcpy(expected, lanes, fault ? 8 : 16);
            } else
                memcpy(expected, &value, size);
            CHECK(memcmp(memory + address, expected, size) == 0);
        }
    }
    for (int i = 0; i < 4; i++)
        for (int lane = 0; lane < 4; lane++)
            CHECK(cpu_state.XMM[i].l[lane] == (uint32_t) ((10 + i * 4 + lane) * (fault ? 2 : 4)));
    CHECK(cpu_state.ST[dynamic_top ? 3 : 0] == (fault ? 3.0 : 6.0));
    if (!fault)
        for (int lane = 0; lane < 4; lane++)
            CHECK(cpu_state.XMM[5].l[lane] == (uint32_t) ((26 + lane) * 4));
}

int
main(void)
{
#ifdef _WIN32
    code_memory = VirtualAlloc(NULL, CODE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    CHECK(code_memory != NULL);
#else
    code_memory = mmap(NULL, CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(code_memory != MAP_FAILED);
#endif
    for (int top = 0; top < 2; top++) {
        for (int store = 0; store < 2; store++) {
            for (int form = FORM_REG; form <= FORM_DOUBLE; form++) {
                for (int size = 1; size <= 16; size *= 2) {
                    if ((form == FORM_ABS || form == FORM_IMM) && size > 4) continue;
                    if (form == FORM_IMM && !store) continue;
                    if (form == FORM_SINGLE && size != 4) continue;
                    if (form == FORM_DOUBLE && size != 8) continue;
                    run_case(store, size, 1, 0, 0, 0, 0, top, form);
                    run_case(store, size, 0, 0, 0, 0, 0, top, form);
                    run_case(store, size, 0, 0, 1, 0, 0, top, form);
                    run_case(store, size, 1, 1, 0, 0, 0, top, form);
                    if (size == 16)
                        run_case(store, size, 0, 1, 2, 0, 0, top, form);
                }
            }
            run_case(store, 1, 1, 0, 0, 1, 0, top, FORM_REG);
            run_case(store, 1, 0, 0, 0, 1, 0, top, FORM_REG);
        }
        run_case(0, 4, 1, 0, 0, 0, 1, top, FORM_REG);
        run_case(0, 4, 0, 0, 0, 0, 1, top, FORM_REG);
        run_case(0, 4, 0, 0, 1, 0, 1, top, FORM_REG);
    }
    /* Move the lookup, writeback and reload branches across allocator chunks. */
    for (padding = 0; padding < BLOCK_MAX; padding += 31) {
        run_case(0, 4, 1, 0, 0, 0, 0, 1, FORM_REG);
        run_case(0, 4, 0, 0, 1, 0, 0, 1, FORM_REG);
        run_case(1, 16, 0, 1, 0, 0, 0, 1, FORM_REG);
        run_case(0, 16, 0, 1, 2, 0, 0, 1, FORM_REG);
    }
#ifdef _WIN32
    CHECK(VirtualFree(code_memory, 0, MEM_RELEASE));
#else
    CHECK(munmap(code_memory, CODE_SIZE) == 0);
#endif
    printf("RAM register preservation tests passed (%u cases)\n", cases);
    return 0;
}
