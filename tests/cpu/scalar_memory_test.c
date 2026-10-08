/* Execute the production scalar memory emitters. Mock helpers record fallback
   addresses, emulate device/RAM data, and inject faults before touching data.
   The production allocator rollover emitter and block ABI are exercised too. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/mman.h>
#endif

#include "../../src/codegen_new/codegen_backend_x86-64_uops.c"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "case %u, %s:%d: %s failed\n", cases, __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

cpu_state_t cpu_state;
uintptr_t readlookup2[2097152], writelookup2[1048576];
uint8_t *ram, *block_write_data;
int block_pos, cpu_block_end;
int timing_misaligned, cpu_cyrix_alignment;
static codeblock_t test_block;
codeblock_t *codeblock = &test_block;

/* Register allocation is exercised separately by ram_register_test. */
int codegen_reg_get_dirty_host_reg(int reg) { (void)reg; return -1; }
void codegen_reg_flush_conditional(codeblock_t *block, ir_reg_t reg) { (void)block; (void)reg; }
void codegen_reg_reload_mem(codeblock_t *block, ir_reg_t reg) { (void)block; (void)reg; }
void codegen_reg_capture_mem(codegen_mem_reg_state_t *state, ir_reg_t reg)
{ (void)reg; memset(state, 0, sizeof(*state)); }
void codegen_reg_sync_mem(codeblock_t *block, const codegen_mem_reg_state_t *state, int reload, int stack_offset)
{ (void)block; (void)state; (void)reload; (void)stack_offset; }


enum { CODE_SIZE = 65536, CHUNK_SIZE = 4096 };
enum { FORM_REG, FORM_ABS, FORM_IMM, FORM_SINGLE, FORM_DOUBLE };
struct mem_block_t { uint8_t *data; };
static struct mem_block_t chunks[CODE_SIZE / CHUNK_SIZE];
static unsigned next_chunk, cases;
static uint8_t *code_memory;
static uint8_t memory[8192], expected_memory[8192];
static uint64_t result, vector_data;
static uint32_t helper_calls, helper_address, aborted;
static uint64_t saved_gprs[5];
static const int gprs[] = { REG_EAX, REG_EBX, REG_EDX, REG_R14, REG_R15 };
static const uint64_t initial_data = UINT64_C(0x89abcdef76543210);

uint64_t readmemql(uint32_t addr) { (void) addr; CHECK(0); return 0; }
void writememql(uint32_t addr, uint64_t value) { (void) addr; (void) value; CHECK(0); }

void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

struct mem_block_t *codegen_allocator_allocate(struct mem_block_t *parent, int nr)
{
    (void) parent;
    (void) nr;
    CHECK(next_chunk < CODE_SIZE / CHUNK_SIZE);
    chunks[next_chunk].data = code_memory + next_chunk * CHUNK_SIZE;
    return &chunks[next_chunk++];
}

uint8_t *codeblock_allocator_get_ptr(struct mem_block_t *block) { return block->data; }

static uint8_t *start_code(void)
{
    test_block.head_mem_block = codegen_allocator_allocate(NULL, 0);
    block_write_data = codeblock_allocator_get_ptr(test_block.head_mem_block);
    block_pos = 0;
    return block_write_data;
}

static void capture(int vector, int reg)
{
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &result);
    if (vector)
        host_x86_MOVQ_BASE_OFFSET_XREG(&test_block, REG_RDI, 0, REG_XMM1);
    else
        host_x86_MOV64_BASE_OFFSET_REG(&test_block, REG_RDI, 0, reg);
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) saved_gprs);
    for (unsigned i = 0; i < sizeof(gprs) / sizeof(gprs[0]); i++)
        host_x86_MOV64_BASE_OFFSET_REG(&test_block, REG_RDI, i * 8, gprs[i]);
}

static void build_helper(int store, int size, int form, int fault)
{
    uint8_t *entry = start_code();
    /* Private helper ABI: ESI address, ECX/XMM0 data, ESI abort result. */
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &helper_address);
    host_x86_MOV32_BASE_OFFSET_REG(&test_block, REG_RDI, 0, REG_ESI);
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &helper_calls);
    host_x86_MOV32_REG_BASE_OFFSET(&test_block, REG_R8, REG_RDI, 0);
    host_x86_ADD32_REG_IMM(&test_block, REG_R8, 1);
    host_x86_MOV32_BASE_OFFSET_REG(&test_block, REG_RDI, 0, REG_R8);
    if (fault) {
        host_x86_MOV32_REG_IMM(&test_block, REG_ESI, 1);
    } else {
        /* Test mappings alias high guest addresses onto the two backing pages. */
        host_x86_AND32_REG_IMM(&test_block, REG_ESI, sizeof(memory) - 1);
        host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) memory);
        if (store) {
            if (size == 1)
                host_x86_MOV8_BASE_INDEX_REG(&test_block, REG_RDI, REG_RSI, REG_ECX);
            else if (size == 2)
                host_x86_MOV16_BASE_INDEX_REG(&test_block, REG_RDI, REG_RSI, REG_ECX);
            else if (size == 4 && form != FORM_SINGLE)
                host_x86_MOV32_BASE_INDEX_REG(&test_block, REG_RDI, REG_RSI, REG_ECX);
            else if (size == 4)
                host_x86_MOVD_BASE_INDEX_XREG(&test_block, REG_RDI, REG_RSI, REG_XMM_TEMP);
            else
                host_x86_MOVQ_BASE_INDEX_XREG(&test_block, REG_RDI, REG_RSI, REG_XMM_TEMP);
        } else {
            if (size == 1)
                host_x86_MOVZX_BASE_INDEX_32_8(&test_block, REG_ECX, REG_RDI, REG_RSI);
            else if (size == 2)
                host_x86_MOVZX_BASE_INDEX_32_16(&test_block, REG_ECX, REG_RDI, REG_RSI);
            else if (size == 4 && form != FORM_SINGLE)
                host_x86_MOV32_REG_BASE_INDEX(&test_block, REG_ECX, REG_RDI, REG_RSI);
            else if (size == 4)
                host_x86_CVTSS2SD_XREG_BASE_INDEX(&test_block, REG_XMM_TEMP, REG_RDI, REG_RSI);
            else
                host_x86_MOVQ_XREG_BASE_INDEX(&test_block, REG_XMM_TEMP, REG_RDI, REG_RSI);
        }
        host_x86_XOR32_REG_REG(&test_block, REG_ESI, REG_ESI);
    }
    host_x86_RET(&test_block);
    codegen_mem_load_byte = codegen_mem_load_word = codegen_mem_load_long = entry;
    codegen_mem_load_quad = codegen_mem_load_single = codegen_mem_load_double = entry;
    codegen_mem_store_byte = codegen_mem_store_word = codegen_mem_store_long = entry;
    codegen_mem_store_quad = codegen_mem_store_single = codegen_mem_store_double = entry;
}

static void run_case(int store, int size, int form, int high_byte, int reg,
                     uint32_t segment, uint32_t address, uint32_t offset,
                     int a16, int mapped, int fault, int padding)
{
    cases++;
    int vector = size == 8 || form == FORM_SINGLE;
    uint32_t linear = segment + address + offset;
    if (a16 && offset && form == FORM_REG)
        linear &= 0xffff;
    unsigned index = linear & (sizeof(memory) - 1);
    CHECK(index + size <= sizeof(memory));
    int expect_helper = !mapped || (linear & 0xfff) > 0x1000u - size;
    CHECK(!fault || expect_helper);

    memset(memory, 0xa5, sizeof(memory));
    if (form == FORM_SINGLE) {
        float value = -3.25f;
        memcpy(memory + index, &value, sizeof(value));
    } else if (form == FORM_DOUBLE) {
        double value = -3.25;
        memcpy(memory + index, &value, sizeof(value));
    }
    memcpy(expected_memory, memory, sizeof(memory));
    vector_data = initial_data;
    if (form == FORM_SINGLE || form == FORM_DOUBLE) {
        double value = 1.25;
        memcpy(&vector_data, &value, sizeof(value));
    }

    uint64_t initial_regs[5];
    for (unsigned i = 0; i < 5; i++)
        initial_regs[i] = initial_data;
    initial_regs[0] = segment;
    initial_regs[2] = address;
    unsigned reg_index = 0;
    while (gprs[reg_index] != reg)
        reg_index++;
    uint64_t expected_result = vector ? vector_data : initial_regs[reg_index];
    if (!fault) {
        if (store) {
            uint64_t value = vector ? vector_data : initial_regs[reg_index];
            if (high_byte)
                value >>= 8;
            if (form == FORM_IMM)
                value = 0xfedcba98;
            if (form == FORM_SINGLE) {
                float single = 1.25f;
                memcpy(expected_memory + index, &single, size);
            } else
                memcpy(expected_memory + index, &value, size);
        } else if (form == FORM_SINGLE) {
            double value = -3.25;
            memcpy(&expected_result, &value, sizeof(value));
        } else if (vector) {
            memcpy(&expected_result, memory + index, size);
        } else {
            uint32_t value = 0;
            memcpy(&value, memory + index, size);
            if (size == 4)
                expected_result = value; /* 32-bit writes zero the host upper half. */
            else {
                unsigned shift = high_byte ? 8 : 0;
                uint64_t mask = ((UINT64_C(1) << (size * 8)) - 1) << shift;
                expected_result = (expected_result & ~mask) | ((uint64_t) value << shift);
            }
        }
    }
    if (!store && !vector)
        initial_regs[reg_index] = expected_result;

    uintptr_t mapping = mapped ? (uintptr_t) memory + index - (uintptr_t) linear : (uintptr_t) -1;
    readlookup2[linear >> 12] = writelookup2[linear >> 12] = mapping;
    next_chunk = 0;
    helper_calls = helper_address = aborted = 0;
    build_helper(store, size, form, fault);
    codegen_exit_rout = start_code();
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &aborted);
    host_x86_MOV32_BASE_OFFSET_IMM(&test_block, REG_RDI, 0, 1);
    capture(vector, reg);
    codegen_backend_epilogue(&test_block);

    uint8_t *entry = start_code();
    codegen_backend_prologue(&test_block);
    codegen_backend_mem_begin();
    for (unsigned i = 0; i < 5; i++)
        host_x86_MOV64_REG_IMM(&test_block, gprs[i], initial_data);
    host_x86_MOV32_REG_IMM(&test_block, REG_EAX, segment);
    host_x86_MOV32_REG_IMM(&test_block, REG_EDX, address);
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &vector_data);
    host_x86_MOVQ_XREG_BASE_OFFSET(&test_block, REG_XMM1, REG_RDI, 0);
    for (int i = 0; i < padding; i++)
        host_x86_NOP(&test_block);

    uop_t uop = { 0 };
    int type = vector ? (form >= FORM_SINGLE ? IREG_SIZE_D : IREG_SIZE_Q)
                     : high_byte ? IREG_SIZE_BH
                     : size == 1 ? IREG_SIZE_B : size == 2 ? IREG_SIZE_W : IREG_SIZE_L;
    uop.dest_reg_a_real = (vector ? REG_XMM1 : reg) | type;
    uop.src_reg_a_real = REG_EAX;
    uop.src_reg_b_real = REG_EDX;
    uop.src_reg_c_real = (vector ? REG_XMM1 : reg) | type;
    uop.imm_data = offset;
    uop.is_a16 = a16;
    if (form == FORM_ABS) {
        uop.imm_data = address + offset;
        if (store) {
            uop.src_reg_b_real = reg | type;
            codegen_MEM_STORE_ABS(&test_block, &uop);
        } else
            codegen_MEM_LOAD_ABS(&test_block, &uop);
    } else if (form == FORM_IMM) {
        uop.imm_data = 0xfedcba98;
        if (size == 1)
            codegen_MEM_STORE_IMM_8(&test_block, &uop);
        else if (size == 2)
            codegen_MEM_STORE_IMM_16(&test_block, &uop);
        else
            codegen_MEM_STORE_IMM_32(&test_block, &uop);
    } else if (form == FORM_SINGLE) {
        if (store) codegen_MEM_STORE_SINGLE(&test_block, &uop);
        else       codegen_MEM_LOAD_SINGLE(&test_block, &uop);
    } else if (form == FORM_DOUBLE) {
        if (store) codegen_MEM_STORE_DOUBLE(&test_block, &uop);
        else       codegen_MEM_LOAD_DOUBLE(&test_block, &uop);
    } else {
        if (store) codegen_MEM_STORE_REG(&test_block, &uop);
        else       codegen_MEM_LOAD_REG(&test_block, &uop);
    }
    capture(vector, reg);
    codegen_backend_epilogue(&test_block);
    codegen_backend_mem_finish(&test_block);
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, CODE_SIZE));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + CODE_SIZE);
#endif
    ((void (*)(void)) entry)();
    CHECK(helper_calls == (uint32_t) !!expect_helper);
    if (expect_helper)
        CHECK(helper_address == linear);
    CHECK(aborted == (uint32_t) fault);
    CHECK(result == expected_result);
    CHECK(memcmp(memory, expected_memory, sizeof(memory)) == 0);
    CHECK(memcmp(saved_gprs, initial_regs, sizeof(saved_gprs)) == 0);
}

int main(void)
{
#ifdef _WIN32
    code_memory = VirtualAlloc(NULL, CODE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    CHECK(code_memory != NULL);
#else
    code_memory = mmap(NULL, CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC,
                       MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(code_memory != MAP_FAILED);
#endif
    memset(readlookup2, 0xff, sizeof(readlookup2));
    memset(writelookup2, 0xff, sizeof(writelookup2));
    for (int store = 0; store <= 1; store++) {
        for (int form = FORM_REG; form <= FORM_DOUBLE; form++) {
            if (!store && form == FORM_IMM) continue;
            for (int size = 1; size <= 8; size *= 2) {
                if ((form == FORM_ABS || form == FORM_IMM) && size == 8) continue;
                if (form == FORM_SINGLE && size != 4) continue;
                if (form == FORM_DOUBLE && size != 8) continue;
                for (int mapped = 0; mapped <= 1; mapped++) {
                    run_case(store, size, form, 0, REG_EBX, 16, 48, 0, 0, mapped, 0, 0);
                    run_case(store, size, form, 0, REG_EBX, 0, 4096 - size, 0, 0, mapped, 0, 0);
                    run_case(store, size, form, 0, REG_EBX, 0x80000000, 32, 0, 0, mapped, 0, 0);
                    if (form != FORM_IMM) {
                        run_case(store, size, form, 0, REG_EBX, 16, 16, 32, 0, mapped, 0, 0);
                        run_case(store, size, form, 0, REG_EBX, 0, 0xfffffff0, 0x30, 0, mapped, 0, 0);
                    }
                    if (form == FORM_REG) {
                        run_case(store, size, form, 0, REG_EBX, 0, 32, 0, 1, mapped, 0, 0);
                        run_case(store, size, form, 0, REG_EBX, 0, 0x10000, 32, 1, mapped, 0, 0);
                    }
                    if (size > 1) {
                        run_case(store, size, form, 0, REG_EBX, 0, 33, 0, 0, mapped, 0, 0);
                        run_case(store, size, form, 0, REG_EBX, 0, 4095, 0, 0, mapped, 0, 0);
                        run_case(store, size, form, 0, REG_EBX, 0, 4095, 0, 0, mapped, 1, 0);
                    }
                    if (!mapped)
                        run_case(store, size, form, 0, REG_EBX, 0, 32, 0, 0, 0, 1, 0);
                    /* Exercise relocation across the production 0x3c0-byte chunks. */
                    for (int padding = 720; padding < 940; padding += 5)
                        run_case(store, size, form, 0, REG_EBX, 0, 32, 0, 0, mapped, !mapped, padding);
                }
            }
        }
        for (int mapped = 0; mapped <= 1; mapped++) {
            for (int form = FORM_REG; form <= FORM_ABS; form++) {
                run_case(store, 1, form, 1, REG_EBX, 0, 32, 0, 0, mapped, 0, 0);
                for (int size = 2; size <= 4; size *= 2)
                    for (int reg = REG_R14; reg <= REG_R15; reg++)
                        run_case(store, size, form, 0, reg, 0, 32, 0, 0, mapped, 0, 0);
            }
            /* Load destination aliases an address input. */
            if (!store)
                for (int size = 1; size <= 4; size *= 2) {
                    run_case(0, size, FORM_REG, 0, REG_EAX, 16, 16, 0, 0, mapped, 0, 0);
                    run_case(0, size, FORM_REG, 0, REG_EDX, 16, 16, 0, 0, mapped, 0, 0);
                }
        }
    }
#ifdef _WIN32
    CHECK(VirtualFree(code_memory, 0, MEM_RELEASE));
#else
    CHECK(munmap(code_memory, CODE_SIZE) == 0);
#endif
    printf("Scalar memory execution tests passed (%u cases)\n", cases);
    return 0;
}
