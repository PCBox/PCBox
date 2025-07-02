/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opMOVSD_f_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[0];
        CLOCK_CYCLES(1);
    } else {
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[0] = dst;
        cpu_state.XMM[cpu_reg].q[1] = 0;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVSD_f_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[0];
        CLOCK_CYCLES(1);
    } else {
        uint64_t dst;

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[0] = dst;
        cpu_state.XMM[cpu_reg].q[1] = 0;

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVSD_xmm_f_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    } else {
        uint64_t rm = cpu_state.XMM[cpu_reg].q[0];

        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, rm);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVSD_xmm_f_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    } else {
        uint64_t rm = cpu_state.XMM[cpu_reg].q[0];

        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, rm);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVLPD_f_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    uint64_t dst;

    SEG_CHECK_READ(cpu_state.ea_seg);
    dst = readmemq(easeg, cpu_state.eaaddr);
    if (cpu_state.abrt)
        return 1;
    cpu_state.XMM[cpu_reg].q[0] = dst;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opMOVLPD_f_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    uint64_t dst;

    SEG_CHECK_READ(cpu_state.ea_seg);
    dst = readmemq(easeg, cpu_state.eaaddr);
    if (cpu_state.abrt)
        return 1;
    cpu_state.XMM[cpu_reg].q[0] = dst;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opMOVLPD_xmm_f_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVLPD_xmm_f_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opUNPCKLPD_f_xmm_a16(uint32_t fetchdat)
{
    SSE_REG src, tmp;
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    tmp.q[0] = cpu_state.XMM[cpu_reg].q[0];
    tmp.q[1] = src.q[0];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opUNPCKLPD_f_xmm_a32(uint32_t fetchdat)
{
    SSE_REG src, tmp;
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    tmp.q[0] = cpu_state.XMM[cpu_reg].q[0];
    tmp.q[1] = src.q[0];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opUNPCKHPD_f_xmm_a16(uint32_t fetchdat)
{
    SSE_REG src, tmp;
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    tmp.q[0] = cpu_state.XMM[cpu_reg].q[1];
    tmp.q[1] = src.q[1];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opUNPCKHPD_f_xmm_a32(uint32_t fetchdat)
{
    SSE_REG src, tmp;
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    tmp.q[0] = cpu_state.XMM[cpu_reg].q[1];
    tmp.q[1] = src.q[1];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opMOVHPD_f_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    uint64_t dst;

    SEG_CHECK_READ(cpu_state.ea_seg);
    dst = readmemq(easeg, cpu_state.eaaddr);
    if (cpu_state.abrt)
        return 1;
    cpu_state.XMM[cpu_reg].q[1] = dst;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opMOVHPD_f_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    uint64_t dst;

    SEG_CHECK_READ(cpu_state.ea_seg);
    dst = readmemq(easeg, cpu_state.eaaddr);
    if (cpu_state.abrt)
        return 1;
    cpu_state.XMM[cpu_reg].q[1] = dst;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opMOVHPD_xmm_f_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[1] = cpu_state.XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[1]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVHPD_xmm_f_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[1] = cpu_state.XMM[cpu_reg].q[0];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[1]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVMSKPD_l_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        uint32_t result = 0;
        if (cpu_state.XMM[cpu_rm].q[0] & (1ULL << 63))
            result |= 1;
        if (cpu_state.XMM[cpu_rm].q[1] & (1ULL << 63))
            result |= 2;
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int
opMOVMSKPD_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        uint32_t result = 0;
        if (cpu_state.XMM[cpu_rm].q[0] & (1ULL << 63))
            result |= 1;
        if (cpu_state.XMM[cpu_rm].q[1] & (1ULL << 63))
            result |= 2;
        setr32(cpu_reg, result);
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int
opMOVD_l_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] = getr32(cpu_rm);
        cpu_state.XMM[cpu_reg].l[1] = 0;
        cpu_state.XMM[cpu_reg].l[2] = 0;
        cpu_state.XMM[cpu_reg].l[3] = 0;
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
opMOVD_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] = getr32(cpu_rm);
        cpu_state.XMM[cpu_reg].l[1] = 0;
        cpu_state.XMM[cpu_reg].l[2] = 0;
        cpu_state.XMM[cpu_reg].l[3] = 0;
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
opMOVDQA_l_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[0];
        cpu_state.XMM[cpu_reg].q[1] = cpu_state.XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    } else {
        uint64_t dst[2];

        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 8);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[0] = dst[0];
        cpu_state.XMM[cpu_reg].q[1] = dst[1];

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVDQA_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[0];
        cpu_state.XMM[cpu_reg].q[1] = cpu_state.XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    } else {
        uint64_t dst[2];

        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 8);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[0] = dst[0];
        cpu_state.XMM[cpu_reg].q[1] = dst[1];

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVDQU_l_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[0];
        cpu_state.XMM[cpu_reg].q[1] = cpu_state.XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    } else {
        uint64_t dst[2];

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 8);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[0] = dst[0];
        cpu_state.XMM[cpu_reg].q[1] = dst[1];

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVDQU_l_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[0];
        cpu_state.XMM[cpu_reg].q[1] = cpu_state.XMM[cpu_rm].q[1];
        CLOCK_CYCLES(1);
    } else {
        uint64_t dst[2];

        SEG_CHECK_READ(cpu_state.ea_seg);
        dst[0] = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        dst[1] = readmemq(easeg, cpu_state.eaaddr + 8);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[0] = dst[0];
        cpu_state.XMM[cpu_reg].q[1] = dst[1];

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opPSHUFLW_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;
    SSE_GETSRC();
    tmp.q[1] = src.q[1];
    tmp.w[0] = src.w[imm & 3];
    tmp.w[1] = src.w[(imm >> 2) & 3];
    tmp.w[2] = src.w[(imm >> 4) & 3];
    tmp.w[3] = src.w[(imm >> 6) & 3];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPSHUFLW_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;
    SSE_GETSRC();
    tmp.q[1] = src.q[1];
    tmp.w[0] = src.w[imm & 3];
    tmp.w[1] = src.w[(imm >> 2) & 3];
    tmp.w[2] = src.w[(imm >> 4) & 3];
    tmp.w[3] = src.w[(imm >> 6) & 3];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPSHUFHW_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;
    fetch_ea_16(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;
    SSE_GETSRC();
    tmp.q[0] = src.q[0];
    tmp.w[4] = src.w[(imm & 3) + 4];
    tmp.w[5] = src.w[((imm >> 2) & 3) + 4];
    tmp.w[6] = src.w[((imm >> 4) & 3) + 4];
    tmp.w[7] = src.w[((imm >> 6) & 3) + 4];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];

    return 0;
}

static int
opPSHUFHW_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    SSE_REG src, tmp;
    fetch_ea_32(fetchdat);
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;
    SSE_GETSRC();
    tmp.q[0] = src.q[0];
    tmp.w[4] = src.w[(imm & 3) + 4];
    tmp.w[5] = src.w[((imm >> 2) & 3) + 4];
    tmp.w[6] = src.w[((imm >> 4) & 3) + 4];
    tmp.w[7] = src.w[((imm >> 6) & 3) + 4];

    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPSHUFD_a16(uint32_t fetchdat)
{
    SSE_REG src, tmp;
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;
    tmp.l[0]          = src.l[imm & 3];
    tmp.l[1]          = src.l[(imm >> 2) & 3];
    tmp.l[2]          = src.l[(imm >> 4) & 3];
    tmp.l[3]          = src.l[(imm >> 6) & 3];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opPSHUFD_a32(uint32_t fetchdat)
{
    SSE_REG src, tmp;
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    uint8_t imm = getbyte();
    if (cpu_state.abrt)
        return 1;
    tmp.l[0]          = src.l[imm & 3];
    tmp.l[1]          = src.l[(imm >> 2) & 3];
    tmp.l[2]          = src.l[(imm >> 4) & 3];
    tmp.l[3]          = src.l[(imm >> 6) & 3];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opMOVD_xmm_l_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        setr32(cpu_rm, cpu_state.XMM[cpu_reg].l[0]);
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
        writememl(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].l[0]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVD_xmm_l_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        setr32(cpu_rm, cpu_state.XMM[cpu_reg].l[0]);
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
        writememl(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].l[0]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVQ_xmm_q_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        cpu_state.XMM[cpu_rm].q[1] = 0;
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}
static int
opMOVQ_xmm_q_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        cpu_state.XMM[cpu_rm].q[1] = 0;
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVDQA_xmm_q_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        cpu_state.XMM[cpu_rm].q[1] = cpu_state.XMM[cpu_reg].q[1];
        CLOCK_CYCLES(1);
    } else {
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }

        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 15);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
        writememq(easeg, cpu_state.eaaddr + 8, cpu_state.XMM[cpu_reg].q[1]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVDQA_xmm_q_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        cpu_state.XMM[cpu_rm].q[1] = cpu_state.XMM[cpu_reg].q[1];
        CLOCK_CYCLES(1);
    } else {
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }

        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 15);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
        writememq(easeg, cpu_state.eaaddr + 8, cpu_state.XMM[cpu_reg].q[1]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVDQU_xmm_q_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        cpu_state.XMM[cpu_rm].q[1] = cpu_state.XMM[cpu_reg].q[1];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 15);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
        writememq(easeg, cpu_state.eaaddr + 8, cpu_state.XMM[cpu_reg].q[1]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVDQU_xmm_q_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_rm].q[0] = cpu_state.XMM[cpu_reg].q[0];
        cpu_state.XMM[cpu_rm].q[1] = cpu_state.XMM[cpu_reg].q[1];
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 15);
        writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
        writememq(easeg, cpu_state.eaaddr + 8, cpu_state.XMM[cpu_reg].q[1]);
        if (cpu_state.abrt)
            return 1;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVNTI_a16(uint32_t fetchdat)
{
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
    writememl(easeg, cpu_state.eaaddr, getr32(cpu_reg));
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(1);
    return 0;
}

static int
opMOVNTI_a32(uint32_t fetchdat)
{
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
    writememl(easeg, cpu_state.eaaddr, getr32(cpu_reg));
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(1);
    return 0;
}

static int
opSHUFPD_xmm_w_a16(uint32_t fetchdat)
{
    SSE_REG src,tmp;
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    SSE_GETSRC();
    uint8_t imm = getbyte();
    tmp.q[0] = cpu_state.XMM[cpu_reg].q[imm & 1];
    tmp.q[1] = src.q[(imm >> 1) & 1];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opSHUFPD_xmm_w_a32(uint32_t fetchdat)
{
    SSE_REG src,tmp;
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    SSE_GETSRC();
    uint8_t imm = getbyte();
    tmp.q[0] = cpu_state.XMM[cpu_reg].q[imm & 1];
    tmp.q[1] = src.q[(imm >> 1) & 1];
    cpu_state.XMM[cpu_reg].q[0] = tmp.q[0];
    cpu_state.XMM[cpu_reg].q[1] = tmp.q[1];
    return 0;
}

static int
opMOVQ_q_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[0];
        cpu_state.XMM[cpu_reg].q[1] = 0;
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
        cpu_state.XMM[cpu_reg].q[0] = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[1] = 0;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVQ_q_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].q[0] = cpu_state.XMM[cpu_rm].q[0];
        cpu_state.XMM[cpu_reg].q[1] = 0;
        CLOCK_CYCLES(1);
    } else {
        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
        cpu_state.XMM[cpu_reg].q[0] = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[1] = 0;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVQ2DQ_a16(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    MMX_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] = src.q;
    cpu_state.XMM[cpu_reg].q[1] = 0;
    CLOCK_CYCLES(1);
    return 0;
}

static int
opMOVQ2DQ_a32(uint32_t fetchdat)
{
    MMX_REG src;
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    MMX_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] = src.q;
    cpu_state.XMM[cpu_reg].q[1] = 0;
    CLOCK_CYCLES(1);
    return 0;
}

static int
opMOVDQ2Q_a16(uint32_t fetchdat)
{
    MMX_REG *dst;
    MMX_ENTER();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    dst = MMX_GETREGP(cpu_reg);

    dst->q = cpu_state.XMM[cpu_rm].q[0];
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);
    return 0;
}

static int
opMOVDQ2Q_a32(uint32_t fetchdat)
{
    MMX_REG *dst;
    MMX_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    dst = MMX_GETREGP(cpu_reg);

    dst->q = cpu_state.XMM[cpu_rm].q[0];
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);
    return 0;
}

static int
opMOVNTDQ_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);

    if (cpu_state.eaaddr & 0xf) {
        x86gpf(NULL, 0);
        if (cpu_state.abrt)
            return 1;
    }

    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 15);
    writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
    writememq(easeg, cpu_state.eaaddr + 8, cpu_state.XMM[cpu_reg].q[1]);
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opMOVNTDQ_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);

    if (cpu_state.eaaddr & 0xf) {
        x86gpf(NULL, 0);
        if (cpu_state.abrt)
            return 1;
    }

    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE_COMMON(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 15);
    writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
    writememq(easeg, cpu_state.eaaddr + 8, cpu_state.XMM[cpu_reg].q[1]);
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opMASKMOVDQU_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        SEG_CHECK_WRITE(cpu_state.ea_seg);
        SSE_REG src, dst;
        dst = cpu_state.XMM[cpu_reg];
        src = cpu_state.XMM[cpu_rm];
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
        if (src.b[8] & (1 << 7)) {
            writememb(easeg, DI + 8, dst.b[8]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[9] & (1 << 7)) {
            writememb(easeg, DI + 9, dst.b[9]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[10] & (1 << 7)) {
            writememb(easeg, DI + 10, dst.b[10]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[11] & (1 << 7)) {
            writememb(easeg, DI + 11, dst.b[11]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[12] & (1 << 7)) {
            writememb(easeg, DI + 12, dst.b[12]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[13] & (1 << 7)) {
            writememb(easeg, DI + 13, dst.b[13]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[14] & (1 << 7)) {
            writememb(easeg, DI + 14, dst.b[14]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[15] & (1 << 7)) {
            writememb(easeg, DI + 15, dst.b[15]);
            if (cpu_state.abrt)
                return 1;
        }
        CLOCK_CYCLES(1);
    }
    return 0;
}

static int
opMASKMOVDQU_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    if (cpu_mod == 3) {
        SSE_REG src, dst;
        dst = cpu_state.XMM[cpu_reg];
        src = cpu_state.XMM[cpu_rm];
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
        if (src.b[8] & (1 << 7)) {
            writememb(easeg, EDI + 8, dst.b[8]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[9] & (1 << 7)) {
            writememb(easeg, EDI + 9, dst.b[9]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[10] & (1 << 7)) {
            writememb(easeg, EDI + 10, dst.b[10]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[11] & (1 << 7)) {
            writememb(easeg, EDI + 11, dst.b[11]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[12] & (1 << 7)) {
            writememb(easeg, EDI + 12, dst.b[12]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[13] & (1 << 7)) {
            writememb(easeg, EDI + 13, dst.b[13]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[14] & (1 << 7)) {
            writememb(easeg, EDI + 14, dst.b[14]);
            if (cpu_state.abrt)
                return 1;
        }
        if (src.b[15] & (1 << 7)) {
            writememb(easeg, EDI + 15, dst.b[15]);
            if (cpu_state.abrt)
                return 1;
        }
        CLOCK_CYCLES(1);
    }
    return 0;
}
