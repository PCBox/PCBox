/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opPCMPGTB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0]  = (cpu_state.XMM[cpu_reg].sb[0] > src.sb[0]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[1]  = (cpu_state.XMM[cpu_reg].sb[1] > src.sb[1]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[2]  = (cpu_state.XMM[cpu_reg].sb[2] > src.sb[2]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[3]  = (cpu_state.XMM[cpu_reg].sb[3] > src.sb[3]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[4]  = (cpu_state.XMM[cpu_reg].sb[4] > src.sb[4]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[5]  = (cpu_state.XMM[cpu_reg].sb[5] > src.sb[5]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[6]  = (cpu_state.XMM[cpu_reg].sb[6] > src.sb[6]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[7]  = (cpu_state.XMM[cpu_reg].sb[7] > src.sb[7]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[8]  = (cpu_state.XMM[cpu_reg].sb[8] > src.sb[8]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[9]  = (cpu_state.XMM[cpu_reg].sb[9] > src.sb[9]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[10] = (cpu_state.XMM[cpu_reg].sb[10] > src.sb[10]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[11] = (cpu_state.XMM[cpu_reg].sb[11] > src.sb[11]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[12] = (cpu_state.XMM[cpu_reg].sb[12] > src.sb[12]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[13] = (cpu_state.XMM[cpu_reg].sb[13] > src.sb[13]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[14] = (cpu_state.XMM[cpu_reg].sb[14] > src.sb[14]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[15] = (cpu_state.XMM[cpu_reg].sb[15] > src.sb[15]) ? 0xff : 0;

    return 0;
}

static int
opPCMPGTB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0]  = (cpu_state.XMM[cpu_reg].sb[0] > src.sb[0]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[1]  = (cpu_state.XMM[cpu_reg].sb[1] > src.sb[1]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[2]  = (cpu_state.XMM[cpu_reg].sb[2] > src.sb[2]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[3]  = (cpu_state.XMM[cpu_reg].sb[3] > src.sb[3]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[4]  = (cpu_state.XMM[cpu_reg].sb[4] > src.sb[4]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[5]  = (cpu_state.XMM[cpu_reg].sb[5] > src.sb[5]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[6]  = (cpu_state.XMM[cpu_reg].sb[6] > src.sb[6]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[7]  = (cpu_state.XMM[cpu_reg].sb[7] > src.sb[7]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[8]  = (cpu_state.XMM[cpu_reg].sb[8] > src.sb[8]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[9]  = (cpu_state.XMM[cpu_reg].sb[9] > src.sb[9]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[10] = (cpu_state.XMM[cpu_reg].sb[10] > src.sb[10]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[11] = (cpu_state.XMM[cpu_reg].sb[11] > src.sb[11]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[12] = (cpu_state.XMM[cpu_reg].sb[12] > src.sb[12]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[13] = (cpu_state.XMM[cpu_reg].sb[13] > src.sb[13]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[14] = (cpu_state.XMM[cpu_reg].sb[14] > src.sb[14]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[15] = (cpu_state.XMM[cpu_reg].sb[15] > src.sb[15]) ? 0xff : 0;

    return 0;
}

static int
opPCMPGTW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] = (cpu_state.XMM[cpu_reg].sw[0] > src.sw[0]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[1] = (cpu_state.XMM[cpu_reg].sw[1] > src.sw[1]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[2] = (cpu_state.XMM[cpu_reg].sw[2] > src.sw[2]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[3] = (cpu_state.XMM[cpu_reg].sw[3] > src.sw[3]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[4] = (cpu_state.XMM[cpu_reg].sw[4] > src.sw[4]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[5] = (cpu_state.XMM[cpu_reg].sw[5] > src.sw[5]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[6] = (cpu_state.XMM[cpu_reg].sw[6] > src.sw[6]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[7] = (cpu_state.XMM[cpu_reg].sw[7] > src.sw[7]) ? 0xffff : 0;

    return 0;
}

static int
opPCMPGTW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] = (cpu_state.XMM[cpu_reg].sw[0] > src.sw[0]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[1] = (cpu_state.XMM[cpu_reg].sw[1] > src.sw[1]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[2] = (cpu_state.XMM[cpu_reg].sw[2] > src.sw[2]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[3] = (cpu_state.XMM[cpu_reg].sw[3] > src.sw[3]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[4] = (cpu_state.XMM[cpu_reg].sw[4] > src.sw[4]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[5] = (cpu_state.XMM[cpu_reg].sw[5] > src.sw[5]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[6] = (cpu_state.XMM[cpu_reg].sw[6] > src.sw[6]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[7] = (cpu_state.XMM[cpu_reg].sw[7] > src.sw[7]) ? 0xffff : 0;

    return 0;
}

static int
opPCMPGTD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] = (cpu_state.XMM[cpu_reg].sl[0] > src.sl[0]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[1] = (cpu_state.XMM[cpu_reg].sl[1] > src.sl[1]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[2] = (cpu_state.XMM[cpu_reg].sl[2] > src.sl[2]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[3] = (cpu_state.XMM[cpu_reg].sl[3] > src.sl[3]) ? 0xffffffff : 0;

    return 0;
}

static int
opPCMPGTD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] = (cpu_state.XMM[cpu_reg].sl[0] > src.sl[0]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[1] = (cpu_state.XMM[cpu_reg].sl[1] > src.sl[1]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[2] = (cpu_state.XMM[cpu_reg].sl[2] > src.sl[2]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[3] = (cpu_state.XMM[cpu_reg].sl[3] > src.sl[3]) ? 0xffffffff : 0;

    return 0;
}

static int
opPCMPEQB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0]  = (cpu_state.XMM[cpu_reg].b[0] == src.b[0]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[1]  = (cpu_state.XMM[cpu_reg].b[1] == src.b[1]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[2]  = (cpu_state.XMM[cpu_reg].b[2] == src.b[2]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[3]  = (cpu_state.XMM[cpu_reg].b[3] == src.b[3]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[4]  = (cpu_state.XMM[cpu_reg].b[4] == src.b[4]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[5]  = (cpu_state.XMM[cpu_reg].b[5] == src.b[5]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[6]  = (cpu_state.XMM[cpu_reg].b[6] == src.b[6]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[7]  = (cpu_state.XMM[cpu_reg].b[7] == src.b[7]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[8]  = (cpu_state.XMM[cpu_reg].b[8] == src.b[8]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[9]  = (cpu_state.XMM[cpu_reg].b[9] == src.b[9]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[10] = (cpu_state.XMM[cpu_reg].b[10] == src.b[10]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[11] = (cpu_state.XMM[cpu_reg].b[11] == src.b[11]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[12] = (cpu_state.XMM[cpu_reg].b[12] == src.b[12]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[13] = (cpu_state.XMM[cpu_reg].b[13] == src.b[13]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[14] = (cpu_state.XMM[cpu_reg].b[14] == src.b[14]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[15] = (cpu_state.XMM[cpu_reg].b[15] == src.b[15]) ? 0xff : 0;

    return 0;
}

static int
opPCMPEQB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].b[0]  = (cpu_state.XMM[cpu_reg].b[0] == src.b[0]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[1]  = (cpu_state.XMM[cpu_reg].b[1] == src.b[1]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[2]  = (cpu_state.XMM[cpu_reg].b[2] == src.b[2]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[3]  = (cpu_state.XMM[cpu_reg].b[3] == src.b[3]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[4]  = (cpu_state.XMM[cpu_reg].b[4] == src.b[4]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[5]  = (cpu_state.XMM[cpu_reg].b[5] == src.b[5]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[6]  = (cpu_state.XMM[cpu_reg].b[6] == src.b[6]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[7]  = (cpu_state.XMM[cpu_reg].b[7] == src.b[7]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[8]  = (cpu_state.XMM[cpu_reg].b[8] == src.b[8]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[9]  = (cpu_state.XMM[cpu_reg].b[9] == src.b[9]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[10] = (cpu_state.XMM[cpu_reg].b[10] == src.b[10]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[11] = (cpu_state.XMM[cpu_reg].b[11] == src.b[11]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[12] = (cpu_state.XMM[cpu_reg].b[12] == src.b[12]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[13] = (cpu_state.XMM[cpu_reg].b[13] == src.b[13]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[14] = (cpu_state.XMM[cpu_reg].b[14] == src.b[14]) ? 0xff : 0;
    cpu_state.XMM[cpu_reg].b[15] = (cpu_state.XMM[cpu_reg].b[15] == src.b[15]) ? 0xff : 0;

    return 0;
}

static int
opPCMPEQW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] = (cpu_state.XMM[cpu_reg].w[0] == src.w[0]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[1] = (cpu_state.XMM[cpu_reg].w[1] == src.w[1]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[2] = (cpu_state.XMM[cpu_reg].w[2] == src.w[2]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[3] = (cpu_state.XMM[cpu_reg].w[3] == src.w[3]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[4] = (cpu_state.XMM[cpu_reg].w[4] == src.w[4]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[5] = (cpu_state.XMM[cpu_reg].w[5] == src.w[5]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[6] = (cpu_state.XMM[cpu_reg].w[6] == src.w[6]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[7] = (cpu_state.XMM[cpu_reg].w[7] == src.w[7]) ? 0xffff : 0;

    return 0;
}

static int
opPCMPEQW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].w[0] = (cpu_state.XMM[cpu_reg].w[0] == src.w[0]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[1] = (cpu_state.XMM[cpu_reg].w[1] == src.w[1]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[2] = (cpu_state.XMM[cpu_reg].w[2] == src.w[2]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[3] = (cpu_state.XMM[cpu_reg].w[3] == src.w[3]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[4] = (cpu_state.XMM[cpu_reg].w[4] == src.w[4]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[5] = (cpu_state.XMM[cpu_reg].w[5] == src.w[5]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[6] = (cpu_state.XMM[cpu_reg].w[6] == src.w[6]) ? 0xffff : 0;
    cpu_state.XMM[cpu_reg].w[7] = (cpu_state.XMM[cpu_reg].w[7] == src.w[7]) ? 0xffff : 0;

    return 0;
}

static int
opPCMPEQD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] = (cpu_state.XMM[cpu_reg].l[0] == src.l[0]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[1] = (cpu_state.XMM[cpu_reg].l[1] == src.l[1]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[2] = (cpu_state.XMM[cpu_reg].l[2] == src.l[2]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[3] = (cpu_state.XMM[cpu_reg].l[3] == src.l[3]) ? 0xffffffff : 0;

    return 0;
}

static int
opPCMPEQD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] = (cpu_state.XMM[cpu_reg].l[0] == src.l[0]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[1] = (cpu_state.XMM[cpu_reg].l[1] == src.l[1]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[2] = (cpu_state.XMM[cpu_reg].l[2] == src.l[2]) ? 0xffffffff : 0;
    cpu_state.XMM[cpu_reg].l[3] = (cpu_state.XMM[cpu_reg].l[3] == src.l[3]) ? 0xffffffff : 0;

    return 0;
}
