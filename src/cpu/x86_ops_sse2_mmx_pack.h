/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opPUNPCKLBW_xmm_a16(uint32_t fetchdat)
{
    SSE_REG src;
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[15] = src.b[7];
    cpu_state.XMM[cpu_reg].b[14] = cpu_state.XMM[cpu_reg].b[7];
    cpu_state.XMM[cpu_reg].b[13] = src.b[6];
    cpu_state.XMM[cpu_reg].b[12] = cpu_state.XMM[cpu_reg].b[6];
    cpu_state.XMM[cpu_reg].b[11] = src.b[5];
    cpu_state.XMM[cpu_reg].b[10] = cpu_state.XMM[cpu_reg].b[5];
    cpu_state.XMM[cpu_reg].b[9]  = src.b[4];
    cpu_state.XMM[cpu_reg].b[8]  = cpu_state.XMM[cpu_reg].b[4];
    cpu_state.XMM[cpu_reg].b[7]  = src.b[3];
    cpu_state.XMM[cpu_reg].b[6]  = cpu_state.XMM[cpu_reg].b[3];
    cpu_state.XMM[cpu_reg].b[5]  = src.b[2];
    cpu_state.XMM[cpu_reg].b[4]  = cpu_state.XMM[cpu_reg].b[2];
    cpu_state.XMM[cpu_reg].b[3]  = src.b[1];
    cpu_state.XMM[cpu_reg].b[2]  = cpu_state.XMM[cpu_reg].b[1];
    cpu_state.XMM[cpu_reg].b[1]  = src.b[0];
    cpu_state.XMM[cpu_reg].b[0]  = cpu_state.XMM[cpu_reg].b[0];
    
    return 0;
}

static int
opPUNPCKLBW_xmm_a32(uint32_t fetchdat)
{
    SSE_REG src;
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[15] = src.b[7];
    cpu_state.XMM[cpu_reg].b[14] = cpu_state.XMM[cpu_reg].b[7];
    cpu_state.XMM[cpu_reg].b[13] = src.b[6];
    cpu_state.XMM[cpu_reg].b[12] = cpu_state.XMM[cpu_reg].b[6];
    cpu_state.XMM[cpu_reg].b[11] = src.b[5];
    cpu_state.XMM[cpu_reg].b[10] = cpu_state.XMM[cpu_reg].b[5];
    cpu_state.XMM[cpu_reg].b[9]  = src.b[4];
    cpu_state.XMM[cpu_reg].b[8]  = cpu_state.XMM[cpu_reg].b[4];
    cpu_state.XMM[cpu_reg].b[7]  = src.b[3];
    cpu_state.XMM[cpu_reg].b[6]  = cpu_state.XMM[cpu_reg].b[3];
    cpu_state.XMM[cpu_reg].b[5]  = src.b[2];
    cpu_state.XMM[cpu_reg].b[4]  = cpu_state.XMM[cpu_reg].b[2];
    cpu_state.XMM[cpu_reg].b[3]  = src.b[1];
    cpu_state.XMM[cpu_reg].b[2]  = cpu_state.XMM[cpu_reg].b[1];
    cpu_state.XMM[cpu_reg].b[1]  = src.b[0];
    cpu_state.XMM[cpu_reg].b[0]  = cpu_state.XMM[cpu_reg].b[0];
    return 0;
}

static int
opPUNPCKLWD_xmm_a16(uint32_t fetchdat)
{
    SSE_REG src;
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[7] = src.w[3];
    cpu_state.XMM[cpu_reg].w[6] = cpu_state.XMM[cpu_reg].w[3];
    cpu_state.XMM[cpu_reg].w[5] = src.w[2];
    cpu_state.XMM[cpu_reg].w[4] = cpu_state.XMM[cpu_reg].w[2];
    cpu_state.XMM[cpu_reg].w[3] = src.w[1];
    cpu_state.XMM[cpu_reg].w[2] = cpu_state.XMM[cpu_reg].w[1];
    cpu_state.XMM[cpu_reg].w[1] = src.w[0];
    cpu_state.XMM[cpu_reg].w[0] = cpu_state.XMM[cpu_reg].w[0];
    
    return 0;
}

static int
opPUNPCKLWD_xmm_a32(uint32_t fetchdat)
{
    SSE_REG src;
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[7] = src.w[3];
    cpu_state.XMM[cpu_reg].w[6] = cpu_state.XMM[cpu_reg].w[3];
    cpu_state.XMM[cpu_reg].w[5] = src.w[2];
    cpu_state.XMM[cpu_reg].w[4] = cpu_state.XMM[cpu_reg].w[2];
    cpu_state.XMM[cpu_reg].w[3] = src.w[1];
    cpu_state.XMM[cpu_reg].w[2] = cpu_state.XMM[cpu_reg].w[1];
    cpu_state.XMM[cpu_reg].w[1] = src.w[0];
    cpu_state.XMM[cpu_reg].w[0] = cpu_state.XMM[cpu_reg].w[0];
    
    return 0;
}

static int
opPUNPCKLDQ_xmm_a16(uint32_t fetchdat)
{
    SSE_REG src;
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[3] = src.l[1];
    cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_reg].l[1];
    cpu_state.XMM[cpu_reg].l[1] = src.l[0];
    cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_reg].l[0];
    
    return 0;
}

static int
opPUNPCKLDQ_xmm_a32(uint32_t fetchdat)
{
    SSE_REG src;
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[3] = src.l[1];
    cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_reg].l[1];
    cpu_state.XMM[cpu_reg].l[1] = src.l[0];
    cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_reg].l[0];
    
    return 0;
}

static int
opPACKSSWB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    SSE_REG dst;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    dst = cpu_state.XMM[cpu_reg];

    cpu_state.XMM[cpu_reg].sb[0] = SSATB(dst.sw[0]);
    cpu_state.XMM[cpu_reg].sb[1] = SSATB(dst.sw[1]);
    cpu_state.XMM[cpu_reg].sb[2] = SSATB(dst.sw[2]);
    cpu_state.XMM[cpu_reg].sb[3] = SSATB(dst.sw[3]);
    cpu_state.XMM[cpu_reg].sb[4] = SSATB(dst.sw[4]);
    cpu_state.XMM[cpu_reg].sb[5] = SSATB(dst.sw[5]);
    cpu_state.XMM[cpu_reg].sb[6] = SSATB(dst.sw[6]);
    cpu_state.XMM[cpu_reg].sb[7] = SSATB(dst.sw[7]);
    cpu_state.XMM[cpu_reg].sb[8] = SSATB(src.sw[0]);
    cpu_state.XMM[cpu_reg].sb[9] = SSATB(src.sw[1]);
    cpu_state.XMM[cpu_reg].sb[10] = SSATB(src.sw[2]);
    cpu_state.XMM[cpu_reg].sb[11] = SSATB(src.sw[3]);
    cpu_state.XMM[cpu_reg].sb[12] = SSATB(src.sw[4]);
    cpu_state.XMM[cpu_reg].sb[13] = SSATB(src.sw[5]);
    cpu_state.XMM[cpu_reg].sb[14] = SSATB(src.sw[6]);
    cpu_state.XMM[cpu_reg].sb[15] = SSATB(src.sw[7]);

    return 0;
}

static int
opPACKSSWB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    SSE_REG dst;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    dst = cpu_state.XMM[cpu_reg];

    cpu_state.XMM[cpu_reg].sb[0] = SSATB(dst.sw[0]);
    cpu_state.XMM[cpu_reg].sb[1] = SSATB(dst.sw[1]);
    cpu_state.XMM[cpu_reg].sb[2] = SSATB(dst.sw[2]);
    cpu_state.XMM[cpu_reg].sb[3] = SSATB(dst.sw[3]);
    cpu_state.XMM[cpu_reg].sb[4] = SSATB(dst.sw[4]);
    cpu_state.XMM[cpu_reg].sb[5] = SSATB(dst.sw[5]);
    cpu_state.XMM[cpu_reg].sb[6] = SSATB(dst.sw[6]);
    cpu_state.XMM[cpu_reg].sb[7] = SSATB(dst.sw[7]);
    cpu_state.XMM[cpu_reg].sb[8] = SSATB(src.sw[0]);
    cpu_state.XMM[cpu_reg].sb[9] = SSATB(src.sw[1]);
    cpu_state.XMM[cpu_reg].sb[10] = SSATB(src.sw[2]);
    cpu_state.XMM[cpu_reg].sb[11] = SSATB(src.sw[3]);
    cpu_state.XMM[cpu_reg].sb[12] = SSATB(src.sw[4]);
    cpu_state.XMM[cpu_reg].sb[13] = SSATB(src.sw[5]);
    cpu_state.XMM[cpu_reg].sb[14] = SSATB(src.sw[6]);
    cpu_state.XMM[cpu_reg].sb[15] = SSATB(src.sw[7]);

    return 0;
}

static int
opPACKUSWB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    SSE_REG dst;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    dst = cpu_state.XMM[cpu_reg];

    cpu_state.XMM[cpu_reg].b[0] = USATB(dst.sw[0]);
    cpu_state.XMM[cpu_reg].b[1] = USATB(dst.sw[1]);
    cpu_state.XMM[cpu_reg].b[2] = USATB(dst.sw[2]);
    cpu_state.XMM[cpu_reg].b[3] = USATB(dst.sw[3]);
    cpu_state.XMM[cpu_reg].b[4] = USATB(dst.sw[4]);
    cpu_state.XMM[cpu_reg].b[5] = USATB(dst.sw[5]);
    cpu_state.XMM[cpu_reg].b[6] = USATB(dst.sw[6]);
    cpu_state.XMM[cpu_reg].b[7] = USATB(dst.sw[7]);
    cpu_state.XMM[cpu_reg].b[8] = USATB(src.sw[0]);
    cpu_state.XMM[cpu_reg].b[9] = USATB(src.sw[1]);
    cpu_state.XMM[cpu_reg].b[10] = USATB(src.sw[2]);
    cpu_state.XMM[cpu_reg].b[11] = USATB(src.sw[3]);
    cpu_state.XMM[cpu_reg].b[12] = USATB(src.sw[4]);
    cpu_state.XMM[cpu_reg].b[13] = USATB(src.sw[5]);
    cpu_state.XMM[cpu_reg].b[14] = USATB(src.sw[6]);
    cpu_state.XMM[cpu_reg].b[15] = USATB(src.sw[7]);

    return 0;
}

static int
opPACKUSWB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    SSE_REG dst;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    dst = cpu_state.XMM[cpu_reg];

    cpu_state.XMM[cpu_reg].b[0] = USATB(dst.sw[0]);
    cpu_state.XMM[cpu_reg].b[1] = USATB(dst.sw[1]);
    cpu_state.XMM[cpu_reg].b[2] = USATB(dst.sw[2]);
    cpu_state.XMM[cpu_reg].b[3] = USATB(dst.sw[3]);
    cpu_state.XMM[cpu_reg].b[4] = USATB(dst.sw[4]);
    cpu_state.XMM[cpu_reg].b[5] = USATB(dst.sw[5]);
    cpu_state.XMM[cpu_reg].b[6] = USATB(dst.sw[6]);
    cpu_state.XMM[cpu_reg].b[7] = USATB(dst.sw[7]);
    cpu_state.XMM[cpu_reg].b[8] = USATB(src.sw[0]);
    cpu_state.XMM[cpu_reg].b[9] = USATB(src.sw[1]);
    cpu_state.XMM[cpu_reg].b[10] = USATB(src.sw[2]);
    cpu_state.XMM[cpu_reg].b[11] = USATB(src.sw[3]);
    cpu_state.XMM[cpu_reg].b[12] = USATB(src.sw[4]);
    cpu_state.XMM[cpu_reg].b[13] = USATB(src.sw[5]);
    cpu_state.XMM[cpu_reg].b[14] = USATB(src.sw[6]);
    cpu_state.XMM[cpu_reg].b[15] = USATB(src.sw[7]);

    return 0;
}

static int
opPUNPCKHBW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0]  = cpu_state.XMM[cpu_reg].b[8];
    cpu_state.XMM[cpu_reg].b[1]  = src.b[8];
    cpu_state.XMM[cpu_reg].b[2]  = cpu_state.XMM[cpu_reg].b[9];
    cpu_state.XMM[cpu_reg].b[3]  = src.b[9];
    cpu_state.XMM[cpu_reg].b[4]  = cpu_state.XMM[cpu_reg].b[10];
    cpu_state.XMM[cpu_reg].b[5]  = src.b[10];
    cpu_state.XMM[cpu_reg].b[6]  = cpu_state.XMM[cpu_reg].b[11];
    cpu_state.XMM[cpu_reg].b[7]  = src.b[11];
    cpu_state.XMM[cpu_reg].b[8]  = cpu_state.XMM[cpu_reg].b[12];
    cpu_state.XMM[cpu_reg].b[9]  = src.b[12];
    cpu_state.XMM[cpu_reg].b[10] = cpu_state.XMM[cpu_reg].b[13];
    cpu_state.XMM[cpu_reg].b[11] = src.b[13];
    cpu_state.XMM[cpu_reg].b[12] = cpu_state.XMM[cpu_reg].b[14];
    cpu_state.XMM[cpu_reg].b[13] = src.b[14];
    cpu_state.XMM[cpu_reg].b[14] = cpu_state.XMM[cpu_reg].b[15];
    cpu_state.XMM[cpu_reg].b[15] = src.b[15];

    return 0;
}

static int
opPUNPCKHBW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0]  = cpu_state.XMM[cpu_reg].b[8];
    cpu_state.XMM[cpu_reg].b[1]  = src.b[8];
    cpu_state.XMM[cpu_reg].b[2]  = cpu_state.XMM[cpu_reg].b[9];
    cpu_state.XMM[cpu_reg].b[3]  = src.b[9];
    cpu_state.XMM[cpu_reg].b[4]  = cpu_state.XMM[cpu_reg].b[10];
    cpu_state.XMM[cpu_reg].b[5]  = src.b[10];
    cpu_state.XMM[cpu_reg].b[6]  = cpu_state.XMM[cpu_reg].b[11];
    cpu_state.XMM[cpu_reg].b[7]  = src.b[11];
    cpu_state.XMM[cpu_reg].b[8]  = cpu_state.XMM[cpu_reg].b[12];
    cpu_state.XMM[cpu_reg].b[9]  = src.b[12];
    cpu_state.XMM[cpu_reg].b[10] = cpu_state.XMM[cpu_reg].b[13];
    cpu_state.XMM[cpu_reg].b[11] = src.b[13];
    cpu_state.XMM[cpu_reg].b[12] = cpu_state.XMM[cpu_reg].b[14];
    cpu_state.XMM[cpu_reg].b[13] = src.b[14];
    cpu_state.XMM[cpu_reg].b[14] = cpu_state.XMM[cpu_reg].b[15];
    cpu_state.XMM[cpu_reg].b[15] = src.b[15];

    return 0;
}

static int
opPUNPCKHWD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] = cpu_state.XMM[cpu_reg].w[4];
    cpu_state.XMM[cpu_reg].w[1] = src.w[4];
    cpu_state.XMM[cpu_reg].w[2] = cpu_state.XMM[cpu_reg].w[5];
    cpu_state.XMM[cpu_reg].w[3] = src.w[5];
    cpu_state.XMM[cpu_reg].w[4] = cpu_state.XMM[cpu_reg].w[6];
    cpu_state.XMM[cpu_reg].w[5] = src.w[6];
    cpu_state.XMM[cpu_reg].w[6] = cpu_state.XMM[cpu_reg].w[7];
    cpu_state.XMM[cpu_reg].w[7] = src.w[7];

    return 0;
}

static int
opPUNPCKHWD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] = cpu_state.XMM[cpu_reg].w[4];
    cpu_state.XMM[cpu_reg].w[1] = src.w[4];
    cpu_state.XMM[cpu_reg].w[2] = cpu_state.XMM[cpu_reg].w[5];
    cpu_state.XMM[cpu_reg].w[3] = src.w[5];
    cpu_state.XMM[cpu_reg].w[4] = cpu_state.XMM[cpu_reg].w[6];
    cpu_state.XMM[cpu_reg].w[5] = src.w[6];
    cpu_state.XMM[cpu_reg].w[6] = cpu_state.XMM[cpu_reg].w[7];
    cpu_state.XMM[cpu_reg].w[7] = src.w[7];

    return 0;
}

static int
opPUNPCKHDQ_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_reg].l[2];
    cpu_state.XMM[cpu_reg].l[1] = src.l[2];
    cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_reg].l[3];
    cpu_state.XMM[cpu_reg].l[3] = src.l[3];

    return 0;
}

static int
opPUNPCKHDQ_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_reg].l[2];
    cpu_state.XMM[cpu_reg].l[1] = src.l[2];
    cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_reg].l[3];
    cpu_state.XMM[cpu_reg].l[3] = src.l[3];

    return 0;
}

static int
opPACKSSDW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    SSE_REG dst;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    dst = cpu_state.XMM[cpu_reg];

    cpu_state.XMM[cpu_reg].sw[0] = SSATW(dst.sl[0]);
    cpu_state.XMM[cpu_reg].sw[1] = SSATW(dst.sl[1]);
    cpu_state.XMM[cpu_reg].sw[2] = SSATW(dst.sl[2]);
    cpu_state.XMM[cpu_reg].sw[3] = SSATW(dst.sl[3]);
    cpu_state.XMM[cpu_reg].sw[4] = SSATW(src.sl[0]);
    cpu_state.XMM[cpu_reg].sw[5] = SSATW(src.sl[1]);
    cpu_state.XMM[cpu_reg].sw[6] = SSATW(src.sl[2]);
    cpu_state.XMM[cpu_reg].sw[7] = SSATW(src.sl[3]);

    return 0;
}

static int
opPACKSSDW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;
    SSE_REG dst;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    dst = cpu_state.XMM[cpu_reg];

    cpu_state.XMM[cpu_reg].sw[0] = SSATW(dst.sl[0]);
    cpu_state.XMM[cpu_reg].sw[1] = SSATW(dst.sl[1]);
    cpu_state.XMM[cpu_reg].sw[2] = SSATW(dst.sl[2]);
    cpu_state.XMM[cpu_reg].sw[3] = SSATW(dst.sl[3]);
    cpu_state.XMM[cpu_reg].sw[4] = SSATW(src.sl[0]);
    cpu_state.XMM[cpu_reg].sw[5] = SSATW(src.sl[1]);
    cpu_state.XMM[cpu_reg].sw[6] = SSATW(src.sl[2]);
    cpu_state.XMM[cpu_reg].sw[7] = SSATW(src.sl[3]);

    return 0;
}

static int
opPUNPCKLQDQ_a16(uint32_t fetchdat)
{
    ILLEGAL_ON(!cpu_state.sse_xmm);
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_reg].q[0];
    cpu_state.XMM[cpu_reg].q[1] = src.q[0];

    return 0;
}

static int
opPUNPCKLQDQ_a32(uint32_t fetchdat)
{
    ILLEGAL_ON(!cpu_state.sse_xmm);
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_reg].q[0];
    cpu_state.XMM[cpu_reg].q[1] = src.q[0];

    return 0;
}

static int
opPUNPCKHQDQ_a16(uint32_t fetchdat)
{
    ILLEGAL_ON(!cpu_state.sse_xmm);
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_reg].q[1];
    cpu_state.XMM[cpu_reg].q[1] = src.q[1];

    return 0;
}

static int
opPUNPCKHQDQ_a32(uint32_t fetchdat)
{
    ILLEGAL_ON(!cpu_state.sse_xmm);
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_reg].q[1];
    cpu_state.XMM[cpu_reg].q[1] = src.q[1];

    return 0;
}
