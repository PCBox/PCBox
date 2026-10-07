/* Run the production REP handlers with controlled RAM, MMIO and faulting
   mappings. Expected memory is produced one guest element at a time. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include "cpu.h"
#include "x86.h"
#include "x86_ops.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include <86box/mem.h>
#include <86box/io.h>
#include "386_common.h"
#include "x86_flags.h"

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s:%d: %s (%s)\n", __FILE__, __LINE__, #c, active_case); \
    exit(1); \
} } while (0)
#define CLOCK_CYCLES(c) (cycles -= (c))
#define CPU_BLOCK_END() (cpu_block_end = 1)

cpu_state_t cpu_state;
int is386 = 1, is486 = 1, cpu_use_dynarec = 1, trap, cpu_block_end;
uint32_t dr[8], addr64, addr64_2, addr64a[8], addr64a_2[8];
uint8_t high_page;
static int fopcode;
static int checkio(uint32_t port, int mask);
uintptr_t readlookup2[2097152], writelookup2[1048576];
page_t *page_lookup[1048576];

#include "x86_ops_rep_dyn.h"

#define RAM_SIZE 0x40000
static uint8_t memory[RAM_SIZE], expected[RAM_SIZE];
static uint32_t physical_page[1048576];
static uint8_t page_mode[1048576];
enum { RAM, COLD, MMIO, TRACKED, READ_FAULT, WRITE_FAULT };
static page_t tracked_page;
static unsigned checks, slow_reads, slow_writes, translations, watched;
static uint32_t fault_address;
static const char *active_case = "setup";

void x86gpf(char *message, uint16_t error) { (void) message; (void) error; cpu_state.abrt = ABRT_GPF; }
void x86ss(char *message, uint16_t error) { (void) message; (void) error; cpu_state.abrt = ABRT_SS; }
void x86np(char *message, uint16_t error) { (void) message; (void) error; cpu_state.abrt = ABRT_NP; }
static int checkio(uint32_t port, int mask) { (void) port; (void) mask; CHECK(0); return 1; }

static uint32_t physical(uint32_t linear)
{
    CHECK(physical_page[linear >> 12] < RAM_SIZE);
    return physical_page[linear >> 12] + (linear & 0xfff);
}

static void map_page(uint32_t linear, uint32_t backing, int mode)
{
    unsigned page = linear >> 12;
    physical_page[page] = backing;
    page_mode[page] = mode;
    readlookup2[page] = mode == RAM || mode == TRACKED || mode == WRITE_FAULT
                           ? (uintptr_t) (memory + backing) - linear : LOOKUP_INV;
    writelookup2[page] = mode == RAM || mode == READ_FAULT
                            ? (uintptr_t) (memory + backing) - linear : LOOKUP_INV;
    page_lookup[page] = mode == TRACKED ? &tracked_page : NULL;
}

void do_mmutranslate(uint32_t linear, uint32_t *translated, int num, int write)
{
    translations++;
    for (int i = 0; i < num; i++) {
        uint32_t a = linear + i;
        int mode = page_mode[a >> 12];
        if (mode == READ_FAULT || (write && mode == WRITE_FAULT)) {
            cpu_state.abrt = ABRT_PF;
            fault_address = a;
            return;
        }
        translated[i] = physical(a);
    }
    if (dr[7] & 0xff)
        watched++;
}

static void warm_page(uint32_t linear, int write)
{
    unsigned page = linear >> 12;
    if (page_mode[page] != COLD)
        return;
    uintptr_t base = (uintptr_t) (memory + physical_page[page]) - (linear & ~0xfffu);
    uintptr_t *lookup = write ? &writelookup2[page] : &readlookup2[page];
    if (*lookup == (uintptr_t) LOOKUP_INV) {
        *lookup = base;
        cycles -= 9; /* addreadlookup/addwritelookup charge */
    }
}

static uint32_t read_slow(uint32_t linear, unsigned width)
{
    uint32_t value = 0;
    slow_reads++;
    for (unsigned i = 0; i < width; i++)
        value |= (uint32_t) memory[physical(linear + i)] << (i * 8);
    warm_page(linear, 0);
    return value;
}

static void write_slow(uint32_t linear, uint32_t value, unsigned width)
{
    slow_writes++;
    for (unsigned i = 0; i < width; i++)
        memory[physical(linear + i)] = value >> (i * 8);
    warm_page(linear, 1);
}

uint8_t readmembl_no_mmut(uint32_t a, uint32_t p) { (void) p; return read_slow(a, 1); }
uint16_t readmemwl_no_mmut(uint32_t a, uint32_t *p) { (void) p; return read_slow(a, 2); }
uint32_t readmemll_no_mmut(uint32_t a, uint32_t *p) { (void) p; return read_slow(a, 4); }
void writemembl_no_mmut(uint32_t a, uint32_t p, uint8_t v) { (void) p; write_slow(a, v, 1); }
void writememwl_no_mmut(uint32_t a, uint32_t *p, uint16_t v) { (void) p; write_slow(a, v, 2); }
void writememll_no_mmut(uint32_t a, uint32_t *p, uint32_t v) { (void) p; write_slow(a, v, 4); }
static void store(uint32_t a, uint32_t v, unsigned width)
{
    uint32_t translated[4];
    do_mmutranslate(a, translated, width, 1);
    if (!cpu_state.abrt)
        write_slow(a, v, width);
}
void writemembl(uint32_t a, uint8_t v) { store(a, v, 1); }
void writememwl(uint32_t a, uint16_t v) { store(a, v, 2); }
void writememll(uint32_t a, uint32_t v) { store(a, v, 4); }

static OpFn handlers[2][2][3] = {
    {{opREP_STOSB_a16, opREP_STOSW_a16, opREP_STOSL_a16},
     {opREP_STOSB_a32, opREP_STOSW_a32, opREP_STOSL_a32}},
    {{opREP_MOVSB_a16, opREP_MOVSW_a16, opREP_MOVSL_a16},
     {opREP_MOVSB_a32, opREP_MOVSW_a32, opREP_MOVSL_a32}}
};

static void reset(int a32, int backwards, uint32_t source, uint32_t destination, uint32_t count)
{
    memset(&cpu_state, 0, sizeof(cpu_state));
    memset(dr, 0, sizeof(dr));
    cpu_state.seg_ds.limit_high = cpu_state.seg_es.limit_high = UINT32_MAX;
    cpu_state.seg_ds.access = cpu_state.seg_es.access = 0x93;
    cpu_state.ea_seg = &cpu_state.seg_ds;
    cpu_state.CR0.l = 1;
    cpu_state.flags = 0x203 | (backwards ? D_FLAG : 0);
    cpu_state.flags_op = FLAGS_ADD32;
    cpu_state.flags_op1 = 0x11223344;
    cpu_state.flags_op2 = 0xaabbccdd;
    cpu_state.flags_res = 0xbbde0021;
    cpu_state.oldpc = 0x1234;
    cpu_state.pc = 0x1237;
    EAX = 0x12345678;
    ESI = source | (a32 ? 0 : 0xabcd0000);
    EDI = destination | (a32 ? 0 : 0xbcde0000);
    ECX = count | (a32 ? 0 : 0xcdef0000);
    cycles = 100000;
    is386 = is486 = cpu_use_dynarec = 1;
    trap = cpu_block_end = 0;
    slow_reads = slow_writes = translations = watched = 0;
    fault_address = 0;
    for (unsigned i = 0; i < RAM_SIZE; i++)
        memory[i] = (uint8_t) ((i * 97) ^ (i >> 8));
    for (unsigned a = 0; a < RAM_SIZE; a += 4096)
        map_page(a, a, RAM);
    /* Mappings used to exercise 32-bit address and linear-address wrap. */
    map_page(0xfffff000, 0x30000, RAM);
}

static void oracle(int movs, int a32, unsigned width, uint32_t done)
{
    uint32_t mask = a32 ? UINT32_MAX : 0xffff;
    uint32_t source = ESI & mask, destination = EDI & mask;
    int step = cpu_state.flags & D_FLAG ? -(int) width : (int) width;
    memcpy(expected, memory, sizeof(memory));
    for (uint32_t i = 0; i < done; i++) {
        uint8_t element[4];
        for (unsigned byte = 0; byte < width; byte++)
            element[byte] = movs ? expected[physical(cpu_state.ea_seg->base + source + byte)]
                                 : (uint8_t) (EAX >> (byte * 8));
        for (unsigned byte = 0; byte < width; byte++)
            expected[physical(es + destination + byte)] = element[byte];
        source = (source + step) & mask;
        destination = (destination + step) & mask;
    }
}

static void execute_check(int movs, int a32, unsigned width, uint32_t done, int expected_abort, int extra_cycles)
{
    uint32_t mask = a32 ? UINT32_MAX : 0xffff;
    cpu_state_t before = cpu_state;
    uint32_t initial_count = ECX & mask;
    uint32_t delta = done * width;
    int cost = movs ? (is486 ? 3 : 4) : (is486 ? 4 : 5);
    if (cpu_state.flags & D_FLAG)
        delta = -delta;
    oracle(movs, a32, width, done);
    int result = handlers[movs][a32][width == 1 ? 0 : width == 2 ? 1 : 2](0);
    CHECK(cpu_state.abrt == expected_abort);
    CHECK((ECX & mask) == initial_count - done);
    CHECK(ECX == ((before.regs[1].l & ~mask) | ((initial_count - done) & mask)));
    CHECK(EDI == ((before.regs[7].l & ~mask) | ((before.regs[7].l + delta) & mask)));
    CHECK(ESI == (movs ? ((before.regs[6].l & ~mask) | ((before.regs[6].l + delta) & mask)) : before.regs[6].l));
    CHECK(cycles == before._cycles - (int) done * cost - extra_cycles);
    CHECK(cpu_state.flags == before.flags && cpu_state.eflags == before.eflags);
    CHECK(cpu_state.flags_op == before.flags_op && cpu_state.flags_op1 == before.flags_op1);
    CHECK(cpu_state.flags_op2 == before.flags_op2 && cpu_state.flags_res == before.flags_res);
    CHECK(EAX == before.regs[0].l);
    CHECK(cpu_state.pc == (done < initial_count ? before.oldpc : before.pc));
    CHECK(cpu_block_end == (done < initial_count));
    CHECK(result == (done < initial_count ? 1 : expected_abort));
    CHECK(memcmp(memory, expected, sizeof(memory)) == 0);
    checks++;
}

static void test_matrix(void)
{
    const uint32_t counts[] = {0, 1, 2, 7, 16, 255, 256, 257, 333, 334, 335, 4096};
    for (int movs = 0; movs <= 1; movs++)
        for (int a32 = 0; a32 <= 1; a32++)
            for (int backwards = 0; backwards <= 1; backwards++)
                for (unsigned width = 1; width <= 4; width *= 2)
                    for (unsigned c = 0; c < sizeof(counts) / sizeof(counts[0]); c++)
                        for (int old_cpu = 0; old_cpu <= 1; old_cpu++) {
                            active_case = "count/direction/address-size/cycle budget";
                            reset(a32, backwards, 0x4800, 0xa800, counts[c]);
                            is486 = !old_cpu;
                            int cost = movs ? (is486 ? 3 : 4) : (is486 ? 4 : 5);
                            uint32_t done = 1000 / cost + 1;
                            if (done > counts[c]) done = counts[c];
                            execute_check(movs, a32, width, done, 0, 0);
                        }
}

static void test_boundaries(void)
{
    for (int movs = 0; movs <= 1; movs++)
        for (int a32 = 0; a32 <= 1; a32++)
            for (int backwards = 0; backwards <= 1; backwards++)
                for (unsigned width = 1; width <= 4; width *= 2) {
                    active_case = "page boundary and unaligned/split element";
                    for (unsigned misalign = 0; misalign < width; misalign++) {
                        reset(a32, backwards, 0x5000 - 4 * width + misalign, 0x9000 - 3 * width + misalign, 32);
                        execute_check(movs, a32, width, 32, 0, 0);
                    }
                    active_case = "segment bases / source override";
                    reset(a32, backwards, 0x4800, 0x5800, 32);
                    cpu_state.seg_fs = cpu_state.seg_ds;
                    cpu_state.ea_seg = &cpu_state.seg_fs;
                    cpu_state.seg_fs.base = 0x8000;
                    cpu_state.seg_es.base = 0x10000;
                    execute_check(movs, a32, width, 32, 0, 0);

                    active_case = "segment boundary fault after completed elements";
                    for (int source = 0; source <= movs; source++) {
                        reset(a32, backwards, 0x4800, 0x8800, 32);
                        x86seg *seg = source ? cpu_state.ea_seg : &cpu_state.seg_es;
                        uint32_t offset = source ? 0x4800 : 0x8800;
                        if (backwards) seg->limit_low = offset - 15 * width;
                        else seg->limit_high = offset + 16 * width - 1;
                        execute_check(movs, a32, width, 16, ABRT_GPF, 0);
                    }

                    active_case = "address-size wrap";
                    uint32_t start = backwards ? 8 * width : (a32 ? 0u : 0x10000u) - 8 * width;
                    reset(a32, backwards, start, start, 32);
                    if (movs) cpu_state.seg_es.base = 0x10000;
                    execute_check(movs, a32, width, 32, 0, 0);

                    active_case = "page fault after completed elements";
                    for (int source = 0; source <= movs; source++) {
                        reset(a32, backwards, backwards ? 0x403c : 0x4fc0, backwards ? 0x803c : 0x8fc0, 128);
                        uint32_t fault_page = source ? (backwards ? 0x3000 : 0x5000) : (backwards ? 0x7000 : 0x9000);
                        map_page(fault_page, fault_page, source ? READ_FAULT : WRITE_FAULT);
                        uint32_t done = backwards ? 0x3c / width + 1 : 0x40 / width;
                        execute_check(movs, a32, width, done, ABRT_PF, 0);
                        CHECK((fault_address & ~0xfffu) == fault_page);
                    }
                }
}

static void test_fallbacks(void)
{
    for (int movs = 0; movs <= 1; movs++)
        for (int a32 = 0; a32 <= 1; a32++)
            for (int backwards = 0; backwards <= 1; backwards++)
                for (unsigned width = 1; width <= 4; width *= 2) {
                    active_case = "trap executes only one element";
                    reset(a32, backwards, 0x4800, 0x8800, 32);
                    trap = 1;
                    cpu_state.flags |= T_FLAG;
                    execute_check(movs, a32, width, 1, 0, 0);

                    active_case = "debug registers keep memory checks";
                    reset(a32, backwards, 0x4800, 0x8800, 32);
                    dr[7] = 1;
                    execute_check(movs, a32, width, 32, 0, 0);
                    CHECK(watched == (movs ? 64 : 32));

                    for (int mode = COLD; mode <= TRACKED; mode++) {
                        active_case = "cold RAM / MMIO / code-tracked destination";
                        reset(a32, backwards, 0x4800, 0x8800, 32);
                        map_page(0x4000, 0x4000, mode == TRACKED ? RAM : mode);
                        map_page(0x8000, 0x8000, mode);
                        execute_check(movs, a32, width, 32, 0, mode == COLD ? (movs ? 18 : 9) : 0);
                        CHECK(slow_writes == (mode == COLD ? 1 : 32));
                        if (movs) CHECK(slow_reads == (mode == COLD ? 1 : mode == MMIO ? 32 : 0));
                    }

                    active_case = "null/not-present segment, zero count and fault priority";
                    reset(a32, backwards, 0x4800, 0x8800, 0);
                    cpu_state.ea_seg->base = cpu_state.seg_es.base = UINT32_MAX;
                    execute_check(movs, a32, width, 0, 0, 0);
                    reset(a32, backwards, 0x4800, 0x8800, 32);
                    cpu_state.seg_es.access &= ~0x80;
                    execute_check(movs, a32, width, 0, ABRT_NP, 0);
                    if (movs) {
                        reset(a32, backwards, 0x4800, 0x8800, 32);
                        map_page(0x4000, 0x4000, READ_FAULT);
                        cpu_state.seg_es.base = UINT32_MAX;
                        execute_check(movs, a32, width, 0, ABRT_PF, 0);
                        CHECK(slow_reads == 0 && slow_writes == 0);
                    }
                }
    active_case = "overlap and physical aliases retain guest element order";
    for (int a32 = 0; a32 <= 1; a32++)
        for (int backwards = 0; backwards <= 1; backwards++)
            for (unsigned width = 1; width <= 4; width *= 2)
                for (int displacement = -8; displacement <= 8; displacement++)
                    for (int alias = 0; alias <= 1; alias++) {
                        reset(a32, backwards, 0x4800, 0x4800 + displacement * (int) width, 128);
                        if (alias) {
                            cpu_state.seg_es.base = 0x10000;
                            map_page(0x14000, 0x4000, RAM);
                        }
                        execute_check(1, a32, width, 128, 0, 0);
                    }
}

static void test_restart_and_split_faults(void)
{
    for (int movs = 0; movs <= 1; movs++)
        for (int a32 = 0; a32 <= 1; a32++)
            for (unsigned width = 1; width <= 4; width *= 2) {
                active_case = "cold lookup costs shorten the original cycle budget";
                reset(a32, 0, 0x4000, 0x8000, 4096);
                map_page(0x4000, 0x4000, COLD);
                map_page(0x8000, 0x8000, COLD);
                int cost = movs ? 3 : 4, extra = movs ? 18 : 9;
                execute_check(movs, a32, width, (1000 - extra) / cost + 1, 0, extra);

                active_case = "short budget and exhausted outer cycle counter";
                reset(a32, 0, 0x4000, 0x8000, 4096);
                cpu_use_dynarec = 0;
                cycles = -100;
                execute_check(movs, a32, width, 100 / cost + 1, 0, 0);

                active_case = "restart REP until the entire range completes";
                reset(a32, 0, 0x4000, 0x18000 & (a32 ? UINT32_MAX : 0xffff), 4096);
                while ((a32 ? ECX : CX) != 0) {
                    uint32_t left = a32 ? ECX : CX;
                    uint32_t done = 1000 / cost + 1;
                    if (done > left) done = left;
                    cpu_state.pc = 0x1237;
                    cpu_block_end = 0;
                    execute_check(movs, a32, width, done, 0, 0);
                }

                if (width > 1) {
                    active_case = "faulting split element commits no bytes or registers";
                    reset(a32, 0, 0x4800, 0x8fff, 16);
                    map_page(0x9000, 0x9000, WRITE_FAULT);
                    execute_check(movs, a32, width, 0, ABRT_PF, 0);
                    CHECK(slow_reads == 0 && slow_writes == 0);
                    if (movs) {
                        reset(a32, 0, 0x4fff, 0x8800, 16);
                        map_page(0x5000, 0x5000, READ_FAULT);
                        execute_check(movs, a32, width, 0, ABRT_PF, 0);
                        CHECK(slow_reads == 0 && slow_writes == 0);
                    }
                }
            }
}

static void benchmark(void)
{
    const unsigned lengths[] = {1, 16, 256, 4096};
    puts("operation,width,direction,count,ns_per_element");
    for (int movs = 0; movs <= 1; movs++)
        for (unsigned width = 1; width <= 4; width *= 2)
            for (int backwards = 0; backwards <= 1; backwards++)
                for (unsigned c = 0; c < sizeof(lengths) / sizeof(lengths[0]); c++) {
                    reset(1, backwards, 0x8000, 0x18000, lengths[c]);
                    OpFn fn = handlers[movs][1][width == 1 ? 0 : width == 2 ? 1 : 2];
                    uint64_t iterations = 0;
                    clock_t start = clock(), elapsed;
                    do {
                        for (int i = 0; i < 256; i++) {
                            ESI = 0x8000;
                            EDI = 0x18000;
                            ECX = lengths[c];
                            cycles = 100000;
                            do { fn(0); } while (ECX);
                            iterations++;
                        }
                        elapsed = clock() - start;
                    } while (elapsed < CLOCKS_PER_SEC / 10);
                    printf("%s,%u,%s,%u,%.4f\n", movs ? "movs" : "stos", width,
                           backwards ? "backward" : "forward", lengths[c],
                           (double) elapsed * 1e9 / CLOCKS_PER_SEC / iterations / lengths[c]);
                }
}

static void benchmark_fallbacks(void)
{
    puts("operation,width,direction,kind,ns_per_element");
    for (int movs = 0; movs <= 1; movs++)
        for (unsigned width = 1; width <= 4; width *= 2)
            for (int backwards = 0; backwards <= 1; backwards++)
                for (int kind = 0; kind < 3; kind++) {
                    if (kind == 0 && !movs) continue;
                    if (kind == 1 && width == 1) continue;
                    reset(1, backwards, 0x8800, 0x18800, 256);
                    if (kind == 2) {
                        map_page(0x8000, 0x8000, MMIO);
                        map_page(0x18000, 0x18000, MMIO);
                    }
                    OpFn fn = handlers[movs][1][width == 1 ? 0 : width == 2 ? 1 : 2];
                    uint64_t iterations = 0;
                    clock_t start = clock(), elapsed;
                    do {
                        for (int i = 0; i < 256; i++) {
                            ESI = 0x8800 + (kind == 1);
                            EDI = kind == 0 ? ESI + (backwards ? -(int) width : (int) width)
                                           : 0x18800 + (kind == 1);
                            ECX = 256;
                            cycles = 100000;
                            do { fn(0); } while (ECX);
                            iterations++;
                        }
                        elapsed = clock() - start;
                    } while (elapsed < CLOCKS_PER_SEC / 10);
                    printf("%s,%u,%s,%s,%.4f\n", movs ? "movs" : "stos", width,
                           backwards ? "backward" : "forward",
                           kind == 0 ? "overlap" : kind == 1 ? "unaligned" : "mmio",
                           (double) elapsed * 1e9 / CLOCKS_PER_SEC / iterations / 256);
                }
}

int main(int argc, char **argv)
{
    memset(readlookup2, 0xff, sizeof(readlookup2));
    memset(writelookup2, 0xff, sizeof(writelookup2));
    memset(physical_page, 0xff, sizeof(physical_page));
    if (argc == 2 && !strcmp(argv[1], "--bench")) {
        benchmark();
        return 0;
    }
    if (argc == 2 && !strcmp(argv[1], "--bench-fallback")) {
        benchmark_fallbacks();
        return 0;
    }
    test_matrix();
    test_boundaries();
    test_fallbacks();
    test_restart_and_split_faults();
    printf("REP chunk tests passed (%u cases)\n", checks);
    return 0;
}
