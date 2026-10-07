/* Exercise relative-JMP decoding and execute unrolled IR with the real x86-64
   backend. Guest instruction bytes, instruction metadata and allocation are
   supplied by the fixture; the unrolling gate and IR duplication are real. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/mman.h>
#endif

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
#include "../../src/codegen_new/codegen_ops_jump.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "case %u, line %d: %s failed\n", cases, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

cpu_state_t cpu_state;
uintptr_t readlookup2[2097152], writelookup2[1048576];
int timing_misaligned, cpu_cyrix_alignment;
uint32_t cr4, pccache = UINT32_MAX;
uint8_t *ram, *pccache2;
int cpu_block_end;
static codeblock_t test_block;
codeblock_t *codeblock = &test_block;
uint16_t *codeblock_hash;

enum { CODE_SIZE = 65536, CHUNK_SIZE = 4096 };
struct mem_block_t { uint8_t *data; };
static struct mem_block_t chunks[CODE_SIZE / CHUNK_SIZE];
static uint8_t *code_memory;
static unsigned next_chunk, cases, lookup_calls;
static uint32_t immediate_addr, immediate_value, instruction_pc;
static unsigned immediate_size;
static int instruction_uop, instruction_top;

void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}
void x86illegal(void) { fatal("Unexpected #UD\n"); }
void x86_int(int vector) { fatal("Unexpected interrupt %d\n", vector); }

struct mem_block_t *codegen_allocator_allocate(struct mem_block_t *parent, int nr)
{
    (void) parent;
    (void) nr;
    CHECK(next_chunk < CODE_SIZE / CHUNK_SIZE);
    chunks[next_chunk].data = code_memory + next_chunk * CHUNK_SIZE;
    return &chunks[next_chunk++];
}
uint8_t *codeblock_allocator_get_ptr(struct mem_block_t *block) { return block->data; }

int codegen_get_instruction_uop(codeblock_t *block, uint32_t addr, int *first, int *top)
{
    (void) block;
    lookup_calls++;
    *first = 0;
    *top = instruction_top;
    return addr == instruction_pc ? instruction_uop : -1;
}
void codegen_set_loop_start(ir_data_t *ir, int first)
{
    (void) ir;
    CHECK(first == 0); /* These loops do not change decoding context. */
}

uint8_t *getpccache(uint32_t addr) { (void) addr; return NULL; }
uint8_t readmembl(uint32_t addr)
{
    unsigned byte = addr - immediate_addr;
    CHECK(byte < immediate_size);
    return immediate_value >> (byte * 8);
}
uint16_t readmemwl(uint32_t addr)
{
    return readmembl(addr) | ((uint16_t) readmembl(addr + 1) << 8);
}
uint32_t readmemll(uint32_t addr)
{
    return readmemwl(addr) | ((uint32_t) readmemwl(addr + 2) << 16);
}
uint64_t readmemql(uint32_t addr) { (void) addr; CHECK(0); return 0; }
void writememql(uint32_t addr, uint64_t value) { (void) addr; (void) value; CHECK(0); }

const uOpFn uop_handlers[UOP_MAX] = {
    [UOP_MOV_IMM & UOP_MASK] = codegen_MOV_IMM,
    [UOP_ADD_IMM & UOP_MASK] = codegen_ADD_IMM,
    [UOP_CMP_IMM_JNZ_DEST & UOP_MASK] = codegen_CMP_IMM_JNZ_DEST,
    [UOP_JMP & UOP_MASK] = codegen_JMP,
};

static ir_data_t *reset_case(void)
{
    cases++;
    memset(&cpu_state, 0, sizeof(cpu_state));
    memset(&test_block, 0, sizeof(test_block));
    cpu_state.seg_cs.base = 0x10000;
    test_block.pc = cpu_state.seg_cs.base + 0x100;
    cpu_state.oldpc = 0x110;
    instruction_pc = 0x100;
    instruction_uop = instruction_top = 0;
    lookup_calls = next_chunk = 0;
    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    ir->block = &test_block;
    return ir;
}

static uint32_t translate(ir_data_t *ir, unsigned size, uint32_t op32,
                          uint32_t operand_pc, uint32_t displacement)
{
    immediate_addr = cpu_state.seg_cs.base + operand_pc;
    immediate_value = displacement;
    immediate_size = size;
    switch (size) {
        case 1: return ropJMP_r8(&test_block, ir, 0xeb, 0, op32, operand_pc);
        case 2: return ropJMP_r16(&test_block, ir, 0xe9, 0, op32, operand_pc);
        default: return ropJMP_r32(&test_block, ir, 0xe9, 0, op32, operand_pc);
    }
}

static void check_decoding(void)
{
    static const struct {
        unsigned size;
        uint32_t op32, operand_pc, displacement, target;
        int unroll;
    } tests[] = {
        {1, 0,     0x111, 0xee,       0x100, 1},
        {1, 0x100, 0x111, 0xee,       0x100, 1},
        {1, 0x200, 0x111, 0xee,       0x100, 1},
        {1, 0x300, 0x111, 0xee,       0x100, 1},
        {2, 0,     0x111, 0xffed,     0x100, 1},
        {4, 0x100, 0x111, 0xffffffeb, 0x100, 1},
        {1, 0x100, 0x180, 0x80,       0x101, 1},
        {2, 0,     0x80fe, 0x8000,    0x100, 1},
        {4, 0x100, 0x800000fc, 0x80000000, 0x100, 1},
        {1, 0x100, 0x111, 0,          0x112, 0},
        {2, 0,     0x111, 0,          0x113, 0},
        {4, 0x100, 0x111, 0,          0x115, 0},
        {1, 0x100, 0x111, 0x7f,       0x191, 0},
        {2, 0,     0x111, 0x7fff,     0x8112, 0},
        {4, 0x100, 0x111, 0x7fffffff, 0x80000114, 0},
        /* Operand-size wrapping must not turn forward jumps into loops. */
        {1, 0,     0xffff, 1,         1, 0},
        {1, 0x100, 0xffff, 1,         0x10001, 0},
        {2, 0,     0xffff, 1,         2, 0},
        {4, 0x100, 0xfffffffd, 1,     2, 0},
        {1, 0,     1, 0xfc,          0xfffe, 0},
        {2, 0,     1, 0xfffc,        0xffff, 0},
        {4, 0x100, 1, 0xfffffff8,    0xfffffffd, 0},
    };
    for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); i++) {
        ir_data_t *ir = reset_case();
        instruction_pc = tests[i].target;
        CHECK(translate(ir, tests[i].size, tests[i].op32, tests[i].operand_pc,
                        tests[i].displacement) == tests[i].target);
        CHECK((codegen_unroll_count > 1) == tests[i].unroll);
    }
}

static void check_guards(unsigned size)
{
    for (unsigned guard = 0; guard < 8; guard++) {
        ir_data_t *ir = reset_case();
        switch (guard) {
            case 0: test_block.flags = CODEBLOCK_BYTE_MASK; break;
            case 1: test_block.pc++; break; /* Target precedes block. */
            case 2: instruction_pc++; break; /* Not an instruction boundary. */
            case 3: instruction_top = 1; break;
            case 4: ir->uops[ir->wr_pos++].type = UOP_CALL_INSTRUCTION_FUNC; break;
            case 5: max_version_refcount = 101; break;
            case 6:
                for (int i = 0; i < 500; i++)
                    ir->uops[ir->wr_pos++].type = UOP_ADD_IMM;
                break;
            case 7: /* -1 lands inside the immediate, beyond op_pc + 1
                       for rel16/rel32. Check the real instruction end. */
                CHECK(translate(ir, size, 0x100, 0x111, UINT32_MAX) == 0x110 + size);
                CHECK(lookup_calls == 1 && codegen_unroll_count == 0);
                continue;
        }
        CHECK(translate(ir, size, 0x100, 0x111, 0x100 - (0x111 + size)) == 0x100);
        CHECK(codegen_unroll_count == 0);
    }
}

static uint8_t *start_code(void)
{
    test_block.head_mem_block = codegen_allocator_allocate(NULL, 0);
    block_write_data = codeblock_allocator_get_ptr(test_block.head_mem_block);
    block_pos = 0;
    return block_write_data;
}

static void check_execution(unsigned size, unsigned limit, int prefix)
{
    ir_data_t *ir = reset_case();
    codegen_exit_rout = start_code();
    codegen_backend_epilogue(&test_block);
    if (prefix) {
        /* Instructions before the loop target must run only once per block. */
        uop_ADD_IMM(ir, IREG_EBX, IREG_EBX, 1);
        instruction_uop = ir->wr_pos;
    }
    if (limit) {
        uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, 1);
        uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -1);
        int jump = uop_CMP_IMM_JNZ_DEST(ir, IREG_EAX, limit);
        uop_MOV_IMM(ir, IREG_pc, 0x200);
        uop_JMP(ir, codegen_exit_rout);
        uop_set_jump_dest(ir, jump);
        uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -2);
    } else {
        /* A self-jump still returns after the bounded number of copies. */
        cpu_state.oldpc = instruction_pc;
        uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -3);
    }
    uint32_t operand_pc = limit ? 0x111 : instruction_pc + 1;
    uint32_t target = translate(ir, size, 0x100, operand_pc,
                                instruction_pc - (operand_pc + size));
    CHECK(target == instruction_pc);
    unsigned copies = codegen_unroll_count;
    CHECK(copies > 1 && copies <= 10);
    uop_MOV_IMM(ir, IREG_pc, target); /* Normally emitted by codegen_generate_call. */
    void (*run)(void) = (void (*)(void)) start_code();
    codegen_ir_compile(ir, &test_block);
    cpu_state.pc = target;
    cycles = limit ? 1000 : 1;
    unsigned calls = 0;
    do {
        run();
        calls++;
        CHECK(calls <= limit + 1);
        if (limit && cpu_state.pc == target) {
            CHECK(EAX == calls * copies);
            CHECK(cycles == 1000 - (int) (3 * EAX));
        }
    } while (limit && cpu_state.pc == target);
    CHECK(EBX == (prefix ? calls : 0));
    if (limit) {
        CHECK(EAX == limit && cpu_state.pc == 0x200);
        CHECK(cycles == 1000 - (int) (3 * limit - 2));
        CHECK(calls == (limit + copies - 1) / copies);
    } else {
        CHECK(cpu_state.pc == target && cycles == 1 - (int) (3 * copies));
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
    check_decoding();
    const unsigned limits[] = {0, 1, 3, 9, 10, 11, 25};
    for (unsigned size = 1; size <= 4; size *= 2) {
        check_guards(size);
        for (unsigned i = 0; i < sizeof(limits) / sizeof(limits[0]); i++)
            for (int prefix = 0; prefix <= 1; prefix++)
                check_execution(size, limits[i], prefix);
    }
#ifdef _WIN32
    CHECK(VirtualFree(code_memory, 0, MEM_RELEASE));
#else
    CHECK(munmap(code_memory, CODE_SIZE) == 0);
#endif
    printf("Relative JMP tests passed (%u decoding, unroll guard and execution cases)\n", cases);
    return 0;
}
