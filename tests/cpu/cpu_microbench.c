/* Standalone x86-64 dynarec benchmarks. Compile real IR and execute the real
   allocator/emitter/helper ABI; supply only RAM callbacks and executable memory.
   Execution is measured by default; --measure compile times block creation. */
#include <errno.h>
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <cpuid.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/mman.h>
#endif

/* As in ram_register_test, retain only the handlers used by this fixture. */
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
#include "../../src/codegen_new/codegen_ops_mov.h"

#ifndef CPU_BENCH_BUILD
#    define CPU_BENCH_BUILD "standalone"
#endif
enum { CODE_SIZE = 1024 * 1024, CHUNK_SIZE = 4096, RAM_SIZE = 16 * 1024 * 1024,
       MAX_CASES = 1024, MAX_SAMPLES = 99 };
enum { MEMORY, INTEGER_ADD, MMX_ADD, SSE_INTEGER, SSE_ADD, SSE_MUL, SSE_ENTRY, EMPTY,
       INTEGER_XOR, INTEGER_MUL, INTEGER_SHIFT, SSE_SCALAR_ADD, SSE_SHUFFLE, JMP_LOOP, MOVS, MIXED_STORES };
enum { REG_FORM, ABS_FORM, IMM_FORM, SINGLE_FORM, DOUBLE_FORM };

typedef struct {
    char name[128];
    int kind, size, store, form, cached_cycles, live_regs, entry_checks;
    uint32_t address, working_set;
    int lookup_miss, gpr_pressure, simd_pressure, dynamic_top, loop_body;
    unsigned stride, body_ops;
    int address32, backward, phased_stores;
} bench_case_t;

cpu_state_t cpu_state;
uint32_t cr4;
uint16_t cpu_cur_status;
int timing_misaligned = 3, cpu_cyrix_alignment;
uintptr_t readlookup2[2097152], writelookup2[1048576];
uint8_t *ram;
int cpu_block_end;
static codeblock_t bench_block;
codeblock_t *codeblock = &bench_block;

struct mem_block_t { uint8_t *data; };
static struct mem_block_t chunks[CODE_SIZE / CHUNK_SIZE];
static uint8_t *code_memory;
static _Alignas(64) uint8_t memory[RAM_SIZE];
static unsigned next_chunk, case_count, block_ops = 32;
static unsigned first_codegen_chunk;
static int measure_compile;
static bench_case_t cases[MAX_CASES];
static const char *active_case = "initialization";
static int verifying;
static unsigned helper_calls;
static uint8_t instruction_bytes[4096];
uint32_t pccache;
uint8_t *pccache2 = instruction_bytes;
static const int pressure_regs[] = { IREG_ECX, IREG_EDX, IREG_ESI, IREG_EDI, IREG_EBP, IREG_ESP };
#ifdef _WIN32
static double tick_ns;
#endif

void fatal(const char *fmt, ...)
{
    va_list ap;
    fprintf(stderr, "\n%s: ", active_case);
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}
#define CHECK(c) do { if (!(c)) fatal("line %d: %s\n", __LINE__, #c); } while (0)

/* MOVS cases use valid segments and the production instruction translator;
   the surrounding instruction decoder is outside this fixture. */
void codegen_check_seg_read(codeblock_t *block, ir_data_t *ir, x86seg *seg)
{
    (void) block; (void) ir; (void) seg;
    CHECK(!(cr0 & 1));
}
void codegen_check_seg_write(codeblock_t *block, ir_data_t *ir, x86seg *seg)
{
    codegen_check_seg_read(block, ir, seg);
}
x86seg *codegen_generate_ea(ir_data_t *ir, x86seg *seg, uint32_t fetchdat, int ssegs,
                          uint32_t *pc, uint32_t op32, int offset)
{
    (void) ir; (void) seg; (void) fetchdat; (void) ssegs;
    (void) pc; (void) op32; (void) offset;
    fatal("unexpected generic effective-address decoder call\n");
    return NULL;
}

void x86illegal(void) { fatal("unexpected #UD\n"); }
void x86_int(int vector) { fatal("unexpected exception %d\n", vector); }
void x86gpf(char *message, uint16_t error) { (void) message; (void) error; fatal("unexpected #GP\n"); }
void codegen_set_loop_start(ir_data_t *ir, int first)
{
    CHECK(first == 0);
    /* Same context restoration as codegen.c, using this fixture's metadata. */
    uop_MOV_IMM(ir, IREG_op32, 0x100);
    uop_MOV_PTR(ir, IREG_ea_seg, &cpu_state.seg_ds);
    uop_MOV_IMM(ir, IREG_ssegs, 0);
    uop_MOV_IMM(ir, IREG_sse_xmm, 0);
}
int codegen_get_instruction_uop(codeblock_t *block, uint32_t addr, int *first, int *top)
{
    (void) block;
    *first = 0;
    *top = 0;
    return addr == 0x100 ? 0 : -1;
}
uint8_t *getpccache(uint32_t addr) { (void) addr; return instruction_bytes; }

struct mem_block_t *codegen_allocator_allocate(struct mem_block_t *parent, int nr)
{
    (void) parent;
    (void) nr;
    CHECK(next_chunk < CODE_SIZE / CHUNK_SIZE);
    /* Gaps expose bad relocation assumptions; emitted chunks keep their
       production size and use the production rollover jump emitter. */
    chunks[next_chunk].data = code_memory + next_chunk * CHUNK_SIZE;
    return &chunks[next_chunk++];
}
uint8_t *codeblock_allocator_get_ptr(struct mem_block_t *block) { return block->data; }

static uint8_t *start_code(void)
{
    bench_block.head_mem_block = codegen_allocator_allocate(NULL, 0);
    block_write_data = codeblock_allocator_get_ptr(bench_block.head_mem_block);
    block_pos = 0;
    return block_write_data;
}

/* Synthetic slow memory: intentionally no devices, page walker or code-page
   invalidation. Counters/bounds checks are enabled only for validation. */
static uint64_t access_memory(uint32_t addr, uint64_t value, unsigned size, int store)
{
    if (verifying) {
        helper_calls++;
        CHECK(addr <= RAM_SIZE - size);
    }
    if (addr & (size - 1))
        cycles = (int32_t) ((uint32_t) cycles - timing_misaligned);
    if (store)
        memcpy(memory + addr, &value, size);
    else {
        value = 0;
        memcpy(&value, memory + addr, size);
    }
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

const uOpFn uop_handlers[UOP_MAX] = {
    [UOP_MOV & UOP_MASK] = codegen_MOV,
    [UOP_MOV_IMM & UOP_MASK] = codegen_MOV_IMM,
    [UOP_MOV_PTR & UOP_MASK] = codegen_MOV_PTR,
    [UOP_CALL_FUNC & UOP_MASK] = codegen_CALL_FUNC,
    [UOP_ADD_IMM & UOP_MASK] = codegen_ADD_IMM,
    [UOP_XOR_IMM & UOP_MASK] = codegen_XOR_IMM,
    [UOP_IMUL_IMM & UOP_MASK] = codegen_IMUL_IMM,
    [UOP_SHL_IMM & UOP_MASK] = codegen_SHL_IMM,
    [UOP_AND_IMM & UOP_MASK] = codegen_AND_IMM,
    [UOP_PADDD & UOP_MASK] = codegen_PADDD,
    [UOP_ADDPS & UOP_MASK] = codegen_ADDPS,
    [UOP_MULPS & UOP_MASK] = codegen_MULPS,
    [UOP_ADDSS & UOP_MASK] = codegen_ADDSS,
    [UOP_SHUFPS & UOP_MASK] = codegen_SHUFPS,
    [UOP_CMP_IMM_JNZ_DEST & UOP_MASK] = codegen_CMP_IMM_JNZ_DEST,
    [UOP_JMP & UOP_MASK] = codegen_JMP,
    [UOP_SSE_ENTER & UOP_MASK] = codegen_SSE_ENTER,
    [UOP_MEM_LOAD_REG & UOP_MASK] = codegen_MEM_LOAD_REG,
    [UOP_MEM_STORE_REG & UOP_MASK] = codegen_MEM_STORE_REG,
    [UOP_MEM_LOAD_ABS & UOP_MASK] = codegen_MEM_LOAD_ABS,
    [UOP_MEM_STORE_ABS & UOP_MASK] = codegen_MEM_STORE_ABS,
    [UOP_MEM_STORE_IMM_8 & UOP_MASK] = codegen_MEM_STORE_IMM_8,
    [UOP_MEM_STORE_IMM_16 & UOP_MASK] = codegen_MEM_STORE_IMM_16,
    [UOP_MEM_STORE_IMM_32 & UOP_MASK] = codegen_MEM_STORE_IMM_32,
    [UOP_MEM_LOAD_SINGLE & UOP_MASK] = codegen_MEM_LOAD_SINGLE,
    [UOP_MEM_STORE_SINGLE & UOP_MASK] = codegen_MEM_STORE_SINGLE,
    [UOP_MEM_LOAD_DOUBLE & UOP_MASK] = codegen_MEM_LOAD_DOUBLE,
    [UOP_MEM_STORE_DOUBLE & UOP_MASK] = codegen_MEM_STORE_DOUBLE,
};

static bench_case_t *add_case(const char *name, int kind)
{
    CHECK(case_count < MAX_CASES);
    bench_case_t *c = &cases[case_count++];
    CHECK(strlen(name) < sizeof(c->name));
    strcpy(c->name, name);
    c->kind = kind;
    c->live_regs = 1;
    c->stride = 64;
    return c;
}

static void make_cases(void)
{
    static const char *locations[] = { "aligned", "unaligned", "cacheline-split", "page-end", "page-split", "lookup-miss" };
    static const char *forms[] = { "reg", "abs", "imm", "float32", "float64" };
    static const uint32_t working_sets[] = { 32768, 1024 * 1024, RAM_SIZE };
    char name[128];
    add_case("control/empty-block", EMPTY);
    for (int store = 0; store < 2; store++) {
        for (int size = 1; size <= 16; size *= 2) {
            for (int location = 0; location < 6; location++) {
                if (size == 1 && location > 0 && location < 5) continue;
                for (int cached = 0; cached < 2; cached++) {
                    snprintf(name, sizeof(name), "ram/%s%u/%s/cycles-%s", store ? "store" : "load", size * 8,
                             locations[location], cached ? "live" : "memory");
                    bench_case_t *c = add_case(name, MEMORY);
                    c->size = size;
                    c->store = store;
                    c->cached_cycles = cached;
                    c->lookup_miss = location == 5;
                    c->address = location == 1 ? 129 : location == 2 ? 127 : location == 3 ? 4096 - size
                               : location == 4 ? 4095 : 128;
                }
            }
        }
        for (int form = ABS_FORM; form <= DOUBLE_FORM; form++) {
            if (!store && form == IMM_FORM) continue;
            for (int unaligned = 0; unaligned < 2; unaligned++) {
                snprintf(name, sizeof(name), "form/%s/%s/%s", store ? "store" : "load", forms[form],
                         unaligned ? "unaligned" : "aligned");
                bench_case_t *c = add_case(name, MEMORY);
                c->size = form == DOUBLE_FORM ? 8 : 4;
                c->form = form;
                c->store = store;
                c->address = 128 + unaligned;
                c->cached_cycles = 1;
            }
        }
        for (int size = 4; size <= 16; size *= 4) {
            for (unsigned w = 0; w < sizeof(working_sets) / sizeof(working_sets[0]); w++) {
                uint32_t working_set = working_sets[w];
                snprintf(name, sizeof(name), "stream/%s%u/%uKiB", store ? "store" : "load", size * 8, working_set / 1024);
                bench_case_t *c = add_case(name, MEMORY);
                c->size = size;
                c->store = store;
                c->working_set = working_set;
            }
        }
    }
    for (int kind = INTEGER_ADD; kind <= SSE_MUL; kind++) {
        static const char *names[] = { "", "integer/add", "mmx/paddd", "sse/paddd", "sse/addps", "sse/mulps" };
        for (int live = 1; live <= 8; live *= 2) {
            if (kind >= SSE_ADD && live == 8) continue; /* XMM7 is the constant source. */
            snprintf(name, sizeof(name), "%s/live-%d", names[kind], live);
            bench_case_t *c = add_case(name, kind);
            c->live_regs = live;
        }
    }
    add_case("sse/entry-checks/coalesced", SSE_ENTRY);
    bench_case_t *c = add_case("sse/entry-checks/with-memory", MEMORY);
    c->size = 16;
    c->address = 128;
    c->entry_checks = 1;

    /* Deliberately stress both register pools across inline and helper paths. */
    for (int store = 0; store < 2; store++)
        for (int size = 2; size <= 16; size *= 2)
            for (int location = 0; location < 3; location++)
                for (int pressure = 0; pressure < 3; pressure++) {
                    snprintf(name, sizeof(name), "pressure/%s%u/%s/%s", store ? "store" : "load", size * 8,
                             location == 0 ? "aligned" : location == 1 ? "page-split" : "lookup-miss",
                             pressure == 0 ? "gpr-6" : pressure == 1 ? "simd-7" : "mixed");
                    c = add_case(name, MEMORY);
                    c->size = size;
                    c->store = store;
                    c->address = location == 1 ? 4095 : 128;
                    c->lookup_miss = location == 2;
                    c->gpr_pressure = pressure == 1 ? 0 : 6;
                    c->simd_pressure = pressure == 0 ? 0 : 7;
                    c->cached_cycles = 1;
                }
    for (int store = 0; store < 2; store++)
        for (int size = 4; size <= 16; size *= 2)
            for (int offset = 1; offset < size; offset++) {
                if (offset == 1 || offset == size - 1) continue; /* Covered above. */
                snprintf(name, sizeof(name), "alignment/%s%u/offset-%d", store ? "store" : "load", size * 8, offset);
                c = add_case(name, MEMORY);
                c->size = size;
                c->store = store;
                c->address = 128 + offset;
                c->cached_cycles = 1;
            }
    for (int store = 0; store < 2; store++)
        for (int form = SINGLE_FORM; form <= DOUBLE_FORM; form++)
            for (int miss = 0; miss < 2; miss++) {
                snprintf(name, sizeof(name), "x87/%s/%s/dynamic-top/%s", store ? "store" : "load",
                         forms[form], miss ? "lookup-miss" : "aligned");
                c = add_case(name, MEMORY);
                c->size = form == SINGLE_FORM ? 4 : 8;
                c->form = form;
                c->store = store;
                c->address = 128;
                c->lookup_miss = miss;
                c->dynamic_top = c->cached_cycles = 1;
            }
    for (int store = 0; store < 2; store++)
        for (int size = 4; size <= 16; size *= 4)
            for (unsigned w = 0; w < sizeof(working_sets) / sizeof(working_sets[0]); w++)
                for (int page_stride = 0; page_stride < 2; page_stride++) {
                    unsigned stride = page_stride ? 4096 : size;
                    snprintf(name, sizeof(name), "stream/%s%u/%uKiB/stride-%u", store ? "store" : "load",
                             size * 8, working_sets[w] / 1024, stride);
                    c = add_case(name, MEMORY);
                    c->size = size;
                    c->store = store;
                    c->working_set = working_sets[w];
                    c->stride = stride;
                }
    for (int kind = INTEGER_XOR; kind <= SSE_SHUFFLE; kind++) {
        const char *names[] = { "integer/xor", "integer/imul", "integer/shl", "sse/addss", "sse/shufps" };
        for (int live = 1; live <= 8; live *= 2) {
            if (kind == SSE_SCALAR_ADD && live == 8) continue;
            snprintf(name, sizeof(name), "%s/live-%d", names[kind - INTEGER_XOR], live);
            c = add_case(name, kind);
            c->live_regs = live;
        }
    }
    c = add_case("sse/entry-checks/at-joins", SSE_ENTRY);
    c->entry_checks = 2;
    for (int store = 0; store < 2; store++)
        for (int location = 0; location < 3; location++) {
            snprintf(name, sizeof(name), "sse/entry-checks/%s/%s", store ? "store" : "load",
                     location == 0 ? "aligned" : location == 1 ? "page-split" : "lookup-miss");
            c = add_case(name, MEMORY);
            c->size = 16;
            c->store = store;
            c->address = location == 1 ? 4095 : 128;
            c->lookup_miss = location == 2;
            c->entry_checks = 1;
            c->simd_pressure = 4;
        }
    for (int size = 1; size <= 4; size *= 2)
        for (unsigned body = 1; body <= 32; body *= 2)
            for (int simd = 0; simd < 2; simd++) {
                snprintf(name, sizeof(name), "loop/jmp%u/%s/body-%u", size * 8, simd ? "sse" : "integer", body);
                c = add_case(name, JMP_LOOP);
                c->size = size;
                c->body_ops = body;
                c->loop_body = simd;
            }
    for (int size = 1; size <= 4; size *= 2)
        for (int a32 = 0; a32 < 2; a32++)
            for (int backward = 0; backward < 2; backward++)
                for (int miss = 0; miss < 2; miss++) {
                    snprintf(name, sizeof(name), "string/movs%u/a%u/%s/%s", size * 8, a32 ? 32 : 16,
                             backward ? "backward" : "forward", miss ? "lookup-miss" : "ram");
                    c = add_case(name, MOVS);
                    c->size = size;
                    c->address = 1024;
                    c->address32 = a32;
                    c->backward = backward;
                    c->lookup_miss = miss;
                }
}

static int memory_reg(const bench_case_t *c)
{
    return c->form >= SINGLE_FORM ? IREG_ST(0) : c->size == 16 ? IREG_XMM(0)
         : c->size == 8 ? IREG_MM(0) : c->size == 4 ? IREG_EBX : c->size == 2 ? IREG_BX : IREG_BL;
}

static void emit_memory(ir_data_t *ir, const bench_case_t *c)
{
    int reg = memory_reg(c);
    if (c->entry_checks) uop_SSE_ENTER(ir);
    if (c->form == ABS_FORM) {
        if (c->store) uop_MEM_STORE_ABS(ir, IREG_DS_base, c->address, reg);
        else uop_MEM_LOAD_ABS(ir, reg, IREG_DS_base, c->address);
    } else if (c->form == IMM_FORM) {
        uop_MEM_STORE_IMM_32(ir, IREG_DS_base, IREG_EAX, 0x12345678);
    } else if (c->form == SINGLE_FORM) {
        if (c->store) uop_MEM_STORE_SINGLE(ir, IREG_DS_base, IREG_EAX, reg);
        else uop_MEM_LOAD_SINGLE(ir, reg, IREG_DS_base, IREG_EAX);
    } else if (c->form == DOUBLE_FORM) {
        if (c->store) uop_MEM_STORE_DOUBLE(ir, IREG_DS_base, IREG_EAX, reg);
        else uop_MEM_LOAD_DOUBLE(ir, reg, IREG_DS_base, IREG_EAX);
    } else {
        if (c->store) uop_MEM_STORE_REG(ir, IREG_DS_base, IREG_EAX, reg);
        else uop_MEM_LOAD_REG(ir, reg, IREG_DS_base, IREG_EAX);
    }
    if (!c->store) {
        /* Each load is the work being measured, even if the next overwrites it. */
        int r = IREG_GET_REG(reg);
        reg_version[r][reg_last_version[r]].flags |= REG_FLAGS_REQUIRED;
    }
    if (c->working_set) {
        uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, c->stride);
        uop_AND_IMM(ir, IREG_EAX, IREG_EAX, c->working_set - 1);
    }
}

static void prepare_codegen(void)
{
    next_chunk = 0;
    memset(&bench_block, 0, sizeof(bench_block));
    start_code();
    build_loadstore_routines(&bench_block);
    codegen_exit_rout = start_code();
    codegen_backend_epilogue(&bench_block);
    first_codegen_chunk = next_chunk;
}

static void flush_code(void)
{
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, CODE_SIZE));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + CODE_SIZE);
#endif
}

static void (*compile_case(const bench_case_t *c, unsigned *jit_bytes, unsigned *work_ops, unsigned *copies))(void)
{
    next_chunk = first_codegen_chunk;
    memset(&bench_block, 0, sizeof(bench_block));
    uint8_t *entry = start_code();
    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    bench_block.flags = CODEBLOCK_HAS_FPU | (c->dynamic_top ? 0 : CODEBLOCK_STATIC_TOP);
    bench_block.TOP = cpu_state.TOP;
    for (int r = 0; r < c->gpr_pressure; r++)
        uop_ADD_IMM(ir, pressure_regs[r], pressure_regs[r], 1);
    for (int r = 1; r <= c->simd_pressure; r++)
        uop_PADDD(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(r));
    if (c->cached_cycles) uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -1);
    unsigned body_ops = c->body_ops ? c->body_ops : block_ops;
    if (c->kind == MIXED_STORES) uop_MOV_IMM(ir, IREG_eaaddr, c->address);
    if (c->kind == MOVS) {
        /* Reset each burst in generated code so repeated timed calls remain
           inside the same two pages; include that small setup in the timing. */
        uop_MOV_IMM(ir, IREG_ESI, c->address);
        uop_MOV_IMM(ir, IREG_EDI, c->address + 4096);
        op_ea_seg = &cpu_state.seg_ds;
    }
    for (unsigned i = 0; i < (c->kind == EMPTY ? 0 : body_ops); i++) {
        int r = i % c->live_regs;
        switch (c->kind) {
            case MEMORY: emit_memory(ir, c); break;
            case MIXED_STORES: {
                /* Changing guest/host mappings generates distinct snapshots,
                   unlike a repeated load with one stable allocator state. */
                r = ((i + 1) * 2654435761u) >> 29;
                unsigned width = c->phased_stores ? i * 3 / body_ops : i % 3;
                uop_ADD_IMM(ir, IREG_32(r), IREG_32(r), i + 1);
                uop_PADDD(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(r));
                uop_MEM_STORE_REG(ir, IREG_DS_base, IREG_eaaddr, c->size ? IREG_32(r)
                                  : width == 0 ? IREG_16(r) : width == 1 ? IREG_32(r) : IREG_MM(0));
                break;
            }
            case MOVS: {
                uint32_t op32 = c->address32 ? 0x200 : 0;
                uint32_t pc = c->size == 1 ? ropMOVS_b(&bench_block, ir, 0xa4, 0, op32, 0x101)
                            : c->size == 2 ? ropMOVS_w(&bench_block, ir, 0xa5, 0, op32, 0x101)
                                           : ropMOVS_l(&bench_block, ir, 0xa5, 0, op32 | 0x100, 0x101);
                CHECK(pc == 0x101);
                break;
            }
            case INTEGER_ADD: uop_ADD_IMM(ir, IREG_32(r), IREG_32(r), 3); break;
            case MMX_ADD: uop_PADDD(ir, IREG_MM(r), IREG_MM(r), IREG_MM(r)); break;
            case SSE_INTEGER: uop_PADDD(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(r)); break;
            case SSE_ADD: uop_ADDPS(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(7)); break;
            case SSE_MUL: uop_MULPS(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(7)); break;
            case INTEGER_XOR: uop_XOR_IMM(ir, IREG_32(r), IREG_32(r), 0x76543210); break;
            case INTEGER_MUL: uop_IMUL_IMM(ir, IREG_32(r), IREG_32(r), 3); break;
            case INTEGER_SHIFT: uop_SHL_IMM(ir, IREG_32(r), IREG_32(r), 1); break;
            case SSE_SCALAR_ADD: uop_ADDSS(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(7)); break;
            case SSE_SHUFFLE: uop_SHUFPS(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(r), 0x1b); break;
            case JMP_LOOP:
                if (c->loop_body) uop_PADDD(ir, IREG_XMM(0), IREG_XMM(0), IREG_XMM(0));
                else uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, 3);
                uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -1);
                break;
            case SSE_ENTRY:
                if (c->entry_checks == 2) {
                    int jump = uop_CMP_IMM_JNZ_DEST(ir, IREG_EAX, 0);
                    uop_set_jump_dest(ir, jump);
                }
                uop_SSE_ENTER(ir);
                break;
        }
    }
    if (c->kind == MIXED_STORES)
        for (int r = 0; r < 8; r++) {
            uop_ADD_IMM(ir, IREG_32(r), IREG_32(r), 1);
            uop_PADDD(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(r));
        }
    for (int r = 0; r < c->gpr_pressure; r++)
        uop_ADD_IMM(ir, pressure_regs[r], pressure_regs[r], 1);
    for (int r = 1; r <= c->simd_pressure; r++)
        uop_PADDD(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(r));
    if (c->cached_cycles) uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -1);
    if (c->kind == JMP_LOOP) {
        bench_block.pc = 0x100;
        cpu_state.oldpc = 0x140;
        uint32_t displacement = 0x100 - (0x141 + c->size);
        memcpy(instruction_bytes + 0x141, &displacement, c->size);
        uint32_t target = c->size == 1 ? ropJMP_r8(&bench_block, ir, 0xeb, 0, 0x100, 0x141)
                        : c->size == 2 ? ropJMP_r16(&bench_block, ir, 0xe9, 0, 0, 0x141)
                        : ropJMP_r32(&bench_block, ir, 0xe9, 0, 0x100, 0x141);
        CHECK(target == 0x100);
        uop_MOV_IMM(ir, IREG_pc, target);
    }
    *copies = codegen_unroll_count ? codegen_unroll_count : 1;
    *work_ops = c->kind == EMPTY ? 1 : body_ops * *copies;
    codegen_ir_compile(ir, &bench_block);
    /* Allocated payload, including unused tails, excludes the shared helpers. */
    *jit_bytes = (next_chunk - first_codegen_chunk - 1) * MEM_BLOCK_SIZE + block_pos;
    return (void (*)(void)) entry;
}

static void reset_state(const bench_case_t *c, int timed)
{
    memset(&cpu_state, 0, sizeof(cpu_state));
    cr4 = CR4_OSFXSR;
    cpu_state.old_fp_control = 0x1f80;
    cpu_state.new_fp_control = 0x1f80;
    cycles = 1000000000;
    cpu_state.TOP = c->dynamic_top ? 3 : 0;
    for (int i = 0; i < 8; i++) {
        cpu_state.regs[i].l = 0x12345678 + i;
        cpu_state.MM[i].q = UINT64_C(0x1234567812345678) + i;
        for (int lane = 0; lane < 4; lane++)
            cpu_state.XMM[i].l[lane] = 0x12345678 + i + lane;
    }
    if (c->kind == MEMORY) {
        EAX = c->address;
        EBX = 0x12345678;
        cpu_state.ST[cpu_state.TOP] = 2.25;
    }
    if (c->kind == MOVS) {
        cpu_state.flags = c->backward ? D_FLAG : 0;
        cpu_state.oldpc = 0x100;
    }
    if (c->kind == SSE_ADD || c->kind == SSE_MUL || c->kind == SSE_SCALAR_ADD) {
        for (int i = 0; i < 8; i++)
            for (int lane = 0; lane < 4; lane++)
                cpu_state.XMM[i].f2[lane] = 1.25f;
        for (int lane = 0; lane < 4; lane++)
            cpu_state.XMM[7].f2[lane] = c->kind == SSE_MUL ? (timed ? 1.0f : 2.0f) : (timed ? 0.0f : 0.25f);
    }
}

static unsigned validate(const bench_case_t *c, void (*entry)(void), unsigned work_ops)
{
    memset(memory, 0xa5, sizeof(memory));
    reset_state(c, 0);
    if (c->kind == MOVS) {
        for (unsigned i = 0; i < 4096; i++) memory[i] = (uint8_t) (i ^ (i >> 8));
        memset(memory + 4096, 0, 4096);
    }
    if (c->kind == MEMORY && c->form >= SINGLE_FORM) {
        float f = 1.25f;
        double d = 1.25;
        memcpy(memory + c->address, c->size == 4 ? (void *) &f : (void *) &d, c->size);
    }
    cpu_state_t expected = cpu_state;
    uint8_t expected_value[16];
    if (c->kind == MEMORY) {
        if (c->form == SINGLE_FORM) {
            float f = 2.25f;
            memcpy(expected_value, &f, 4);
        } else if (c->form == DOUBLE_FORM)
            memcpy(expected_value, &expected.ST[expected.TOP], 8);
        else if (c->size == 16)
            memcpy(expected_value, &expected.XMM[0], 16);
        else if (c->size == 8)
            memcpy(expected_value, &expected.MM[0], 8);
        else
            memcpy(expected_value, &expected.regs[3].l, c->size);
    }
    helper_calls = 0;
    verifying = 1;
    entry();
    verifying = 0;
    CHECK(!cpu_state.abrt);
    for (int r = 0; r < c->gpr_pressure; r++) {
        int reg = IREG_GET_REG(pressure_regs[r]);
        CHECK(cpu_state.regs[reg].l == expected.regs[reg].l + 2);
    }
    for (int r = 1; r <= c->simd_pressure; r++)
        for (int lane = 0; lane < 4; lane++)
            CHECK(cpu_state.XMM[r].l[lane] == expected.XMM[r].l[lane] * 4);
    if (c->kind == MEMORY) {
        if (c->store) {
            for (unsigned i = 0; i < (c->working_set ? block_ops : 1); i++)
                CHECK(memcmp(memory + (c->working_set ? (i * c->stride) & (c->working_set - 1) : c->address),
                             expected_value, c->size) == 0);
        } else if (c->form >= SINGLE_FORM)
            CHECK(cpu_state.ST[cpu_state.TOP] == 1.25);
        else if (c->size == 16) {
            for (int i = 0; i < 4; i++) CHECK(cpu_state.XMM[0].l[i] == 0xa5a5a5a5);
        } else if (c->size == 8)
            CHECK(cpu_state.MM[0].q == UINT64_C(0xa5a5a5a5a5a5a5a5));
        else
            CHECK(EBX == (c->size == 4 ? 0xa5a5a5a5 : c->size == 2 ? 0x1234a5a5 : 0x123456a5));
        if (c->working_set) CHECK(EAX == ((block_ops * c->stride) & (c->working_set - 1)));
        unsigned penalty = c->size == 16 ? (helper_calls && (c->address & 7) ? helper_calls * 3 : 0)
                         : (c->address & (c->size - 1)) ? block_ops * 3 : 0;
        CHECK(cycles == 1000000000 - (int) penalty - c->cached_cycles * 2);
        if (c->working_set) {
            /* Also check the wraparound which long timed streams will reach. */
            EAX = c->working_set - c->stride;
            if (c->store) {
                memset(memory, 0, c->working_set);
            }
            verifying = 1;
            entry();
            verifying = 0;
            CHECK(EAX == (((block_ops - 1) * c->stride) & (c->working_set - 1)));
            if (c->store) {
                CHECK(memcmp(memory + c->working_set - c->stride, expected_value, c->size) == 0);
                for (unsigned i = 0; i + 1 < block_ops; i++)
                    CHECK(memcmp(memory + ((i * c->stride) & (c->working_set - 1)), expected_value, c->size) == 0);
            }
            CHECK(helper_calls == 0);
        }
    } else if (c->kind == MOVS) {
        int step = c->backward ? -c->size : c->size;
        CHECK(ESI == c->address + work_ops * step && EDI == ESI + 4096);
        for (unsigned i = 0; i < work_ops; i++) {
            uint32_t addr = c->address + i * step;
            CHECK(memcmp(memory + addr, memory + addr + 4096, c->size) == 0);
        }
        CHECK(EAX == expected.regs[0].l && ECX == expected.regs[1].l);
        CHECK(cpu_state.flags == expected.flags && cpu_state.oldpc == 0x100);
        CHECK(cycles == expected._cycles);
        CHECK(helper_calls == (c->lookup_miss ? work_ops * 2 : 0));
    } else if (c->kind == MIXED_STORES) {
        uint8_t stored[8];
        memset(stored, 0xa5, sizeof(stored));
        for (unsigned i = 0; i < block_ops; i++) {
            unsigned r = ((i + 1) * 2654435761u) >> 29;
            unsigned width = c->phased_stores ? i * 3 / block_ops : i % 3;
            expected.regs[r].l += i + 1;
            for (int lane = 0; lane < 4; lane++) expected.XMM[r].l[lane] *= 2;
            memcpy(stored, !c->size && width == 2 ? (void *) &expected.MM[0].q : (void *) &expected.regs[r].l,
                   c->size ? 4 : 2 << width);
        }
        CHECK(!memcmp(memory + c->address, stored, sizeof(stored)));
        for (int r = 0; r < 8; r++) expected.regs[r].l++;
        for (int r = 0; r < 8; r++)
            for (int lane = 0; lane < 4; lane++) expected.XMM[r].l[lane] *= 2;
        CHECK(!memcmp(cpu_state.regs, expected.regs, sizeof(expected.regs)));
        CHECK(!memcmp(cpu_state.XMM, expected.XMM, sizeof(expected.XMM)));
        CHECK(!memcmp(cpu_state.MM, expected.MM, sizeof(expected.MM)));
        CHECK(helper_calls == (c->lookup_miss ? block_ops : 0));
        CHECK(cycles == expected._cycles - c->cached_cycles * 2);
    } else if (c->kind == JMP_LOOP) {
        for (unsigned i = 0; i < work_ops; i++) {
            if (c->loop_body)
                for (int lane = 0; lane < 4; lane++) expected.XMM[0].l[lane] *= 2;
            else expected.regs[0].l += 3;
        }
        CHECK(EAX == expected.regs[0].l);
        CHECK(memcmp(cpu_state.XMM, expected.XMM, sizeof(expected.XMM)) == 0);
        CHECK(cpu_state.pc == 0x100 && cycles == 1000000000 - (int) work_ops);
    } else {
        for (unsigned i = 0; i < block_ops; i++) {
            unsigned r = i % c->live_regs;
            if (c->kind == INTEGER_ADD) expected.regs[r].l += 3;
            if (c->kind == INTEGER_XOR) expected.regs[r].l ^= 0x76543210;
            if (c->kind == INTEGER_MUL) expected.regs[r].l *= 3;
            if (c->kind == INTEGER_SHIFT) expected.regs[r].l <<= 1;
            if (c->kind == SSE_SCALAR_ADD) expected.XMM[r].f2[0] += 0.25f;
            if (c->kind == SSE_SHUFFLE) {
                for (int lane = 0; lane < 2; lane++) {
                    uint32_t tmp = expected.XMM[r].l[lane];
                    expected.XMM[r].l[lane] = expected.XMM[r].l[3 - lane];
                    expected.XMM[r].l[3 - lane] = tmp;
                }
            }
            for (int lane = 0; lane < 4; lane++) {
                if (c->kind == MMX_ADD && lane < 2) expected.MM[r].l[lane] *= 2;
                if (c->kind == SSE_INTEGER) expected.XMM[r].l[lane] *= 2;
                if (c->kind == SSE_ADD) expected.XMM[r].f2[lane] += 0.25f;
                if (c->kind == SSE_MUL) expected.XMM[r].f2[lane] *= 2.0f;
            }
        }
        if (c->kind == INTEGER_ADD || (c->kind >= INTEGER_XOR && c->kind <= INTEGER_SHIFT))
            CHECK(memcmp(cpu_state.regs, expected.regs, sizeof(expected.regs)) == 0);
        if (c->kind == MMX_ADD) CHECK(memcmp(cpu_state.MM, expected.MM, sizeof(expected.MM)) == 0);
        if ((c->kind >= SSE_INTEGER && c->kind <= SSE_MUL) || c->kind == SSE_SCALAR_ADD || c->kind == SSE_SHUFFLE)
            CHECK(memcmp(cpu_state.XMM, expected.XMM, sizeof(expected.XMM)) == 0);
    }
    return helper_calls;
}

static double now_ns(void)
{
#ifdef _WIN32
    LARGE_INTEGER t;
    CHECK(QueryPerformanceCounter(&t));
    return t.QuadPart * tick_ns;
#else
    struct timespec t;
    CHECK(clock_gettime(CLOCK_MONOTONIC, &t) == 0);
    return (double) t.tv_sec * 1e9 + t.tv_nsec;
#endif
}

/* Keep the call boundary in the measurement: real blocks also have entry,
   register load/writeback and exit costs. Never subtract an empty-loop result. */
static double measure(void (*entry)(void), unsigned iterations)
{
    double begin = now_ns();
    for (unsigned i = 0; i < iterations; i++) entry();
    return now_ns() - begin;
}

static double measure_compilation(const bench_case_t *c, unsigned iterations)
{
    unsigned bytes, ops, copies;
    double begin = now_ns();
    for (unsigned i = 0; i < iterations; i++) compile_case(c, &bytes, &ops, &copies);
    return now_ns() - begin;
}
static int compare_double(const void *a, const void *b)
{
    double x = *(const double *) a, y = *(const double *) b;
    return (x > y) - (x < y);
}

static unsigned number(const char *text, unsigned min, unsigned max)
{
    char *end;
    errno = 0;
    unsigned long n = strtoul(text, &end, 10);
    if (errno || !*text || *end || n < min || n > max) fatal("invalid numeric argument: %s\n", text);
    return (unsigned) n;
}

static double baseline[MAX_CASES];
static void load_baseline(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) fatal("cannot open baseline %s: %s\n", path, strerror(errno));
    char line[8192], name[128];
    unsigned ops, iterations, samples, matched = 0;
    double median;
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' && strstr(line, "measure=compile"))
            fatal("cannot use a compilation baseline for execution timings\n");
        if (line[0] == '#' || !strncmp(line, "case,", 5)) continue;
        if (sscanf(line, "%127[^,],%u,%u,%u,%lf", name, &ops, &iterations, &samples, &median) != 5 ||
            !isfinite(median) || median <= 0) fatal("invalid baseline row\n");
        for (unsigned i = 0; i < case_count; i++) {
            if (strcmp(name, cases[i].name)) continue;
            if (cases[i].kind != JMP_LOOP && ops != (cases[i].kind == EMPTY ? 1 : block_ops))
                fatal("baseline block size differs for %s\n", name);
            if (baseline[i]) fatal("duplicate baseline row for %s\n", name);
            baseline[i] = median;
            matched++;
        }
    }
    CHECK(!ferror(f));
    fclose(f);
    if (!matched) fatal("no matching baseline cases\n");
}

static void metadata(FILE *out)
{
    char brand[49] = { 0 };
    unsigned a, b, c, d;
    if (__get_cpuid_max(0x80000000, NULL) >= 0x80000004) {
        for (unsigned i = 0; i < 3; i++) {
            __cpuid(0x80000002 + i, a, b, c, d);
            memcpy(brand + i * 16, &a, 4); memcpy(brand + i * 16 + 4, &b, 4);
            memcpy(brand + i * 16 + 8, &c, 4); memcpy(brand + i * 16 + 12, &d, 4);
        }
    }
    fprintf(out, "# cpu_microbench v2; host=%s\n# build=%s; compiler=%s; compiled=%s %s\n",
            brand, CPU_BENCH_BUILD, __VERSION__, __DATE__, __TIME__);
    fprintf(out, "# block_ops=%u; host_gprs=%d; host_simd=%d; misalignment_cycles=%d; measure=%s\n",
            block_ops, CODEGEN_HOST_REGS, CODEGEN_HOST_FP_REGS, timing_misaligned,
            measure_compile ? "compile" : "execute");
}

int main(int argc, char **argv)
{
    unsigned samples = 21, sample_ms = 75, warmup_ms = 100;
    const char *filter = "", *exact_case = NULL, *csv_path = NULL, *baseline_path = NULL;
    int list = 0, affinity_cpu = -1;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            puts("cpu_microbench [--filter substring] [--samples 3..99] [--sample-ms 1..1000]\n"
                 "               [--block-ops 1..64] [--csv result.csv] [--baseline old.csv]\n"
                 "               [--case exact-name] [--warmup-ms 0..5000] [--cpu 0..63 (Windows)]\n"
                 "               [--list] [--quick]\n"
                 "               [--measure execute|compile] (compile: ns/block; compile/ cases accept up to 192 ops)\n"
                 "Lower ns/op is better. Positive baseline delta means slower.\n"
                 "Measures generated blocks, not a complete guest CPU or emulated MHz.");
            return 0;
        } else if (!strcmp(argv[i], "--list")) list = 1;
        else if (!strcmp(argv[i], "--quick")) { samples = 3; sample_ms = 1; warmup_ms = 1; }
        else {
            const char *option = argv[i];
            if (++i == argc) fatal("missing value for %s\n", option);
            if (!strcmp(option, "--filter")) filter = argv[i];
            else if (!strcmp(option, "--case")) exact_case = argv[i];
            else if (!strcmp(option, "--samples")) samples = number(argv[i], 3, MAX_SAMPLES);
            else if (!strcmp(option, "--sample-ms")) sample_ms = number(argv[i], 1, 1000);
            else if (!strcmp(option, "--warmup-ms")) warmup_ms = number(argv[i], 0, 5000);
            else if (!strcmp(option, "--cpu")) affinity_cpu = number(argv[i], 0, 63);
            else if (!strcmp(option, "--block-ops")) block_ops = number(argv[i], 1, 192);
            else if (!strcmp(option, "--csv")) csv_path = argv[i];
            else if (!strcmp(option, "--baseline")) baseline_path = argv[i];
            else if (!strcmp(option, "--measure")) {
                if (strcmp(argv[i], "compile") && strcmp(argv[i], "execute")) fatal("invalid measurement mode\n");
                measure_compile = !strcmp(argv[i], "compile");
            }
            else fatal("unknown option: %s\n", option);
        }
    }
    make_cases();
    if (measure_compile) {
        bench_case_t *c = &cases[case_count++];
        *c = (bench_case_t) { .kind = MIXED_STORES, .address = 64, .size = 4, .store = 1, .live_regs = 8 };
        strcpy(c->name, "compile/mixed-stores");
        c = &cases[case_count++];
        *c = cases[case_count - 2];
        c->size = 0;
        c->cached_cycles = 1;
        strcpy(c->name, "compile/mixed-width-stores");
        c = &cases[case_count++];
        *c = cases[case_count - 2];
        c->phased_stores = 1;
        strcpy(c->name, "compile/phased-stores");
    }
    if (measure_compile && baseline_path) fatal("use the paired runner for compilation comparisons\n");
    unsigned selected = 0;
    for (unsigned i = 0; i < case_count; i++) {
        if (!strstr(cases[i].name, filter)) continue;
        if (exact_case && strcmp(cases[i].name, exact_case)) continue;
        if (block_ops > 64 && cases[i].kind != MIXED_STORES) fatal("only compile/ cases support more than 64 ops\n");
        selected++;
        if (list) puts(cases[i].name);
    }
    if (!selected) fatal("no cases match '%s'\n", filter);
    if (list) return 0;
    if (affinity_cpu >= 0) {
#ifdef _WIN32
        CHECK(SetProcessAffinityMask(GetCurrentProcess(), (DWORD_PTR) 1 << affinity_cpu));
#else
        fatal("--cpu is supported on Windows; use taskset on Linux\n");
#endif
    }
    if (csv_path && baseline_path && !strcmp(csv_path, baseline_path)) fatal("use different output and baseline paths\n");
    if (baseline_path) load_baseline(baseline_path);
    FILE *csv = csv_path ? fopen(csv_path, "w") : NULL;
    if (csv_path && !csv) fatal("cannot write %s: %s\n", csv_path, strerror(errno));
#ifdef _WIN32
    LARGE_INTEGER frequency;
    CHECK(QueryPerformanceFrequency(&frequency));
    tick_ns = 1e9 / frequency.QuadPart;
    code_memory = VirtualAlloc(NULL, CODE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    CHECK(code_memory != NULL);
#else
    code_memory = mmap(NULL, CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(code_memory != MAP_FAILED);
#endif
    memset(readlookup2, 0xff, sizeof(readlookup2));
    memset(writelookup2, 0xff, sizeof(writelookup2));
    for (unsigned i = 0; i < RAM_SIZE / 4096; i++) readlookup2[i] = writelookup2[i] = (uintptr_t) memory;
    metadata(stdout);
    printf("%u cases, %u samples, target %u ms/sample; units %s\n", selected, samples, sample_ms,
           measure_compile ? "ns/compiled block" : "ns/operation (empty: ns/block)");
    puts("Case                                                   median        min        p95    delta   helpers/block");
    if (csv) {
        metadata(csv);
        fprintf(csv, "# samples=%u; target_ms=%u; warmup_ms=%u; affinity_cpu=%d\n", samples, sample_ms, warmup_ms, affinity_cpu);
        fputs("case,ops_per_block,iterations,samples,median_ns,min_ns,p95_ns,helpers_per_block,jit_bytes,mean_ns,stddev_ns,cv_pct,mad_ns,max_ns,body_ops,unroll_copies,measured_ms,measure,slow_sites,slow_stubs", csv);
        for (unsigned i = 0; i < samples; i++) fprintf(csv, ",sample_%u_ns", i + 1);
        fputc('\n', csv);
    }
    for (unsigned index = 0; index < case_count; index++) {
        bench_case_t *c = &cases[index];
        if (!strstr(c->name, filter)) continue;
        if (exact_case && strcmp(c->name, exact_case)) continue;
        active_case = c->name;
        readlookup2[c->address >> 12] = writelookup2[c->address >> 12] = c->lookup_miss ? (uintptr_t) -1 : (uintptr_t) memory;
        if (c->kind == MOVS)
            writelookup2[1] = c->lookup_miss ? (uintptr_t) -1 : (uintptr_t) memory;
        reset_state(c, 0);
        prepare_codegen();
        unsigned jit_bytes, ops, copies;
        void (*entry)(void) = compile_case(c, &jit_bytes, &ops, &copies);
        flush_code();
        unsigned calls = validate(c, entry, ops);
        unsigned divisor = measure_compile ? 1 : ops;
        int slow_sites = -1, slow_stubs = -1;
#ifdef CODEGEN_BACKEND_HAS_MEM_STUBS
        slow_sites = mem_slow_count;
        slow_stubs = 0;
        for (int i = 0; i < mem_slow_count; i++) slow_stubs += mem_slow_sites[i].owner == i;
#endif
        /* Warm the code/data and calibrate enough work to amortize the timer.
           Reset state outside timing; floating-point timing inputs stay finite. */
        unsigned iterations = 1024;
        double elapsed;
        do {
            reset_state(c, 1);
            elapsed = measure_compile ? measure_compilation(c, iterations) : measure(entry, iterations);
            if (elapsed >= 5e6 || iterations >= (1u << 27)) break;
            iterations *= 2;
        } while (1);
        double estimate = ceil(iterations * sample_ms * 1e6 / elapsed);
        iterations = estimate > (1u << 28) ? (1u << 28) : estimate < 1024 ? 1024 : (unsigned) estimate;
        double warm_end = now_ns() + warmup_ms * 1e6;
        do {
            reset_state(c, 1);
            if (measure_compile) measure_compilation(c, iterations);
            else measure(entry, iterations);
        } while (now_ns() < warm_end);
        double values[MAX_SAMPLES], sorted[MAX_SAMPLES];
        double mean = 0, variance = 0, measured_ms = 0;
        for (unsigned s = 0; s < samples; s++) {
            reset_state(c, 1);
            elapsed = measure_compile ? measure_compilation(c, iterations) : measure(entry, iterations);
            measured_ms += elapsed / 1e6;
            values[s] = elapsed / ((double) iterations * divisor);
            CHECK(isfinite(values[s]) && values[s] > 0);
            sorted[s] = values[s];
            mean += values[s] / samples;
        }
        qsort(sorted, samples, sizeof(*sorted), compare_double);
        double median = (sorted[(samples - 1) / 2] + sorted[samples / 2]) / 2;
        double p95 = sorted[(95 * samples + 99) / 100 - 1];
        double deviations[MAX_SAMPLES];
        for (unsigned s = 0; s < samples; s++) {
            variance += (values[s] - mean) * (values[s] - mean);
            deviations[s] = fabs(values[s] - median);
        }
        qsort(deviations, samples, sizeof(*deviations), compare_double);
        double stddev = sqrt(variance / (samples - 1));
        double mad = (deviations[(samples - 1) / 2] + deviations[samples / 2]) / 2;
        printf("%-52s %10.3f %10.3f %10.3f ", c->name, median, sorted[0], p95);
        if (baseline[index]) printf("%+7.1f%%", (median / baseline[index] - 1) * 100);
        else printf("      --");
        printf(" %8u\n", calls);
        fflush(stdout);
        if (csv) {
            fprintf(csv, "%s,%u,%u,%u,%.9f,%.9f,%.9f,%u,%u", c->name, divisor, iterations, samples,
                    median, sorted[0], p95, calls, jit_bytes);
            fprintf(csv, ",%.9f,%.9f,%.6f,%.9f,%.9f,%u,%u,%.3f,%s,%d,%d", mean, stddev, stddev / mean * 100,
                    mad, sorted[samples - 1], c->body_ops ? c->body_ops : block_ops, copies, measured_ms,
                    measure_compile ? "compile" : "execute", slow_sites, slow_stubs);
            for (unsigned s = 0; s < samples; s++) fprintf(csv, ",%.9f", values[s]);
            fputc('\n', csv);
            CHECK(fflush(csv) == 0);
        }
        if (measure_compile) {
            reset_state(c, 0);
            flush_code();
            CHECK(validate(c, entry, ops) == calls);
        }
        readlookup2[c->address >> 12] = writelookup2[c->address >> 12] = (uintptr_t) memory;
        if (c->kind == MOVS) writelookup2[1] = (uintptr_t) memory;
    }
    if (csv && fclose(csv)) fatal("failed to finish CSV output\n");
#ifdef _WIN32
    CHECK(VirtualFree(code_memory, 0, MEM_RELEASE));
#else
    CHECK(munmap(code_memory, CODE_SIZE) == 0);
#endif
    puts(measure_compile ? "All selected cases validated. Compilation timed; execution excluded."
                         : "All selected cases validated. Compilation and validation excluded from timings.");
    return 0;
}
