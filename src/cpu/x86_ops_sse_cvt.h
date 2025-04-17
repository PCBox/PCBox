/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opCVTPI2PS_xmm_mm_a16(uint32_t fetchdat)
{
    MMX_REG src;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPI2PD_xmm_mm_a16(fetchdat);

    MMX_ENTER();
    fetch_ea_16(fetchdat);
    MMX_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    cpu_state.XMM[cpu_reg].f[0] = i32_to_f32(src.sl[0], &status);
    cpu_state.XMM[cpu_reg].f[1] = i32_to_f32(src.sl[1], &status);
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/

    return 0;
}

static int
opCVTPI2PS_xmm_mm_a32(uint32_t fetchdat)
{
    MMX_REG src;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPI2PD_xmm_mm_a32(fetchdat);

    MMX_ENTER();
    fetch_ea_32(fetchdat);
    MMX_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    cpu_state.XMM[cpu_reg].f[0] = i32_to_f32(src.sl[0], &status);
    cpu_state.XMM[cpu_reg].f[1] = i32_to_f32(src.sl[1], &status);
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/

    return 0;
}

static int
opCVTSI2SS_xmm_l_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].f[0] = i32_to_f32((int32_t)getr32(cpu_rm), &status);
        CLOCK_CYCLES(1);
    } else {
        int32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].f[0] = i32_to_f32(src, &status);
        CLOCK_CYCLES(2);
    }
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/

    return 0;
}

static int
opCVTSI2SS_xmm_l_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].f[0] = i32_to_f32((int32_t)getr32(cpu_rm), &status);
        CLOCK_CYCLES(1);
    } else {
        int32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].f[0] = i32_to_f32(src, &status);
        CLOCK_CYCLES(2);
    }
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/

    return 0;
}

static int
opCVTTPS2PI_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG *dst;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTTPD2PI_mm_xmm_a16(fetchdat);

    MMX_ENTER();
    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    dst->sl[0] = f32_to_i32_round_to_zero(src.f[0], &status);
    dst->sl[1] = f32_to_i32_round_to_zero(src.f[1], &status);
    MMX_SETEXP(cpu_reg);
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTTPS2PI_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG *dst;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTTPD2PI_mm_xmm_a32(fetchdat);

    MMX_ENTER();
    fetch_ea_32(fetchdat);
    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    dst->sl[0] = f32_to_i32_round_to_zero(src.f[0], &status);
    dst->sl[1] = f32_to_i32_round_to_zero(src.f[1], &status);
    MMX_SETEXP(cpu_reg);
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTTSS2SI_l_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG  src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    setr32(cpu_reg, f32_to_i32_round_to_zero(src.f[0], &status));
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTTSS2SI_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    setr32(cpu_reg, f32_to_i32_round_to_zero(src.f[0], &status));
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTPS2PI_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG *dst;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPD2PI_mm_xmm_a16(fetchdat);

    MMX_ENTER();
    fetch_ea_16(fetchdat);
    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    dst->sl[0] = f32_to_i32_normal(src.f[0], &status);
    dst->sl[1] = f32_to_i32_normal(src.f[1], &status);
    MMX_SETEXP(cpu_reg);
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/
    CLOCK_CYCLES(1);

    return 0;
}

static int
opCVTPS2PI_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG *dst;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPD2PI_mm_xmm_a32(fetchdat);

    MMX_ENTER();
    fetch_ea_32(fetchdat);
    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    dst->sl[0] = f32_to_i32_normal(src.f[0], &status);
    dst->sl[1] = f32_to_i32_normal(src.f[1], &status);
    MMX_SETEXP(cpu_reg);
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/
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
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result = f32_to_i32_normal(src.f[0], &status);
    setr32(cpu_reg, result);
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/

    return 0;
}

static int
opCVTSS2SI_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG  src;
    int32_t result;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    result = f32_to_i32_normal(src.f[0], &status);
    setr32(cpu_reg, result);
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/

    return 0;
}
