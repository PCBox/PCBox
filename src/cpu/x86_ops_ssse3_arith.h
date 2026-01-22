/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opPHADDW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    tmp.w[0] = cpu_state.XMM[cpu_reg].w[1] + cpu_state.XMM[cpu_reg].w[0];
    tmp.w[1] = cpu_state.XMM[cpu_reg].w[3] + cpu_state.XMM[cpu_reg].w[2];
    tmp.w[2] = cpu_state.XMM[cpu_reg].w[5] + cpu_state.XMM[cpu_reg].w[4];
    tmp.w[3] = cpu_state.XMM[cpu_reg].w[7] + cpu_state.XMM[cpu_reg].w[6];
    tmp.w[4] = src.w[1] + src.w[0];
    tmp.w[5] = src.w[3] + src.w[2];
    tmp.w[6] = src.w[5] + src.w[4];
    tmp.w[7] = src.w[7] + src.w[6];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHADDW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    tmp.w[0] = cpu_state.XMM[cpu_reg].w[1] + cpu_state.XMM[cpu_reg].w[0];
    tmp.w[1] = cpu_state.XMM[cpu_reg].w[3] + cpu_state.XMM[cpu_reg].w[2];
    tmp.w[2] = cpu_state.XMM[cpu_reg].w[5] + cpu_state.XMM[cpu_reg].w[4];
    tmp.w[3] = cpu_state.XMM[cpu_reg].w[7] + cpu_state.XMM[cpu_reg].w[6];
    tmp.w[4] = src.w[1] + src.w[0];
    tmp.w[5] = src.w[3] + src.w[2];
    tmp.w[6] = src.w[5] + src.w[4];
    tmp.w[7] = src.w[7] + src.w[6];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHADDW_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHADDW_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.w[0] = dst->w[1] + dst->w[0];
    tmp.w[1] = dst->w[3] + dst->w[2];
    tmp.w[2] = src.w[1] + src.w[0];
    tmp.w[3] = src.w[3] + src.w[2];

    dst->q = tmp.q;
    return 0;
}

static int
opPHADDW_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHADDW_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.w[0] = dst->w[1] + dst->w[0];
    tmp.w[1] = dst->w[3] + dst->w[2];
    tmp.w[2] = src.w[1] + src.w[0];
    tmp.w[3] = src.w[3] + src.w[2];

    dst->q = tmp.q;
    return 0;
}

static int
opPHADDD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    tmp.l[0] = cpu_state.XMM[cpu_reg].l[1] + cpu_state.XMM[cpu_reg].l[0];
    tmp.l[1] = cpu_state.XMM[cpu_reg].l[3] + cpu_state.XMM[cpu_reg].l[2];
    tmp.l[2] = src.l[1] + src.l[0];
    tmp.l[3] = src.l[3] + src.l[2];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHADDD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    tmp.l[0] = cpu_state.XMM[cpu_reg].l[1] + cpu_state.XMM[cpu_reg].l[0];
    tmp.l[1] = cpu_state.XMM[cpu_reg].l[3] + cpu_state.XMM[cpu_reg].l[2];
    tmp.l[2] = src.l[1] + src.l[0];
    tmp.l[3] = src.l[3] + src.l[2];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHADDD_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHADDD_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.l[0] = dst->l[1] + dst->l[0];
    tmp.l[1] = src.l[1] + src.l[0];

    dst->q = tmp.q;
    return 0;
}

static int
opPHADDD_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHADDD_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.l[0] = dst->l[1] + dst->l[0];
    tmp.l[1] = src.l[1] + src.l[0];

    dst->q = tmp.q;
    return 0;
}

static int
opPHADDSW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    tmp.sw[0] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[1] + (int32_t)cpu_state.XMM[cpu_reg].sw[0]);
    tmp.sw[1] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[3] + (int32_t)cpu_state.XMM[cpu_reg].sw[2]);
    tmp.sw[2] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[5] + (int32_t)cpu_state.XMM[cpu_reg].sw[4]);
    tmp.sw[3] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[7] + (int32_t)cpu_state.XMM[cpu_reg].sw[6]);
    tmp.sw[4] = SSATW((int32_t)src.sw[1] + (int32_t)src.sw[0]);
    tmp.sw[5] = SSATW((int32_t)src.sw[3] + (int32_t)src.sw[2]);
    tmp.sw[6] = SSATW((int32_t)src.sw[5] + (int32_t)src.sw[4]);
    tmp.sw[7] = SSATW((int32_t)src.sw[7] + (int32_t)src.sw[6]);

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHADDSW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    tmp.sw[0] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[1] + (int32_t)cpu_state.XMM[cpu_reg].sw[0]);
    tmp.sw[1] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[3] + (int32_t)cpu_state.XMM[cpu_reg].sw[2]);
    tmp.sw[2] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[5] + (int32_t)cpu_state.XMM[cpu_reg].sw[4]);
    tmp.sw[3] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[7] + (int32_t)cpu_state.XMM[cpu_reg].sw[6]);
    tmp.sw[4] = SSATW((int32_t)src.sw[1] + (int32_t)src.sw[0]);
    tmp.sw[5] = SSATW((int32_t)src.sw[3] + (int32_t)src.sw[2]);
    tmp.sw[6] = SSATW((int32_t)src.sw[5] + (int32_t)src.sw[4]);
    tmp.sw[7] = SSATW((int32_t)src.sw[7] + (int32_t)src.sw[6]);

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHADDSW_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHADDSW_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.sw[0] = SSATW((int32_t)dst->sw[1] + (int32_t)dst->sw[0]);
    tmp.sw[1] = SSATW((int32_t)dst->sw[3] + (int32_t)dst->sw[2]);
    tmp.sw[2] = SSATW((int32_t)src.sw[1] + (int32_t)src.sw[0]);
    tmp.sw[3] = SSATW((int32_t)src.sw[3] + (int32_t)src.sw[2]);

    dst->q = tmp.q;
    return 0;
}

static int
opPHADDSW_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHADDSW_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.sw[0] = SSATW((int32_t)dst->sw[1] + (int32_t)dst->sw[0]);
    tmp.sw[1] = SSATW((int32_t)dst->sw[3] + (int32_t)dst->sw[2]);
    tmp.sw[2] = SSATW((int32_t)src.sw[1] + (int32_t)src.sw[0]);
    tmp.sw[3] = SSATW((int32_t)src.sw[3] + (int32_t)src.sw[2]);

    dst->q = tmp.q;
    return 0;
}

static int
opPMADDUBSW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    tmp.sw[0] = SSATW(((int32_t)src.sb[1] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[1]) + ((int32_t)src.sb[0] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[0]));
    tmp.sw[1] = SSATW(((int32_t)src.sb[3] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[3]) + ((int32_t)src.sb[2] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[2]));
    tmp.sw[2] = SSATW(((int32_t)src.sb[5] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[5]) + ((int32_t)src.sb[4] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[4]));
    tmp.sw[3] = SSATW(((int32_t)src.sb[7] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[7]) + ((int32_t)src.sb[6] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[6]));
    tmp.sw[4] = SSATW(((int32_t)src.sb[9] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[9]) + ((int32_t)src.sb[8] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[8]));
    tmp.sw[5] = SSATW(((int32_t)src.sb[11] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[11]) + ((int32_t)src.sb[10] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[10]));
    tmp.sw[6] = SSATW(((int32_t)src.sb[13] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[13]) + ((int32_t)src.sb[12] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[12]));
    tmp.sw[7] = SSATW(((int32_t)src.sb[15] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[15]) + ((int32_t)src.sb[14] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[14]));

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPMADDUBSW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    tmp.sw[0] = SSATW(((int32_t)src.sb[1] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[1]) + ((int32_t)src.sb[0] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[0]));
    tmp.sw[1] = SSATW(((int32_t)src.sb[3] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[3]) + ((int32_t)src.sb[2] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[2]));
    tmp.sw[2] = SSATW(((int32_t)src.sb[5] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[5]) + ((int32_t)src.sb[4] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[4]));
    tmp.sw[3] = SSATW(((int32_t)src.sb[7] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[7]) + ((int32_t)src.sb[6] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[6]));
    tmp.sw[4] = SSATW(((int32_t)src.sb[9] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[9]) + ((int32_t)src.sb[8] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[8]));
    tmp.sw[5] = SSATW(((int32_t)src.sb[11] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[11]) + ((int32_t)src.sb[10] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[10]));
    tmp.sw[6] = SSATW(((int32_t)src.sb[13] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[13]) + ((int32_t)src.sb[12] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[12]));
    tmp.sw[7] = SSATW(((int32_t)src.sb[15] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[15]) + ((int32_t)src.sb[14] * (int32_t)(uint32_t)cpu_state.XMM[cpu_reg].b[14]));

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPMADDUBSW_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPMADDUBSW_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.sw[0] = SSATW(((int32_t)src.sb[1] * (int32_t)(uint32_t)dst->b[1]) + ((int32_t)src.sb[0] * (int32_t)(uint32_t)dst->b[0]));
    tmp.sw[1] = SSATW(((int32_t)src.sb[3] * (int32_t)(uint32_t)dst->b[3]) + ((int32_t)src.sb[2] * (int32_t)(uint32_t)dst->b[2]));
    tmp.sw[2] = SSATW(((int32_t)src.sb[5] * (int32_t)(uint32_t)dst->b[5]) + ((int32_t)src.sb[4] * (int32_t)(uint32_t)dst->b[4]));
    tmp.sw[3] = SSATW(((int32_t)src.sb[7] * (int32_t)(uint32_t)dst->b[7]) + ((int32_t)src.sb[6] * (int32_t)(uint32_t)dst->b[6]));

    dst->q = tmp.q;
    return 0;
}

static int
opPMADDUBSW_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPMADDUBSW_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.sw[0] = SSATW(((int32_t)src.sb[1] * (int32_t)(uint32_t)dst->b[1]) + ((int32_t)src.sb[0] * (int32_t)(uint32_t)dst->b[0]));
    tmp.sw[1] = SSATW(((int32_t)src.sb[3] * (int32_t)(uint32_t)dst->b[3]) + ((int32_t)src.sb[2] * (int32_t)(uint32_t)dst->b[2]));
    tmp.sw[2] = SSATW(((int32_t)src.sb[5] * (int32_t)(uint32_t)dst->b[5]) + ((int32_t)src.sb[4] * (int32_t)(uint32_t)dst->b[4]));
    tmp.sw[3] = SSATW(((int32_t)src.sb[7] * (int32_t)(uint32_t)dst->b[7]) + ((int32_t)src.sb[6] * (int32_t)(uint32_t)dst->b[6]));

    dst->q = tmp.q;
    return 0;
}

static int
opPHSUBW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    tmp.w[0] = cpu_state.XMM[cpu_reg].w[0] - cpu_state.XMM[cpu_reg].w[1];
    tmp.w[1] = cpu_state.XMM[cpu_reg].w[2] - cpu_state.XMM[cpu_reg].w[3];
    tmp.w[2] = cpu_state.XMM[cpu_reg].w[4] - cpu_state.XMM[cpu_reg].w[5];
    tmp.w[3] = cpu_state.XMM[cpu_reg].w[6] - cpu_state.XMM[cpu_reg].w[7];
    tmp.w[4] = src.w[0] - src.w[1];
    tmp.w[5] = src.w[2] - src.w[3];
    tmp.w[6] = src.w[4] - src.w[5];
    tmp.w[7] = src.w[6] - src.w[7];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHSUBW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    tmp.w[0] = cpu_state.XMM[cpu_reg].w[0] - cpu_state.XMM[cpu_reg].w[1];
    tmp.w[1] = cpu_state.XMM[cpu_reg].w[2] - cpu_state.XMM[cpu_reg].w[3];
    tmp.w[2] = cpu_state.XMM[cpu_reg].w[4] - cpu_state.XMM[cpu_reg].w[5];
    tmp.w[3] = cpu_state.XMM[cpu_reg].w[6] - cpu_state.XMM[cpu_reg].w[7];
    tmp.w[4] = src.w[0] - src.w[1];
    tmp.w[5] = src.w[2] - src.w[3];
    tmp.w[6] = src.w[4] - src.w[5];
    tmp.w[7] = src.w[6] - src.w[7];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHSUBW_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHSUBW_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.w[0] = dst->w[0] - dst->w[1];
    tmp.w[1] = dst->w[2] - dst->w[3];
    tmp.w[2] = src.w[0] - src.w[1];
    tmp.w[3] = src.w[2] - src.w[3];

    dst->q = tmp.q;
    return 0;
}

static int
opPHSUBW_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHSUBW_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.w[0] = dst->w[0] - dst->w[1];
    tmp.w[1] = dst->w[2] - dst->w[3];
    tmp.w[2] = src.w[0] - src.w[1];
    tmp.w[3] = src.w[2] - src.w[3];

    dst->q = tmp.q;
    return 0;
}

static int
opPHSUBD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    tmp.l[0] = cpu_state.XMM[cpu_reg].l[0] - cpu_state.XMM[cpu_reg].l[1];
    tmp.l[1] = cpu_state.XMM[cpu_reg].l[2] - cpu_state.XMM[cpu_reg].l[3];
    tmp.l[2] = src.l[0] - src.l[1];
    tmp.l[3] = src.l[2] - src.l[3];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHSUBD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    tmp.l[0] = cpu_state.XMM[cpu_reg].l[0] - cpu_state.XMM[cpu_reg].l[1];
    tmp.l[1] = cpu_state.XMM[cpu_reg].l[2] - cpu_state.XMM[cpu_reg].l[3];
    tmp.l[2] = src.l[0] - src.l[1];
    tmp.l[3] = src.l[2] - src.l[3];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHSUBD_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHSUBD_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.l[0] = dst->l[0] - dst->l[1];
    tmp.l[1] = src.l[0] - src.l[1];

    dst->q = tmp.q;
    return 0;
}

static int
opPHSUBD_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHSUBD_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.l[0] = dst->l[0] - dst->l[1];
    tmp.l[1] = src.l[0] - src.l[1];

    dst->q = tmp.q;
    return 0;
}

static int
opPHSUBSW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    tmp.sw[0] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[0] - (int32_t)cpu_state.XMM[cpu_reg].sw[1]);
    tmp.sw[1] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[2] - (int32_t)cpu_state.XMM[cpu_reg].sw[3]);
    tmp.sw[2] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[4] - (int32_t)cpu_state.XMM[cpu_reg].sw[5]);
    tmp.sw[3] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[6] - (int32_t)cpu_state.XMM[cpu_reg].sw[7]);
    tmp.sw[4] = SSATW((int32_t)src.sw[0] - (int32_t)src.sw[1]);
    tmp.sw[5] = SSATW((int32_t)src.sw[2] - (int32_t)src.sw[3]);
    tmp.sw[6] = SSATW((int32_t)src.sw[4] - (int32_t)src.sw[5]);
    tmp.sw[7] = SSATW((int32_t)src.sw[6] - (int32_t)src.sw[7]);

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHSUBSW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    tmp.sw[0] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[0] - (int32_t)cpu_state.XMM[cpu_reg].sw[1]);
    tmp.sw[1] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[2] - (int32_t)cpu_state.XMM[cpu_reg].sw[3]);
    tmp.sw[2] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[4] - (int32_t)cpu_state.XMM[cpu_reg].sw[5]);
    tmp.sw[3] = SSATW((int32_t)cpu_state.XMM[cpu_reg].sw[6] - (int32_t)cpu_state.XMM[cpu_reg].sw[7]);
    tmp.sw[4] = SSATW((int32_t)src.sw[0] - (int32_t)src.sw[1]);
    tmp.sw[5] = SSATW((int32_t)src.sw[2] - (int32_t)src.sw[3]);
    tmp.sw[6] = SSATW((int32_t)src.sw[4] - (int32_t)src.sw[5]);
    tmp.sw[7] = SSATW((int32_t)src.sw[6] - (int32_t)src.sw[7]);

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPHSUBSW_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHSUBSW_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.sw[0] = SSATW((int32_t)dst->sw[0] - (int32_t)dst->sw[1]);
    tmp.sw[1] = SSATW((int32_t)dst->sw[2] - (int32_t)dst->sw[3]);
    tmp.sw[2] = SSATW((int32_t)src.sw[0] - (int32_t)src.sw[1]);
    tmp.sw[3] = SSATW((int32_t)src.sw[2] - (int32_t)src.sw[3]);

    dst->q = tmp.q;
    return 0;
}

static int
opPHSUBSW_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPHSUBSW_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.sw[0] = SSATW((int32_t)dst->sw[0] - (int32_t)dst->sw[1]);
    tmp.sw[1] = SSATW((int32_t)dst->sw[2] - (int32_t)dst->sw[3]);
    tmp.sw[2] = SSATW((int32_t)src.sw[0] - (int32_t)src.sw[1]);
    tmp.sw[3] = SSATW((int32_t)src.sw[2] - (int32_t)src.sw[3]);

    dst->q = tmp.q;
    return 0;
}

static int
opPSIGNB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 16; i++)
    {
        int sign = (src.sb[i] > 0) - (src.sb[i] < 0);
        cpu_state.XMM[cpu_reg].sb[i] *= sign;
    }
    return 0;
}

static int
opPSIGNB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 16; i++)
    {
        int sign = (src.sb[i] > 0) - (src.sb[i] < 0);
        cpu_state.XMM[cpu_reg].sb[i] *= sign;
    }
    return 0;
}

static int
opPSIGNB_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPSIGNB_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 8; i++)
    {
        int sign = (src.sb[i] > 0) - (src.sb[i] < 0);
        dst->sb[i] *= sign;
    }
    return 0;
}

static int
opPSIGNB_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPSIGNB_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 8; i++)
    {
        int sign = (src.sb[i] > 0) - (src.sb[i] < 0);
        dst->sb[i] *= sign;
    }
    return 0;
}

static int
opPSIGNW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 8; i++)
    {
        int sign = (src.sw[i] > 0) - (src.sw[i] < 0);
        cpu_state.XMM[cpu_reg].sw[i] *= sign;
    }
    return 0;
}

static int
opPSIGNW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 8; i++)
    {
        int sign = (src.sw[i] > 0) - (src.sw[i] < 0);
        cpu_state.XMM[cpu_reg].sw[i] *= sign;
    }
    return 0;
}

static int
opPSIGNW_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPSIGNW_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 4; i++)
    {
        int sign = (src.sw[i] > 0) - (src.sw[i] < 0);
        dst->sw[i] *= sign;
    }
    return 0;
}

static int
opPSIGNW_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPSIGNW_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 4; i++)
    {
        int sign = (src.sw[i] > 0) - (src.sw[i] < 0);
        dst->sw[i] *= sign;
    }
    return 0;
}

static int
opPSIGND_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 4; i++)
    {
        int sign = (src.sl[i] > 0) - (src.sl[i] < 0);
        cpu_state.XMM[cpu_reg].sl[i] *= sign;
    }
    return 0;
}

static int
opPSIGND_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 4; i++)
    {
        int sign = (src.sl[i] > 0) - (src.sl[i] < 0);
        cpu_state.XMM[cpu_reg].sl[i] *= sign;
    }
    return 0;
}

static int
opPSIGND_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPSIGND_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 2; i++)
    {
        int sign = (src.sl[i] > 0) - (src.sl[i] < 0);
        dst->sl[i] *= sign;
    }
    return 0;
}

static int
opPSIGND_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPSIGND_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 2; i++)
    {
        int sign = (src.sl[i] > 0) - (src.sl[i] < 0);
        dst->sl[i] *= sign;
    }
    return 0;
}

static int
opPMULHRSW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    tmp.w[0] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[0] * (int32_t)src.sw[0]) >> 14) + 1) >> 1;
    tmp.w[1] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[1] * (int32_t)src.sw[1]) >> 14) + 1) >> 1;
    tmp.w[2] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[2] * (int32_t)src.sw[2]) >> 14) + 1) >> 1;
    tmp.w[3] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[3] * (int32_t)src.sw[3]) >> 14) + 1) >> 1;
    tmp.w[4] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[4] * (int32_t)src.sw[4]) >> 14) + 1) >> 1;
    tmp.w[5] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[5] * (int32_t)src.sw[5]) >> 14) + 1) >> 1;
    tmp.w[6] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[6] * (int32_t)src.sw[6]) >> 14) + 1) >> 1;
    tmp.w[7] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[7] * (int32_t)src.sw[7]) >> 14) + 1) >> 1;

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPMULHRSW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    tmp.w[0] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[0] * (int32_t)src.sw[0]) >> 14) + 1) >> 1;
    tmp.w[1] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[1] * (int32_t)src.sw[1]) >> 14) + 1) >> 1;
    tmp.w[2] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[2] * (int32_t)src.sw[2]) >> 14) + 1) >> 1;
    tmp.w[3] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[3] * (int32_t)src.sw[3]) >> 14) + 1) >> 1;
    tmp.w[4] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[4] * (int32_t)src.sw[4]) >> 14) + 1) >> 1;
    tmp.w[5] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[5] * (int32_t)src.sw[5]) >> 14) + 1) >> 1;
    tmp.w[6] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[6] * (int32_t)src.sw[6]) >> 14) + 1) >> 1;
    tmp.w[7] = ((((int32_t)cpu_state.XMM[cpu_reg].sw[7] * (int32_t)src.sw[7]) >> 14) + 1) >> 1;

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPMULHRSW_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPMULHRSW_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.w[0] = ((((int32_t)dst->sw[0] * (int32_t)src.sw[0]) >> 14) + 1) >> 1;
    tmp.w[1] = ((((int32_t)dst->sw[1] * (int32_t)src.sw[1]) >> 14) + 1) >> 1;
    tmp.w[2] = ((((int32_t)dst->sw[2] * (int32_t)src.sw[2]) >> 14) + 1) >> 1;
    tmp.w[3] = ((((int32_t)dst->sw[3] * (int32_t)src.sw[3]) >> 14) + 1) >> 1;

    dst->q = tmp.q;
    return 0;
}

static int
opPMULHRSW_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPMULHRSW_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp.w[0] = ((((int32_t)dst->sw[0] * (int32_t)src.sw[0]) >> 14) + 1) >> 1;
    tmp.w[1] = ((((int32_t)dst->sw[1] * (int32_t)src.sw[1]) >> 14) + 1) >> 1;
    tmp.w[2] = ((((int32_t)dst->sw[2] * (int32_t)src.sw[2]) >> 14) + 1) >> 1;
    tmp.w[3] = ((((int32_t)dst->sw[3] * (int32_t)src.sw[3]) >> 14) + 1) >> 1;

    dst->q = tmp.q;
    return 0;
}

static int
opPABSB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 16; i++)
    {
        if(src.sb[i] < 0) cpu_state.XMM[cpu_reg].sb[i] = -src.sb[i];
        else cpu_state.XMM[cpu_reg].sb[i] = src.sb[i];
    }
    return 0;
}

static int
opPABSB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 16; i++)
    {
        if(src.sb[i] < 0) cpu_state.XMM[cpu_reg].sb[i] = -src.sb[i];
        else cpu_state.XMM[cpu_reg].sb[i] = src.sb[i];
    }
    return 0;
}

static int
opPABSB_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPABSB_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 8; i++)
    {
        if(src.sb[i] < 0) dst->sb[i] = -src.sb[i];
        else dst->sb[i] = src.sb[i];
    }
    return 0;
}

static int
opPABSB_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPABSB_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 8; i++)
    {
        if(src.sb[i] < 0) dst->sb[i] = -src.sb[i];
        else dst->sb[i] = src.sb[i];
    }
    return 0;
}

static int
opPABSW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 8; i++)
    {
        if(src.sw[i] < 0) cpu_state.XMM[cpu_reg].sw[i] = -src.sw[i];
        else cpu_state.XMM[cpu_reg].sw[i] = src.sw[i];
    }
    return 0;
}

static int
opPABSW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 8; i++)
    {
        if(src.sw[i] < 0) cpu_state.XMM[cpu_reg].sw[i] = -src.sw[i];
        else cpu_state.XMM[cpu_reg].sw[i] = src.sw[i];
    }
    return 0;
}

static int
opPABSW_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPABSW_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 4; i++)
    {
        if(src.sw[i] < 0) dst->sw[i] = -src.sw[i];
        else dst->sw[i] = src.sw[i];
    }
    return 0;
}

static int
opPABSW_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPABSW_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 4; i++)
    {
        if(src.sw[i] < 0) dst->sw[i] = -src.sw[i];
        else dst->sw[i] = src.sw[i];
    }
    return 0;
}

static int
opPABSD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 4; i++)
    {
        if(src.sl[i] < 0) cpu_state.XMM[cpu_reg].sl[i] = -src.sl[i];
        else cpu_state.XMM[cpu_reg].sl[i] = src.sl[i];
    }
    return 0;
}

static int
opPABSD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    for(int i = 0; i < 4; i++)
    {
        if(src.sl[i] < 0) cpu_state.XMM[cpu_reg].sl[i] = -src.sl[i];
        else cpu_state.XMM[cpu_reg].sl[i] = src.sl[i];
    }
    return 0;
}

static int
opPABSD_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPABSD_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 2; i++)
    {
        if(src.sl[i] < 0) dst->sl[i] = -src.sl[i];
        else dst->sl[i] = src.sl[i];
    }
    return 0;
}

static int
opPABSD_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPABSD_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    for(int i = 0; i < 2; i++)
    {
        if(src.sl[i] < 0) dst->sl[i] = -src.sl[i];
        else dst->sl[i] = src.sl[i];
    }
    return 0;
}