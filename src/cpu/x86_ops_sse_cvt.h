/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opCVTPI2PS_xmm_mm_a16(uint32_t fetchdat)
{
    MMX_REG src;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPI2PD_xmm_mm_a16(fetchdat);

    SSE_ENTER();
    if ((fetchdat & 0xc0) == 0xc0) {
        MMX_CHECK();
    }
    fetch_ea_16(fetchdat);
    MMX_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.f[0] = i32_to_f32(src.sl[0], &status);
    result.f[1] = i32_to_f32(src.sl[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    if (cpu_mod == 3) {
        MMX_COMMIT();
    }
    cpu_state.XMM[cpu_reg] = result;

    return 0;
}

static int
opCVTPI2PS_xmm_mm_a32(uint32_t fetchdat)
{
    MMX_REG src;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPI2PD_xmm_mm_a32(fetchdat);

    SSE_ENTER();
    if ((fetchdat & 0xc0) == 0xc0) {
        MMX_CHECK();
    }
    fetch_ea_32(fetchdat);
    MMX_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.f[0] = i32_to_f32(src.sl[0], &status);
    result.f[1] = i32_to_f32(src.sl[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    if (cpu_mod == 3) {
        MMX_COMMIT();
    }
    cpu_state.XMM[cpu_reg] = result;

    return 0;
}

static int
opCVTSI2SS_xmm_l_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    if (cpu_mod == 3) {
        result.f[0] = i32_to_f32((int32_t)getr32(cpu_rm), &status);
        CLOCK_CYCLES(1);
    } else {
        int32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        result.f[0] = i32_to_f32(src, &status);
        CLOCK_CYCLES(2);
    }
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;

    return 0;
}

static int
opCVTSI2SS_xmm_l_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    if (cpu_mod == 3) {
        result.f[0] = i32_to_f32((int32_t)getr32(cpu_rm), &status);
        CLOCK_CYCLES(1);
    } else {
        int32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        result.f[0] = i32_to_f32(src, &status);
        CLOCK_CYCLES(2);
    }
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;

    return 0;
}

static int
opCVTTPS2PI_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG result;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTTPD2PI_mm_xmm_a16(fetchdat);

    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_16(fetchdat);

    SSE_GETSRC64();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f32_to_i32_round_to_zero(src.f[0], &status);
    result.sl[1] = f32_to_i32_round_to_zero(src.f[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    MMX_ENTER();
    MMX_GETREG(cpu_reg) = result;
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTTPS2PI_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG result;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTTPD2PI_mm_xmm_a32(fetchdat);

    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_32(fetchdat);
    SSE_GETSRC64();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f32_to_i32_round_to_zero(src.f[0], &status);
    result.sl[1] = f32_to_i32_round_to_zero(src.f[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    MMX_ENTER();
    MMX_GETREG(cpu_reg) = result;
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTTSS2SI_l_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG  src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC32();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    uint32_t result = f32_to_i32_round_to_zero(src.f[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    setr32(cpu_reg, result);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTTSS2SI_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC32();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    uint32_t result = f32_to_i32_round_to_zero(src.f[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    setr32(cpu_reg, result);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTPS2PI_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG result;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPD2PI_mm_xmm_a16(fetchdat);

    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_16(fetchdat);
    SSE_GETSRC64();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f32_to_i32_normal(src.f[0], &status);
    result.sl[1] = f32_to_i32_normal(src.f[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    MMX_ENTER();
    MMX_GETREG(cpu_reg) = result;
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTPS2PI_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG result;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPD2PI_mm_xmm_a32(fetchdat);

    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_32(fetchdat);
    SSE_GETSRC64();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f32_to_i32_normal(src.f[0], &status);
    result.sl[1] = f32_to_i32_normal(src.f[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    MMX_ENTER();
    MMX_GETREG(cpu_reg) = result;
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTSS2SI_l_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG  src;
    int32_t result;
    fetch_ea_16(fetchdat);
    SSE_GETSRC32();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result = f32_to_i32_normal(src.f[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    setr32(cpu_reg, result);

    return 0;
}

static int
opCVTSS2SI_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG  src;
    int32_t result;
    fetch_ea_32(fetchdat);
    SSE_GETSRC32();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result = f32_to_i32_normal(src.f[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    setr32(cpu_reg, result);

    return 0;
}
