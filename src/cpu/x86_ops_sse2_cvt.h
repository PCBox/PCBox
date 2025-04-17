/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opCVTPI2PD_xmm_mm_a16(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    cpu_state.XMM[cpu_reg].d[0] = i32_to_f64(src.sl[0]);
    cpu_state.XMM[cpu_reg].d[1] = i32_to_f64(src.sl[1]);
    CLOCK_CYCLES(1);
    return 0;
}

static int
opCVTPI2PD_xmm_mm_a32(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    cpu_state.XMM[cpu_reg].d[0] = i32_to_f64(src.sl[0]);
    cpu_state.XMM[cpu_reg].d[1] = i32_to_f64(src.sl[1]);
    CLOCK_CYCLES(1);
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
    MMX_REG *dst;
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    
    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    dst->sl[0] = f64_to_i32_round_to_zero(src.d[0], &status);
    dst->sl[1] = f64_to_i32_round_to_zero(src.d[1], &status);
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/
    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opCVTTPD2PI_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG src;
    MMX_REG *dst;
    MMX_ENTER();
    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    dst->sl[0] = f64_to_i32_round_to_zero(src.d[0], &status);
    dst->sl[1] = f64_to_i32_round_to_zero(src.d[1], &status);
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/
    MMX_SETEXP(cpu_reg);
    return 0;
}

static int
opCVTTSD2SI_l_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    setr32(cpu_reg, f64_to_i32_round_to_zero(src.d[0], &status));
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
opCVTTSD2SI_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    setr32(cpu_reg, f64_to_i32_round_to_zero(src.d[0], &status));
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
opCVTSD2SI_l_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    setr32(cpu_reg, f64_to_i32_normal(src.d[0], &status));
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
opCVTSD2SI_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    setr32(cpu_reg, f64_to_i32_normal(src.d[0], &status));
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
opCVTPD2PS_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    cpu_state.XMM[cpu_reg].f[0] = f64_to_f32(src.d[0], &status);
    cpu_state.XMM[cpu_reg].f[1] = f64_to_f32(src.d[1], &status);
    cpu_state.XMM[cpu_reg].l[2] = 0;
    cpu_state.XMM[cpu_reg].l[3] = 0;
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
opCVTPD2PS_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    cpu_state.XMM[cpu_reg].f[0] = f64_to_f32(src.d[0], &status);
    cpu_state.XMM[cpu_reg].f[1] = f64_to_f32(src.d[1], &status);
    cpu_state.XMM[cpu_reg].l[2] = 0;
    cpu_state.XMM[cpu_reg].l[3] = 0;
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
opCVTPS2PD_mm_xmm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPD2PS_mm_xmm_a16(fetchdat);
    
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_REG src;
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    cpu_state.XMM[cpu_reg].d[0] = f32_to_f64(src.f[0], &status);
    cpu_state.XMM[cpu_reg].d[1] = f32_to_f64(src.f[1], &status);
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
opCVTPS2PD_mm_xmm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPD2PS_mm_xmm_a32(fetchdat);
    
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_REG src;
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    cpu_state.XMM[cpu_reg].d[0] = f32_to_f64(src.f[0], &status);
    cpu_state.XMM[cpu_reg].d[1] = f32_to_f64(src.f[1], &status);
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
opCVTSS2SD_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_REG src;
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    cpu_state.XMM[cpu_reg].d[0] = f32_to_f64(src.f[0], &status);
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
opCVTSS2SD_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_REG src;
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    cpu_state.XMM[cpu_reg].d[0] = f32_to_f64(src.f[0], &status);
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
opCVTSD2SS_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_REG src;
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    cpu_state.XMM[cpu_reg].f[0] = f64_to_f32(src.d[0], &status);
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
opCVTSD2SS_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_REG src;
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    cpu_state.XMM[cpu_reg].f[0] = f64_to_f32(src.d[0], &status);
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
opCVTPS2DQ_xmm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    cpu_state.XMM[cpu_reg].sl[0] = f32_to_i32_normal(src.f[0], &status);
    cpu_state.XMM[cpu_reg].sl[1] = f32_to_i32_normal(src.f[1], &status);
    cpu_state.XMM[cpu_reg].sl[2] = f32_to_i32_normal(src.f[2], &status);
    cpu_state.XMM[cpu_reg].sl[3] = f32_to_i32_normal(src.f[3], &status);
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
opCVTPS2DQ_xmm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word(); 
    cpu_state.XMM[cpu_reg].sl[0] = f32_to_i32_normal(src.f[0], &status);
    cpu_state.XMM[cpu_reg].sl[1] = f32_to_i32_normal(src.f[1], &status);
    cpu_state.XMM[cpu_reg].sl[2] = f32_to_i32_normal(src.f[2], &status);
    cpu_state.XMM[cpu_reg].sl[3] = f32_to_i32_normal(src.f[3], &status);
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
opCVTDQ2PS_xmm_xmm_a16(uint32_t fetchdat)
{
    SSE_REG src;
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPS2DQ_xmm_xmm_a16(fetchdat);
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    cpu_state.XMM[cpu_reg].f[0] = i32_to_f32(src.sl[0], &status);
    cpu_state.XMM[cpu_reg].f[1] = i32_to_f32(src.sl[1], &status);
    cpu_state.XMM[cpu_reg].f[2] = i32_to_f32(src.sl[2], &status);
    cpu_state.XMM[cpu_reg].f[3] = i32_to_f32(src.sl[3], &status);
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
opCVTDQ2PS_xmm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG src;
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCVTPS2DQ_xmm_xmm_a32(fetchdat);
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    cpu_state.XMM[cpu_reg].f[0] = i32_to_f32(src.sl[0], &status);
    cpu_state.XMM[cpu_reg].f[1] = i32_to_f32(src.sl[1], &status);
    cpu_state.XMM[cpu_reg].f[2] = i32_to_f32(src.sl[2], &status);
    cpu_state.XMM[cpu_reg].f[3] = i32_to_f32(src.sl[3], &status);
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
opCVTTPS2DQ_xmm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    cpu_state.XMM[cpu_reg].sl[0] = f32_to_i32_round_to_zero(src.f[0], &status);
    cpu_state.XMM[cpu_reg].sl[1] = f32_to_i32_round_to_zero(src.f[1], &status);
    cpu_state.XMM[cpu_reg].sl[2] = f32_to_i32_round_to_zero(src.f[2], &status);
    cpu_state.XMM[cpu_reg].sl[3] = f32_to_i32_round_to_zero(src.f[3], &status);
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
opCVTTPS2DQ_xmm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    cpu_state.XMM[cpu_reg].sl[0] = f32_to_i32_round_to_zero(src.f[0], &status);
    cpu_state.XMM[cpu_reg].sl[1] = f32_to_i32_round_to_zero(src.f[1], &status);
    cpu_state.XMM[cpu_reg].sl[2] = f32_to_i32_round_to_zero(src.f[2], &status);
    cpu_state.XMM[cpu_reg].sl[3] = f32_to_i32_round_to_zero(src.f[3], &status);
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
opCVTPD2DQ_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    cpu_state.XMM[cpu_reg].sl[0] = f64_to_i32_normal(src.d[0], &status);
    cpu_state.XMM[cpu_reg].sl[1] = f64_to_i32_normal(src.d[1], &status);
    cpu_state.XMM[cpu_reg].q[1] = 0;
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
opCVTPD2DQ_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    cpu_state.XMM[cpu_reg].sl[0] = f64_to_i32_normal(src.d[0], &status);
    cpu_state.XMM[cpu_reg].sl[1] = f64_to_i32_normal(src.d[1], &status);
    cpu_state.XMM[cpu_reg].q[1] = 0;
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
opCVTTPD2DQ_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(!cpu_state.sse_xmm);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    cpu_state.XMM[cpu_reg].sl[0] = f64_to_i32_round_to_zero(src.d[0], &status);
    cpu_state.XMM[cpu_reg].sl[1] = f64_to_i32_round_to_zero(src.d[1], &status);
    cpu_state.XMM[cpu_reg].q[1] = 0;
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
opCVTTPD2DQ_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(!cpu_state.sse_xmm);
    SSE_GETSRC();
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    cpu_state.XMM[cpu_reg].sl[0] = f64_to_i32_round_to_zero(src.d[0], &status);
    cpu_state.XMM[cpu_reg].sl[1] = f64_to_i32_round_to_zero(src.d[1], &status);
    cpu_state.XMM[cpu_reg].q[1] = 0;
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
opCVTDQ2PD_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
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
    SSE_GETSRC();
    cpu_state.XMM[cpu_reg].d[0] = i32_to_f64(src.sl[0]);
    cpu_state.XMM[cpu_reg].d[1] = i32_to_f64(src.sl[1]);
    return 0;
}

static int
opCVTPD2PI_mm_xmm_a16(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG *dst;
    MMX_ENTER();
    fetch_ea_16(fetchdat);

    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    dst->sl[0] = f64_to_i32_normal(src.d[0], &status);
    dst->sl[1] = f64_to_i32_normal(src.d[1], &status);
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
opCVTPD2PI_mm_xmm_a32(uint32_t fetchdat)
{
    SSE_REG  src;
    MMX_REG *dst;
    MMX_ENTER();
    fetch_ea_32(fetchdat);

    dst = MMX_GETREGP(cpu_reg);
    SSE_GETSRC();
    
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    dst->sl[0] = f64_to_i32_normal(src.d[0], &status);
    dst->sl[1] = f64_to_i32_normal(src.d[1], &status);
    /*softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        //ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }*/
    return 0;
}
