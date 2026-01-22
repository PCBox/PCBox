/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opMOVUPS_q_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] = cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] = cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst[4];

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4);
        if (cpu_state.abrt)
            return 1;
        dst[2] = readmeml(easeg, cpu_state.eaaddr + 8);
        if (cpu_state.abrt)
            return 1;
        dst[3] = readmeml(easeg, cpu_state.eaaddr + 12);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst[0];
        cpu_state.XMM[cpu_reg].l[1] = dst[1];
        cpu_state.XMM[cpu_reg].l[2] = dst[2];
        cpu_state.XMM[cpu_reg].l[3] = dst[3];

        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVUPS_q_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] = cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] = cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst[4];

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4);
        if (cpu_state.abrt)
            return 1;
        dst[2] = readmeml(easeg, cpu_state.eaaddr + 8);
        if (cpu_state.abrt)
            return 1;
        dst[3] = readmeml(easeg, cpu_state.eaaddr + 12);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst[0];
        cpu_state.XMM[cpu_reg].l[1] = dst[1];
        cpu_state.XMM[cpu_reg].l[2] = dst[2];
        cpu_state.XMM[cpu_reg].l[3] = dst[3];

        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVUPS_xmm_q_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].l[0] = cpu_state.XMM[cpu_reg].l[0];
        cpu_state.XMM[cpu_rm].l[1] = cpu_state.XMM[cpu_reg].l[1];
        cpu_state.XMM[cpu_rm].l[2] = cpu_state.XMM[cpu_reg].l[2];
        cpu_state.XMM[cpu_rm].l[3] = cpu_state.XMM[cpu_reg].l[3];
        CLOCK_CYCLES(1);
    } else {
        uint32_t rm[4] = { cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3] };
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememl(easeg, cpu_state.eaaddr, rm[0]);
        writememl(easeg, cpu_state.eaaddr + 4, rm[1]);
        writememl(easeg, cpu_state.eaaddr + 8, rm[2]);
        writememl(easeg, cpu_state.eaaddr + 12, rm[3]);
        if (cpu_state.abrt)
            return 1;

        CLOCK_CYCLES(2);        if (cpu_state.abrt)
            return 1;
    }

    return 0;
}

static int
opMOVUPS_xmm_q_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].l[0] = cpu_state.XMM[cpu_reg].l[0];
        cpu_state.XMM[cpu_rm].l[1] = cpu_state.XMM[cpu_reg].l[1];
        cpu_state.XMM[cpu_rm].l[2] = cpu_state.XMM[cpu_reg].l[2];
        cpu_state.XMM[cpu_rm].l[3] = cpu_state.XMM[cpu_reg].l[3];
        CLOCK_CYCLES(1);
    } else {
        uint32_t rm[4] = { cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3] };
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememl(easeg, cpu_state.eaaddr, rm[0]);
        writememl(easeg, cpu_state.eaaddr + 4, rm[1]);
        writememl(easeg, cpu_state.eaaddr + 8, rm[2]);
        writememl(easeg, cpu_state.eaaddr + 12, rm[3]);
        if (cpu_state.abrt)
            return 1;

        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVSS_f_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_rm].l[0];
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst;
        cpu_state.XMM[cpu_reg].l[1] = 0;
        cpu_state.XMM[cpu_reg].l[2] = 0;
        cpu_state.XMM[cpu_reg].l[3] = 0;

        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVSS_f_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_rm].l[0];
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst;
        cpu_state.XMM[cpu_reg].l[1] = 0;
        cpu_state.XMM[cpu_reg].l[2] = 0;
        cpu_state.XMM[cpu_reg].l[3] = 0;

        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVSS_xmm_f_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].l[0] = cpu_state.XMM[cpu_reg].l[0];
        CLOCK_CYCLES(1);
    } else {
        uint32_t rm = cpu_state.XMM[cpu_reg].l[0];

        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememl(easeg, cpu_state.eaaddr, rm);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVSS_xmm_f_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].l[0] = cpu_state.XMM[cpu_reg].l[0];
        CLOCK_CYCLES(1);
    } else {
        uint32_t rm = cpu_state.XMM[cpu_reg].l[0];

        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememl(easeg, cpu_state.eaaddr, rm);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVLPS_f_xmm_MOVHLPS_xmm_xmm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVLPD_f_xmm_a16(fetchdat);

    SSE_ENTER();

    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        // MOVHLPS
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    } else {
        // MOVLPS
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[0] = dst;
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVLPS_f_xmm_MOVHLPS_xmm_xmm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVLPD_f_xmm_a32(fetchdat);

    SSE_ENTER();

    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        // MOVHLPS
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    } else {
        // MOVLPS
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[0] = dst;
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVLPS_xmm_f_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVLPD_xmm_f_a16(fetchdat);

    SSE_ENTER();

    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(2);

    return 0;
}

static int
opMOVLPS_xmm_f_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVLPD_xmm_f_a32(fetchdat);

    SSE_ENTER();

    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opUNPCKLPS_f_xmm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opUNPCKLPD_f_xmm_a16(fetchdat);

    SSE_REG src, tmp;

    SSE_ENTER();

    fetch_ea_16(fetchdat);

    SSE_GETSRC();
    tmp.l[0] = cpu_state.XMM[cpu_reg].l[0];
    tmp.l[2] = cpu_state.XMM[cpu_reg].l[1];
    tmp.l[1] = src.l[0];
    tmp.l[3] = src.l[1];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    
    return 0;
}

static int
opUNPCKLPS_f_xmm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opUNPCKLPD_f_xmm_a32(fetchdat);

    SSE_REG src, tmp;

    SSE_ENTER();

    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    tmp.l[0] = cpu_state.XMM[cpu_reg].l[0];
    tmp.l[2] = cpu_state.XMM[cpu_reg].l[1];
    tmp.l[1] = src.l[0];
    tmp.l[3] = src.l[1];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];

    return 0;
}

static int
opUNPCKHPS_f_xmm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opUNPCKHPD_f_xmm_a16(fetchdat);

    SSE_REG src, tmp;

    SSE_ENTER();

    fetch_ea_16(fetchdat);

    SSE_GETSRC();
    tmp.l[0] = cpu_state.XMM[cpu_reg].l[2];
    tmp.l[2] = cpu_state.XMM[cpu_reg].l[3];
    tmp.l[1] = src.l[2];
    tmp.l[3] = src.l[3];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];

    return 0;
}

static int
opUNPCKHPS_f_xmm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opUNPCKHPD_f_xmm_a32(fetchdat);

    SSE_REG src, tmp;

    SSE_ENTER();
    
    fetch_ea_32(fetchdat);
    
    SSE_GETSRC();
    tmp.l[0] = cpu_state.XMM[cpu_reg].l[2];
    tmp.l[2] = cpu_state.XMM[cpu_reg].l[3];
    tmp.l[1] = src.l[2];
    tmp.l[3] = src.l[3];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opMOVHPS_f_xmm_MOVLHPS_xmm_xmm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVHPD_f_xmm_a16(fetchdat);

    SSE_ENTER();

    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        // MOVLHPS
        cpu_state.XMM[cpu_reg].q[1] = cpu_state.XMM[cpu_rm].q[0];
        CLOCK_CYCLES(1);
    } else {
        // MOVHPS
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[1] = dst;
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVHPS_f_xmm_MOVLHPS_xmm_xmm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVHPD_f_xmm_a32(fetchdat);

    SSE_ENTER();

    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        // MOVLHPS
        cpu_state.XMM[cpu_reg].q[1] = cpu_state.XMM[cpu_rm].q[0];
        CLOCK_CYCLES(1);
    } else {
        // MOVHPS
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[1] = dst;
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVHPS_xmm_f_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVHPD_xmm_f_a16(fetchdat);

    SSE_ENTER();

    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[1]);
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opMOVHPS_xmm_f_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVHPD_xmm_f_a32(fetchdat);

    SSE_ENTER();

    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[1]);
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opMOVAPS_q_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] = cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] = cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst[4];

        SEG_CHECK_READ(cpu_state.ea_seg);
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
        dst[0] = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4);
        if (cpu_state.abrt)
            return 1;
        dst[2] = readmeml(easeg, cpu_state.eaaddr + 8);
        if (cpu_state.abrt)
            return 1;
        dst[3] = readmeml(easeg, cpu_state.eaaddr + 12);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst[0];
        cpu_state.XMM[cpu_reg].l[1] = dst[1];
        cpu_state.XMM[cpu_reg].l[2] = dst[2];
        cpu_state.XMM[cpu_reg].l[3] = dst[3];

        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVAPS_q_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] = cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] = cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] = cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] = cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        uint32_t dst[4];

        SEG_CHECK_READ(cpu_state.ea_seg);
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
        dst[0] = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        dst[1] = readmeml(easeg, cpu_state.eaaddr + 4);
        if (cpu_state.abrt)
            return 1;
        dst[2] = readmeml(easeg, cpu_state.eaaddr + 8);
        if (cpu_state.abrt)
            return 1;
        dst[3] = readmeml(easeg, cpu_state.eaaddr + 12);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].l[0] = dst[0];
        cpu_state.XMM[cpu_reg].l[1] = dst[1];
        cpu_state.XMM[cpu_reg].l[2] = dst[2];
        cpu_state.XMM[cpu_reg].l[3] = dst[3];

        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVAPS_xmm_q_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].l[0] = cpu_state.XMM[cpu_reg].l[0];
        cpu_state.XMM[cpu_rm].l[1] = cpu_state.XMM[cpu_reg].l[1];
        cpu_state.XMM[cpu_rm].l[2] = cpu_state.XMM[cpu_reg].l[2];
        cpu_state.XMM[cpu_rm].l[3] = cpu_state.XMM[cpu_reg].l[3];
        CLOCK_CYCLES(1);
    } else {
        uint32_t rm[4] = { cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3] };
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
        writememl(easeg, cpu_state.eaaddr, rm[0]);
        writememl(easeg, cpu_state.eaaddr + 4, rm[1]);
        writememl(easeg, cpu_state.eaaddr + 8, rm[2]);
        writememl(easeg, cpu_state.eaaddr + 12, rm[3]);
        if (cpu_state.abrt)
            return 1;

        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVAPS_xmm_q_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].l[0] = cpu_state.XMM[cpu_reg].l[0];
        cpu_state.XMM[cpu_rm].l[1] = cpu_state.XMM[cpu_reg].l[1];
        cpu_state.XMM[cpu_rm].l[2] = cpu_state.XMM[cpu_reg].l[2];
        cpu_state.XMM[cpu_rm].l[3] = cpu_state.XMM[cpu_reg].l[3];
        CLOCK_CYCLES(1);
    } else {
        uint32_t rm[4] = { cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3] };
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
        writememl(easeg, cpu_state.eaaddr, rm[0]);
        if (cpu_state.abrt)
            return 1;
        writememl(easeg, cpu_state.eaaddr + 4, rm[1]);
        if (cpu_state.abrt)
            return 1;
        writememl(easeg, cpu_state.eaaddr + 8, rm[2]);
        if (cpu_state.abrt)
            return 1;
        writememl(easeg, cpu_state.eaaddr + 12, rm[3]);
        if (cpu_state.abrt)
            return 1;

        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opMOVNTPS_xmm_q_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);

    uint32_t rm[4] = { cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3] };
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_state.eaaddr & 0xf) {
        x86gpf(NULL, 0);
        if (cpu_state.abrt)
            return 1;
    }
    writememl(easeg, cpu_state.eaaddr, rm[0]);
    writememl(easeg, cpu_state.eaaddr + 4, rm[1]);
    writememl(easeg, cpu_state.eaaddr + 8, rm[2]);
    writememl(easeg, cpu_state.eaaddr + 12, rm[3]);
    if (cpu_state.abrt)
        return 1;

    CLOCK_CYCLES(2);

    return 0;
}

static int
opMOVNTPS_xmm_q_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);

    uint32_t rm[4] = { cpu_state.XMM[cpu_reg].l[0], cpu_state.XMM[cpu_reg].l[1], cpu_state.XMM[cpu_reg].l[2], cpu_state.XMM[cpu_reg].l[3] };
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    if (cpu_state.eaaddr & 0xf) {
        x86gpf(NULL, 0);
        if (cpu_state.abrt)
            return 1;
    }
    writememl(easeg, cpu_state.eaaddr, rm[0]);
    writememl(easeg, cpu_state.eaaddr + 4, rm[1]);
    writememl(easeg, cpu_state.eaaddr + 8, rm[2]);
    writememl(easeg, cpu_state.eaaddr + 12, rm[3]);
    if (cpu_state.abrt)
        return 1;

    CLOCK_CYCLES(2);

    return 0;
}

static int
opMOVMSKPS_l_xmm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVMSKPD_l_xmm_a16(fetchdat);
    
    SSE_ENTER();

    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        uint32_t result = 0;
        if (cpu_state.XMM[cpu_rm].l[0] & (1u << 31))
            result |= 1;
        if (cpu_state.XMM[cpu_rm].l[1] & (1u << 31))
            result |= 2;
        if (cpu_state.XMM[cpu_rm].l[2] & (1u << 31))
            result |= 4;
        if (cpu_state.XMM[cpu_rm].l[3] & (1u << 31))
            result |= 8;
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }

    return 0;
}

static int
opMOVMSKPS_l_xmm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVMSKPD_l_xmm_a32(fetchdat);

    SSE_ENTER();

    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        uint32_t result = 0;
        if (cpu_state.XMM[cpu_rm].l[0] & (1u << 31))
            result |= 1;
        if (cpu_state.XMM[cpu_rm].l[1] & (1u << 31))
            result |= 2;
        if (cpu_state.XMM[cpu_rm].l[2] & (1u << 31))
            result |= 4;
        if (cpu_state.XMM[cpu_rm].l[3] & (1u << 31))
            result |= 8;
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }

    return 0;
}

static int
opPSHUFW_mm_mm_a16(uint32_t fetchdat)
{
    MMX_REG  src, tmp;
    MMX_REG *dst;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opPSHUFD_a16(fetchdat);

    MMX_ENTER();
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;

    MMX_GETSRC();
    dst = MMX_GETREGP(cpu_reg);

    tmp.w[0] = src.w[imm & 3];
    tmp.w[1] = src.w[(imm >> 2) & 3];
    tmp.w[2] = src.w[(imm >> 4) & 3];
    tmp.w[3] = src.w[(imm >> 6) & 3];
    dst->q = tmp.q;
    CLOCK_CYCLES(1);
    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPSHUFW_mm_mm_a32(uint32_t fetchdat)
{
    MMX_REG  src, tmp;
    MMX_REG *dst;

    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opPSHUFD_a32(fetchdat);

    MMX_ENTER();
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;

    MMX_GETSRC();
    dst = MMX_GETREGP(cpu_reg);

    tmp.w[0] = src.w[imm & 3];
    tmp.w[1] = src.w[(imm >> 2) & 3];
    tmp.w[2] = src.w[(imm >> 4) & 3];
    tmp.w[3] = src.w[(imm >> 6) & 3];
    dst->q = tmp.q;
    CLOCK_CYCLES(1);
    MMX_SETEXP(cpu_reg);

    return 0;
}

static int
opPINSRW_xmm_w_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;
    if (cpu_mod == 3) {
        uint16_t rm = getr16(cpu_rm);
        if (cpu_state.sse_xmm) {
            SSE_ENTER();
            cpu_state.XMM[cpu_reg].w[imm & 7] = rm;
        }
        else {
            MMX_ENTER();
            MMX_REG *dst;
            dst = MMX_GETREGP(cpu_reg);
            dst->w[imm & 3] = rm;
            MMX_SETEXP(cpu_reg);
        }
        CLOCK_CYCLES(1);
    } else {
        uint16_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmemw(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        if (cpu_state.sse_xmm) {
            SSE_ENTER();
            cpu_state.XMM[cpu_reg].w[imm & 7] = src;
        }
        else {
            MMX_ENTER();
            MMX_REG *dst;
            dst = MMX_GETREGP(cpu_reg);
            dst->w[imm & 3] = src;
            MMX_SETEXP(cpu_reg);
        }
        CLOCK_CYCLES(2);
    }

    return 0;
}

static int
opPINSRW_xmm_w_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;
    if (cpu_mod == 3) {
        uint16_t rm = getr16(cpu_rm);
        if (cpu_state.sse_xmm) {
            SSE_ENTER();
            cpu_state.XMM[cpu_reg].w[imm & 7] = rm;
        }
        else {
            MMX_ENTER();
            MMX_REG *dst;
            dst = MMX_GETREGP(cpu_reg);
            dst->w[imm & 3] = rm;
            MMX_SETEXP(cpu_reg);
        }
        CLOCK_CYCLES(1);
    } else {
        uint16_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmemw(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        if (cpu_state.sse_xmm) {
            SSE_ENTER();
            cpu_state.XMM[cpu_reg].w[imm & 7] = src;
        }
        else {
            MMX_ENTER();
            MMX_REG *dst;
            dst = MMX_GETREGP(cpu_reg);
            dst->w[imm & 3] = src;
            MMX_SETEXP(cpu_reg);
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opPEXTRW_xmm_w_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        if (cpu_state.sse_xmm)
        {
            SSE_ENTER();
            setr32(cpu_reg, cpu_state.XMM[cpu_rm].w[imm & 7]);
        }
        else {
            MMX_ENTER();
            MMX_REG src;
            src = MMX_GETREG(cpu_rm);
            setr32(cpu_reg, src.w[imm & 3]);
        }
        CLOCK_CYCLES(1);
    }
    
    return 0;
}

static int
opPEXTRW_xmm_w_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        if (cpu_state.sse_xmm)
        {
            SSE_ENTER();
            setr32(cpu_reg, cpu_state.XMM[cpu_rm].w[imm & 7]);
        }
        else {
            MMX_ENTER();
            MMX_REG src;
            src = MMX_GETREG(cpu_rm);
            setr32(cpu_reg, src.w[imm & 3]);
        }
        CLOCK_CYCLES(1);
    }
    
    return 0;
}

static int
opSHUFPS_xmm_w_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opSHUFPD_xmm_w_a16(fetchdat);

    SSE_REG src, tmp;

    SSE_ENTER();

    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    uint8_t imm = getbyte();
    tmp.l[0]          = cpu_state.XMM[cpu_reg].l[imm & 3];
    tmp.l[1]          = cpu_state.XMM[cpu_reg].l[(imm >> 2) & 3];
    tmp.l[2]          = src.l[(imm >> 4) & 3];
    tmp.l[3]          = src.l[(imm >> 6) & 3];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opSHUFPS_xmm_w_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opSHUFPD_xmm_w_a32(fetchdat);

    SSE_REG src, tmp;

    SSE_ENTER();

    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    uint8_t imm = getbyte();
    tmp.l[0]          = cpu_state.XMM[cpu_reg].l[imm & 3];
    tmp.l[1]          = cpu_state.XMM[cpu_reg].l[(imm >> 2) & 3];
    tmp.l[2]          = src.l[(imm >> 4) & 3];
    tmp.l[3]          = src.l[(imm >> 6) & 3];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;

    return 0;
}

static int
opPMOVMSKB_l_xmm_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        uint32_t result = 0;
        if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm) {
            SSE_ENTER();
            if (cpu_state.XMM[cpu_rm].b[0] & (1 << 7))
                result |= 1;
            if (cpu_state.XMM[cpu_rm].b[1] & (1 << 7))
                result |= 2;
            if (cpu_state.XMM[cpu_rm].b[2] & (1 << 7))
                result |= 4;
            if (cpu_state.XMM[cpu_rm].b[3] & (1 << 7))
                result |= 8;
            if (cpu_state.XMM[cpu_rm].b[4] & (1 << 7))
                result |= 0x10;
            if (cpu_state.XMM[cpu_rm].b[5] & (1 << 7))
                result |= 0x20;
            if (cpu_state.XMM[cpu_rm].b[6] & (1 << 7))
                result |= 0x40;
            if (cpu_state.XMM[cpu_rm].b[7] & (1 << 7))
                result |= 0x80;
            if (cpu_state.XMM[cpu_rm].b[8] & (1 << 7))
                result |= 0x100;
            if (cpu_state.XMM[cpu_rm].b[9] & (1 << 7))
                result |= 0x200;
            if (cpu_state.XMM[cpu_rm].b[10] & (1 << 7))
                result |= 0x400;
            if (cpu_state.XMM[cpu_rm].b[11] & (1 << 7))
                result |= 0x800;
            if (cpu_state.XMM[cpu_rm].b[12] & (1 << 7))
                result |= 0x1000;
            if (cpu_state.XMM[cpu_rm].b[13] & (1 << 7))
                result |= 0x2000;
            if (cpu_state.XMM[cpu_rm].b[14] & (1 << 7))
                result |= 0x4000;
            if (cpu_state.XMM[cpu_rm].b[15] & (1 << 7))
                result |= 0x8000;
        } else {
            MMX_ENTER();
            MMX_REG src;
            src = MMX_GETREG(cpu_rm);
            if (src.b[0] & (1 << 7))
                result |= 1;
            if (src.b[1] & (1 << 7))
                result |= 2;
            if (src.b[2] & (1 << 7))
                result |= 4;
            if (src.b[3] & (1 << 7))
                result |= 8;
            if (src.b[4] & (1 << 7))
                result |= 0x10;
            if (src.b[5] & (1 << 7))
                result |= 0x20;
            if (src.b[6] & (1 << 7))
                result |= 0x40;
            if (src.b[7] & (1 << 7))
                result |= 0x80;
        }
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }

    return 0;
}

static int
opPMOVMSKB_l_xmm_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        uint32_t result = 0;
        if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm) {
            SSE_ENTER();
            if (cpu_state.XMM[cpu_rm].b[0] & (1 << 7))
                result |= 1;
            if (cpu_state.XMM[cpu_rm].b[1] & (1 << 7))
                result |= 2;
            if (cpu_state.XMM[cpu_rm].b[2] & (1 << 7))
                result |= 4;
            if (cpu_state.XMM[cpu_rm].b[3] & (1 << 7))
                result |= 8;
            if (cpu_state.XMM[cpu_rm].b[4] & (1 << 7))
                result |= 0x10;
            if (cpu_state.XMM[cpu_rm].b[5] & (1 << 7))
                result |= 0x20;
            if (cpu_state.XMM[cpu_rm].b[6] & (1 << 7))
                result |= 0x40;
            if (cpu_state.XMM[cpu_rm].b[7] & (1 << 7))
                result |= 0x80;
            if (cpu_state.XMM[cpu_rm].b[8] & (1 << 7))
                result |= 0x100;
            if (cpu_state.XMM[cpu_rm].b[9] & (1 << 7))
                result |= 0x200;
            if (cpu_state.XMM[cpu_rm].b[10] & (1 << 7))
                result |= 0x400;
            if (cpu_state.XMM[cpu_rm].b[11] & (1 << 7))
                result |= 0x800;
            if (cpu_state.XMM[cpu_rm].b[12] & (1 << 7))
                result |= 0x1000;
            if (cpu_state.XMM[cpu_rm].b[13] & (1 << 7))
                result |= 0x2000;
            if (cpu_state.XMM[cpu_rm].b[14] & (1 << 7))
                result |= 0x4000;
            if (cpu_state.XMM[cpu_rm].b[15] & (1 << 7))
                result |= 0x8000;
        } else {
            MMX_ENTER();
            MMX_REG src;
            src = MMX_GETREG(cpu_rm);
            if (src.b[0] & (1 << 7))
                result |= 1;
            if (src.b[1] & (1 << 7))
                result |= 2;
            if (src.b[2] & (1 << 7))
                result |= 4;
            if (src.b[3] & (1 << 7))
                result |= 8;
            if (src.b[4] & (1 << 7))
                result |= 0x10;
            if (src.b[5] & (1 << 7))
                result |= 0x20;
            if (src.b[6] & (1 << 7))
                result |= 0x40;
            if (src.b[7] & (1 << 7))
                result |= 0x80;
        }
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }

    return 0;
}

static int
opMOVNTQ_q_mm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVNTDQ_a16(fetchdat);

    MMX_ENTER();

    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);

    MMX_REG src;

    src = MMX_GETREG(cpu_reg);

    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
    writememq(easeg, cpu_state.eaaddr, src.q);
    if (cpu_state.abrt)
        return 1;

    CLOCK_CYCLES(2);

    return 0;
}

static int
opMOVNTQ_q_mm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMOVNTDQ_a32(fetchdat);

    MMX_ENTER();

    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);

    MMX_REG src;

    src = MMX_GETREG(cpu_reg);

    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
    writememq(easeg, cpu_state.eaaddr, src.q);
    if (cpu_state.abrt)
        return 1;

    CLOCK_CYCLES(2);

    return 0;
}

static int
opMASKMOVQ_l_mm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMASKMOVDQU_a16(fetchdat);

    MMX_ENTER();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        MMX_REG src;
        MMX_REG dst;
        dst = MMX_GETREG(cpu_reg);
        src = MMX_GETREG(cpu_rm);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        if (src.b[0] & (1 << 7)) {
            writememb(easeg, DI, dst.b[0]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[1] & (1 << 7)) {
            writememb(easeg, DI + 1, dst.b[1]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[2] & (1 << 7)) {
            writememb(easeg, DI + 2, dst.b[2]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[3] & (1 << 7)) {
            writememb(easeg, DI + 3, dst.b[3]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[4] & (1 << 7)) {
            writememb(easeg, DI + 4, dst.b[4]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[5] & (1 << 7)) {
            writememb(easeg, DI + 5, dst.b[5]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[6] & (1 << 7)) {
            writememb(easeg, DI + 6, dst.b[6]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[7] & (1 << 7)) {
            writememb(easeg, DI + 7, dst.b[7]);
            if (cpu_state.abrt)
                return 1;
        }
        CLOCK_CYCLES(1);
    }

    return 0;
}

static int
opMASKMOVQ_l_mm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && cpu_state.sse_xmm)
        return opMASKMOVDQU_a32(fetchdat);

    MMX_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        MMX_REG src;
        MMX_REG dst;
        dst = MMX_GETREG(cpu_reg);
        src = MMX_GETREG(cpu_rm);
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        if (src.b[0] & (1 << 7)) {
            writememb(easeg, EDI, dst.b[0]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[1] & (1 << 7)) {
            writememb(easeg, EDI + 1, dst.b[1]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[2] & (1 << 7)) {
            writememb(easeg, EDI + 2, dst.b[2]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[3] & (1 << 7)) {
            writememb(easeg, EDI + 3, dst.b[3]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[4] & (1 << 7)) {
            writememb(easeg, EDI + 4, dst.b[4]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[5] & (1 << 7)) {
            writememb(easeg, EDI + 5, dst.b[5]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[6] & (1 << 7)) {
            writememb(easeg, EDI + 6, dst.b[6]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[7] & (1 << 7)) {
            writememb(easeg, EDI + 7, dst.b[7]);
            if (cpu_state.abrt)
                return 1;
        }
        CLOCK_CYCLES(1);
    }

    return 0;
}
