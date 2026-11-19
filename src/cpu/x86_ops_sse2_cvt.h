/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opCVTPI2PD_xmm_mm_a16(uint32_t fetchdat)
{
    MMX_REG src;
    SSE_ENTER();
    if ((fetchdat & 0xc0) == 0xc0) {
        MMX_CHECK();
    }
    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    cpu_state.XMM[cpu_reg].d[0] = i32_to_f64(src.sl[0]);
    cpu_state.XMM[cpu_reg].d[1] = i32_to_f64(src.sl[1]);
    CLOCK_CYCLES(1);
    if (cpu_mod == 3) {
        MMX_COMMIT();
    }
    return 0;
}

static int
opCVTPI2PD_xmm_mm_a32(uint32_t fetchdat)
{
    MMX_REG src;
    SSE_ENTER();
    if ((fetchdat & 0xc0) == 0xc0) {
        MMX_CHECK();
    }
    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    cpu_state.XMM[cpu_reg].d[0] = i32_to_f64(src.sl[0]);
    cpu_state.XMM[cpu_reg].d[1] = i32_to_f64(src.sl[1]);
    CLOCK_CYCLES(1);
    if (cpu_mod == 3) {
        MMX_COMMIT();
    }
    return 0;
}

static int
opCVTSI2SD_xmm_l_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].d[0] = i32_to_f64((int32_t)getr32(cpu_rm));
        CLOCK_CYCLES(1);
    } else {
        int32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].d[0] = i32_to_f64(dst);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opCVTSI2SD_xmm_l_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].d[0] = i32_to_f64((int32_t)getr32(cpu_rm));
        CLOCK_CYCLES(1);
    } else {
        int32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3UL);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].d[0] = i32_to_f64(dst);
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opCVTTPD2PI_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_REG src;
    MMX_REG result;
    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_16(fetchdat);
    
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.sl[0] = f64_to_i32_round_to_zero(src.d[0], &status);
    result.sl[1] = f64_to_i32_round_to_zero(src.d[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    MMX_ENTER();
    MMX_GETREG(cpu_reg) = result;
    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opCVTTPD2PI_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG src;
    MMX_REG result;
    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_32(fetchdat);

    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.sl[0] = f64_to_i32_round_to_zero(src.d[0], &status);
    result.sl[1] = f64_to_i32_round_to_zero(src.d[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    MMX_ENTER();
    MMX_GETREG(cpu_reg) = result;
    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opCVTTSD2SI_l_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC64();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    uint32_t result = f64_to_i32_round_to_zero(src.d[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    setr32(cpu_reg, result);
    return 0;
}

static int
opCVTTSD2SI_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC64();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    uint32_t result = f64_to_i32_round_to_zero(src.d[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    setr32(cpu_reg, result);
    return 0;
}

static int
opCVTSD2SI_l_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC64();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    uint32_t result = f64_to_i32_normal(src.d[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    setr32(cpu_reg, result);
    return 0;
}

static int
opCVTSD2SI_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC64();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    uint32_t result = f64_to_i32_normal(src.d[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    setr32(cpu_reg, result);
    return 0;
}

static int
opCVTPD2PS_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.f[0] = f64_to_f32(src.d[0], &status);
    result.f[1] = f64_to_f32(src.d[1], &status);
    result.l[2] = 0;
    result.l[3] = 0;
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTPD2PS_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.f[0] = f64_to_f32(src.d[0], &status);
    result.f[1] = f64_to_f32(src.d[1], &status);
    result.l[2] = 0;
    result.l[3] = 0;
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTPS2PD_mm_xmm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPD2PS_mm_xmm_a16(fetchdat);
    
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_REG src;
    SSE_GETSRC64();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.d[0] = f32_to_f64(src.f[0], &status);
    result.d[1] = f32_to_f64(src.f[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTPS2PD_mm_xmm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPD2PS_mm_xmm_a32(fetchdat);
    
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_REG src;
    SSE_GETSRC64();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.d[0] = f32_to_f64(src.f[0], &status);
    result.d[1] = f32_to_f64(src.f[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTSS2SD_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_REG src;
    SSE_GETSRC32();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.d[0] = f32_to_f64(src.f[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTSS2SD_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_REG src;
    SSE_GETSRC32();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.d[0] = f32_to_f64(src.f[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTSD2SS_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_REG src;
    SSE_GETSRC64();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.f[0] = f64_to_f32(src.d[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTSD2SS_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_REG src;
    SSE_GETSRC64();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.f[0] = f64_to_f32(src.d[0], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTPS2DQ_xmm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.sl[0] = f32_to_i32_normal(src.f[0], &status);
    result.sl[1] = f32_to_i32_normal(src.f[1], &status);
    result.sl[2] = f32_to_i32_normal(src.f[2], &status);
    result.sl[3] = f32_to_i32_normal(src.f[3], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTPS2DQ_xmm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    result.sl[0] = f32_to_i32_normal(src.f[0], &status);
    result.sl[1] = f32_to_i32_normal(src.f[1], &status);
    result.sl[2] = f32_to_i32_normal(src.f[2], &status);
    result.sl[3] = f32_to_i32_normal(src.f[3], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTDQ2PS_xmm_xmm_a16(uint32_t fetchdat)
{
    SSE_REG src;
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPS2DQ_xmm_xmm_a16(fetchdat);
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.f[0] = i32_to_f32(src.sl[0], &status);
    result.f[1] = i32_to_f32(src.sl[1], &status);
    result.f[2] = i32_to_f32(src.sl[2], &status);
    result.f[3] = i32_to_f32(src.sl[3], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTDQ2PS_xmm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG src;
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPS2DQ_xmm_xmm_a32(fetchdat);
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.f[0] = i32_to_f32(src.sl[0], &status);
    result.f[1] = i32_to_f32(src.sl[1], &status);
    result.f[2] = i32_to_f32(src.sl[2], &status);
    result.f[3] = i32_to_f32(src.sl[3], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTTPS2DQ_xmm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f32_to_i32_round_to_zero(src.f[0], &status);
    result.sl[1] = f32_to_i32_round_to_zero(src.f[1], &status);
    result.sl[2] = f32_to_i32_round_to_zero(src.f[2], &status);
    result.sl[3] = f32_to_i32_round_to_zero(src.f[3], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTTPS2DQ_xmm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f32_to_i32_round_to_zero(src.f[0], &status);
    result.sl[1] = f32_to_i32_round_to_zero(src.f[1], &status);
    result.sl[2] = f32_to_i32_round_to_zero(src.f[2], &status);
    result.sl[3] = f32_to_i32_round_to_zero(src.f[3], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTPD2DQ_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f64_to_i32_normal(src.d[0], &status);
    result.sl[1] = f64_to_i32_normal(src.d[1], &status);
    result.q[1] = 0;
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTPD2DQ_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f64_to_i32_normal(src.d[0], &status);
    result.sl[1] = f64_to_i32_normal(src.d[1], &status);
    result.q[1] = 0;
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTTPD2DQ_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(!cpu_state.sse_xmm);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f64_to_i32_round_to_zero(src.d[0], &status);
    result.sl[1] = f64_to_i32_round_to_zero(src.d[1], &status);
    result.q[1] = 0;
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTTPD2DQ_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(!cpu_state.sse_xmm);
    SSE_GETSRC();
    SSE_REG result = cpu_state.XMM[cpu_reg];
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f64_to_i32_round_to_zero(src.d[0], &status);
    result.sl[1] = f64_to_i32_round_to_zero(src.d[1], &status);
    result.q[1] = 0;
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    cpu_state.XMM[cpu_reg] = result;
    return 0;
}

static int
opCVTDQ2PD_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC64();
    cpu_state.XMM[cpu_reg].d[0] = i32_to_f64(src.sl[0]);
    cpu_state.XMM[cpu_reg].d[1] = i32_to_f64(src.sl[1]);
    return 0;
}

static int
opCVTDQ2PD_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC64();
    cpu_state.XMM[cpu_reg].d[0] = i32_to_f64(src.sl[0]);
    cpu_state.XMM[cpu_reg].d[1] = i32_to_f64(src.sl[1]);
    return 0;
}

static int
opCVTPD2PI_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG result;
    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_16(fetchdat);

    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f64_to_i32_normal(src.d[0], &status);
    result.sl[1] = f64_to_i32_normal(src.d[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    MMX_ENTER();
    MMX_GETREG(cpu_reg) = result;
    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opCVTPD2PI_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG result;
    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_32(fetchdat);

    SSE_GETSRC();
    
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result.sl[0] = f64_to_i32_normal(src.d[0], &status);
    result.sl[1] = f64_to_i32_normal(src.d[1], &status);
    if (softfloat_status_word_to_mxcsr(status))
        return 1;
    MMX_ENTER();
    MMX_GETREG(cpu_reg) = result;
    MMX_SETEXP(cpu_reg);
    return 0;
}
