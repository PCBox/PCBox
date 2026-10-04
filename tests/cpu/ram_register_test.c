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
uint32_t cr4;
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
static uint32_t observed_eax, observed_xmm[4], aborted, expected_oldpc;
static unsigned exception, memory_control;
static cpu_state_t fault_state;
static uint64_t saved_r13;
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

static void
record_exception(unsigned vector)
{
    CHECK(exception == 0);
    exception = vector;
    fault_state = cpu_state;
    /* A fault exit must not run another writeback over handler changes. */
    EAX = 0xdeadbeef;
    cycles = -100;
}

void x86illegal(void) { record_exception(6); }
void x86_int(int vector) { record_exception(vector); }
static void alignment_fault(void) { record_exception(13); }

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
    /* Model a device callback changing control state. The next SSE instruction
       must still check it, even after a successful memory helper return. */
    if (memory_control == 1)
        cr0 |= 8;
    else if (memory_control == 2)
        cr4 &= ~CR4_OSFXSR;
    else if (memory_control == 3)
        cr0 |= 4;
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
    __asm__ volatile("mov $0x13579bdf, %%r10d\n\t"
                     "mov $0x2468ace0, %%r11d\n\t"
                     "pxor %%xmm1, %%xmm1\n\t"
                     "pxor %%xmm2, %%xmm2\n\t"
                     "pxor %%xmm3, %%xmm3\n\t"
                     "pxor %%xmm4, %%xmm4\n\t"
                     "pxor %%xmm5, %%xmm5"
                     : : : "r10", "r11", "xmm1", "xmm2", "xmm3", "xmm4", "xmm5");
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
#define UOP_TEST_DIV_HELPER 0x1d

static uint32_t
clobber_div_helper(uint32_t a, uint32_t b, uint32_t c)
{
    __asm__ volatile("mov $0x13579bdf, %%r10d\n\tmov $0x2468ace0, %%r11d"
                     : : : "r10", "r11");
    return a ^ b ^ c;
}

static void
clobber_call_helper(void)
{
    for (int i = 0; i < 8; i++)
        cpu_state.regs[i].l += 0x100;
    __asm__ volatile("mov $0x13579bdf, %%r10d\n\tmov $0x2468ace0, %%r11d"
                     : : : "r10", "r11");
}

static int
test_div_helper(codeblock_t *block, uop_t *uop)
{
    return codegen_DIV_HELPER(block, uop, clobber_div_helper);
}

static int
observe_state(codeblock_t *block, uop_t *uop)
{
    (void) uop;
    /* Observe backing state without a barrier that would itself flush it. */
    host_x86_MOV32_REG_ABS(block, REG_ECX, &EAX);
    host_x86_MOV64_REG_IMM(block, REG_RDI, (uintptr_t) &observed_eax);
    host_x86_MOV32_BASE_OFFSET_REG(block, REG_RDI, 0, REG_ECX);
    for (int i = 0; i < 4; i++) {
        host_x86_MOV32_REG_ABS(block, REG_ECX, &cpu_state.XMM[i].l[0]);
        host_x86_MOV64_REG_IMM(block, REG_RDI, (uintptr_t) &observed_xmm[i]);
        host_x86_MOV32_BASE_OFFSET_REG(block, REG_RDI, 0, REG_ECX);
    }
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
    [UOP_ADD & UOP_MASK] = codegen_ADD,
    [UOP_ADD_LSHIFT & UOP_MASK] = codegen_ADD_LSHIFT,
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
    [UOP_SSE_ENTER & UOP_MASK] = codegen_SSE_ENTER,
    [UOP_CHECK_ALIGN & UOP_MASK] = codegen_CHECK_ALIGN,
    [UOP_CMP_IMM_JZ_DEST & UOP_MASK] = codegen_CMP_IMM_JZ_DEST,
    [UOP_CALL_FUNC & UOP_MASK] = codegen_CALL_FUNC,
    [UOP_TEST_DIV_HELPER] = test_div_helper,
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

static void
run_sse_case(unsigned control, unsigned misalignment, int dynamic_top, unsigned memory_mode)
{
    cases++;
    memset(&cpu_state, 0, sizeof(cpu_state));
    memset(&test_block, 0, sizeof(test_block));
    memset(memory, 0xa5, sizeof(memory));
    memset(readlookup2, 0xff, sizeof(readlookup2));
    next_chunk = helper_calls = aborted = exception = 0;
    fault_on_call = 0;
    memory_control = memory_mode >= 3 ? memory_mode - 2 : 0;
    expected_oldpc = 0x1234;
    observed_eax = 0xffffffff;
    EAX = 32;
    cycles = 1000;
    cr0 = (control & 1 ? 4 : 0) | (control & 4 ? 8 : 0);
    cr4 = control & 2 ? 0 : CR4_OSFXSR;
    cpu_state.ST[0] = 1.5;
    for (int i = 0; i < 8; i++)
        for (int lane = 0; lane < 4; lane++)
            cpu_state.XMM[i].l[lane] = 10 + i * 4 + lane;
    if (memory_mode == 1)
        readlookup2[0] = (uintptr_t) memory;

    start_code();
    build_loadstore_routines(&test_block);
    codegen_gpf_rout = start_code();
    host_x86_CALL(&test_block, alignment_fault);
    codegen_exit_rout = &block_write_data[block_pos];
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &aborted);
    host_x86_MOV32_BASE_OFFSET_IMM(&test_block, REG_RDI, 0, 1);
    codegen_backend_epilogue(&test_block);

    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    test_block.flags = CODEBLOCK_HAS_FPU | (dynamic_top ? 0 : CODEBLOCK_STATIC_TOP);
    test_block.TOP = 0;
    uop_FADD(ir, IREG_ST(0), IREG_ST(0), IREG_ST(0));
    uop_MOV(ir, IREG_temp0_DQ, IREG_XMM(4));
    uop_PADDD(ir, IREG_temp0_DQ, IREG_temp0_DQ, IREG_temp0_DQ);
    for (int i = 0; i < 4; i++)
        uop_PADDD(ir, IREG_XMM(i), IREG_XMM(i), IREG_XMM(i));
    uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -3);
    uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, 16);
    uop_MOV_IMM(ir, IREG_eaaddr, 64 + misalignment);
    /* This value is overwritten after the checks. Fault liveness must retain
       it even though the success path never reads it. */
    uop_MOV_IMM(ir, IREG_EDI, 0x12345678);
    uop_MOV_IMM(ir, IREG_oldpc, 0x1111);
    cpu_state.oldpc = expected_oldpc;
    uop_gen_imm(UOP_TEST_PADDING, ir, padding);
    uop_SSE_ENTER(ir);
    uop_CHECK_ALIGN(ir);
    uop_gen(UOP_TEST_OBSERVE, ir);
    if (memory_mode) {
        uop_MOV_IMM(ir, IREG_oldpc, expected_oldpc);
        uop_MEM_LOAD_REG(ir, IREG_XMM(6), IREG_DS_base, IREG_eaaddr);
    }
    cpu_state.oldpc = expected_oldpc + 4;
    uop_SSE_ENTER(ir);
    uop_CHECK_ALIGN(ir);
    uop_MOV_IMM(ir, IREG_EDI, 0x87654321);
    uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, 1);
    uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -2);
    for (int i = 0; i < 4; i++)
        uop_PADDD(ir, IREG_XMM(i), IREG_XMM(i), IREG_XMM(i));
    uop_FADD(ir, IREG_ST(0), IREG_ST(0), IREG_ST(0));
    uop_MOV(ir, IREG_XMM(5), IREG_temp0_DQ);
    uint8_t *entry = start_code();
    codegen_ir_compile(ir, &test_block);

    if (dynamic_top) {
        cpu_state.ST[3] = cpu_state.ST[0];
        cpu_state.TOP = 3;
    }
    cpu_state.oldpc = 0;
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, CODE_SIZE));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + CODE_SIZE);
#endif
    ((void (*)(void)) entry)();

    unsigned vector = control & 3 ? 6 : control & 4 ? 7 : misalignment ? 13
                      : memory_mode == 3 ? 7 : memory_mode >= 4 ? 6 : 0;
    unsigned calls = control || misalignment || memory_mode < 2 ? 0 : 2;
    CHECK(exception == vector && aborted == !!vector);
    CHECK(helper_calls == calls);
    CHECK(observed_eax == (control || misalignment ? 0xffffffff : 32));
    if (!control && !misalignment) {
        /* Register pressure may spill some values, but a successful guard
           must not force every dirty SIMD value back to guest state. */
        int retained = 0;
        for (int i = 0; i < 4; i++)
            retained |= observed_xmm[i] == (uint32_t) (10 + i * 4);
        CHECK(retained);
    }
    const cpu_state_t *state = vector ? &fault_state : &cpu_state;
    CHECK(state->regs[0].l == (vector ? 48 : 49));
    CHECK(state->regs[7].l == (vector ? 0x12345678 : 0x87654321));
    CHECK(state->_cycles == (vector ? 997 : 995) - (int) calls * 5);
    CHECK(state->ST[dynamic_top ? 3 : 0] == (vector ? 3.0 : 6.0));
    for (int i = 0; i < 4; i++)
        for (int lane = 0; lane < 4; lane++)
            CHECK(state->XMM[i].l[lane] == (uint32_t) ((10 + i * 4 + lane) * (vector ? 2 : 4)));
    if (vector) {
        CHECK(state->oldpc == expected_oldpc + (calls ? 4 : 0));
        CHECK(EAX == 0xdeadbeef && cycles == -100);
    } else {
        for (int lane = 0; lane < 4; lane++)
            CHECK(cpu_state.XMM[5].l[lane] == (uint32_t) ((26 + lane) * 2));
    }
    memory_control = 0;
}

static void
run_sse_join(int taken, int alignment)
{
    cases++;
    memset(&cpu_state, 0, sizeof(cpu_state));
    memset(&test_block, 0, sizeof(test_block));
    next_chunk = exception = 0;
    EAX = 1;
    EBX = !taken;
    cr4 = CR4_OSFXSR;
    for (int lane = 0; lane < 4; lane++)
        cpu_state.XMM[0].l[lane] = 7;
    codegen_exit_rout = start_code();
    codegen_backend_epilogue(&test_block);
    codegen_gpf_rout = codegen_exit_rout;

    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    uop_MOV_IMM(ir, IREG_eaaddr, 64);
    int branch = uop_CMP_IMM_JZ_DEST(ir, IREG_EBX, 0);
    uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, 10);
    uop_PADDD(ir, IREG_XMM(0), IREG_XMM(0), IREG_XMM(0));
    uop_set_jump_dest(ir, branch);
    if (alignment)
        uop_CHECK_ALIGN(ir);
    else
        uop_SSE_ENTER(ir);
    uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, 1);
    uop_PADDD(ir, IREG_XMM(0), IREG_XMM(0), IREG_XMM(0));
    uint8_t *entry = start_code();
    codegen_ir_compile(ir, &test_block);
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, CODE_SIZE));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + CODE_SIZE);
#endif
    ((void (*)(void)) entry)();
    CHECK(exception == 0);
    CHECK(EAX == (taken ? 2 : 12));
    for (int lane = 0; lane < 4; lane++)
        CHECK(cpu_state.XMM[0].l[lane] == (uint32_t) (taken ? 14 : 28));
}

static void
run_integer_registers(int rotation, int mode, int dest)
{
    uint32_t expected[8];
    int allocations[8];
    unsigned used = 0;

    cases++;
    memset(&cpu_state, 0, sizeof(cpu_state));
    memset(&test_block, 0, sizeof(test_block));
    memset(memory, 0xa5, sizeof(memory));
    memset(readlookup2, 0xff, sizeof(readlookup2));
    memset(writelookup2, 0xff, sizeof(writelookup2));
    next_chunk = helper_calls = aborted = 0;
    expected_oldpc = 0x1234;
    fault_on_call = mode == 3 || mode == 6 ? 1 : mode == 7 || mode == 10 ? 2 : 0;
    for (int i = 0; i < 8; i++) {
        expected[i] = i >= 6 ? 32 : 0x12345670 + i * 0x100;
        cpu_state.regs[i].l = expected[i] - i - 1;
    }
    for (int lane = 0; lane < 4; lane++)
        cpu_state.XMM[0].l[lane] = 0x11223344 + lane;
    if (mode == 1 || mode == 4 || mode == 8)
        readlookup2[0] = writelookup2[0] = (uintptr_t) memory;

    start_code();
    build_loadstore_routines(&test_block);
    codegen_exit_rout = start_code();
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &aborted);
    host_x86_MOV32_BASE_OFFSET_IMM(&test_block, REG_RDI, 0, 1);
    codegen_backend_epilogue(&test_block);

    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    /* Independent loads must fill all eight slots, keeping each dirty value
       live for the reads after the memory access or helper call. */
    for (int i = 0; i < 8; i++) {
        int reg = (i + rotation) & 7;
        allocations[i] = ir->wr_pos;
        uop_ADD_IMM(ir, reg, reg, reg + 1);
    }
    uop_MOV_IMM(ir, IREG_oldpc, expected_oldpc);
    if (mode >= 1 && mode <= 10) {
        /* With rotation zero these two address components occupy R10/R11.
           A 128-bit miss must retain both across the first 64-bit helper. */
        if (mode >= 8)
            uop_MEM_STORE_REG(ir, IREG_ESI, IREG_EDI, IREG_XMM(0));
        else
            uop_MEM_LOAD_REG(ir, mode <= 3 ? IREG_EAX : IREG_XMM(0), IREG_ESI, IREG_EDI);
        if (mode <= 2)
            expected[0] = 0xa5a5a5a5;
    } else if (mode == 11) {
        uop_CALL_FUNC(ir, clobber_call_helper);
        for (int i = 0; i < 8; i++)
            expected[i] += 0x100;
    } else if (mode == 12) {
        uop_gen_reg_dst_src3(UOP_TYPE_PARAMS_REGS | UOP_TEST_DIV_HELPER, ir,
                            dest, dest, IREG_EDX, IREG_EDI);
        expected[dest] = expected[dest] ^ expected[2] ^ expected[7];
    } else if (mode >= 13 && mode <= 15) {
        int reg = mode == 13 ? IREG_AL : mode == 14 ? IREG_AH : IREG_AX;
        uop_ADD_IMM(ir, reg, reg, 0x21);
        if (mode == 13)
            expected[0] = (expected[0] & ~0xffu) | ((expected[0] + 0x21) & 0xff);
        else if (mode == 14)
            expected[0] = (expected[0] & ~0xff00u) | ((expected[0] + 0x2100) & 0xff00);
        else
            expected[0] = (expected[0] & ~0xffffu) | ((expected[0] + 0x21) & 0xffff);
    } else if (mode >= 16) {
        /* R13 is the sixth allocated slot. Exercise its special base encoding
           for zero displacement, nonzero displacement and scaled indices. */
        int base = (rotation + 5) & 7;
        uint32_t value = expected[base];
        if (mode == 18) {
            uop_ADD_LSHIFT(ir, IREG_temp0, base, IREG_EDI, 3);
            value += expected[7] << 3;
        } else {
            uop_ADD_IMM(ir, IREG_temp0, base, mode == 16 ? 0 : 128);
            value += mode == 16 ? 0 : 128;
        }
        uop_ADD(ir, IREG_EAX, IREG_EAX, IREG_temp0);
        expected[0] += value;
    }
    for (int i = 0; i < 8; i++)
        uop_ADD_IMM(ir, i, i, 100 + i);
    uint8_t *entry = start_code();
    codegen_ir_compile(ir, &test_block);
    for (int i = 0; i < 8; i++) {
        int host = HOST_REG_GET(ir->uops[allocations[i]].dest_reg_a_real);
        CHECK(!(used & (1u << host)));
        used |= 1u << host;
    }
    CHECK((used & ((1u << REG_R13) | (1u << REG_R10) | (1u << REG_R11)))
          == ((1u << REG_R13) | (1u << REG_R10) | (1u << REG_R11)));

    /* R13 belongs to our caller on both ABIs, including fault exits. */
    uint8_t *wrapper = start_code();
    codegen_backend_prologue(&test_block);
    host_x86_MOV64_REG_IMM(&test_block, REG_R13, UINT64_C(0x1122334455667788));
    host_x86_CALL(&test_block, entry);
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &saved_r13);
    host_x86_MOV64_BASE_OFFSET_REG(&test_block, REG_RDI, 0, REG_R13);
    codegen_backend_epilogue(&test_block);
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, CODE_SIZE));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + CODE_SIZE);
#endif
    ((void (*)(void)) wrapper)();
    CHECK(saved_r13 == UINT64_C(0x1122334455667788));
    CHECK(aborted == !!fault_on_call);
    CHECK(helper_calls == (fault_on_call ? fault_on_call : mode == 2 ? 1 : mode == 5 || mode == 9 ? 2 : 0));
    for (int i = 0; i < 8; i++)
        CHECK(cpu_state.regs[i].l == expected[i] + (fault_on_call ? 0 : 100 + i));
    if (mode >= 4 && mode <= 7)
        for (int lane = 0; lane < 4; lane++)
            CHECK(cpu_state.XMM[0].l[lane] == (fault_on_call ? (uint32_t) (0x11223344 + lane) : 0xa5a5a5a5));
    if (mode >= 8 && mode <= 10) {
        uint32_t written[4];
        memcpy(written, memory + 64, sizeof(written));
        for (int lane = 0; lane < 4; lane++)
            CHECK(written[lane] == (mode == 10 && lane >= 2 ? 0xa5a5a5a5 : (uint32_t) (0x11223344 + lane)));
    }
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
    /* Try every byte offset: an instruction that reserves one byte too few
       can consume the space needed to jump to the next allocator chunk. */
    for (padding = 0; padding < BLOCK_MAX; padding++) {
        run_case(0, 4, 1, 0, 0, 0, 0, 1, FORM_REG);
        run_case(0, 4, 0, 0, 1, 0, 0, 1, FORM_REG);
        run_case(1, 16, 0, 1, 0, 0, 0, 1, FORM_REG);
        run_case(0, 16, 0, 1, 2, 0, 0, 1, FORM_REG);
        run_case(0, 4, 1, 0, 0, 0, 0, 1, FORM_SINGLE);
        run_case(0, 4, 0, 0, 0, 0, 0, 1, FORM_SINGLE);
        run_case(0, 4, 0, 0, 1, 0, 0, 1, FORM_SINGLE);
    }
    padding = 0;
    for (int top = 0; top < 2; top++) {
        for (unsigned control = 0; control < 8; control++)
            for (unsigned alignment = 0; alignment < 16; alignment++)
                run_sse_case(control, alignment, top, 0);
        for (unsigned mode = 1; mode <= 5; mode++)
            run_sse_case(0, 0, top, mode);
    }
    for (padding = 0; padding < BLOCK_MAX; padding++) {
        run_sse_case(0, 0, 1, 0);
        run_sse_case(1, 0, 1, 0);
        run_sse_case(2, 0, 1, 0);
        run_sse_case(4, 0, 1, 0);
        run_sse_case(0, 1, 1, 0);
    }
    for (int taken = 0; taken < 2; taken++)
        for (int alignment = 0; alignment < 2; alignment++)
            run_sse_join(taken, alignment);
    for (int rotation = 0; rotation < 8; rotation++)
        for (int mode = 0; mode < 19; mode++)
            for (int dest = 0; dest < (mode == 12 ? 8 : 1); dest++)
                run_integer_registers(rotation, mode, dest);
#ifdef _WIN32
    CHECK(VirtualFree(code_memory, 0, MEM_RELEASE));
#else
    CHECK(munmap(code_memory, CODE_SIZE) == 0);
#endif
    printf("RAM and SSE register preservation tests passed (%u cases)\n", cases);
    return 0;
}
