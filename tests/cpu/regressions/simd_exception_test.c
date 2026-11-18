/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Real CPU/MMU/exception delivery, with a small protected-mode guest and no OS.
 * Cases use raw IEEE encodings; host floating point is never the oracle. */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include "cpu.h"
#include "x86.h"
#include "x86seg.h"
#include "codegen.h"

extern int     inrecomp;
extern int32_t cycles_main;
enum { DRIVER   = 0x10000,
       BODY     = 0x11000,
       DATA     = 0x20000,
       RESULT   = 0x20100,
       TRAP     = 0x20200,
       FAULT_PC = 0x20204,
       GDT_ADDR = 0x8000,
       IDT_ADDR = 0x9000,
       HANDLERS = 0xa000,
       CALLS    = 64 };
enum { XMM_RESULT,
       GPR_RESULT,
       FLAGS_RESULT,
       MMX_RESULT,
       FTZ_RESULT };
typedef struct {
    const char *name;
    uint8_t     prefix, opcode, imm, has_imm, sse2, result_kind, exception;
    uint32_t    a[4], b[4];
    uint32_t    expected[4], control;
    uint32_t    alternate_control, alternate_expected[4];
} simd_case;
#define FONE 0x3f800000
#define DLO  0, 0x3ff00000
static const simd_case cases[] = {
    { "DIVSS zero", 0xf3, 0x5e, 0, 0, 0, XMM_RESULT, 4, { FONE, 2, 3, 4 }, { 0 } },
    { "DIVPS late lane", 0, 0x5e, 0, 0, 0, XMM_RESULT, 4, { FONE, FONE, FONE, FONE }, { FONE, FONE, FONE, 0 } },
    { "DIVSD zero", 0xf2, 0x5e, 0, 0, 1, XMM_RESULT, 4, { DLO, 3, 4 }, { 0 } },
    { "DIVPD late lane", 0x66, 0x5e, 0, 0, 1, XMM_RESULT, 4, { DLO, DLO }, { DLO, 0, 0 } },
    { "SQRTSS invalid", 0xf3, 0x51, 0, 0, 0, XMM_RESULT, 1, { FONE, 2, 3, 4 }, { 0xbf800000 } },
    { "SQRTPS late lane", 0, 0x51, 0, 0, 0, XMM_RESULT, 1, { FONE, FONE, FONE, FONE }, { FONE, FONE, FONE, 0xbf800000 } },
    { "SQRTSD invalid", 0xf2, 0x51, 0, 0, 1, XMM_RESULT, 1, { 0, 0x40450000, 3, 4 }, { 0, 0xbff00000 } },
    { "SQRTPD late lane", 0x66, 0x51, 0, 0, 1, XMM_RESULT, 1, { DLO, DLO }, { DLO, 0, 0xbff00000 } },
    { "ADDSS denormal", 0xf3, 0x58, 0, 0, 0, XMM_RESULT, 2, { FONE, 2, 3, 4 }, { 1 } },
    { "ADDSS precision", 0xf3, 0x58, 0, 0, 0, XMM_RESULT, 32, { FONE, 2, 3, 4 }, { 0x33800000 } },
    { "MULSS overflow", 0xf3, 0x59, 0, 0, 0, XMM_RESULT, 8, { 0x7f7fffff, 2, 3, 4 }, { 0x40000000 } },
    { "MULSS underflow", 0xf3, 0x59, 0, 0, 0, XMM_RESULT, 16, { 0x00800000, 2, 3, 4 }, { 0x3effffff } },
    { "CVTTSS2SI invalid", 0xf3, 0x2c, 0, 0, 0, GPR_RESULT, 1, { FONE, 2, 3, 4 }, { 0x7fc12345 } },
    { "CVTSS2SI precision", 0xf3, 0x2d, 0, 0, 0, GPR_RESULT, 32, { FONE, 2, 3, 4 }, { 0x3fc00000 } },
    { "CVTTSD2SI invalid", 0xf2, 0x2c, 0, 0, 1, GPR_RESULT, 1, { DLO, 3, 4 }, { 0, 0x7ff80000 } },
    { "CVTSD2SI precision", 0xf2, 0x2d, 0, 0, 1, GPR_RESULT, 32, { DLO, 3, 4 }, { 0, 0x3ff80000 } },
    { "CVTPS2DQ late lane", 0x66, 0x5b, 0, 0, 1, XMM_RESULT, 1, { 1, 2, 3, 4 }, { FONE, FONE, FONE, 0x7fc12345 } },
    { "CVTPD2PS overflow", 0x66, 0x5a, 0, 0, 1, XMM_RESULT, 8, { 1, 2, 3, 4 }, { DLO, 0, 0x7fe00000 } },
    { "CVTSD2SS overflow", 0xf2, 0x5a, 0, 0, 1, XMM_RESULT, 8, { 1, 2, 3, 4 }, { 0, 0x7fe00000 } },
    { "CVTTPS2PI invalid", 0, 0x2c, 0, 0, 0, MMX_RESULT, 1, { 1, 2, 3, 4 }, { FONE, 0x7fc12345 } },
    { "CVTTPD2PI invalid", 0x66, 0x2c, 0, 0, 1, MMX_RESULT, 1, { 1, 2, 3, 4 }, { DLO, 0, 0x7ff80000 } },
    { "COMISS invalid", 0, 0x2f, 0, 0, 0, FLAGS_RESULT, 1, { FONE, 2, 3, 4 }, { 0x7fc12345 } },
    { "UCOMISS signaling", 0, 0x2e, 0, 0, 0, FLAGS_RESULT, 1, { FONE, 2, 3, 4 }, { 0x7f812345 } },
    { "COMISD invalid", 0x66, 0x2f, 0, 0, 1, FLAGS_RESULT, 1, { DLO, 3, 4 }, { 0, 0x7ff80000 } },
    { "UCOMISD signaling", 0x66, 0x2e, 0, 0, 1, FLAGS_RESULT, 1, { DLO, 3, 4 }, { 1, 0x7ff00000 } },
    { "CMPPS invalid", 0, 0xc2, 1, 1, 0, XMM_RESULT, 1, { FONE, FONE, FONE, FONE }, { FONE, FONE, FONE, 0x7fc12345 } },
    { "CMPPD invalid", 0x66, 0xc2, 1, 1, 1, XMM_RESULT, 1, { DLO, DLO }, { DLO, 0, 0x7ff80000 } },
    { "FTZ MULSS positive", 0xf3, 0x59, 0, 0, 0, FTZ_RESULT, 0x30, { 0x00800000, 2, 3, 4 }, { 0x3effffff }, { 0, 2, 3, 4 }, 0x9f80, 0x1f80, { 0x00400000, 2, 3, 4 } },
    { "FTZ MULSS negative", 0xf3, 0x59, 0, 0, 0, FTZ_RESULT, 0x30, { 0x80800000, 2, 3, 4 }, { 0x3effffff }, { 0x80000000, 2, 3, 4 }, 0x9f80 },
    { "FTZ MULPS", 0, 0x59, 0, 0, 0, FTZ_RESULT, 0x30, { 0x00800000, 0x80800000, FONE, FONE }, { 0x3effffff, 0x3effffff, FONE, FONE }, { 0, 0x80000000, FONE, FONE }, 0x9f80 },
    { "FTZ MULSD", 0xf2, 0x59, 0, 0, 1, FTZ_RESULT, 0x30, { 0, 0x00100000, 3, 4 }, { 0xffffffff, 0x3fdfffff }, { 0, 0, 3, 4 }, 0x9f80 },
    { "FTZ MULPD", 0x66, 0x59, 0, 0, 1, FTZ_RESULT, 0x30, { 0, 0x00100000, 0, 0x80100000 }, { 0xffffffff, 0x3fdfffff, 0xffffffff, 0x3fdfffff }, { 0, 0, 0, 0x80000000 }, 0x9f80 },
    { "gradual MULSS", 0xf3, 0x59, 0, 0, 0, FTZ_RESULT, 0x30, { 0x00800000, 2, 3, 4 }, { 0x3effffff }, { 0x00400000, 2, 3, 4 }, 0x1f80 }
};
static const simd_case *test;
static unsigned         completed, failures, compiled_results;
static unsigned         native_regions;
#ifdef PCBOX_SSE_NATIVE_CHECK
extern void __real_codegen_backend_sse_mxcsr_begin(codeblock_t *block);
void
__wrap_codegen_backend_sse_mxcsr_begin(codeblock_t *block)
{
    if (block->status & CPU_STATUS_SSE_UNMASKED)
        fatal("Native SSE in unmasked block");
    ++native_regions;
    __real_codegen_backend_sse_mxcsr_begin(block);
}
#endif
static int      done, osxmm, engine, memory_form, address16, restore;
static int      alignment = -1;
static uint32_t test_pc, resume_pc, emit_pos;
uint64_t        timer_target = 1000;
static void
put32(uint32_t a, uint32_t v)
{
    memcpy(ram + a, &v, 4);
}
static uint32_t
get32(uint32_t a)
{
    uint32_t v;
    memcpy(&v, ram + a, 4);
    return v;
}
static void
b(uint8_t v)
{
    ram[emit_pos++] = v;
}
static void
d(uint32_t v)
{
    put32(emit_pos, v);
    emit_pos += 4;
}
static void
mov_mem_imm(uint32_t a, uint32_t v)
{
    b(0xc7);
    b(5);
    d(a);
    d(v);
}
void
fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    fprintf(stderr, "\n%s at %04x:%08x\n", test ? test->name : "setup", CS, cpu_state.pc);
    exit(2);
}
void
pclog(const char *fmt, ...)
{
    (void) fmt;
}
void
pclog_ex(const char *fmt, va_list ap)
{
    (void) fmt;
    (void) ap;
}
void
outb(uint16_t p, uint8_t v)
{
    fatal("OUTB %x=%x", p, v);
}
void
outw(uint16_t p, uint16_t v)
{
    fatal("OUTW %x=%x", p, v);
}
uint8_t
inb(uint16_t p)
{
    fatal("INB %x", p);
    return 0;
}
uint16_t
inw(uint16_t p)
{
    fatal("INW %x", p);
    return 0;
}
uint32_t
inl(uint16_t p)
{
    fatal("INL %x", p);
    return 0;
}
static int
unmasked(unsigned call)
{
    return (call / 8) & 1;
}
static void
check(int ok, const char *what)
{
    if (!ok) {
        if (failures < 12)
            fprintf(stderr, "FAIL %s %s call=%u mem=%d a16=%d fxrstor=%d osxmm=%d result=%08x/%08x/%08x/%08x mxcsr=%08x\n",
                    test->name, what, completed, memory_form, address16, restore, osxmm,
                    get32(RESULT), get32(RESULT + 4), get32(RESULT + 8), get32(RESULT + 12), get32(RESULT + 16));
        ++failures;
    }
}
void
outl(uint16_t port, uint32_t value)
{
    (void) value;
    if (port == 0xec) {
        done = 1;
        return;
    }
    if (port != 0xe9 || completed >= CALLS)
        fatal("Bad mailbox");
    if (alignment >= 0) {
        int      store  = test->opcode == 0x29 || test->opcode == 0x7f || test->opcode == 0x2b || test->opcode == 0xe7;
        unsigned trap   = (alignment & 1) && test->opcode != 0x10 ? 13 : 0;
        uint32_t linear = (alignment & 1) ? 0x6021 : 0x6020;
        check(get32(TRAP) == trap, "linear alignment exception");
        if (trap)
            check(get32(FAULT_PC) == test_pc, "alignment fault EIP");
        check(!memcmp(ram + RESULT, (trap || store) ? test->a : test->expected, 16), "aligned XMM result");
        if (store)
            check(!memcmp(ram + linear, trap ? test->b : test->a, 16), "aligned store atomicity");
        compiled_results += !!inrecomp;
        if (++completed == CALLS) {
            done   = 1;
            cycles = cycles_main = 0;
        }
        return;
    }
    unsigned trap = test->result_kind != FTZ_RESULT && unmasked(completed) ? (osxmm ? 19 : 6) : 0;
    check(get32(TRAP) == trap, "exception vector");
    if (test->result_kind == FTZ_RESULT) {
        const uint32_t *expected = test->alternate_control && unmasked(completed) ? test->alternate_expected : test->expected;
        check(!memcmp(ram + RESULT, expected, 16), "underflow result and preserved lanes");
    }
    if (trap) {
        check(get32(FAULT_PC) == test_pc, "fault EIP");
        if (test->result_kind == GPR_RESULT)
            check(get32(RESULT) == 0x12345678, "GPR destination");
        else if (test->result_kind == FLAGS_RESULT)
            check((get32(RESULT) & 0x8d5) == 0x890, "EFLAGS destination");
        else if (test->result_kind == MMX_RESULT)
            check(get32(RESULT) == 0x12345678 && get32(RESULT + 4) == 0x87654321, "MMX destination");
        else
            check(!memcmp(ram + RESULT, test->a, 16), "XMM destination");
    }
    check((get32(RESULT + 16) & test->exception) == test->exception, "MXCSR sticky flag");
    compiled_results += !!inrecomp;
    ++completed;
    uint32_t mxcsr = 0x1f80 & ~(unmasked(completed) ? test->exception << 7 : 0);
    if (test->result_kind == FTZ_RESULT)
        mxcsr = test->alternate_control && unmasked(completed) ? test->alternate_control : test->control;
    put32(DATA + 32, mxcsr);
    put32(DATA + 0x418, mxcsr);
    if (completed == CALLS) {
        done   = 1;
        cycles = cycles_main = 0;
    }
}
void
timer_process(void)
{
    timer_target = tsc + 1000;
    if (tsc > 10000000 || x86_was_reset || cpu_state.abrt)
        fatal("Guest watchdog");
}
static void
flat_segment(x86seg *s, uint16_t selector, uint8_t access)
{
    *s = (x86seg) { .base = 0, .limit = UINT32_MAX, .access = access, .ar_high = 0xcf, .seg = selector, .limit_low = 0, .limit_high = UINT32_MAX, .checked = 1 };
}
static void
exception_tables(void)
{
    uint64_t descriptors[] = { 0, UINT64_C(0x00cf9b000000ffff), UINT64_C(0x00cf93000000ffff) };
    memcpy(ram + GDT_ADDR, descriptors, sizeof(descriptors));
    gdt.base  = GDT_ADDR;
    gdt.limit = sizeof(descriptors) - 1;
    idt.base  = IDT_ADDR;
    idt.limit = 256 * 8 - 1;
    memset(ram + IDT_ADDR, 0, 256 * 8);
    for (unsigned v = 0; v < 32; ++v) {
        uint32_t h    = HANDLERS + v * 64;
        uint64_t gate = (h & 0xffff) | UINT64_C(0x00008e0000080000) | ((uint64_t) (h >> 16) << 48);
        memcpy(ram + IDT_ADDR + v * 8, &gate, 8);
        emit_pos = h;
        if (v == 8 || (v >= 10 && v <= 14) || v == 17) {
            b(0x83);
            b(0xc4);
            b(4);
        }
        b(0x50);
        b(0x8b);
        b(0x44);
        b(0x24);
        b(4);
        b(0xa3);
        d(FAULT_PC);
        mov_mem_imm(TRAP, v);
        b(0xc7);
        b(0x44);
        b(0x24);
        b(4);
        d(resume_pc);
        b(0x58);
        b(0xcf);
    }
}
static void
run_case(void)
{
    completed = compiled_results = native_regions = 0;
    done                                          = 0;
    resetx86();
    cr0 = 1;
    cr4 = CR4_OSFXSR | (osxmm ? CR4_OSXMMEXCPT : 0);
    flat_segment(&cpu_state.seg_cs, 8, 0x9b);
    flat_segment(&cpu_state.seg_ds, 16, 0x93);
    flat_segment(&cpu_state.seg_es, 16, 0x93);
    flat_segment(&cpu_state.seg_ss, 16, 0x93);
    flat_segment(&cpu_state.seg_fs, 16, 0x93);
    flat_segment(&cpu_state.seg_gs, 16, 0x93);
    if (alignment >= 0)
        cpu_state.seg_fs.base = 1;
    cpu_cur_status   = CPU_STATUS_PMODE | CPU_STATUS_USE32 | CPU_STATUS_STACK32;
    use32            = 0x300;
    stack32          = 1;
    cpu_state.flags  = 2;
    cpu_state.eflags = 0;
    ESP              = 0x700000;
    cycles = cycles_main = 0;
    tsc                  = 0;
    timer_target         = 1000;
    x86_was_reset        = 0;
    mem_a20_alt          = 1;
    mem_a20_recalc();
    flushmmucache();
    memset(ram + DATA, 0, 0x1000);
    memcpy(ram + DATA, test->a, 16);
    memcpy(ram + DATA + 16, test->b, 16);
    memcpy(ram + 0x6000, test->b, 16);
    put32(DATA + 32, test->control ? test->control : 0x1f80);
    if (alignment >= 0)
        memcpy(ram + ((alignment & 1) ? 0x6021 : 0x6020), test->b, 16);
    put32(DATA + 40, 0x12345678);
    put32(DATA + 44, 0x87654321);
    /* Minimal FXSAVE image: masked x87, empty tags, controlled MXCSR. */
    put32(DATA + 0x400, 0x37f);
    put32(DATA + 0x418, test->control ? test->control : 0x1f80);
    static const uint8_t driver[] = { 0xbf, CALLS, 0, 0, 0, 0xe8, 0xf6, 0x0f, 0, 0,
                                      0x66, 0xba, 0xe9, 0, 0xef, 0x4f, 0x75, 0xf3, 0x66, 0xba, 0xec, 0, 0xef, 0xeb, 0xfe };
    memcpy(ram + DRIVER, driver, sizeof(driver));
    emit_pos = BODY;
    mov_mem_imm(TRAP, 0);
    b(0x0f);
    b(0xae);
    b(restore ? 0x0d : 0x15);
    d(restore ? DATA + 0x400 : DATA + 32);
    b(0x0f);
    b(0x10);
    b(5);
    d(DATA);
    b(0x0f);
    b(0x10);
    b(0x0d);
    d(DATA + 16);
    if (test->result_kind == MMX_RESULT) {
        b(0x0f);
        b(0x6f);
        b(5);
        d(DATA + 40);
    }
    b(0xb8);
    d(0x12345678);
    b(0x68);
    d(0x892);
    b(0x9d); /* Set OF,SF,AF before comparisons. */
    test_pc = emit_pos;
    if (alignment >= 0)
        b(0x64); /* FS base deliberately misaligned. */
    if (address16)
        b(0x67);
    if (test->prefix)
        b(test->prefix);
    b(0x0f);
    b(test->opcode);
    if (!memory_form)
        b(0xc1);
    else if (address16) {
        b(6);
        b(alignment < 0 ? 0 : (alignment & 1) ? 0x20
                                              : 0x1f);
        b(0x60);
    } else {
        b(5);
        d(alignment < 0 ? DATA + 16 : (alignment & 1) ? 0x6020
                                                      : 0x601f);
    }
    if (test->has_imm)
        b(test->imm);
    resume_pc = emit_pos;
    if (test->result_kind == FLAGS_RESULT) {
        b(0x9c);
        b(0x58);
    }
    if (test->result_kind == GPR_RESULT || test->result_kind == FLAGS_RESULT) {
        b(0xa3);
        d(RESULT);
    } else {
        b(0x0f);
        b(test->result_kind == MMX_RESULT ? 0x7f : 0x11);
        b(5);
        d(RESULT);
    }
    b(0x0f);
    b(0xae);
    b(0x1d);
    d(RESULT + 16);
    b(0xc3);
    exception_tables();
    cpu_state.pc    = DRIVER;
    cpu_use_dynarec = engine;
    while (!done) {
        if (engine)
            exec386_dynarec(10000);
        else
            exec386(10000);
    }
    check(completed == CALLS, "call count");
    if (engine)
        check(compiled_results > 8, "warm compiled execution");
#ifdef PCBOX_SSE_NATIVE_CHECK
    if (engine && !fpu_softfloat && alignment < 0 && (test->opcode == 0x59 || test->opcode == 0x5e))
        check(native_regions > 0, "masked SSE stays native");
#endif
}
int
main(int argc, char **argv)
{
    int p3        = argc > 1 && !strcmp(argv[1], "p3");
    engine        = argc > 2 ? atoi(argv[2]) : 0;
    fpu_softfloat = argc > 3 ? atoi(argv[3]) : 0;
    cpu_f         = cpu_get_family(p3 ? "pentium3_katmai" : "pentium4w");
    cpu           = p3 ? 0 : 4;
    fpu_type      = FPU_INTERNAL;
    cpu_set();
    codegen_init();
    mem_size = 16 * 1024;
    mem_init();
    mem_reset();
    unsigned runs = 0;
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); ++i) {
        test = &cases[i];
        if (p3 && test->sse2)
            continue;
        if (argc > 4 && !strstr(test->name, argv[4]))
            continue;
        for (osxmm = 0; osxmm < 2; ++osxmm)
            for (memory_form = 0; memory_form < 2; ++memory_form)
                for (address16 = 0; address16 < 2; ++address16)
                    for (restore = 0; restore < 2; ++restore) {
                        run_case();
                        ++runs;
                    }
    }
    if (argc <= 4 || !strcmp(argv[4], "alignment")) {
        static const uint8_t ops[][3] = {
            { 0,    0x28, 0 },
            { 0,    0x29, 0 },
            { 0,    0x2b, 0 },
            { 0,    0x58, 0 },
            { 0,    0x54, 0 },
            { 0,    0x10, 0 },
            { 0x66, 0x28, 1 },
            { 0x66, 0x29, 1 },
            { 0x66, 0x6f, 1 },
            { 0x66, 0x7f, 1 },
            { 0x66, 0xe7, 1 }
        };
        for (unsigned i = 0; i < sizeof(ops) / sizeof(ops[0]); ++i) {
            if (p3 && ops[i][2])
                continue;
            simd_case aligned = {
                .name = "alignment", .prefix = ops[i][0], .opcode = ops[i][1], .a = { FONE,       FONE,       FONE,       FONE       },
                           .b = { 0x40000000, 0x40000000, 0x40000000, 0x40000000 },
                           .expected = { 0x40000000, 0x40000000, 0x40000000, 0x40000000 }
            };
            if (ops[i][1] == 0x58)
                for (int lane = 0; lane < 4; ++lane)
                    aligned.expected[lane] = 0x40400000;
            if (ops[i][1] == 0x54)
                for (int lane = 0; lane < 4; ++lane)
                    aligned.expected[lane] = 0;
            test        = &aligned;
            memory_form = 1;
            restore     = 0;
            osxmm       = 1;
            for (alignment = 0; alignment < 2; ++alignment)
                for (address16 = 0; address16 < 2; ++address16) {
                    run_case();
                    ++runs;
                }
        }
    }
    printf("%u SIMD cases, %u guest calls, %u failures (%s, engine=%d, softfloat=%d)\n",
           runs, runs * CALLS, failures, p3 ? "P3" : "P4", engine, fpu_softfloat);
    return failures ? 1 : 0;
}
