/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdarg.h>
#include <stdlib.h>
#include "../../../src/cpu/codegen_timing_p6.c"
#include "../../../src/cpu/codegen_timing_p4.c"
cpu_state_t cpu_state;
int cpu_has_feature(int feature) { return feature == CPU_FEATURE_MMX; }
static CPU  model = { .cpu_type = CPU_PENTIUM3 };
CPU        *cpu_s = &model;
int         codegen_block_cycles;
void
fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}
#define CHECK(c)                                  \
    do {                                          \
        if (!(c))                                 \
            fatal("line %d: %s\n", __LINE__, #c); \
    } while (0)
static codegen_timing_t *timing = &codegen_timing_p6;
static void
begin(void)
{
    timing->start();
    timing->block_start();
    codegen_block_cycles = 0;
}
static void
emit(uint8_t prefix, uint8_t opcode, uint32_t modrm)
{
    timing->start();
    if (prefix)
        timing->prefix(prefix, modrm);
    timing->prefix(0x0f, modrm);
    timing->opcode(opcode, modrm, 0x300, 0);
}
static int
measure(uint8_t prefix, uint8_t op, int n, int chain)
{
    begin();
    for (int i = 0; i < n; ++i)
        /* Eight independent recurrences: PMULLW mm has latency 8 and can
         * issue every cycle, so seven destinations cannot saturate it. */
        emit(prefix, op, chain ? 0xc0 : 0xc0 | ((i % 8) * 9));
    timing->block_end();
    return codegen_block_cycles;
}
typedef struct {
    const char *name;
    uint8_t     prefix, opcode;
    int         latency, throughput;
} anchor;
static void
verify(const anchor *cases, unsigned count)
{
    for (unsigned i = 0; i < count; ++i) {
        const anchor *a          = &cases[i];
        int           latency    = measure(a->prefix, a->opcode, 420, 1) - measure(a->prefix, a->opcode, 210, 1);
        int           throughput = measure(a->prefix, a->opcode, 420, 0) - measure(a->prefix, a->opcode, 210, 0);
        if (latency != 210 * a->latency || throughput != 210 * a->throughput)
            fatal("%s: latency %.3f/%d throughput %.3f/%d\n", a->name, latency / 210.0, a->latency, throughput / 210.0, a->throughput);
    }
}
int
main(void)
{
    /* Independent expected values from Fog's Pentium III tables, pp.187-188.
     * Long streams separate dependency latency from reciprocal throughput. */
    static const anchor p3[] = {
        { "ADDPS",  0,    0x58, 3,  2  },
        { "ADDSS",  0xf3, 0x58, 3,  1  },
        { "MULPS",  0,    0x59, 4,  2  },
        { "MULSS",  0xf3, 0x59, 4,  1  },
        { "DIVPS",  0,    0x5e, 48, 34 },
        { "DIVSS",  0xf3, 0x5e, 18, 17 },
        { "SQRTPS", 0,    0x51, 56, 56 },
        { "SQRTSS", 0xf3, 0x51, 30, 28 },
        { "ANDPS",  0,    0x54, 2,  2  },
        { "CMPPS",  0,    0xc2, 3,  2  },
        { "MOVAPS", 0,    0x28, 1,  1  },
        { "RCPPS",  0,    0x53, 2,  2  }
    };
    verify(p3, sizeof(p3) / sizeof(p3[0]));
    begin();
    reg_available_timestamp[0] = 20;
    emit(0, 0x58, 0x00);
    CHECK(xmm_timestamp[0] >= 25); /* ADDPS xmm0,[eax] */
    begin();
    xmm_timestamp[0] = 30;
    emit(0, 0x28, 0xc1);
    CHECK(xmm_timestamp[0] == 1); /* MOVAPS overwrites. */
    begin();
    xmm_timestamp[1] = 20;
    emit(0, 0x50, 0xc1);
    CHECK(reg_available_timestamp[0] == 21); /* MOVMSKPS bridge. */
    begin();
    emit(0, 0x58, 0xc1);
    int clean = codegen_block_cycles;
    begin();
    emit(0, 0x58, 0xc1);
    CHECK(codegen_block_cycles == clean);
    puts("Pentium III SSE hardware anchors and operand dependencies passed");
    /* Intel 248966-004, Appendix C: Willamette, not the later Prescott table. */
    static const anchor p4[] = {
        { "ADDPS",      0,    0x58, 4,  2  },
        { "ADDSS",      0xf3, 0x58, 4,  2  },
        { "ADDPD",      0x66, 0x58, 4,  2  },
        { "ADDSD",      0xf2, 0x58, 4,  2  },
        { "MULPS",      0,    0x59, 6,  2  },
        { "MULSD",      0xf2, 0x59, 6,  2  },
        { "DIVPS",      0,    0x5e, 32, 32 },
        { "DIVSS",      0xf3, 0x5e, 22, 22 },
        { "DIVPD",      0x66, 0x5e, 62, 62 },
        { "DIVSD",      0xf2, 0x5e, 35, 35 },
        { "SQRTPS",     0,    0x51, 32, 32 },
        { "SQRTSS",     0xf3, 0x51, 22, 22 },
        { "SQRTPD",     0x66, 0x51, 62, 62 },
        { "SQRTSD",     0xf2, 0x51, 35, 35 },
        { "ANDPS",      0,    0x54, 4,  2  },
        { "CMPPD",      0x66, 0xc2, 4,  2  },
        { "MOVAPS",     0,    0x28, 6,  1  },
        { "RCPPS",      0,    0x53, 6,  4  },
        { "PADDD xmm",  0x66, 0xfe, 2,  2  },
        { "PMULLW xmm", 0x66, 0xd5, 8,  2  },
        { "PADDD mm",   0,    0xfe, 2,  1  },
        { "PMULLW mm",  0,    0xd5, 8,  1  }
    };
    model.cpu_type = CPU_PENTIUM4W;
    timing         = &codegen_timing_p4;
    verify(p4, sizeof(p4) / sizeof(p4[0]));
    begin();
    for (int i = 0; i < 210; ++i) {
        timing->start();
        timing->opcode(0x01, 0xc0, 0x300, 0);
    }
    timing->block_end();
    CHECK(codegen_block_cycles == 105); /* Half-clock ADD chain. */
    begin();
    for (int i = 0; i < 210; ++i) {
        timing->start();
        timing->prefix(0x0f, 0xc0);
        timing->opcode(0xaf, 0xc0, 0x300, 0);
    }
    timing->block_end();
    if (codegen_block_cycles != 2940) fatal("IMUL chain: %d, EAX ready %d\n", codegen_block_cycles, nb.gpr[0]); /* 14-clock IMUL chain. */
    begin();
    for (int i = 0; i < 210; ++i) {
        timing->start();
        timing->prefix(0xd8, 0xc0);
        timing->opcode(0xc0, 0xc0, 0x300, 0);
    }
    timing->block_end();
    CHECK(codegen_block_cycles == 1050); /* 5-clock x87 FADD chain. */
    begin();
    nb.gpr[0] = 40;
    emit(0, 0x58, 0);
    CHECK(nb.xmm[0] >= 60);
    begin();
    nb.xmm[1] = 40;
    emit(0, 0x50, 0xc1);
    CHECK(nb.gpr[0] == 52);
    begin();
    emit(0, 0x5e, 0xc1);
    begin();
    emit(0, 0x58, 0xc1);
    CHECK(codegen_block_cycles == 4);
    puts("Willamette SSE/SSE2, MMX, integer and x87 hardware anchors passed");
    return 0;
}
