/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opPSxxW_xmm_imm(uint32_t fetchdat)
{
    SSE_ENTER();
    int      reg   = fetchdat & 7;
    int      op    = fetchdat & 0x38;
    int      shift = (fetchdat >> 8) & 0xff;

    cpu_state.pc+=2;

    switch (op) {
        case 0x10: /*PSRLW*/
            if (shift > 15) {
                cpu_state.XMM[reg].q[0] = 0;
                cpu_state.XMM[reg].q[1] = 0;
            } else {
                cpu_state.XMM[reg].w[0] >>= shift;
                cpu_state.XMM[reg].w[1] >>= shift;
                cpu_state.XMM[reg].w[2] >>= shift;
                cpu_state.XMM[reg].w[3] >>= shift;
                cpu_state.XMM[reg].w[4] >>= shift;
                cpu_state.XMM[reg].w[5] >>= shift;
                cpu_state.XMM[reg].w[6] >>= shift;
                cpu_state.XMM[reg].w[7] >>= shift;
            }
            break;
        case 0x20: /*PSRAW*/
            if (shift > 15)
                shift = 15;
            cpu_state.XMM[reg].sw[0] >>= shift;
            cpu_state.XMM[reg].sw[1] >>= shift;
            cpu_state.XMM[reg].sw[2] >>= shift;
            cpu_state.XMM[reg].sw[3] >>= shift;
            cpu_state.XMM[reg].sw[4] >>= shift;
            cpu_state.XMM[reg].sw[5] >>= shift;
            cpu_state.XMM[reg].sw[6] >>= shift;
            cpu_state.XMM[reg].sw[7] >>= shift;
            break;
        case 0x30: /*PSLLW*/
            if (shift > 15) {
                cpu_state.XMM[reg].q[0] = 0;
                cpu_state.XMM[reg].q[1] = 0;
            } else {
                cpu_state.XMM[reg].w[0] <<= shift;
                cpu_state.XMM[reg].w[1] <<= shift;
                cpu_state.XMM[reg].w[2] <<= shift;
                cpu_state.XMM[reg].w[3] <<= shift;
                cpu_state.XMM[reg].w[4] <<= shift;
                cpu_state.XMM[reg].w[5] <<= shift;
                cpu_state.XMM[reg].w[6] <<= shift;
                cpu_state.XMM[reg].w[7] <<= shift;
            }
            break;
        default:
            cpu_state.pc = cpu_state.oldpc;
            x86illegal();
            return 0;
    }

    CLOCK_CYCLES(1);
    return 0;
}

static int
opPSxxD_xmm_imm(uint32_t fetchdat)
{
    SSE_ENTER();
    int      reg   = fetchdat & 7;
    int      op    = fetchdat & 0x38;
    int      shift = (fetchdat >> 8) & 0xff;

    cpu_state.pc+=2;

    switch (op) {
        case 0x10: /*PSRLD*/
            if (shift > 31) {
                cpu_state.XMM[reg].q[0] = 0;
                cpu_state.XMM[reg].q[1] = 0;
            } else {
                cpu_state.XMM[reg].l[0] >>= shift;
                cpu_state.XMM[reg].l[1] >>= shift;
                cpu_state.XMM[reg].l[2] >>= shift;
                cpu_state.XMM[reg].l[3] >>= shift;
            }
            break;
        case 0x20: /*PSRAD*/
            if (shift > 31)
                shift = 31;
            cpu_state.XMM[reg].sl[0] >>= shift;
            cpu_state.XMM[reg].sl[1] >>= shift;
            cpu_state.XMM[reg].sl[2] >>= shift;
            cpu_state.XMM[reg].sl[3] >>= shift;
            break;
        case 0x30: /*PSLLD*/
            if (shift > 31) {
                cpu_state.XMM[reg].q[0] = 0;
                cpu_state.XMM[reg].q[1] = 0;
            } else {
                cpu_state.XMM[reg].l[0] <<= shift;
                cpu_state.XMM[reg].l[1] <<= shift;
                cpu_state.XMM[reg].l[2] <<= shift;
                cpu_state.XMM[reg].l[3] <<= shift;
            }
            break;
        default:
            cpu_state.pc = cpu_state.oldpc;
            x86illegal();
            return 0;
    }

    CLOCK_CYCLES(1);
    return 0;
}

static int
opPSxxQ_xmm_imm(uint32_t fetchdat)
{
    SSE_ENTER();
    int      reg   = fetchdat & 7;
    int      op    = fetchdat & 0x38;
    int      shift = (fetchdat >> 8) & 0xff;

    cpu_state.pc+=2;

    switch (op) {
        case 0x10: /*PSRLQ*/
            if (shift > 63) {
                cpu_state.XMM[reg].q[0] = 0;
                cpu_state.XMM[reg].q[1] = 0;
            } else {
                cpu_state.XMM[reg].q[0] >>= shift;
                cpu_state.XMM[reg].q[1] >>= shift;
            }
            break;
        case 0x18: /*PSRLDQ*/
            {
                if(shift > 15)
                {
                    cpu_state.XMM[reg].q[0] = 0;
                    cpu_state.XMM[reg].q[1] = 0;
                    break;
                }
                else if(shift > 7)
                {
                    cpu_state.XMM[reg].q[0] = cpu_state.XMM[reg].q[1];
                    cpu_state.XMM[reg].q[1] = 0;
                    shift -= 8;
                }
                shift <<= 3;
                if(shift != 0)
                {
                    cpu_state.XMM[reg].q[0] = (cpu_state.XMM[reg].q[0] >> shift) | (cpu_state.XMM[reg].q[1] << (64 - shift));
                    cpu_state.XMM[reg].q[1] = cpu_state.XMM[reg].q[1] >> shift;
                }
            }
            break;
        case 0x20: /*PSRAQ*/
            if (shift > 63)
                shift = 63;
            cpu_state.XMM[reg].sq[0] >>= shift;
            cpu_state.XMM[reg].sq[1] >>= shift;
            break;
        case 0x30: /*PSLLQ*/
            if (shift > 63) {
                cpu_state.XMM[reg].q[0] = 0;
                cpu_state.XMM[reg].q[1] = 0;
            } else {
                cpu_state.XMM[reg].q[0] <<= shift;
                cpu_state.XMM[reg].q[1] <<= shift;
            }
            break;
        case 0x38: /*PSLLDQ*/
            {
                if(shift > 15)
                {
                    cpu_state.XMM[reg].q[0] = 0;
                    cpu_state.XMM[reg].q[1] = 0;
                    break;
                }
                else if(shift > 7)
                {
                    cpu_state.XMM[reg].q[1] = cpu_state.XMM[reg].q[0];
                    cpu_state.XMM[reg].q[0] = 0;
                    shift -= 8;
                }
                shift <<= 3;
                if(shift != 0)
                {
                    cpu_state.XMM[reg].q[1] = (cpu_state.XMM[reg].q[1] << shift) | (cpu_state.XMM[reg].q[0] >> (64 - shift));
                    cpu_state.XMM[reg].q[0] = cpu_state.XMM[reg].q[0] << shift;
                }
            }
            break;
        default:
            cpu_state.pc = cpu_state.oldpc;
            x86illegal();
            return 0;
    }

    CLOCK_CYCLES(1);
    return 0;
}

//

#define SSE_GETSHIFT()                             \
    if (cpu_mod == 3) {                            \
        shift = cpu_state.XMM[cpu_rm].q[0];        \
        CLOCK_CYCLES(1);                           \
    } else {                                       \
        SEG_CHECK_READ(cpu_state.ea_seg);          \
        if (cpu_state.eaaddr & 0xf) {              \
            x86gpf(NULL, 0);                       \
            if (cpu_state.abrt)                    \
                return 1;                          \
        }                                          \
        shift = readmemq(easeg, cpu_state.eaaddr); \
        if (cpu_state.abrt)                        \
            return 0;                              \
        CLOCK_CYCLES(2);                           \
    }

static int
opPSRLW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_16(fetchdat);
    SSE_GETSHIFT();

    if (shift > 15) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].w[0] >>= shift;
        cpu_state.XMM[cpu_reg].w[1] >>= shift;
        cpu_state.XMM[cpu_reg].w[2] >>= shift;
        cpu_state.XMM[cpu_reg].w[3] >>= shift;
        cpu_state.XMM[cpu_reg].w[4] >>= shift;
        cpu_state.XMM[cpu_reg].w[5] >>= shift;
        cpu_state.XMM[cpu_reg].w[6] >>= shift;
        cpu_state.XMM[cpu_reg].w[7] >>= shift;
    }

    return 0;
}

static int
opPSRLW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_32(fetchdat);
    SSE_GETSHIFT();

    if (shift > 15) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].w[0] >>= shift;
        cpu_state.XMM[cpu_reg].w[1] >>= shift;
        cpu_state.XMM[cpu_reg].w[2] >>= shift;
        cpu_state.XMM[cpu_reg].w[3] >>= shift;
        cpu_state.XMM[cpu_reg].w[4] >>= shift;
        cpu_state.XMM[cpu_reg].w[5] >>= shift;
        cpu_state.XMM[cpu_reg].w[6] >>= shift;
        cpu_state.XMM[cpu_reg].w[7] >>= shift;
    }

    return 0;
}

static int
opPSRLD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_16(fetchdat);
    SSE_GETSHIFT();

    if (shift > 31) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].l[0] >>= shift;
        cpu_state.XMM[cpu_reg].l[1] >>= shift;
        cpu_state.XMM[cpu_reg].l[2] >>= shift;
        cpu_state.XMM[cpu_reg].l[3] >>= shift;
    }

    return 0;
}

static int
opPSRLD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_32(fetchdat);
    SSE_GETSHIFT();

    if (shift > 31) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].l[0] >>= shift;
        cpu_state.XMM[cpu_reg].l[1] >>= shift;
        cpu_state.XMM[cpu_reg].l[2] >>= shift;
        cpu_state.XMM[cpu_reg].l[3] >>= shift;
    }

    return 0;
}

static int
opPSRLQ_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_16(fetchdat);
    SSE_GETSHIFT();

    if (shift > 63) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].q[0] >>= shift;
        cpu_state.XMM[cpu_reg].q[1] >>= shift;
    }

    return 0;
}

static int
opPSRLQ_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_32(fetchdat);
    SSE_GETSHIFT();

    if (shift > 63) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].q[0] >>= shift;
        cpu_state.XMM[cpu_reg].q[1] >>= shift;
    }

    return 0;
}

static int
opPSRAW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_16(fetchdat);
    SSE_GETSHIFT();

    if (shift > 15)
        shift = 15;

    cpu_state.XMM[cpu_reg].sw[0] >>= shift;
    cpu_state.XMM[cpu_reg].sw[1] >>= shift;
    cpu_state.XMM[cpu_reg].sw[2] >>= shift;
    cpu_state.XMM[cpu_reg].sw[3] >>= shift;
    cpu_state.XMM[cpu_reg].sw[4] >>= shift;
    cpu_state.XMM[cpu_reg].sw[5] >>= shift;
    cpu_state.XMM[cpu_reg].sw[6] >>= shift;
    cpu_state.XMM[cpu_reg].sw[7] >>= shift;

    return 0;
}

static int
opPSRAW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_32(fetchdat);
    SSE_GETSHIFT();

    if (shift > 15)
        shift = 15;

    cpu_state.XMM[cpu_reg].sw[0] >>= shift;
    cpu_state.XMM[cpu_reg].sw[1] >>= shift;
    cpu_state.XMM[cpu_reg].sw[2] >>= shift;
    cpu_state.XMM[cpu_reg].sw[3] >>= shift;
    cpu_state.XMM[cpu_reg].sw[4] >>= shift;
    cpu_state.XMM[cpu_reg].sw[5] >>= shift;
    cpu_state.XMM[cpu_reg].sw[6] >>= shift;
    cpu_state.XMM[cpu_reg].sw[7] >>= shift;

    return 0;
}

static int
opPSRAD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_16(fetchdat);
    SSE_GETSHIFT();

    if (shift > 31)
        shift = 31;

    cpu_state.XMM[cpu_reg].sl[0] >>= shift;
    cpu_state.XMM[cpu_reg].sl[1] >>= shift;
    cpu_state.XMM[cpu_reg].sl[2] >>= shift;
    cpu_state.XMM[cpu_reg].sl[3] >>= shift;

    return 0;
}

static int
opPSRAD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_32(fetchdat);
    SSE_GETSHIFT();

    if (shift > 31)
        shift = 31;

    cpu_state.XMM[cpu_reg].sl[0] >>= shift;
    cpu_state.XMM[cpu_reg].sl[1] >>= shift;
    cpu_state.XMM[cpu_reg].sl[2] >>= shift;
    cpu_state.XMM[cpu_reg].sl[3] >>= shift;

    return 0;
}

static int
opPSLLW_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_16(fetchdat);
    SSE_GETSHIFT();

    if (shift > 15) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].w[0] <<= shift;
        cpu_state.XMM[cpu_reg].w[1] <<= shift;
        cpu_state.XMM[cpu_reg].w[2] <<= shift;
        cpu_state.XMM[cpu_reg].w[3] <<= shift;
        cpu_state.XMM[cpu_reg].w[4] <<= shift;
        cpu_state.XMM[cpu_reg].w[5] <<= shift;
        cpu_state.XMM[cpu_reg].w[6] <<= shift;
        cpu_state.XMM[cpu_reg].w[7] <<= shift;
    }

    return 0;
}

static int
opPSLLW_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_32(fetchdat);
    SSE_GETSHIFT();

    if (shift > 15) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].w[0] <<= shift;
        cpu_state.XMM[cpu_reg].w[1] <<= shift;
        cpu_state.XMM[cpu_reg].w[2] <<= shift;
        cpu_state.XMM[cpu_reg].w[3] <<= shift;
        cpu_state.XMM[cpu_reg].w[4] <<= shift;
        cpu_state.XMM[cpu_reg].w[5] <<= shift;
        cpu_state.XMM[cpu_reg].w[6] <<= shift;
        cpu_state.XMM[cpu_reg].w[7] <<= shift;
    }

    return 0;
}

static int
opPSLLD_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_16(fetchdat);
    SSE_GETSHIFT();

    if (shift > 31) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].l[0] <<= shift;
        cpu_state.XMM[cpu_reg].l[1] <<= shift;
        cpu_state.XMM[cpu_reg].l[2] <<= shift;
        cpu_state.XMM[cpu_reg].l[3] <<= shift;
    }

    return 0;
}

static int
opPSLLD_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_32(fetchdat);
    SSE_GETSHIFT();

    if (shift > 31) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].l[0] <<= shift;
        cpu_state.XMM[cpu_reg].l[1] <<= shift;
        cpu_state.XMM[cpu_reg].l[2] <<= shift;
        cpu_state.XMM[cpu_reg].l[3] <<= shift;
    }

    return 0;
}

static int
opPSLLQ_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t shift;

    fetch_ea_16(fetchdat);
    SSE_GETSHIFT();

    if (shift > 63) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].q[0] <<= shift;
        cpu_state.XMM[cpu_reg].q[1] <<= shift;
    }

    return 0;
}

static int
opPSLLQ_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    uint64_t  shift;

    fetch_ea_32(fetchdat);
    SSE_GETSHIFT();

    if (shift > 63) {
        cpu_state.XMM[cpu_reg].q[0] = 0;
        cpu_state.XMM[cpu_reg].q[1] = 0;
    } else {
        cpu_state.XMM[cpu_reg].q[0] <<= shift;
        cpu_state.XMM[cpu_reg].q[1] <<= shift;
    }

    return 0;
}
