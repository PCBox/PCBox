/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opPSHUFB_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    tmp = cpu_state.XMM[cpu_reg];

    for(int i = 0; i < 16; i++)
    {
        if(src.b[i] & 0x80)
        {
            tmp.b[i] = 0;
        }
        else
        {
            tmp.b[i] = cpu_state.XMM[cpu_reg].b[src.b[i] & 0xf];
        }
    }
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPSHUFB_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    tmp = cpu_state.XMM[cpu_reg];

    for(int i = 0; i < 16; i++)
    {
        if(src.b[i] & 0x80)
        {
            tmp.b[i] = 0;
        }
        else
        {
            tmp.b[i] = cpu_state.XMM[cpu_reg].b[src.b[i] & 0xf];
        }
    }
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPSHUFB_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPSHUFB_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp = *dst;

    for(int i = 0; i < 8; i++)
    {
        if(src.b[i] & 0x80)
        {
            tmp.b[i] = 0;
        }
        else
        {
            tmp.b[i] = dst->b[src.b[i] & 7];
        }
    }
    dst->q = tmp.q;
    return 0;
}

static int
opPSHUFB_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPSHUFB_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst, tmp;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    tmp = *dst;

    for(int i = 0; i < 8; i++)
    {
        if(src.b[i] & 0x80)
        {
            tmp.b[i] = 0;
        }
        else
        {
            tmp.b[i] = dst->b[src.b[i] & 7];
        }
    }
    dst->q = tmp.q;
    return 0;
}

static int
opPALIGNR_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_16(fetchdat);
    SSE_GETSRC();

    int shift = getbyte();

    uint8_t tmp[48];

    memcpy(tmp, &src, 16);
    memcpy(tmp + 16, &cpu_state.XMM[cpu_reg], 16);
    memset(tmp + 32, 0, 16);

    int shift2 = (shift > 32) ? 32 : shift;

    memcpy(&cpu_state.XMM[cpu_reg], tmp + shift2, 16);

    return 0;
}

static int
opPALIGNR_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src;

    fetch_ea_32(fetchdat);
    SSE_GETSRC();

    int shift = getbyte();

    uint8_t tmp[48];

    memcpy(tmp, &src, 16);
    memcpy(tmp + 16, &cpu_state.XMM[cpu_reg], 16);
    memset(tmp + 32, 0, 16);

    int shift2 = (shift > 32) ? 32 : shift;

    memcpy(&cpu_state.XMM[cpu_reg], tmp + shift2, 16);

    return 0;
}


static int
opPALIGNR_mm_a16(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPALIGNR_xmm_a16(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_16(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    int shift = getbyte();

    uint8_t tmp[24];

    memcpy(tmp, &src.q, 8);
    memcpy(tmp + 8, dst, 8);
    memset(tmp + 16, 0, 8);

    int shift2 = (shift > 16) ? 16 : shift;

    memcpy(dst, tmp + shift2, 8);

    return 0;
}

static int
opPALIGNR_mm_a32(uint32_t fetchdat)
{
    if(cpu_state.sse_xmm) return opPALIGNR_xmm_a32(fetchdat);
    MMX_ENTER();
    MMX_REG src, *dst;

    fetch_ea_32(fetchdat);
    MMX_GETSRC();

    dst = MMX_GETREGP(cpu_reg);

    int shift = getbyte();

    uint8_t tmp[24];

    memcpy(tmp, &src.q, 8);
    memcpy(tmp + 8, dst, 8);
    memset(tmp + 16, 0, 8);

    int shift2 = (shift > 16) ? 16 : shift;

    memcpy(dst, tmp + shift2, 8);
    return 0;
}