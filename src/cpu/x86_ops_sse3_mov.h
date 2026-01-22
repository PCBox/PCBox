/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opMOVDDUP_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC64();

    cpu_state.XMM[cpu_reg].q[0] = src.q[0];
    cpu_state.XMM[cpu_reg].q[1] = src.q[0];
    return 0;
}

static int
opMOVDDUP_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC64();

    cpu_state.XMM[cpu_reg].q[0] = src.q[0];
    cpu_state.XMM[cpu_reg].q[1] = src.q[0];
    return 0;
}

static int
opMOVSLDUP_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] = src.l[0];
    cpu_state.XMM[cpu_reg].l[1] = src.l[0];
    cpu_state.XMM[cpu_reg].l[2] = src.l[2];
    cpu_state.XMM[cpu_reg].l[3] = src.l[2];
    return 0;
}

static int
opMOVSLDUP_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] = src.l[0];
    cpu_state.XMM[cpu_reg].l[1] = src.l[0];
    cpu_state.XMM[cpu_reg].l[2] = src.l[2];
    cpu_state.XMM[cpu_reg].l[3] = src.l[2];
    return 0;
}

static int
opMOVSHDUP_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] = src.l[1];
    cpu_state.XMM[cpu_reg].l[1] = src.l[1];
    cpu_state.XMM[cpu_reg].l[2] = src.l[3];
    cpu_state.XMM[cpu_reg].l[3] = src.l[3];
    return 0;
}

static int
opMOVSHDUP_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    cpu_state.XMM[cpu_reg].l[0] = src.l[1];
    cpu_state.XMM[cpu_reg].l[1] = src.l[1];
    cpu_state.XMM[cpu_reg].l[2] = src.l[3];
    cpu_state.XMM[cpu_reg].l[3] = src.l[3];
    return 0;
}