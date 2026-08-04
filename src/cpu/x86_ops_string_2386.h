/* The 286 steps the string pointer registers before it reports a fault on
   the transfer, so an exception handler has to step them back again to
   restart the instruction; which registers were stepped depends on which
   operand faulted.  The 386 and later restart with them untouched, and
   share this file, so the behaviour is gated to the 286. */
#define STR_286 (is286 && !is386)

static int
opMOVSB_a16(UNUSED(uint32_t fetchdat))
{
    uint8_t  temp;
    uint16_t soff = SI;
    uint16_t doff = DI;
    int      step = (cpu_state.flags & D_FLAG) ? -1 : 1;

    addr64 = addr64_2 = 0x00000000;

    if (STR_286)
        SI += step;
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, soff, soff);
    if (STR_286)
        DI += step;
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, doff, doff);
    high_page = 0;
    do_mmut_rb(cpu_state.ea_seg->base, soff, &addr64);
    if (cpu_state.abrt)
        return 1;

    do_mmut_wb(es, doff, &addr64_2);
    if (cpu_state.abrt)
        return 1;
    temp = readmemb_n(cpu_state.ea_seg->base, soff, addr64);
    if (cpu_state.abrt)
        return 1;
    writememb_n(es, doff, addr64_2, temp);
    if (cpu_state.abrt)
        return 1;
    if (!STR_286) {
        DI += step;
        SI += step;
    }
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opMOVSB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    addr64 = addr64_2 = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI);
    high_page = 0;
    do_mmut_rb(cpu_state.ea_seg->base, ESI, &addr64);
    if (cpu_state.abrt)
        return 1;
    do_mmut_wb(es, EDI, &addr64_2);
    if (cpu_state.abrt)
        return 1;
    temp = readmemb_n(cpu_state.ea_seg->base, ESI, addr64);
    if (cpu_state.abrt)
        return 1;
    writememb_n(es, EDI, addr64_2, temp);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG) {
        EDI--;
        ESI--;
    } else {
        EDI++;
        ESI++;
    }
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opMOVSW_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;
    uint16_t soff = SI;
    uint16_t doff = DI;
    int      step = (cpu_state.flags & D_FLAG) ? -2 : 2;

    addr64a[0] = addr64a[1] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = 0x00000000;

    if (STR_286)
        SI += step;
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, soff, soff + 1UL);
    if (STR_286)
        DI += step;
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, doff, doff + 1UL);
    high_page = 0;
    do_mmut_rw(cpu_state.ea_seg->base, soff, addr64a);
    if (cpu_state.abrt)
        return 1;
    do_mmut_ww(es, doff, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    temp = readmemw_n(cpu_state.ea_seg->base, soff, addr64a);
    if (cpu_state.abrt)
        return 1;
    writememw_n(es, doff, addr64a_2, temp);
    if (cpu_state.abrt)
        return 1;
    if (!STR_286) {
        DI += step;
        SI += step;
    }
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opMOVSW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    addr64a[0] = addr64a[1] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 1UL);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 1UL);
    high_page = 0;
    do_mmut_rw(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;
    do_mmut_ww(es, EDI, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    temp = readmemw_n(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;
    writememw_n(es, EDI, addr64a_2, temp);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG) {
        EDI -= 2;
        ESI -= 2;
    } else {
        EDI += 2;
        ESI += 2;
    }
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opMOVSL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 3UL);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI + 3UL);
    high_page = 0;
    do_mmut_rl(cpu_state.ea_seg->base, SI, addr64a);
    if (cpu_state.abrt)
        return 1;
    do_mmut_wl(es, DI, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    temp = readmeml_n(cpu_state.ea_seg->base, SI, addr64a);
    if (cpu_state.abrt)
        return 1;
    writememl_n(es, DI, addr64a_2, temp);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG) {
        DI -= 4;
        SI -= 4;
    } else {
        DI += 4;
        SI += 4;
    }
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 0, 1, 0, 1, 0);
    return 0;
}
static int
opMOVSL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 3UL);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 3UL);
    high_page = 0;
    do_mmut_rl(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;
    do_mmut_wl(es, EDI, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    temp = readmeml_n(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;
    writememl_n(es, EDI, addr64a_2, temp);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG) {
        EDI -= 4;
        ESI -= 4;
    } else {
        EDI += 4;
        ESI += 4;
    }
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 0, 1, 0, 1, 1);
    return 0;
}

static int
opCMPSB_a16(UNUSED(uint32_t fetchdat))
{
    uint8_t  src;
    uint8_t  dst;
    uint16_t soff = SI;
    uint16_t doff = DI;
    int      step = (cpu_state.flags & D_FLAG) ? -1 : 1;

    addr64 = addr64_2 = 0x00000000;

    if (STR_286)
        DI += step;
    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, doff, doff);
    if (STR_286)
        SI += step;
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, soff, soff);
    high_page = uncached = 0;
    do_mmut_rb(cpu_state.ea_seg->base, soff, &addr64);
    if (cpu_state.abrt)
        return 1;
    do_mmut_rb2(es, doff, &addr64_2);
    if (cpu_state.abrt)
        return 1;
    src = readmemb_n(cpu_state.ea_seg->base, soff, addr64);
    if (cpu_state.abrt)
        return 1;
    dst = readmemb_n(es, doff, addr64_2);
    if (cpu_state.abrt)
        return 1;
    setsub8(src, dst);
    if (!STR_286) {
        DI += step;
        SI += step;
    }
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2, 0, 0, 0, 0);
    return 0;
}
static int
opCMPSB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t src;
    uint8_t dst;

    addr64 = addr64_2 = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI);
    high_page = uncached = 0;
    do_mmut_rb(cpu_state.ea_seg->base, ESI, &addr64);
    if (cpu_state.abrt)
        return 1;
    do_mmut_rb2(es, EDI, &addr64_2);
    if (cpu_state.abrt)
        return 1;
    src = readmemb_n(cpu_state.ea_seg->base, ESI, addr64);
    if (cpu_state.abrt)
        return 1;
    dst = readmemb_n(es, EDI, addr64_2);
    if (cpu_state.abrt)
        return 1;
    setsub8(src, dst);
    if (cpu_state.flags & D_FLAG) {
        EDI--;
        ESI--;
    } else {
        EDI++;
        ESI++;
    }
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2, 0, 0, 0, 1);
    return 0;
}

static int
opCMPSW_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t src;
    uint16_t dst;
    uint16_t soff = SI;
    uint16_t doff = DI;
    int      step = (cpu_state.flags & D_FLAG) ? -2 : 2;

    addr64a[0] = addr64a[1] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = 0x00000000;

    if (STR_286)
        DI += step;
    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, doff, doff + 1UL);
    if (STR_286)
        SI += step;
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, soff, soff + 1UL);
    high_page = uncached = 0;
    do_mmut_rw(cpu_state.ea_seg->base, soff, addr64a);
    if (cpu_state.abrt)
        return 1;
    do_mmut_rw2(es, doff, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    src = readmemw_n(cpu_state.ea_seg->base, soff, addr64a);
    if (cpu_state.abrt)
        return 1;
    dst = readmemw_n(es, doff, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    setsub16(src, dst);
    if (!STR_286) {
        DI += step;
        SI += step;
    }
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2, 0, 0, 0, 0);
    return 0;
}
static int
opCMPSW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t src;
    uint16_t dst;

    addr64a[0] = addr64a[1] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 1UL);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI + 1UL);
    high_page = uncached = 0;
    do_mmut_rw(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;
    do_mmut_rw2(es, EDI, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    src = readmemw_n(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;
    dst = readmemw_n(es, EDI, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    setsub16(src, dst);
    if (cpu_state.flags & D_FLAG) {
        EDI -= 2;
        ESI -= 2;
    } else {
        EDI += 2;
        ESI += 2;
    }
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 2, 0, 0, 0, 1);
    return 0;
}

static int
opCMPSL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t src;
    uint32_t dst;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 3UL);
    CHECK_READ(&cpu_state.seg_es, DI, DI + 3UL);
    high_page = uncached = 0;
    do_mmut_rl(cpu_state.ea_seg->base, SI, addr64a);
    if (cpu_state.abrt)
        return 1;
    do_mmut_rl2(es, DI, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    src = readmeml_n(cpu_state.ea_seg->base, SI, addr64a);
    if (cpu_state.abrt)
        return 1;
    dst = readmeml_n(es, DI, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    setsub32(src, dst);
    if (cpu_state.flags & D_FLAG) {
        DI -= 4;
        SI -= 4;
    } else {
        DI += 4;
        SI += 4;
    }
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 0, 2, 0, 0, 0);
    return 0;
}
static int
opCMPSL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t src;
    uint32_t dst;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;
    addr64a_2[0] = addr64a_2[1] = addr64a_2[2] = addr64a_2[3] = 0x00000000;

    SEG_CHECK_READ(cpu_state.ea_seg);
    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 3UL);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI + 3UL);
    high_page = uncached = 0;
    do_mmut_rl(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;
    do_mmut_rl2(es, EDI, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    src = readmeml_n(cpu_state.ea_seg->base, ESI, addr64a);
    if (cpu_state.abrt)
        return 1;
    dst = readmeml_n(es, EDI, addr64a_2);
    if (cpu_state.abrt)
        return 1;
    setsub32(src, dst);
    if (cpu_state.flags & D_FLAG) {
        EDI -= 4;
        ESI -= 4;
    } else {
        EDI += 4;
        ESI += 4;
    }
    CLOCK_CYCLES((is486) ? 8 : 10);
    PREFETCH_RUN((is486) ? 8 : 10, 1, -1, 0, 2, 0, 0, 1);
    return 0;
}

static int
opSTOSB_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t doff = DI;
    int      step = (cpu_state.flags & D_FLAG) ? -1 : 1;

    if (STR_286)
        DI += step;
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, doff, doff);
    writememb(es, doff, AL);
    if (cpu_state.abrt)
        return 1;
    if (!STR_286)
        DI += step;
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 1, 0, 0);
    return 0;
}
static int
opSTOSB_a32(UNUSED(uint32_t fetchdat))
{
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI);
    writememb(es, EDI, AL);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG)
        EDI--;
    else
        EDI++;
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 1, 0, 1);
    return 0;
}

static int
opSTOSW_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t doff = DI;
    int      step = (cpu_state.flags & D_FLAG) ? -2 : 2;

    if (STR_286)
        DI += step;
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, doff, doff + 1UL);
    writememw(es, doff, AX);
    if (cpu_state.abrt)
        return 1;
    if (!STR_286)
        DI += step;
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 1, 0, 0);
    return 0;
}
static int
opSTOSW_a32(UNUSED(uint32_t fetchdat))
{
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 1UL);
    writememw(es, EDI, AX);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG)
        EDI -= 2;
    else
        EDI += 2;
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 1, 0, 1);
    return 0;
}

static int
opSTOSL_a16(UNUSED(uint32_t fetchdat))
{
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI + 3UL);
    writememl(es, DI, EAX);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG)
        DI -= 4;
    else
        DI += 4;
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 0, 1, 0);
    return 0;
}
static int
opSTOSL_a32(UNUSED(uint32_t fetchdat))
{
    SEG_CHECK_WRITE(&cpu_state.seg_es);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 3UL);
    writememl(es, EDI, EAX);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG)
        EDI -= 4;
    else
        EDI += 4;
    CLOCK_CYCLES(4);
    PREFETCH_RUN(4, 1, -1, 0, 0, 0, 1, 1);
    return 0;
}

static int
opLODSB_a16(UNUSED(uint32_t fetchdat))
{
    uint8_t  temp;
    uint16_t soff = SI;
    int      step = (cpu_state.flags & D_FLAG) ? -1 : 1;

    if (STR_286)
        SI += step;
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, soff, soff);
    temp = readmemb(cpu_state.ea_seg->base, soff);
    if (cpu_state.abrt)
        return 1;
    AL = temp;
    if (!STR_286)
        SI += step;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opLODSB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI);
    temp = readmemb(cpu_state.ea_seg->base, ESI);
    if (cpu_state.abrt)
        return 1;
    AL = temp;
    if (cpu_state.flags & D_FLAG)
        ESI--;
    else
        ESI++;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 1);
    return 0;
}

static int
opLODSW_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;
    uint16_t soff = SI;
    int      step = (cpu_state.flags & D_FLAG) ? -2 : 2;

    if (STR_286)
        SI += step;
    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, soff, soff + 1UL);
    temp = readmemw(cpu_state.ea_seg->base, soff);
    if (cpu_state.abrt)
        return 1;
    AX = temp;
    if (!STR_286)
        SI += step;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opLODSW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 1UL);
    temp = readmemw(cpu_state.ea_seg->base, ESI);
    if (cpu_state.abrt)
        return 1;
    AX = temp;
    if (cpu_state.flags & D_FLAG)
        ESI -= 2;
    else
        ESI += 2;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 1, 0, 0, 0, 1);
    return 0;
}

static int
opLODSL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 3UL);
    temp = readmeml(cpu_state.ea_seg->base, SI);
    if (cpu_state.abrt)
        return 1;
    EAX = temp;
    if (cpu_state.flags & D_FLAG)
        SI -= 4;
    else
        SI += 4;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 0, 1, 0, 0, 0);
    return 0;
}
static int
opLODSL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 3UL);
    temp = readmeml(cpu_state.ea_seg->base, ESI);
    if (cpu_state.abrt)
        return 1;
    EAX = temp;
    if (cpu_state.flags & D_FLAG)
        ESI -= 4;
    else
        ESI += 4;
    CLOCK_CYCLES(5);
    PREFETCH_RUN(5, 1, -1, 0, 1, 0, 0, 1);
    return 0;
}

static int
opSCASB_a16(UNUSED(uint32_t fetchdat))
{
    uint8_t  temp;
    uint16_t doff = DI;
    int      step = (cpu_state.flags & D_FLAG) ? -1 : 1;

    if (STR_286)
        DI += step;
    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, doff, doff);
    temp = readmemb(es, doff);
    if (cpu_state.abrt)
        return 1;
    setsub8(AL, temp);
    if (!STR_286)
        DI += step;
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opSCASB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI);
    temp = readmemb(es, EDI);
    if (cpu_state.abrt)
        return 1;
    setsub8(AL, temp);
    if (cpu_state.flags & D_FLAG)
        EDI--;
    else
        EDI++;
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 0, 0, 1);
    return 0;
}

static int
opSCASW_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;
    uint16_t doff = DI;
    int      step = (cpu_state.flags & D_FLAG) ? -2 : 2;

    if (STR_286)
        DI += step;
    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, doff, doff + 1UL);
    temp = readmemw(es, doff);
    if (cpu_state.abrt)
        return 1;
    setsub16(AX, temp);
    if (!STR_286)
        DI += step;
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 0, 0, 0);
    return 0;
}
static int
opSCASW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI + 1UL);
    temp = readmemw(es, EDI);
    if (cpu_state.abrt)
        return 1;
    setsub16(AX, temp);
    if (cpu_state.flags & D_FLAG)
        EDI -= 2;
    else
        EDI += 2;
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 1, 0, 0, 0, 1);
    return 0;
}

static int
opSCASL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, DI, DI + 3UL);
    temp = readmeml(es, DI);
    if (cpu_state.abrt)
        return 1;
    setsub32(EAX, temp);
    if (cpu_state.flags & D_FLAG)
        DI -= 4;
    else
        DI += 4;
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 0, 1, 0, 0, 0);
    return 0;
}
static int
opSCASL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    SEG_CHECK_READ(&cpu_state.seg_es);
    CHECK_READ(&cpu_state.seg_es, EDI, EDI + 3UL);
    temp = readmeml(es, EDI);
    if (cpu_state.abrt)
        return 1;
    setsub32(EAX, temp);
    if (cpu_state.flags & D_FLAG)
        EDI -= 4;
    else
        EDI += 4;
    CLOCK_CYCLES(7);
    PREFETCH_RUN(7, 1, -1, 0, 1, 0, 0, 1);
    return 0;
}

static int
opINSB_a16(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    addr64 = 0x00000000;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    check_io_perm(DX, 1);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI);
    high_page = 0;
    do_mmut_wb(es, DI, &addr64);
    if (cpu_state.abrt)
        return 1;
    temp = inb(DX);
    writememb_n(es, DI, addr64, temp);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG)
        DI--;
    else
        DI++;
    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opINSB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    addr64 = 0x00000000;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    check_io_perm(DX, 1);
    high_page = 0;
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI);
    do_mmut_wb(es, EDI, &addr64);
    if (cpu_state.abrt)
        return 1;
    temp = inb(DX);
    writememb_n(es, EDI, addr64, temp);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG)
        EDI--;
    else
        EDI++;
    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opINSW_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    addr64a[0] = addr64a[1] = 0x00000000;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    check_io_perm(DX, 2);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI + 1UL);
    high_page = 0;
    do_mmut_ww(es, DI, addr64a);
    if (cpu_state.abrt)
        return 1;
    temp = inw(DX);
    writememw_n(es, DI, addr64a, temp);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG)
        DI -= 2;
    else
        DI += 2;
    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opINSW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    addr64a[0] = addr64a[1] = 0x00000000;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    high_page = 0;
    check_io_perm(DX, 2);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 1UL);
    do_mmut_ww(es, EDI, addr64a);
    if (cpu_state.abrt)
        return 1;
    temp = inw(DX);
    writememw_n(es, EDI, addr64a, temp);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG)
        EDI -= 2;
    else
        EDI += 2;
    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opINSL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    check_io_perm(DX, 4);
    CHECK_WRITE(&cpu_state.seg_es, DI, DI + 3UL);
    high_page = 0;
    do_mmut_wl(es, DI, addr64a);
    if (cpu_state.abrt)
        return 1;
    temp = inl(DX);
    writememl_n(es, DI, addr64a, temp);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG)
        DI -= 4;
    else
        DI += 4;
    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 0, 1, 0, 1, 0);
    return 0;
}
static int
opINSL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    addr64a[0] = addr64a[1] = addr64a[2] = addr64a[3] = 0x00000000;

    SEG_CHECK_WRITE(&cpu_state.seg_es);
    check_io_perm(DX, 4);
    CHECK_WRITE(&cpu_state.seg_es, EDI, EDI + 3UL);
    high_page = 0;
    do_mmut_wl(es, DI, addr64a);
    if (cpu_state.abrt)
        return 1;
    temp = inl(DX);
    writememl_n(es, EDI, addr64a, temp);
    if (cpu_state.abrt)
        return 1;
    if (cpu_state.flags & D_FLAG)
        EDI -= 4;
    else
        EDI += 4;
    CLOCK_CYCLES(15);
    PREFETCH_RUN(15, 1, -1, 0, 1, 0, 1, 1);
    return 0;
}

static int
opOUTSB_a16(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI);
    temp = readmemb(cpu_state.ea_seg->base, SI);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 1);
    if (cpu_state.flags & D_FLAG)
        SI--;
    else
        SI++;
    outb(DX, temp);
    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opOUTSB_a32(UNUSED(uint32_t fetchdat))
{
    uint8_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI);
    temp = readmemb(cpu_state.ea_seg->base, ESI);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 1);
    if (cpu_state.flags & D_FLAG)
        ESI--;
    else
        ESI++;
    outb(DX, temp);
    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opOUTSW_a16(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 1UL);
    temp = readmemw(cpu_state.ea_seg->base, SI);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 2);
    if (cpu_state.flags & D_FLAG)
        SI -= 2;
    else
        SI += 2;
    outw(DX, temp);
    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 1, 0, 1, 0, 0);
    return 0;
}
static int
opOUTSW_a32(UNUSED(uint32_t fetchdat))
{
    uint16_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 1UL);
    temp = readmemw(cpu_state.ea_seg->base, ESI);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 2);
    if (cpu_state.flags & D_FLAG)
        ESI -= 2;
    else
        ESI += 2;
    outw(DX, temp);
    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 1, 0, 1, 0, 1);
    return 0;
}

static int
opOUTSL_a16(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, SI, SI + 3UL);
    temp = readmeml(cpu_state.ea_seg->base, SI);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 4);
    if (cpu_state.flags & D_FLAG)
        SI -= 4;
    else
        SI += 4;
    outl(EDX, temp);
    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 0, 1, 0, 1, 0);
    return 0;
}
static int
opOUTSL_a32(UNUSED(uint32_t fetchdat))
{
    uint32_t temp;

    SEG_CHECK_READ(cpu_state.ea_seg);
    CHECK_READ(cpu_state.ea_seg, ESI, ESI + 3UL);
    temp = readmeml(cpu_state.ea_seg->base, ESI);
    if (cpu_state.abrt)
        return 1;
    check_io_perm(DX, 4);
    if (cpu_state.flags & D_FLAG)
        ESI -= 4;
    else
        ESI += 4;
    outl(EDX, temp);
    CLOCK_CYCLES(14);
    PREFETCH_RUN(14, 1, -1, 0, 1, 0, 1, 1);
    return 0;
}

#undef STR_286
