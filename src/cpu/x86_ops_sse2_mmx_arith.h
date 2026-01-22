/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opPADDQ_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] += src.q[0];
    cpu_state.XMM[cpu_reg].q[1] += src.q[1];

    return 0;
}

static int
opPADDQ_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] += src.q[0];
    cpu_state.XMM[cpu_reg].q[1] += src.q[1];

    return 0;
}

static int
opPADDQ_a16(uint32_t fetchdat)
{
    if (cpu_state.sse_xmm)
        return opPADDQ_xmm_a16(fetchdat);
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    MMX_GETSRC();
    dst = MMX_GETREGP(cpu_reg);

    dst->q += src.q;
    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPADDQ_a32(uint32_t fetchdat)
{
    if (cpu_state.sse_xmm)
        return opPADDQ_xmm_a32(fetchdat);
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    MMX_GETSRC();
    dst = MMX_GETREGP(cpu_reg);

    dst->q += src.q;
    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPMULLW_xmm_a16(uint32_t fetchdat)
{
    SSE_REG src;
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    cpu_state.XMM[cpu_reg].sw[0] *= src.sw[0];
    cpu_state.XMM[cpu_reg].sw[1] *= src.sw[1];
    cpu_state.XMM[cpu_reg].sw[2] *= src.sw[2];
    cpu_state.XMM[cpu_reg].sw[3] *= src.sw[3];
    cpu_state.XMM[cpu_reg].sw[4] *= src.sw[4];
    cpu_state.XMM[cpu_reg].sw[5] *= src.sw[5];
    cpu_state.XMM[cpu_reg].sw[6] *= src.sw[6];
    cpu_state.XMM[cpu_reg].sw[7] *= src.sw[7];
    return 0;
}

static int
opPMULLW_xmm_a32(uint32_t fetchdat)
{
    SSE_REG src;
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    cpu_state.XMM[cpu_reg].sw[0] *= src.sw[0];
    cpu_state.XMM[cpu_reg].sw[1] *= src.sw[1];
    cpu_state.XMM[cpu_reg].sw[2] *= src.sw[2];
    cpu_state.XMM[cpu_reg].sw[3] *= src.sw[3];
    cpu_state.XMM[cpu_reg].sw[4] *= src.sw[4];
    cpu_state.XMM[cpu_reg].sw[5] *= src.sw[5];
    cpu_state.XMM[cpu_reg].sw[6] *= src.sw[6];
    cpu_state.XMM[cpu_reg].sw[7] *= src.sw[7];
    return 0;
}

static int
opPSUBUSB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0]  = USATB(cpu_state.XMM[cpu_reg].b[0] - src.b[0]);
    cpu_state.XMM[cpu_reg].b[1]  = USATB(cpu_state.XMM[cpu_reg].b[1] - src.b[1]);
    cpu_state.XMM[cpu_reg].b[2]  = USATB(cpu_state.XMM[cpu_reg].b[2] - src.b[2]);
    cpu_state.XMM[cpu_reg].b[3]  = USATB(cpu_state.XMM[cpu_reg].b[3] - src.b[3]);
    cpu_state.XMM[cpu_reg].b[4]  = USATB(cpu_state.XMM[cpu_reg].b[4] - src.b[4]);
    cpu_state.XMM[cpu_reg].b[5]  = USATB(cpu_state.XMM[cpu_reg].b[5] - src.b[5]);
    cpu_state.XMM[cpu_reg].b[6]  = USATB(cpu_state.XMM[cpu_reg].b[6] - src.b[6]);
    cpu_state.XMM[cpu_reg].b[7]  = USATB(cpu_state.XMM[cpu_reg].b[7] - src.b[7]);
    cpu_state.XMM[cpu_reg].b[8]  = USATB(cpu_state.XMM[cpu_reg].b[8] - src.b[8]);
    cpu_state.XMM[cpu_reg].b[9]  = USATB(cpu_state.XMM[cpu_reg].b[9] - src.b[9]);
    cpu_state.XMM[cpu_reg].b[10] = USATB(cpu_state.XMM[cpu_reg].b[10] - src.b[10]);
    cpu_state.XMM[cpu_reg].b[11] = USATB(cpu_state.XMM[cpu_reg].b[11] - src.b[11]);
    cpu_state.XMM[cpu_reg].b[12] = USATB(cpu_state.XMM[cpu_reg].b[12] - src.b[12]);
    cpu_state.XMM[cpu_reg].b[13] = USATB(cpu_state.XMM[cpu_reg].b[13] - src.b[13]);
    cpu_state.XMM[cpu_reg].b[14] = USATB(cpu_state.XMM[cpu_reg].b[14] - src.b[14]);
    cpu_state.XMM[cpu_reg].b[15] = USATB(cpu_state.XMM[cpu_reg].b[15] - src.b[15]);

    return 0;
}

static int
opPSUBUSB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0]  = USATB(cpu_state.XMM[cpu_reg].b[0] - src.b[0]);
    cpu_state.XMM[cpu_reg].b[1]  = USATB(cpu_state.XMM[cpu_reg].b[1] - src.b[1]);
    cpu_state.XMM[cpu_reg].b[2]  = USATB(cpu_state.XMM[cpu_reg].b[2] - src.b[2]);
    cpu_state.XMM[cpu_reg].b[3]  = USATB(cpu_state.XMM[cpu_reg].b[3] - src.b[3]);
    cpu_state.XMM[cpu_reg].b[4]  = USATB(cpu_state.XMM[cpu_reg].b[4] - src.b[4]);
    cpu_state.XMM[cpu_reg].b[5]  = USATB(cpu_state.XMM[cpu_reg].b[5] - src.b[5]);
    cpu_state.XMM[cpu_reg].b[6]  = USATB(cpu_state.XMM[cpu_reg].b[6] - src.b[6]);
    cpu_state.XMM[cpu_reg].b[7]  = USATB(cpu_state.XMM[cpu_reg].b[7] - src.b[7]);
    cpu_state.XMM[cpu_reg].b[8]  = USATB(cpu_state.XMM[cpu_reg].b[8] - src.b[8]);
    cpu_state.XMM[cpu_reg].b[9]  = USATB(cpu_state.XMM[cpu_reg].b[9] - src.b[9]);
    cpu_state.XMM[cpu_reg].b[10] = USATB(cpu_state.XMM[cpu_reg].b[10] - src.b[10]);
    cpu_state.XMM[cpu_reg].b[11] = USATB(cpu_state.XMM[cpu_reg].b[11] - src.b[11]);
    cpu_state.XMM[cpu_reg].b[12] = USATB(cpu_state.XMM[cpu_reg].b[12] - src.b[12]);
    cpu_state.XMM[cpu_reg].b[13] = USATB(cpu_state.XMM[cpu_reg].b[13] - src.b[13]);
    cpu_state.XMM[cpu_reg].b[14] = USATB(cpu_state.XMM[cpu_reg].b[14] - src.b[14]);
    cpu_state.XMM[cpu_reg].b[15] = USATB(cpu_state.XMM[cpu_reg].b[15] - src.b[15]);

    return 0;
}

static int
opPSUBUSW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] = USATW(cpu_state.XMM[cpu_reg].w[0] - src.w[0]);
    cpu_state.XMM[cpu_reg].w[1] = USATW(cpu_state.XMM[cpu_reg].w[1] - src.w[1]);
    cpu_state.XMM[cpu_reg].w[2] = USATW(cpu_state.XMM[cpu_reg].w[2] - src.w[2]);
    cpu_state.XMM[cpu_reg].w[3] = USATW(cpu_state.XMM[cpu_reg].w[3] - src.w[3]);
    cpu_state.XMM[cpu_reg].w[4] = USATW(cpu_state.XMM[cpu_reg].w[4] - src.w[4]);
    cpu_state.XMM[cpu_reg].w[5] = USATW(cpu_state.XMM[cpu_reg].w[5] - src.w[5]);
    cpu_state.XMM[cpu_reg].w[6] = USATW(cpu_state.XMM[cpu_reg].w[6] - src.w[6]);
    cpu_state.XMM[cpu_reg].w[7] = USATW(cpu_state.XMM[cpu_reg].w[7] - src.w[7]);

    return 0;
}

static int
opPSUBUSW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] = USATW(cpu_state.XMM[cpu_reg].w[0] - src.w[0]);
    cpu_state.XMM[cpu_reg].w[1] = USATW(cpu_state.XMM[cpu_reg].w[1] - src.w[1]);
    cpu_state.XMM[cpu_reg].w[2] = USATW(cpu_state.XMM[cpu_reg].w[2] - src.w[2]);
    cpu_state.XMM[cpu_reg].w[3] = USATW(cpu_state.XMM[cpu_reg].w[3] - src.w[3]);
    cpu_state.XMM[cpu_reg].w[4] = USATW(cpu_state.XMM[cpu_reg].w[4] - src.w[4]);
    cpu_state.XMM[cpu_reg].w[5] = USATW(cpu_state.XMM[cpu_reg].w[5] - src.w[5]);
    cpu_state.XMM[cpu_reg].w[6] = USATW(cpu_state.XMM[cpu_reg].w[6] - src.w[6]);
    cpu_state.XMM[cpu_reg].w[7] = USATW(cpu_state.XMM[cpu_reg].w[7] - src.w[7]);

    return 0;
}

static int
opPADDUSB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0]  = USATB(cpu_state.XMM[cpu_reg].b[0] + src.b[0]);
    cpu_state.XMM[cpu_reg].b[1]  = USATB(cpu_state.XMM[cpu_reg].b[1] + src.b[1]);
    cpu_state.XMM[cpu_reg].b[2]  = USATB(cpu_state.XMM[cpu_reg].b[2] + src.b[2]);
    cpu_state.XMM[cpu_reg].b[3]  = USATB(cpu_state.XMM[cpu_reg].b[3] + src.b[3]);
    cpu_state.XMM[cpu_reg].b[4]  = USATB(cpu_state.XMM[cpu_reg].b[4] + src.b[4]);
    cpu_state.XMM[cpu_reg].b[5]  = USATB(cpu_state.XMM[cpu_reg].b[5] + src.b[5]);
    cpu_state.XMM[cpu_reg].b[6]  = USATB(cpu_state.XMM[cpu_reg].b[6] + src.b[6]);
    cpu_state.XMM[cpu_reg].b[7]  = USATB(cpu_state.XMM[cpu_reg].b[7] + src.b[7]);
    cpu_state.XMM[cpu_reg].b[8]  = USATB(cpu_state.XMM[cpu_reg].b[8] + src.b[8]);
    cpu_state.XMM[cpu_reg].b[9]  = USATB(cpu_state.XMM[cpu_reg].b[9] + src.b[9]);
    cpu_state.XMM[cpu_reg].b[10] = USATB(cpu_state.XMM[cpu_reg].b[10] + src.b[10]);
    cpu_state.XMM[cpu_reg].b[11] = USATB(cpu_state.XMM[cpu_reg].b[11] + src.b[11]);
    cpu_state.XMM[cpu_reg].b[12] = USATB(cpu_state.XMM[cpu_reg].b[12] + src.b[12]);
    cpu_state.XMM[cpu_reg].b[13] = USATB(cpu_state.XMM[cpu_reg].b[13] + src.b[13]);
    cpu_state.XMM[cpu_reg].b[14] = USATB(cpu_state.XMM[cpu_reg].b[14] + src.b[14]);
    cpu_state.XMM[cpu_reg].b[15] = USATB(cpu_state.XMM[cpu_reg].b[15] + src.b[15]);

    return 0;
}

static int
opPADDUSB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0]  = USATB(cpu_state.XMM[cpu_reg].b[0] + src.b[0]);
    cpu_state.XMM[cpu_reg].b[1]  = USATB(cpu_state.XMM[cpu_reg].b[1] + src.b[1]);
    cpu_state.XMM[cpu_reg].b[2]  = USATB(cpu_state.XMM[cpu_reg].b[2] + src.b[2]);
    cpu_state.XMM[cpu_reg].b[3]  = USATB(cpu_state.XMM[cpu_reg].b[3] + src.b[3]);
    cpu_state.XMM[cpu_reg].b[4]  = USATB(cpu_state.XMM[cpu_reg].b[4] + src.b[4]);
    cpu_state.XMM[cpu_reg].b[5]  = USATB(cpu_state.XMM[cpu_reg].b[5] + src.b[5]);
    cpu_state.XMM[cpu_reg].b[6]  = USATB(cpu_state.XMM[cpu_reg].b[6] + src.b[6]);
    cpu_state.XMM[cpu_reg].b[7]  = USATB(cpu_state.XMM[cpu_reg].b[7] + src.b[7]);
    cpu_state.XMM[cpu_reg].b[8]  = USATB(cpu_state.XMM[cpu_reg].b[8] + src.b[8]);
    cpu_state.XMM[cpu_reg].b[9]  = USATB(cpu_state.XMM[cpu_reg].b[9] + src.b[9]);
    cpu_state.XMM[cpu_reg].b[10] = USATB(cpu_state.XMM[cpu_reg].b[10] + src.b[10]);
    cpu_state.XMM[cpu_reg].b[11] = USATB(cpu_state.XMM[cpu_reg].b[11] + src.b[11]);
    cpu_state.XMM[cpu_reg].b[12] = USATB(cpu_state.XMM[cpu_reg].b[12] + src.b[12]);
    cpu_state.XMM[cpu_reg].b[13] = USATB(cpu_state.XMM[cpu_reg].b[13] + src.b[13]);
    cpu_state.XMM[cpu_reg].b[14] = USATB(cpu_state.XMM[cpu_reg].b[14] + src.b[14]);
    cpu_state.XMM[cpu_reg].b[15] = USATB(cpu_state.XMM[cpu_reg].b[15] + src.b[15]);
    return 0;
}

static int
opPADDUSW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] = USATW(cpu_state.XMM[cpu_reg].w[0] + src.w[0]);
    cpu_state.XMM[cpu_reg].w[1] = USATW(cpu_state.XMM[cpu_reg].w[1] + src.w[1]);
    cpu_state.XMM[cpu_reg].w[2] = USATW(cpu_state.XMM[cpu_reg].w[2] + src.w[2]);
    cpu_state.XMM[cpu_reg].w[3] = USATW(cpu_state.XMM[cpu_reg].w[3] + src.w[3]);
    cpu_state.XMM[cpu_reg].w[4] = USATW(cpu_state.XMM[cpu_reg].w[4] + src.w[4]);
    cpu_state.XMM[cpu_reg].w[5] = USATW(cpu_state.XMM[cpu_reg].w[5] + src.w[5]);
    cpu_state.XMM[cpu_reg].w[6] = USATW(cpu_state.XMM[cpu_reg].w[6] + src.w[6]);
    cpu_state.XMM[cpu_reg].w[7] = USATW(cpu_state.XMM[cpu_reg].w[7] + src.w[7]);

    return 0;
}

static int
opPADDUSW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] = USATW(cpu_state.XMM[cpu_reg].w[0] + src.w[0]);
    cpu_state.XMM[cpu_reg].w[1] = USATW(cpu_state.XMM[cpu_reg].w[1] + src.w[1]);
    cpu_state.XMM[cpu_reg].w[2] = USATW(cpu_state.XMM[cpu_reg].w[2] + src.w[2]);
    cpu_state.XMM[cpu_reg].w[3] = USATW(cpu_state.XMM[cpu_reg].w[3] + src.w[3]);
    cpu_state.XMM[cpu_reg].w[4] = USATW(cpu_state.XMM[cpu_reg].w[4] + src.w[4]);
    cpu_state.XMM[cpu_reg].w[5] = USATW(cpu_state.XMM[cpu_reg].w[5] + src.w[5]);
    cpu_state.XMM[cpu_reg].w[6] = USATW(cpu_state.XMM[cpu_reg].w[6] + src.w[6]);
    cpu_state.XMM[cpu_reg].w[7] = USATW(cpu_state.XMM[cpu_reg].w[7] + src.w[7]);

    return 0;
}

static int
opPMULHW_xmm_a16(uint32_t fetchdat)
{
    SSE_REG src;
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    cpu_state.XMM[cpu_reg].sw[0] = ((int32_t) cpu_state.XMM[cpu_reg].sw[0] * (int32_t) src.sw[0]) >> 16;
    cpu_state.XMM[cpu_reg].sw[1] = ((int32_t) cpu_state.XMM[cpu_reg].sw[1] * (int32_t) src.sw[1]) >> 16;
    cpu_state.XMM[cpu_reg].sw[2] = ((int32_t) cpu_state.XMM[cpu_reg].sw[2] * (int32_t) src.sw[2]) >> 16;
    cpu_state.XMM[cpu_reg].sw[3] = ((int32_t) cpu_state.XMM[cpu_reg].sw[3] * (int32_t) src.sw[3]) >> 16;
    cpu_state.XMM[cpu_reg].sw[4] = ((int32_t) cpu_state.XMM[cpu_reg].sw[4] * (int32_t) src.sw[4]) >> 16;
    cpu_state.XMM[cpu_reg].sw[5] = ((int32_t) cpu_state.XMM[cpu_reg].sw[5] * (int32_t) src.sw[5]) >> 16;
    cpu_state.XMM[cpu_reg].sw[6] = ((int32_t) cpu_state.XMM[cpu_reg].sw[6] * (int32_t) src.sw[6]) >> 16;
    cpu_state.XMM[cpu_reg].sw[7] = ((int32_t) cpu_state.XMM[cpu_reg].sw[7] * (int32_t) src.sw[7]) >> 16;
    return 0;
}

static int
opPMULHW_xmm_a32(uint32_t fetchdat)
{
    SSE_REG src;
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    cpu_state.XMM[cpu_reg].sw[0] = ((int32_t) cpu_state.XMM[cpu_reg].sw[0] * (int32_t) src.sw[0]) >> 16;
    cpu_state.XMM[cpu_reg].sw[1] = ((int32_t) cpu_state.XMM[cpu_reg].sw[1] * (int32_t) src.sw[1]) >> 16;
    cpu_state.XMM[cpu_reg].sw[2] = ((int32_t) cpu_state.XMM[cpu_reg].sw[2] * (int32_t) src.sw[2]) >> 16;
    cpu_state.XMM[cpu_reg].sw[3] = ((int32_t) cpu_state.XMM[cpu_reg].sw[3] * (int32_t) src.sw[3]) >> 16;
    cpu_state.XMM[cpu_reg].sw[4] = ((int32_t) cpu_state.XMM[cpu_reg].sw[4] * (int32_t) src.sw[4]) >> 16;
    cpu_state.XMM[cpu_reg].sw[5] = ((int32_t) cpu_state.XMM[cpu_reg].sw[5] * (int32_t) src.sw[5]) >> 16;
    cpu_state.XMM[cpu_reg].sw[6] = ((int32_t) cpu_state.XMM[cpu_reg].sw[6] * (int32_t) src.sw[6]) >> 16;
    cpu_state.XMM[cpu_reg].sw[7] = ((int32_t) cpu_state.XMM[cpu_reg].sw[7] * (int32_t) src.sw[7]) >> 16;
    return 0;
}

static int
opPSUBSB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].sb[0]  = SSATB(cpu_state.XMM[cpu_reg].sb[0] - src.sb[0]);
    cpu_state.XMM[cpu_reg].sb[1]  = SSATB(cpu_state.XMM[cpu_reg].sb[1] - src.sb[1]);
    cpu_state.XMM[cpu_reg].sb[2]  = SSATB(cpu_state.XMM[cpu_reg].sb[2] - src.sb[2]);
    cpu_state.XMM[cpu_reg].sb[3]  = SSATB(cpu_state.XMM[cpu_reg].sb[3] - src.sb[3]);
    cpu_state.XMM[cpu_reg].sb[4]  = SSATB(cpu_state.XMM[cpu_reg].sb[4] - src.sb[4]);
    cpu_state.XMM[cpu_reg].sb[5]  = SSATB(cpu_state.XMM[cpu_reg].sb[5] - src.sb[5]);
    cpu_state.XMM[cpu_reg].sb[6]  = SSATB(cpu_state.XMM[cpu_reg].sb[6] - src.sb[6]);
    cpu_state.XMM[cpu_reg].sb[7]  = SSATB(cpu_state.XMM[cpu_reg].sb[7] - src.sb[7]);
    cpu_state.XMM[cpu_reg].sb[8]  = SSATB(cpu_state.XMM[cpu_reg].sb[8] - src.sb[8]);
    cpu_state.XMM[cpu_reg].sb[9]  = SSATB(cpu_state.XMM[cpu_reg].sb[9] - src.sb[9]);
    cpu_state.XMM[cpu_reg].sb[10] = SSATB(cpu_state.XMM[cpu_reg].sb[10] - src.sb[10]);
    cpu_state.XMM[cpu_reg].sb[11] = SSATB(cpu_state.XMM[cpu_reg].sb[11] - src.sb[11]);
    cpu_state.XMM[cpu_reg].sb[12] = SSATB(cpu_state.XMM[cpu_reg].sb[12] - src.sb[12]);
    cpu_state.XMM[cpu_reg].sb[13] = SSATB(cpu_state.XMM[cpu_reg].sb[13] - src.sb[13]);
    cpu_state.XMM[cpu_reg].sb[14] = SSATB(cpu_state.XMM[cpu_reg].sb[14] - src.sb[14]);
    cpu_state.XMM[cpu_reg].sb[15] = SSATB(cpu_state.XMM[cpu_reg].sb[15] - src.sb[15]);

    return 0;
}

static int
opPSUBSB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].sb[0]  = SSATB(cpu_state.XMM[cpu_reg].sb[0] - src.sb[0]);
    cpu_state.XMM[cpu_reg].sb[1]  = SSATB(cpu_state.XMM[cpu_reg].sb[1] - src.sb[1]);
    cpu_state.XMM[cpu_reg].sb[2]  = SSATB(cpu_state.XMM[cpu_reg].sb[2] - src.sb[2]);
    cpu_state.XMM[cpu_reg].sb[3]  = SSATB(cpu_state.XMM[cpu_reg].sb[3] - src.sb[3]);
    cpu_state.XMM[cpu_reg].sb[4]  = SSATB(cpu_state.XMM[cpu_reg].sb[4] - src.sb[4]);
    cpu_state.XMM[cpu_reg].sb[5]  = SSATB(cpu_state.XMM[cpu_reg].sb[5] - src.sb[5]);
    cpu_state.XMM[cpu_reg].sb[6]  = SSATB(cpu_state.XMM[cpu_reg].sb[6] - src.sb[6]);
    cpu_state.XMM[cpu_reg].sb[7]  = SSATB(cpu_state.XMM[cpu_reg].sb[7] - src.sb[7]);
    cpu_state.XMM[cpu_reg].sb[8]  = SSATB(cpu_state.XMM[cpu_reg].sb[8] - src.sb[8]);
    cpu_state.XMM[cpu_reg].sb[9]  = SSATB(cpu_state.XMM[cpu_reg].sb[9] - src.sb[9]);
    cpu_state.XMM[cpu_reg].sb[10] = SSATB(cpu_state.XMM[cpu_reg].sb[10] - src.sb[10]);
    cpu_state.XMM[cpu_reg].sb[11] = SSATB(cpu_state.XMM[cpu_reg].sb[11] - src.sb[11]);
    cpu_state.XMM[cpu_reg].sb[12] = SSATB(cpu_state.XMM[cpu_reg].sb[12] - src.sb[12]);
    cpu_state.XMM[cpu_reg].sb[13] = SSATB(cpu_state.XMM[cpu_reg].sb[13] - src.sb[13]);
    cpu_state.XMM[cpu_reg].sb[14] = SSATB(cpu_state.XMM[cpu_reg].sb[14] - src.sb[14]);
    cpu_state.XMM[cpu_reg].sb[15] = SSATB(cpu_state.XMM[cpu_reg].sb[15] - src.sb[15]);

    return 0;
}

static int
opPSUBSW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].sw[0] = SSATW(cpu_state.XMM[cpu_reg].sw[0] - src.sw[0]);
    cpu_state.XMM[cpu_reg].sw[1] = SSATW(cpu_state.XMM[cpu_reg].sw[1] - src.sw[1]);
    cpu_state.XMM[cpu_reg].sw[2] = SSATW(cpu_state.XMM[cpu_reg].sw[2] - src.sw[2]);
    cpu_state.XMM[cpu_reg].sw[3] = SSATW(cpu_state.XMM[cpu_reg].sw[3] - src.sw[3]);
    cpu_state.XMM[cpu_reg].sw[4] = SSATW(cpu_state.XMM[cpu_reg].sw[4] - src.sw[4]);
    cpu_state.XMM[cpu_reg].sw[5] = SSATW(cpu_state.XMM[cpu_reg].sw[5] - src.sw[5]);
    cpu_state.XMM[cpu_reg].sw[6] = SSATW(cpu_state.XMM[cpu_reg].sw[6] - src.sw[6]);
    cpu_state.XMM[cpu_reg].sw[7] = SSATW(cpu_state.XMM[cpu_reg].sw[7] - src.sw[7]);

    return 0;
}

static int
opPSUBSW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].sw[0] = SSATW(cpu_state.XMM[cpu_reg].sw[0] - src.sw[0]);
    cpu_state.XMM[cpu_reg].sw[1] = SSATW(cpu_state.XMM[cpu_reg].sw[1] - src.sw[1]);
    cpu_state.XMM[cpu_reg].sw[2] = SSATW(cpu_state.XMM[cpu_reg].sw[2] - src.sw[2]);
    cpu_state.XMM[cpu_reg].sw[3] = SSATW(cpu_state.XMM[cpu_reg].sw[3] - src.sw[3]);
    cpu_state.XMM[cpu_reg].sw[4] = SSATW(cpu_state.XMM[cpu_reg].sw[4] - src.sw[4]);
    cpu_state.XMM[cpu_reg].sw[5] = SSATW(cpu_state.XMM[cpu_reg].sw[5] - src.sw[5]);
    cpu_state.XMM[cpu_reg].sw[6] = SSATW(cpu_state.XMM[cpu_reg].sw[6] - src.sw[6]);
    cpu_state.XMM[cpu_reg].sw[7] = SSATW(cpu_state.XMM[cpu_reg].sw[7] - src.sw[7]);

    return 0;
}

static int
opPADDSB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].sb[0]  = SSATB(cpu_state.XMM[cpu_reg].sb[0] + src.sb[0]);
    cpu_state.XMM[cpu_reg].sb[1]  = SSATB(cpu_state.XMM[cpu_reg].sb[1] + src.sb[1]);
    cpu_state.XMM[cpu_reg].sb[2]  = SSATB(cpu_state.XMM[cpu_reg].sb[2] + src.sb[2]);
    cpu_state.XMM[cpu_reg].sb[3]  = SSATB(cpu_state.XMM[cpu_reg].sb[3] + src.sb[3]);
    cpu_state.XMM[cpu_reg].sb[4]  = SSATB(cpu_state.XMM[cpu_reg].sb[4] + src.sb[4]);
    cpu_state.XMM[cpu_reg].sb[5]  = SSATB(cpu_state.XMM[cpu_reg].sb[5] + src.sb[5]);
    cpu_state.XMM[cpu_reg].sb[6]  = SSATB(cpu_state.XMM[cpu_reg].sb[6] + src.sb[6]);
    cpu_state.XMM[cpu_reg].sb[7]  = SSATB(cpu_state.XMM[cpu_reg].sb[7] + src.sb[7]);
    cpu_state.XMM[cpu_reg].sb[8]  = SSATB(cpu_state.XMM[cpu_reg].sb[8] + src.sb[8]);
    cpu_state.XMM[cpu_reg].sb[9]  = SSATB(cpu_state.XMM[cpu_reg].sb[9] + src.sb[9]);
    cpu_state.XMM[cpu_reg].sb[10] = SSATB(cpu_state.XMM[cpu_reg].sb[10] + src.sb[10]);
    cpu_state.XMM[cpu_reg].sb[11] = SSATB(cpu_state.XMM[cpu_reg].sb[11] + src.sb[11]);
    cpu_state.XMM[cpu_reg].sb[12] = SSATB(cpu_state.XMM[cpu_reg].sb[12] + src.sb[12]);
    cpu_state.XMM[cpu_reg].sb[13] = SSATB(cpu_state.XMM[cpu_reg].sb[13] + src.sb[13]);
    cpu_state.XMM[cpu_reg].sb[14] = SSATB(cpu_state.XMM[cpu_reg].sb[14] + src.sb[14]);
    cpu_state.XMM[cpu_reg].sb[15] = SSATB(cpu_state.XMM[cpu_reg].sb[15] + src.sb[15]);

    return 0;
}

static int
opPADDSB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].sb[0]  = SSATB(cpu_state.XMM[cpu_reg].sb[0] + src.sb[0]);
    cpu_state.XMM[cpu_reg].sb[1]  = SSATB(cpu_state.XMM[cpu_reg].sb[1] + src.sb[1]);
    cpu_state.XMM[cpu_reg].sb[2]  = SSATB(cpu_state.XMM[cpu_reg].sb[2] + src.sb[2]);
    cpu_state.XMM[cpu_reg].sb[3]  = SSATB(cpu_state.XMM[cpu_reg].sb[3] + src.sb[3]);
    cpu_state.XMM[cpu_reg].sb[4]  = SSATB(cpu_state.XMM[cpu_reg].sb[4] + src.sb[4]);
    cpu_state.XMM[cpu_reg].sb[5]  = SSATB(cpu_state.XMM[cpu_reg].sb[5] + src.sb[5]);
    cpu_state.XMM[cpu_reg].sb[6]  = SSATB(cpu_state.XMM[cpu_reg].sb[6] + src.sb[6]);
    cpu_state.XMM[cpu_reg].sb[7]  = SSATB(cpu_state.XMM[cpu_reg].sb[7] + src.sb[7]);
    cpu_state.XMM[cpu_reg].sb[8]  = SSATB(cpu_state.XMM[cpu_reg].sb[8] + src.sb[8]);
    cpu_state.XMM[cpu_reg].sb[9]  = SSATB(cpu_state.XMM[cpu_reg].sb[9] + src.sb[9]);
    cpu_state.XMM[cpu_reg].sb[10] = SSATB(cpu_state.XMM[cpu_reg].sb[10] + src.sb[10]);
    cpu_state.XMM[cpu_reg].sb[11] = SSATB(cpu_state.XMM[cpu_reg].sb[11] + src.sb[11]);
    cpu_state.XMM[cpu_reg].sb[12] = SSATB(cpu_state.XMM[cpu_reg].sb[12] + src.sb[12]);
    cpu_state.XMM[cpu_reg].sb[13] = SSATB(cpu_state.XMM[cpu_reg].sb[13] + src.sb[13]);
    cpu_state.XMM[cpu_reg].sb[14] = SSATB(cpu_state.XMM[cpu_reg].sb[14] + src.sb[14]);
    cpu_state.XMM[cpu_reg].sb[15] = SSATB(cpu_state.XMM[cpu_reg].sb[15] + src.sb[15]);

    return 0;
}

static int
opPADDSW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].sw[0] = SSATW(cpu_state.XMM[cpu_reg].sw[0] + src.sw[0]);
    cpu_state.XMM[cpu_reg].sw[1] = SSATW(cpu_state.XMM[cpu_reg].sw[1] + src.sw[1]);
    cpu_state.XMM[cpu_reg].sw[2] = SSATW(cpu_state.XMM[cpu_reg].sw[2] + src.sw[2]);
    cpu_state.XMM[cpu_reg].sw[3] = SSATW(cpu_state.XMM[cpu_reg].sw[3] + src.sw[3]);
    cpu_state.XMM[cpu_reg].sw[4] = SSATW(cpu_state.XMM[cpu_reg].sw[4] + src.sw[4]);
    cpu_state.XMM[cpu_reg].sw[5] = SSATW(cpu_state.XMM[cpu_reg].sw[5] + src.sw[5]);
    cpu_state.XMM[cpu_reg].sw[6] = SSATW(cpu_state.XMM[cpu_reg].sw[6] + src.sw[6]);
    cpu_state.XMM[cpu_reg].sw[7] = SSATW(cpu_state.XMM[cpu_reg].sw[7] + src.sw[7]);

    return 0;
}

static int
opPADDSW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].sw[0] = SSATW(cpu_state.XMM[cpu_reg].sw[0] + src.sw[0]);
    cpu_state.XMM[cpu_reg].sw[1] = SSATW(cpu_state.XMM[cpu_reg].sw[1] + src.sw[1]);
    cpu_state.XMM[cpu_reg].sw[2] = SSATW(cpu_state.XMM[cpu_reg].sw[2] + src.sw[2]);
    cpu_state.XMM[cpu_reg].sw[3] = SSATW(cpu_state.XMM[cpu_reg].sw[3] + src.sw[3]);
    cpu_state.XMM[cpu_reg].sw[4] = SSATW(cpu_state.XMM[cpu_reg].sw[4] + src.sw[4]);
    cpu_state.XMM[cpu_reg].sw[5] = SSATW(cpu_state.XMM[cpu_reg].sw[5] + src.sw[5]);
    cpu_state.XMM[cpu_reg].sw[6] = SSATW(cpu_state.XMM[cpu_reg].sw[6] + src.sw[6]);
    cpu_state.XMM[cpu_reg].sw[7] = SSATW(cpu_state.XMM[cpu_reg].sw[7] + src.sw[7]);

    return 0;
}

static int
opPMULUDQ_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    tmp.q[0] = (uint64_t) cpu_state.XMM[cpu_reg].l[0] * (uint64_t) src.l[0];
    tmp.q[1] = (uint64_t) cpu_state.XMM[cpu_reg].l[2] * (uint64_t) src.l[2];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];

    return 0;
}

static int
opPMULUDQ_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    tmp.q[0] = (uint64_t) cpu_state.XMM[cpu_reg].l[0] * (uint64_t) src.l[0];
    tmp.q[1] = (uint64_t) cpu_state.XMM[cpu_reg].l[2] * (uint64_t) src.l[2];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];

    return 0;
}

static int
opPMULUDQ_mm_a16(uint32_t fetchdat)
{
    if (cpu_state.sse_xmm)
        return opPMULUDQ_xmm_a16(fetchdat);
    MMX_REG src, tmp;
    MMX_REG* dst;

    MMX_ENTER();

    fetch_ea_16(fetchdat);
    MMX_GETSRC();
    dst = MMX_GETREGP(cpu_reg);

    tmp.q = (uint64_t) dst->l[0] * (uint64_t) src.l[0];

    dst->q = tmp.q;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPMULUDQ_mm_a32(uint32_t fetchdat)
{
    if (cpu_state.sse_xmm)
        return opPMULUDQ_xmm_a32(fetchdat);
    MMX_REG src, tmp;
    MMX_REG* dst;

    MMX_ENTER();

    fetch_ea_32(fetchdat);
    MMX_GETSRC();
    dst = MMX_GETREGP(cpu_reg);

    tmp.q = (uint64_t) dst->l[0] * (uint64_t) src.l[0];

    dst->q = tmp.q;

    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPMADDWD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    if (cpu_state.XMM[cpu_reg].l[0] == 0x80008000 && src.l[0] == 0x80008000)
        cpu_state.XMM[cpu_reg].l[0] = 0x80000000;
    else
        cpu_state.XMM[cpu_reg].sl[0] = ((int32_t) cpu_state.XMM[cpu_reg].sw[0] * (int32_t) src.sw[0]) + ((int32_t) cpu_state.XMM[cpu_reg].sw[1] * (int32_t) src.sw[1]);

    if (cpu_state.XMM[cpu_reg].l[1] == 0x80008000 && src.l[1] == 0x80008000)
        cpu_state.XMM[cpu_reg].l[1] = 0x80000000;
    else
        cpu_state.XMM[cpu_reg].sl[1] = ((int32_t) cpu_state.XMM[cpu_reg].sw[2] * (int32_t) src.sw[2]) + ((int32_t) cpu_state.XMM[cpu_reg].sw[3] * (int32_t) src.sw[3]);

    if (cpu_state.XMM[cpu_reg].l[2] == 0x80008000 && src.l[2] == 0x80008000)
        cpu_state.XMM[cpu_reg].l[2] = 0x80000000;
    else
        cpu_state.XMM[cpu_reg].sl[2] = ((int32_t) cpu_state.XMM[cpu_reg].sw[4] * (int32_t) src.sw[4]) + ((int32_t) cpu_state.XMM[cpu_reg].sw[5] * (int32_t) src.sw[5]);

    if (cpu_state.XMM[cpu_reg].l[3] == 0x80008000 && src.l[3] == 0x80008000)
        cpu_state.XMM[cpu_reg].l[3] = 0x80000000;
    else
        cpu_state.XMM[cpu_reg].sl[3] = ((int32_t) cpu_state.XMM[cpu_reg].sw[6] * (int32_t) src.sw[6]) + ((int32_t) cpu_state.XMM[cpu_reg].sw[7] * (int32_t) src.sw[7]);

    return 0;
}

static int
opPMADDWD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    if (cpu_state.XMM[cpu_reg].l[0] == 0x80008000 && src.l[0] == 0x80008000)
        cpu_state.XMM[cpu_reg].l[0] = 0x80000000;
    else
        cpu_state.XMM[cpu_reg].sl[0] = ((int32_t) cpu_state.XMM[cpu_reg].sw[0] * (int32_t) src.sw[0]) + ((int32_t) cpu_state.XMM[cpu_reg].sw[1] * (int32_t) src.sw[1]);

    if (cpu_state.XMM[cpu_reg].l[1] == 0x80008000 && src.l[1] == 0x80008000)
        cpu_state.XMM[cpu_reg].l[1] = 0x80000000;
    else
        cpu_state.XMM[cpu_reg].sl[1] = ((int32_t) cpu_state.XMM[cpu_reg].sw[2] * (int32_t) src.sw[2]) + ((int32_t) cpu_state.XMM[cpu_reg].sw[3] * (int32_t) src.sw[3]);

    if (cpu_state.XMM[cpu_reg].l[2] == 0x80008000 && src.l[2] == 0x80008000)
        cpu_state.XMM[cpu_reg].l[2] = 0x80000000;
    else
        cpu_state.XMM[cpu_reg].sl[2] = ((int32_t) cpu_state.XMM[cpu_reg].sw[4] * (int32_t) src.sw[4]) + ((int32_t) cpu_state.XMM[cpu_reg].sw[5] * (int32_t) src.sw[5]);

    if (cpu_state.XMM[cpu_reg].l[3] == 0x80008000 && src.l[3] == 0x80008000)
        cpu_state.XMM[cpu_reg].l[3] = 0x80000000;
    else
        cpu_state.XMM[cpu_reg].sl[3] = ((int32_t) cpu_state.XMM[cpu_reg].sw[6] * (int32_t) src.sw[6]) + ((int32_t) cpu_state.XMM[cpu_reg].sw[7] * (int32_t) src.sw[7]);

    return 0;
}

static int
opPSUBB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0] -= src.b[0];
    cpu_state.XMM[cpu_reg].b[1] -= src.b[1];
    cpu_state.XMM[cpu_reg].b[2] -= src.b[2];
    cpu_state.XMM[cpu_reg].b[3] -= src.b[3];
    cpu_state.XMM[cpu_reg].b[4] -= src.b[4];
    cpu_state.XMM[cpu_reg].b[5] -= src.b[5];
    cpu_state.XMM[cpu_reg].b[6] -= src.b[6];
    cpu_state.XMM[cpu_reg].b[7] -= src.b[7];
    cpu_state.XMM[cpu_reg].b[8] -= src.b[8];
    cpu_state.XMM[cpu_reg].b[9] -= src.b[9];
    cpu_state.XMM[cpu_reg].b[10] -= src.b[10];
    cpu_state.XMM[cpu_reg].b[11] -= src.b[11];
    cpu_state.XMM[cpu_reg].b[12] -= src.b[12];
    cpu_state.XMM[cpu_reg].b[13] -= src.b[13];
    cpu_state.XMM[cpu_reg].b[14] -= src.b[14];
    cpu_state.XMM[cpu_reg].b[15] -= src.b[15];

    return 0;
}

static int
opPSUBB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0] -= src.b[0];
    cpu_state.XMM[cpu_reg].b[1] -= src.b[1];
    cpu_state.XMM[cpu_reg].b[2] -= src.b[2];
    cpu_state.XMM[cpu_reg].b[3] -= src.b[3];
    cpu_state.XMM[cpu_reg].b[4] -= src.b[4];
    cpu_state.XMM[cpu_reg].b[5] -= src.b[5];
    cpu_state.XMM[cpu_reg].b[6] -= src.b[6];
    cpu_state.XMM[cpu_reg].b[7] -= src.b[7];
    cpu_state.XMM[cpu_reg].b[8] -= src.b[8];
    cpu_state.XMM[cpu_reg].b[9] -= src.b[9];
    cpu_state.XMM[cpu_reg].b[10] -= src.b[10];
    cpu_state.XMM[cpu_reg].b[11] -= src.b[11];
    cpu_state.XMM[cpu_reg].b[12] -= src.b[12];
    cpu_state.XMM[cpu_reg].b[13] -= src.b[13];
    cpu_state.XMM[cpu_reg].b[14] -= src.b[14];
    cpu_state.XMM[cpu_reg].b[15] -= src.b[15];

    return 0;
}

static int
opPSUBW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] -= src.w[0];
    cpu_state.XMM[cpu_reg].w[1] -= src.w[1];
    cpu_state.XMM[cpu_reg].w[2] -= src.w[2];
    cpu_state.XMM[cpu_reg].w[3] -= src.w[3];
    cpu_state.XMM[cpu_reg].w[4] -= src.w[4];
    cpu_state.XMM[cpu_reg].w[5] -= src.w[5];
    cpu_state.XMM[cpu_reg].w[6] -= src.w[6];
    cpu_state.XMM[cpu_reg].w[7] -= src.w[7];

    return 0;
}

static int
opPSUBW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] -= src.w[0];
    cpu_state.XMM[cpu_reg].w[1] -= src.w[1];
    cpu_state.XMM[cpu_reg].w[2] -= src.w[2];
    cpu_state.XMM[cpu_reg].w[3] -= src.w[3];
    cpu_state.XMM[cpu_reg].w[4] -= src.w[4];
    cpu_state.XMM[cpu_reg].w[5] -= src.w[5];
    cpu_state.XMM[cpu_reg].w[6] -= src.w[6];
    cpu_state.XMM[cpu_reg].w[7] -= src.w[7];

    return 0;
}

static int
opPSUBD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] -= src.l[0];
    cpu_state.XMM[cpu_reg].l[1] -= src.l[1];
    cpu_state.XMM[cpu_reg].l[2] -= src.l[2];
    cpu_state.XMM[cpu_reg].l[3] -= src.l[3];

    return 0;
}

static int
opPSUBD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] -= src.l[0];
    cpu_state.XMM[cpu_reg].l[1] -= src.l[1];
    cpu_state.XMM[cpu_reg].l[2] -= src.l[2];
    cpu_state.XMM[cpu_reg].l[3] -= src.l[3];

    return 0;
}

static int
opPSUBQ_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] -= src.q[0];
    cpu_state.XMM[cpu_reg].q[1] -= src.q[1];

    return 0;
}

static int
opPSUBQ_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] -= src.q[0];
    cpu_state.XMM[cpu_reg].q[1] -= src.q[1];

    return 0;
}

static int
opPSUBQ_a16(uint32_t fetchdat)
{
    if (cpu_state.sse_xmm)
        return opPSUBQ_xmm_a16(fetchdat);
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_16(fetchdat);
    MMX_GETSRC();
    dst = MMX_GETREGP(cpu_reg);

    dst->q -= src.q;
    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSUBQ_a32(uint32_t fetchdat)
{
    if (cpu_state.sse_xmm)
        return opPSUBQ_xmm_a32(fetchdat);
    MMX_REG src;
    MMX_REG *dst;
    MMX_ENTER();

    fetch_ea_32(fetchdat);
    MMX_GETSRC();
    dst = MMX_GETREGP(cpu_reg);

    dst->q -= src.q;
    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPADDB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0] += src.b[0];
    cpu_state.XMM[cpu_reg].b[1] += src.b[1];
    cpu_state.XMM[cpu_reg].b[2] += src.b[2];
    cpu_state.XMM[cpu_reg].b[3] += src.b[3];
    cpu_state.XMM[cpu_reg].b[4] += src.b[4];
    cpu_state.XMM[cpu_reg].b[5] += src.b[5];
    cpu_state.XMM[cpu_reg].b[6] += src.b[6];
    cpu_state.XMM[cpu_reg].b[7] += src.b[7];
    cpu_state.XMM[cpu_reg].b[8] += src.b[8];
    cpu_state.XMM[cpu_reg].b[9] += src.b[9];
    cpu_state.XMM[cpu_reg].b[10] += src.b[10];
    cpu_state.XMM[cpu_reg].b[11] += src.b[11];
    cpu_state.XMM[cpu_reg].b[12] += src.b[12];
    cpu_state.XMM[cpu_reg].b[13] += src.b[13];
    cpu_state.XMM[cpu_reg].b[14] += src.b[14];
    cpu_state.XMM[cpu_reg].b[15] += src.b[15];

    return 0;
}

static int
opPADDB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0] += src.b[0];
    cpu_state.XMM[cpu_reg].b[1] += src.b[1];
    cpu_state.XMM[cpu_reg].b[2] += src.b[2];
    cpu_state.XMM[cpu_reg].b[3] += src.b[3];
    cpu_state.XMM[cpu_reg].b[4] += src.b[4];
    cpu_state.XMM[cpu_reg].b[5] += src.b[5];
    cpu_state.XMM[cpu_reg].b[6] += src.b[6];
    cpu_state.XMM[cpu_reg].b[7] += src.b[7];
    cpu_state.XMM[cpu_reg].b[8] += src.b[8];
    cpu_state.XMM[cpu_reg].b[9] += src.b[9];
    cpu_state.XMM[cpu_reg].b[10] += src.b[10];
    cpu_state.XMM[cpu_reg].b[11] += src.b[11];
    cpu_state.XMM[cpu_reg].b[12] += src.b[12];
    cpu_state.XMM[cpu_reg].b[13] += src.b[13];
    cpu_state.XMM[cpu_reg].b[14] += src.b[14];
    cpu_state.XMM[cpu_reg].b[15] += src.b[15];

    return 0;
}

static int
opPADDW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] += src.w[0];
    cpu_state.XMM[cpu_reg].w[1] += src.w[1];
    cpu_state.XMM[cpu_reg].w[2] += src.w[2];
    cpu_state.XMM[cpu_reg].w[3] += src.w[3];
    cpu_state.XMM[cpu_reg].w[4] += src.w[4];
    cpu_state.XMM[cpu_reg].w[5] += src.w[5];
    cpu_state.XMM[cpu_reg].w[6] += src.w[6];
    cpu_state.XMM[cpu_reg].w[7] += src.w[7];

    return 0;
}

static int
opPADDW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] += src.w[0];
    cpu_state.XMM[cpu_reg].w[1] += src.w[1];
    cpu_state.XMM[cpu_reg].w[2] += src.w[2];
    cpu_state.XMM[cpu_reg].w[3] += src.w[3];
    cpu_state.XMM[cpu_reg].w[4] += src.w[4];
    cpu_state.XMM[cpu_reg].w[5] += src.w[5];
    cpu_state.XMM[cpu_reg].w[6] += src.w[6];
    cpu_state.XMM[cpu_reg].w[7] += src.w[7];

    return 0;
}

static int
opPADDD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] += src.l[0];
    cpu_state.XMM[cpu_reg].l[1] += src.l[1];
    cpu_state.XMM[cpu_reg].l[2] += src.l[2];
    cpu_state.XMM[cpu_reg].l[3] += src.l[3];

    return 0;
}

static int
opPADDD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] += src.l[0];
    cpu_state.XMM[cpu_reg].l[1] += src.l[1];
    cpu_state.XMM[cpu_reg].l[2] += src.l[2];
    cpu_state.XMM[cpu_reg].l[3] += src.l[3];

    return 0;
}
