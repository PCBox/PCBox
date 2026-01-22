/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opHADDPS_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.f[0] = f32_add(cpu_state.XMM[cpu_reg].f[0], cpu_state.XMM[cpu_reg].f[1], &status);
    tmp.f[1] = f32_add(cpu_state.XMM[cpu_reg].f[2], cpu_state.XMM[cpu_reg].f[3], &status);
    tmp.f[2] = f32_add(src.f[0], src.f[1], &status);
    tmp.f[3] = f32_add(src.f[2], src.f[3], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];

    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opHADDPS_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.f[0] = f32_add(cpu_state.XMM[cpu_reg].f[0], cpu_state.XMM[cpu_reg].f[1], &status);
    tmp.f[1] = f32_add(cpu_state.XMM[cpu_reg].f[2], cpu_state.XMM[cpu_reg].f[3], &status);
    tmp.f[2] = f32_add(src.f[0], src.f[1], &status);
    tmp.f[3] = f32_add(src.f[2], src.f[3], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];

    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opHADDPD_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.d[0] = f64_add(cpu_state.XMM[cpu_reg].d[0], cpu_state.XMM[cpu_reg].d[1], &status);
    tmp.d[1] = f64_add(src.d[0], src.d[1], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opHADDPD_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.d[0] = f64_add(cpu_state.XMM[cpu_reg].d[0], cpu_state.XMM[cpu_reg].d[1], &status);
    tmp.d[1] = f64_add(src.d[0], src.d[1], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opHSUBPS_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.f[0] = f32_sub(cpu_state.XMM[cpu_reg].f[0], cpu_state.XMM[cpu_reg].f[1], &status);
    tmp.f[1] = f32_sub(cpu_state.XMM[cpu_reg].f[2], cpu_state.XMM[cpu_reg].f[3], &status);
    tmp.f[2] = f32_sub(src.f[0], src.f[1], &status);
    tmp.f[3] = f32_sub(src.f[2], src.f[3], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];

    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opHSUBPS_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.f[0] = f32_sub(cpu_state.XMM[cpu_reg].f[0], cpu_state.XMM[cpu_reg].f[1], &status);
    tmp.f[1] = f32_sub(cpu_state.XMM[cpu_reg].f[2], cpu_state.XMM[cpu_reg].f[3], &status);
    tmp.f[2] = f32_sub(src.f[0], src.f[1], &status);
    tmp.f[3] = f32_sub(src.f[2], src.f[3], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];

    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opHSUBPD_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.d[0] = f64_sub(cpu_state.XMM[cpu_reg].d[0], cpu_state.XMM[cpu_reg].d[1], &status);
    tmp.d[1] = f64_sub(src.d[0], src.d[1], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opHSUBPD_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.d[0] = f64_sub(cpu_state.XMM[cpu_reg].d[0], cpu_state.XMM[cpu_reg].d[1], &status);
    tmp.d[1] = f64_sub(src.d[0], src.d[1], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opADDSUBPS_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.f[0] = f32_sub(cpu_state.XMM[cpu_reg].f[0], src.f[0], &status);
    tmp.f[1] = f32_add(cpu_state.XMM[cpu_reg].f[1], src.f[1], &status);
    tmp.f[2] = f32_sub(cpu_state.XMM[cpu_reg].f[2], src.f[2], &status);
    tmp.f[3] = f32_add(cpu_state.XMM[cpu_reg].f[3], src.f[3], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opADDSUBPS_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.f[0] = f32_sub(cpu_state.XMM[cpu_reg].f[0], src.f[0], &status);
    tmp.f[1] = f32_add(cpu_state.XMM[cpu_reg].f[1], src.f[1], &status);
    tmp.f[2] = f32_sub(cpu_state.XMM[cpu_reg].f[2], src.f[2], &status);
    tmp.f[3] = f32_add(cpu_state.XMM[cpu_reg].f[3], src.f[3], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opADDSUBPD_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.d[0] = f64_sub(cpu_state.XMM[cpu_reg].d[0], src.d[0], &status);
    tmp.d[1] = f64_add(cpu_state.XMM[cpu_reg].d[1], src.d[1], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    softfloat_status_word_to_mxcsr(status);
    return 0;
}

static int
opADDSUBPD_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    struct softfloat_status_t status = mxcsr_to_softfloat_status_word();
    tmp.d[0] = f64_sub(cpu_state.XMM[cpu_reg].d[0], src.d[0], &status);
    tmp.d[1] = f64_add(cpu_state.XMM[cpu_reg].d[1], src.d[1], &status);
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    softfloat_status_word_to_mxcsr(status);
    return 0;
}