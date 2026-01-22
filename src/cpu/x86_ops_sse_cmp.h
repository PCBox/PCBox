/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opUCOMISS_xmm_xmm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opUCOMISD_xmm_xmm_a16(fetchdat);

    SSE_ENTER();

    flags_rebuild();
    fetch_ea_16(fetchdat);
    SSE_REG src;
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    SSE_GETSRC32();
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG | Z_FLAG | P_FLAG | C_FLAG);
    int relation = f32_compare_quiet(cpu_state.XMM[cpu_reg].f[0], src.f[0], &status);
    FPU_write_eflags_fpu_compare(relation);
    flags_extract();
    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opUCOMISS_xmm_xmm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opUCOMISD_xmm_xmm_a32(fetchdat);

    SSE_ENTER();

    flags_rebuild();
    fetch_ea_32(fetchdat);
    SSE_REG src;
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    SSE_GETSRC32();
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG | Z_FLAG | P_FLAG | C_FLAG);
    int relation = f32_compare_quiet(cpu_state.XMM[cpu_reg].f[0], src.f[0], &status);
    FPU_write_eflags_fpu_compare(relation);
    flags_extract();
    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opCOMISS_xmm_xmm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCOMISD_xmm_xmm_a16(fetchdat);

    SSE_ENTER();

    flags_rebuild();
    fetch_ea_16(fetchdat);
    SSE_REG src;
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    SSE_GETSRC32();
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG | Z_FLAG | P_FLAG | C_FLAG);
    int relation = f32_compare_normal(cpu_state.XMM[cpu_reg].f[0], src.f[0], &status);
    FPU_write_eflags_fpu_compare(relation);
    flags_extract();
    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opCOMISS_xmm_xmm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opCOMISD_xmm_xmm_a32(fetchdat);

    SSE_ENTER();

    flags_rebuild();
    fetch_ea_32(fetchdat);
    SSE_REG src;
    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    SSE_GETSRC32();
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG | Z_FLAG | P_FLAG | C_FLAG);
    int relation = f32_compare_normal(cpu_state.XMM[cpu_reg].f[0], src.f[0], &status);
    FPU_write_eflags_fpu_compare(relation);
    flags_extract();
    softfloat_status_word_to_mxcsr(status);
    return 0;
}
