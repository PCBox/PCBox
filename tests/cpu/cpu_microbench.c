/* Standalone x86-64 dynarec benchmarks. Compile real IR and execute the real
   allocator/emitter/helper ABI; supply only RAM callbacks and executable memory.
   Compilation, validation and printing are outside the timed batches. */
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

#ifndef CPU_BENCH_BUILD
#    define CPU_BENCH_BUILD "standalone"
#endif
enum { CODE_SIZE = 1024 * 1024, CHUNK_SIZE = 4096, RAM_SIZE = 16 * 1024 * 1024,
       MAX_CASES = 256, MAX_SAMPLES = 99 };
enum { MEMORY, INTEGER_ADD, MMX_ADD, SSE_INTEGER, SSE_ADD, SSE_MUL, SSE_ENTRY, EMPTY };
enum { REG_FORM, ABS_FORM, IMM_FORM, SINGLE_FORM, DOUBLE_FORM };

typedef struct {
    char name[128];
    int kind, size, store, form, cached_cycles, live_regs, entry_checks;
    uint32_t address, working_set;
    int lookup_miss;
} bench_case_t;

cpu_state_t cpu_state;
uint32_t cr4;
int timing_misaligned = 3, cpu_cyrix_alignment;
uintptr_t readlookup2[2097152], writelookup2[1048576];
uint8_t *ram, *block_write_data;
int block_pos, cpu_block_end;
static codeblock_t bench_block;
codeblock_t *codeblock = &bench_block;

struct mem_block_t { uint8_t *data; };
static struct mem_block_t chunks[CODE_SIZE / CHUNK_SIZE];
static uint8_t *code_memory;
static _Alignas(64) uint8_t memory[RAM_SIZE];
static unsigned next_chunk, case_count, block_ops = 32;
static bench_case_t cases[MAX_CASES];
static const char *active_case = "initialization";
static int verifying;
static unsigned helper_calls;
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

void x86illegal(void) { fatal("unexpected #UD\n"); }
void x86_int(int vector) { fatal("unexpected exception %d\n", vector); }
void x86gpf(char *message, uint16_t error) { (void) message; (void) error; fatal("unexpected #GP\n"); }
void codegen_set_loop_start(ir_data_t *ir, int first) { (void) ir; (void) first; }

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
    [UOP_ADD_IMM & UOP_MASK] = codegen_ADD_IMM,
    [UOP_AND_IMM & UOP_MASK] = codegen_AND_IMM,
    [UOP_PADDD & UOP_MASK] = codegen_PADDD,
    [UOP_ADDPS & UOP_MASK] = codegen_ADDPS,
    [UOP_MULPS & UOP_MASK] = codegen_MULPS,
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
        uop_ADD_IMM(ir, IREG_EAX, IREG_EAX, 64);
        uop_AND_IMM(ir, IREG_EAX, IREG_EAX, c->working_set - 1);
    }
}

static void (*compile_case(const bench_case_t *c, unsigned *jit_bytes))(void)
{
    next_chunk = 0;
    memset(&bench_block, 0, sizeof(bench_block));
    start_code();
    build_loadstore_routines(&bench_block);
    codegen_exit_rout = start_code();
    codegen_backend_epilogue(&bench_block);
    unsigned first_chunk = next_chunk;
    uint8_t *entry = start_code();
    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    bench_block.flags = CODEBLOCK_STATIC_TOP | CODEBLOCK_HAS_FPU;
    if (c->cached_cycles) uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -1);
    for (unsigned i = 0; i < (c->kind == EMPTY ? 0 : block_ops); i++) {
        int r = i % c->live_regs;
        switch (c->kind) {
            case MEMORY: emit_memory(ir, c); break;
            case INTEGER_ADD: uop_ADD_IMM(ir, IREG_32(r), IREG_32(r), 3); break;
            case MMX_ADD: uop_PADDD(ir, IREG_MM(r), IREG_MM(r), IREG_MM(r)); break;
            case SSE_INTEGER: uop_PADDD(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(r)); break;
            case SSE_ADD: uop_ADDPS(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(7)); break;
            case SSE_MUL: uop_MULPS(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(7)); break;
            case SSE_ENTRY: uop_SSE_ENTER(ir); break;
        }
    }
    if (c->cached_cycles) uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -1);
    codegen_ir_compile(ir, &bench_block);
    /* Allocated payload, including unused tails, excludes the shared helpers. */
    *jit_bytes = (next_chunk - first_chunk - 1) * MEM_BLOCK_SIZE + block_pos;
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, CODE_SIZE));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + CODE_SIZE);
#endif
    return (void (*)(void)) entry;
}

static void reset_state(const bench_case_t *c, int timed)
{
    memset(&cpu_state, 0, sizeof(cpu_state));
    cr4 = CR4_OSFXSR;
    cpu_state.old_fp_control = 0x1f80;
    cpu_state.new_fp_control = 0x1f80;
    cycles = 1000000000;
    for (int i = 0; i < 8; i++) {
        cpu_state.regs[i].l = 0x12345678 + i;
        cpu_state.MM[i].q = UINT64_C(0x1234567812345678) + i;
        for (int lane = 0; lane < 4; lane++)
            cpu_state.XMM[i].l[lane] = 0x12345678 + i + lane;
    }
    if (c->kind == MEMORY) {
        EAX = c->address;
        EBX = 0x12345678;
        cpu_state.ST[0] = 2.25;
    }
    if (c->kind == SSE_ADD || c->kind == SSE_MUL) {
        for (int i = 0; i < 8; i++)
            for (int lane = 0; lane < 4; lane++)
                cpu_state.XMM[i].f2[lane] = 1.25f;
        for (int lane = 0; lane < 4; lane++)
            cpu_state.XMM[7].f2[lane] = c->kind == SSE_MUL ? (timed ? 1.0f : 2.0f) : (timed ? 0.0f : 0.25f);
    }
}

static unsigned validate(const bench_case_t *c, void (*entry)(void))
{
    memset(memory, 0xa5, sizeof(memory));
    reset_state(c, 0);
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
            memcpy(expected_value, &expected.ST[0], 8);
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
    if (c->kind == MEMORY) {
        if (c->store) {
            for (unsigned i = 0; i < (c->working_set ? block_ops : 1); i++)
                CHECK(memcmp(memory + c->address + i * 64, expected_value, c->size) == 0);
        } else if (c->form >= SINGLE_FORM)
            CHECK(cpu_state.ST[0] == 1.25);
        else if (c->size == 16) {
            for (int i = 0; i < 4; i++) CHECK(cpu_state.XMM[0].l[i] == 0xa5a5a5a5);
        } else if (c->size == 8)
            CHECK(cpu_state.MM[0].q == UINT64_C(0xa5a5a5a5a5a5a5a5));
        else
            CHECK(EBX == (c->size == 4 ? 0xa5a5a5a5 : c->size == 2 ? 0x1234a5a5 : 0x123456a5));
        if (c->working_set) CHECK(EAX == block_ops * 64);
        unsigned penalty = c->size == 16 ? (helper_calls && (c->address & 7) ? helper_calls * 3 : 0)
                         : (c->address & (c->size - 1)) ? block_ops * 3 : 0;
        CHECK(cycles == 1000000000 - (int) penalty - c->cached_cycles * 2);
        if (c->working_set) {
            /* Also check the wraparound which long timed streams will reach. */
            EAX = c->working_set - 64;
            if (c->store) {
                memset(memory, 0, block_ops * 64);
                memset(memory + EAX, 0, c->size);
            }
            verifying = 1;
            entry();
            verifying = 0;
            CHECK(EAX == (block_ops - 1) * 64);
            if (c->store) {
                CHECK(memcmp(memory + c->working_set - 64, expected_value, c->size) == 0);
                for (unsigned i = 0; i + 1 < block_ops; i++)
                    CHECK(memcmp(memory + i * 64, expected_value, c->size) == 0);
            }
            CHECK(helper_calls == 0);
        }
    } else {
        for (unsigned i = 0; i < block_ops; i++) {
            unsigned r = i % c->live_regs;
            if (c->kind == INTEGER_ADD) expected.regs[r].l += 3;
            for (int lane = 0; lane < 4; lane++) {
                if (c->kind == MMX_ADD && lane < 2) expected.MM[r].l[lane] *= 2;
                if (c->kind == SSE_INTEGER) expected.XMM[r].l[lane] *= 2;
                if (c->kind == SSE_ADD) expected.XMM[r].f2[lane] += 0.25f;
                if (c->kind == SSE_MUL) expected.XMM[r].f2[lane] *= 2.0f;
            }
        }
        if (c->kind == INTEGER_ADD) CHECK(memcmp(cpu_state.regs, expected.regs, sizeof(expected.regs)) == 0);
        if (c->kind == MMX_ADD) CHECK(memcmp(cpu_state.MM, expected.MM, sizeof(expected.MM)) == 0);
        if (c->kind >= SSE_INTEGER && c->kind <= SSE_MUL) CHECK(memcmp(cpu_state.XMM, expected.XMM, sizeof(expected.XMM)) == 0);
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
        if (line[0] == '#' || !strncmp(line, "case,", 5)) continue;
        if (sscanf(line, "%127[^,],%u,%u,%u,%lf", name, &ops, &iterations, &samples, &median) != 5 ||
            !isfinite(median) || median <= 0) fatal("invalid baseline row\n");
        for (unsigned i = 0; i < case_count; i++) {
            if (strcmp(name, cases[i].name)) continue;
            if (ops != (cases[i].kind == EMPTY ? 1 : block_ops)) fatal("baseline block size differs for %s\n", name);
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
    fprintf(out, "# cpu_microbench v1; host=%s\n# build=%s; compiler=%s; compiled=%s %s\n",
            brand, CPU_BENCH_BUILD, __VERSION__, __DATE__, __TIME__);
    fprintf(out, "# block_ops=%u; host_gprs=%d; host_simd=%d; misalignment_cycles=%d\n",
            block_ops, CODEGEN_HOST_REGS, CODEGEN_HOST_FP_REGS, timing_misaligned);
}

int main(int argc, char **argv)
{
    unsigned samples = 9, sample_ms = 25;
    const char *filter = "", *csv_path = NULL, *baseline_path = NULL;
    int list = 0;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--help")) {
            puts("cpu_microbench [--filter substring] [--samples 3..99] [--sample-ms 1..1000]\n"
                 "               [--block-ops 1..64] [--csv result.csv] [--baseline old.csv]\n"
                 "               [--list] [--quick]\n"
                 "Lower ns/op is better. Positive baseline delta means slower.\n"
                 "Measures generated blocks, not a complete guest CPU or emulated MHz.");
            return 0;
        } else if (!strcmp(argv[i], "--list")) list = 1;
        else if (!strcmp(argv[i], "--quick")) { samples = 3; sample_ms = 1; }
        else {
            const char *option = argv[i];
            if (++i == argc) fatal("missing value for %s\n", option);
            if (!strcmp(option, "--filter")) filter = argv[i];
            else if (!strcmp(option, "--samples")) samples = number(argv[i], 3, MAX_SAMPLES);
            else if (!strcmp(option, "--sample-ms")) sample_ms = number(argv[i], 1, 1000);
            else if (!strcmp(option, "--block-ops")) block_ops = number(argv[i], 1, 64);
            else if (!strcmp(option, "--csv")) csv_path = argv[i];
            else if (!strcmp(option, "--baseline")) baseline_path = argv[i];
            else fatal("unknown option: %s\n", option);
        }
    }
    make_cases();
    unsigned selected = 0;
    for (unsigned i = 0; i < case_count; i++) {
        if (!strstr(cases[i].name, filter)) continue;
        selected++;
        if (list) puts(cases[i].name);
    }
    if (!selected) fatal("no cases match '%s'\n", filter);
    if (list) return 0;
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
    printf("%u cases, %u samples, target %u ms/sample; units ns/operation (empty: ns/block)\n", selected, samples, sample_ms);
    puts("Case                                                   median        min        p95    delta   helpers/block");
    if (csv) {
        metadata(csv);
        fputs("case,ops_per_block,iterations,samples,median_ns,min_ns,p95_ns,helpers_per_block,jit_bytes", csv);
        for (unsigned i = 0; i < samples; i++) fprintf(csv, ",sample_%u_ns", i + 1);
        fputc('\n', csv);
    }
    for (unsigned index = 0; index < case_count; index++) {
        bench_case_t *c = &cases[index];
        if (!strstr(c->name, filter)) continue;
        active_case = c->name;
        readlookup2[c->address >> 12] = writelookup2[c->address >> 12] = c->lookup_miss ? (uintptr_t) -1 : (uintptr_t) memory;
        reset_state(c, 0);
        unsigned jit_bytes;
        void (*entry)(void) = compile_case(c, &jit_bytes);
        unsigned calls = validate(c, entry);
        /* Warm the code/data and calibrate enough work to amortize the timer.
           Reset state outside timing; floating-point timing inputs stay finite. */
        unsigned iterations = 1024;
        double elapsed;
        do {
            reset_state(c, 1);
            elapsed = measure(entry, iterations);
            if (elapsed >= sample_ms * 1e6 || iterations >= (1u << 26)) break;
            iterations *= 2;
        } while (1);
        double values[MAX_SAMPLES], sorted[MAX_SAMPLES];
        unsigned ops = c->kind == EMPTY ? 1 : block_ops;
        for (unsigned s = 0; s < samples; s++) {
            reset_state(c, 1);
            values[s] = measure(entry, iterations) / ((double) iterations * ops);
            CHECK(isfinite(values[s]) && values[s] > 0);
            sorted[s] = values[s];
        }
        qsort(sorted, samples, sizeof(*sorted), compare_double);
        double median = (sorted[(samples - 1) / 2] + sorted[samples / 2]) / 2;
        double p95 = sorted[(95 * samples + 99) / 100 - 1];
        printf("%-52s %10.3f %10.3f %10.3f ", c->name, median, sorted[0], p95);
        if (baseline[index]) printf("%+7.1f%%", (median / baseline[index] - 1) * 100);
        else printf("      --");
        printf(" %8u\n", calls);
        fflush(stdout);
        if (csv) {
            fprintf(csv, "%s,%u,%u,%u,%.9f,%.9f,%.9f,%u,%u", c->name, ops, iterations, samples,
                    median, sorted[0], p95, calls, jit_bytes);
            for (unsigned s = 0; s < samples; s++) fprintf(csv, ",%.9f", values[s]);
            fputc('\n', csv);
        }
        readlookup2[c->address >> 12] = writelookup2[c->address >> 12] = (uintptr_t) memory;
    }
    if (csv && fclose(csv)) fatal("failed to finish CSV output\n");
#ifdef _WIN32
    CHECK(VirtualFree(code_memory, 0, MEM_RELEASE));
#else
    CHECK(munmap(code_memory, CODE_SIZE) == 0);
#endif
    puts("All selected cases validated. Compilation and validation excluded from timings.");
    return 0;
}
