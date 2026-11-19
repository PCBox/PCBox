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
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
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
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
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
        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
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
        CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
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
    CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
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
    CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
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
    ILLEGAL_ON(cpu_mod == 3);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
    writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opMOVLPD_xmm_f_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
    writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[0]);
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(2);
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
    CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
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
    CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
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
    ILLEGAL_ON(cpu_mod == 3);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
    writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[1]);
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(2);
    return 0;
}

static int
opMOVHPD_xmm_f_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);
    SEG_CHECK_WRITE(cpu_state.ea_seg);
    CHECK_WRITE(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
    writememq(easeg, cpu_state.eaaddr, cpu_state.XMM[cpu_reg].q[1]);
    if (cpu_state.abrt)
        return 1;
    CLOCK_CYCLES(2);
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
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
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
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 3);
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

        if ((easeg + cpu_state.eaaddr) & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }

        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 15);
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

        if ((easeg + cpu_state.eaaddr) & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }

        SEG_CHECK_READ(cpu_state.ea_seg);
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 15);
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
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 15);
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
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 15);
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
        if ((easeg + cpu_state.eaaddr) & 0xf) {
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
        if ((easeg + cpu_state.eaaddr) & 0xf) {
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
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
        uint64_t src = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[0] = src;
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
        CHECK_READ(cpu_state.ea_seg, cpu_state.eaaddr, cpu_state.eaaddr + 7);
        uint64_t src = readmemq(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        cpu_state.XMM[cpu_reg].q[0] = src;
        cpu_state.XMM[cpu_reg].q[1] = 0;
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opMOVQ2DQ_a16(uint32_t fetchdat)
{
    MMX_REG src;
    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    MMX_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] = src.q;
    cpu_state.XMM[cpu_reg].q[1] = 0;
    CLOCK_CYCLES(1);
    MMX_COMMIT();
    return 0;
}

static int
opMOVQ2DQ_a32(uint32_t fetchdat)
{
    MMX_REG src;
    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    MMX_GETSRC();

    cpu_state.XMM[cpu_reg].q[0] = src.q;
    cpu_state.XMM[cpu_reg].q[1] = 0;
    CLOCK_CYCLES(1);
    MMX_COMMIT();
    return 0;
}

static int
opMOVDQ2Q_a16(uint32_t fetchdat)
{
    MMX_REG *dst;
    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    dst = MMX_GETREGP(cpu_reg);

    dst->q = cpu_state.XMM[cpu_rm].q[0];
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);
    MMX_COMMIT();
    return 0;
}

static int
opMOVDQ2Q_a32(uint32_t fetchdat)
{
    MMX_REG *dst;
    SSE_ENTER();
    MMX_CHECK();
    fetch_ea_32(fetchdat);
    ILLEGAL_ON(cpu_mod != 3);
    dst = MMX_GETREGP(cpu_reg);

    dst->q = cpu_state.XMM[cpu_rm].q[0];
    MMX_SETEXP(cpu_reg);
    CLOCK_CYCLES(1);
    MMX_COMMIT();
    return 0;
}

static int
opMOVNTDQ_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    ILLEGAL_ON(cpu_mod == 3);

    if ((easeg + cpu_state.eaaddr) & 0xf) {
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

    if ((easeg + cpu_state.eaaddr) & 0xf) {
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
maskmovdqu(uint32_t modrm, uint32_t offset)
{
    x86seg *seg = cpu_state.ea_seg;
    ILLEGAL_ON(cpu_mod != 3);
    SEG_CHECK_WRITE(seg);
    CHECK_WRITE(seg, offset, offset + 15);
    if (x86_maskmovdqu_store(seg->base + offset, modrm))
        return 1;
    CLOCK_CYCLES(1);
    return 0;
}

static int
opMASKMOVDQU_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    return maskmovdqu(fetchdat, DI);
}

static int
opMASKMOVDQU_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    return maskmovdqu(fetchdat, EDI);
}
