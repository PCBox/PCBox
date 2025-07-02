/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opUCOMISD_xmm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    flags_rebuild();
    fetch_ea_16(fetchdat);
    SSE_REG src;
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    SSE_GETSRC64();
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG | Z_FLAG | P_FLAG | C_FLAG);
    int relation = f64_compare_quiet(cpu_state.XMM[cpu_reg].d[0], src.d[0], &status);
    FPU_write_eflags_fpu_compare(relation);
    flags_extract();
    softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }
    return 0;
}

static int
opUCOMISD_xmm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    flags_rebuild();
    fetch_ea_32(fetchdat);
    SSE_REG src;
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    SSE_GETSRC64();
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG | Z_FLAG | P_FLAG | C_FLAG);
    int relation = f64_compare_quiet(cpu_state.XMM[cpu_reg].d[0], src.d[0], &status);
    FPU_write_eflags_fpu_compare(relation);
    flags_extract();
    softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }
    return 0;
}

static int
opCOMISD_xmm_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    flags_rebuild();
    fetch_ea_16(fetchdat);
    SSE_REG src;
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    SSE_GETSRC64();
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG | Z_FLAG | P_FLAG | C_FLAG);
    int relation = f64_compare_normal(cpu_state.XMM[cpu_reg].d[0], src.d[0], &status);
    FPU_write_eflags_fpu_compare(relation);
    flags_extract();
    softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }
    return 0;
}

static int
opCOMISD_xmm_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    flags_rebuild();
    fetch_ea_32(fetchdat);
    SSE_REG src;
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    SSE_GETSRC64();
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG | Z_FLAG | P_FLAG | C_FLAG);
    int relation = f64_compare_normal(cpu_state.XMM[cpu_reg].d[0], src.d[0], &status);
    FPU_write_eflags_fpu_compare(relation);
    flags_extract();
    softfloat_status_word_to_mxcsr(status);
    int unmasked = (~cpu_state.mxcsr >> 7) & 0x3f;
    if ((cpu_state.mxcsr & 0x3f) & (unmasked & 0x3f)) {
        if (cr4 & CR4_OSXMMEXCPT)
            x86_int(0x13);
        ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
    }
    return 0;
}
