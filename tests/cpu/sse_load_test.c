/* Execute the production 128-bit load emitter, including its fallback and
   abort exits. Only the RAM helper and executable-code allocator are faked. */
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
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

cpu_state_t cpu_state;
uintptr_t readlookup2[2097152];
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
struct mem_block_t { uint8_t *data; };
static struct mem_block_t chunks[CODE_SIZE / CHUNK_SIZE];
static unsigned next_chunk;
static uint8_t *code_memory;
static uint8_t memory[8192], result[16];
static const uint8_t sentinel[16] = { 0x55, 0xaa, 0x33, 0xcc };
static uint32_t helper_calls, fault_on_call, aborted;

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
    (void)parent;
    (void)nr;
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

static void store_result(void)
{
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) result);
    host_x86_MOVDQU_BASE_OFFSET_XREG(&test_block, REG_RDI, 0, REG_XMM1);
}

static void build_helper(void)
{
    codegen_mem_load_quad = start_code();
    /* Match the helper's private ABI: ESI is the guest address, XMM0 is the
       result, and ESI returns the abort status. Preserve source registers. */
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &helper_calls);
    host_x86_MOV32_REG_BASE_OFFSET(&test_block, REG_ECX, REG_RDI, 0);
    host_x86_ADD32_REG_IMM(&test_block, REG_ECX, 1);
    host_x86_MOV32_BASE_OFFSET_REG(&test_block, REG_RDI, 0, REG_ECX);
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &fault_on_call);
    host_x86_MOV32_REG_BASE_OFFSET(&test_block, REG_EDI, REG_RDI, 0);
    host_x86_CMP32_REG_REG(&test_block, REG_ECX, REG_EDI);
    uint32_t *fault = host_x86_JZ_long(&test_block);
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) memory);
    host_x86_MOVQ_XREG_BASE_INDEX(&test_block, REG_XMM_TEMP, REG_RDI, REG_RSI);
    host_x86_XOR32_REG_REG(&test_block, REG_ESI, REG_ESI);
    host_x86_RET(&test_block);
    codegen_set_jump_dest(&test_block, fault);
    host_x86_MOV32_REG_IMM(&test_block, REG_ESI, 1);
    host_x86_RET(&test_block);

    codegen_exit_rout = start_code();
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &aborted);
    host_x86_MOV32_BASE_OFFSET_IMM(&test_block, REG_RDI, 0, 1);
    store_result();
    codegen_backend_epilogue(&test_block);
}

static void run_load(uint32_t segment, uint32_t address, uint32_t offset,
                     int a16, int padding, int expected_calls, int fault)
{
    next_chunk = 0;
    helper_calls = aborted = 0;
    fault_on_call = fault;
    build_helper();
    uint8_t *entry = start_code();
    codegen_backend_prologue(&test_block);
    codegen_backend_mem_begin();
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) sentinel);
    host_x86_MOVDQU_XREG_BASE_OFFSET(&test_block, REG_XMM1, REG_RDI, 0);
    host_x86_MOV32_REG_IMM(&test_block, REG_EAX, segment);
    host_x86_MOV32_REG_IMM(&test_block, REG_EDX, address);
    for (int i = 0; i < padding; i++)
        host_x86_NOP(&test_block);

    uop_t uop = { 0 };
    uop.dest_reg_a_real = REG_XMM1 | IREG_SIZE_DQ;
    uop.src_reg_a_real = REG_EAX;
    uop.src_reg_b_real = REG_EDX;
    uop.imm_data = offset;
    uop.is_a16 = a16;
    codegen_MEM_LOAD_REG(&test_block, &uop);
    store_result();
    codegen_backend_epilogue(&test_block);
    codegen_backend_mem_finish(&test_block);
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, CODE_SIZE));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + CODE_SIZE);
#endif
    ((void (*)(void)) entry)();

    CHECK(helper_calls == (uint32_t) expected_calls);
    CHECK(aborted == (fault != 0));
    if (fault)
        CHECK(memcmp(result, sentinel, sizeof(result)) == 0);
    else {
        uint32_t linear = segment + address + offset;
        if (a16 && offset)
            linear &= 0xffff;
        CHECK(linear <= sizeof(memory) - sizeof(result));
        CHECK(memcmp(result, memory + linear, sizeof(result)) == 0);
    }
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
    for (unsigned i = 0; i < sizeof(memory); i++)
        memory[i] = (uint8_t) (i * 37 + (i >> 8));
    readlookup2[0] = readlookup2[1] = (uintptr_t) memory;

    run_load(0, 32, 0, 0, 0, 0, 0);
    run_load(16, 16, 16, 0, 0, 0, 0);
    run_load(0, 0xfffffff0, 0x30, 0, 0, 0, 0); /* 32-bit address wrap. */
    readlookup2[1] = (uintptr_t) -1;
    run_load(0, 4096 - 16, 0, 0, 0, 0, 0); /* Last vector in mapped page. */
    run_load(0, 4096 - 8, 0, 0, 0, 2, 0);  /* Crossing uses both helpers. */
    run_load(0, 33, 0, 0, 0, 0, 0); /* Unaligned vectors within a page are inlined. */
    run_load(0, 32, 0, 1, 0, 2, 0); /* Keep the 16-bit path. */
    run_load(0, 0x10000, 32, 1, 0, 2, 0);

    readlookup2[0] = (uintptr_t) -1;
    run_load(0, 32, 0, 0, 0, 2, 0); /* Uncached RAM or device handler. */
    run_load(0, 32, 0, 0, 0, 1, 1); /* First read aborts. */
    run_load(0, 32, 0, 0, 0, 2, 2); /* Second read aborts, XMM unchanged. */

    /* Sweep code-cache boundaries: forward branches must remain valid when
       the allocator inserts a jump to another, nonadjacent code chunk. */
    for (int padding = 650; padding < 900; padding += 7) {
        readlookup2[0] = (uintptr_t) memory;
        run_load(0, 32, 0, 0, padding, 0, 0);
        run_load(0, 33, 0, 0, padding, 0, 0);
        readlookup2[0] = (uintptr_t) -1;
        run_load(0, 32, 0, 0, padding, 2, 0);
    }
#ifdef _WIN32
    CHECK(VirtualFree(code_memory, 0, MEM_RELEASE));
#else
    CHECK(munmap(code_memory, CODE_SIZE) == 0);
#endif
    puts("SSE load execution tests passed");
    return 0;
}
