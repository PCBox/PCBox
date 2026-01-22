/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opANDPS_q_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] &= cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] &= cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] &= cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] &= cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
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
        cpu_state.XMM[cpu_reg].l[0] &= dst[0];
        cpu_state.XMM[cpu_reg].l[1] &= dst[1];
        cpu_state.XMM[cpu_reg].l[2] &= dst[2];
        cpu_state.XMM[cpu_reg].l[3] &= dst[3];

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opANDPS_q_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] &= cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] &= cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] &= cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] &= cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
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
        cpu_state.XMM[cpu_reg].l[0] &= dst[0];
        cpu_state.XMM[cpu_reg].l[1] &= dst[1];
        cpu_state.XMM[cpu_reg].l[2] &= dst[2];
        cpu_state.XMM[cpu_reg].l[3] &= dst[3];

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opANDNPS_q_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] = ~cpu_state.XMM[cpu_reg].l[0] & cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] = ~cpu_state.XMM[cpu_reg].l[1] & cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] = ~cpu_state.XMM[cpu_reg].l[2] & cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] = ~cpu_state.XMM[cpu_reg].l[3] & cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
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
        cpu_state.XMM[cpu_reg].l[0] = ~cpu_state.XMM[cpu_reg].l[0] & dst[0];
        cpu_state.XMM[cpu_reg].l[1] = ~cpu_state.XMM[cpu_reg].l[1] & dst[1];
        cpu_state.XMM[cpu_reg].l[2] = ~cpu_state.XMM[cpu_reg].l[2] & dst[2];
        cpu_state.XMM[cpu_reg].l[3] = ~cpu_state.XMM[cpu_reg].l[3] & dst[3];

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opANDNPS_q_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] = ~cpu_state.XMM[cpu_reg].l[0] & cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] = ~cpu_state.XMM[cpu_reg].l[1] & cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] = ~cpu_state.XMM[cpu_reg].l[2] & cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] = ~cpu_state.XMM[cpu_reg].l[3] & cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
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
        cpu_state.XMM[cpu_reg].l[0] = ~cpu_state.XMM[cpu_reg].l[0] & dst[0];
        cpu_state.XMM[cpu_reg].l[1] = ~cpu_state.XMM[cpu_reg].l[1] & dst[1];
        cpu_state.XMM[cpu_reg].l[2] = ~cpu_state.XMM[cpu_reg].l[2] & dst[2];
        cpu_state.XMM[cpu_reg].l[3] = ~cpu_state.XMM[cpu_reg].l[3] & dst[3];

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opORPS_q_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] |= cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] |= cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] |= cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] |= cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
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
        cpu_state.XMM[cpu_reg].l[0] |= dst[0];
        cpu_state.XMM[cpu_reg].l[1] |= dst[1];
        cpu_state.XMM[cpu_reg].l[2] |= dst[2];
        cpu_state.XMM[cpu_reg].l[3] |= dst[3];

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opORPS_q_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] |= cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] |= cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] |= cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] |= cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
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
        cpu_state.XMM[cpu_reg].l[0] |= dst[0];
        cpu_state.XMM[cpu_reg].l[1] |= dst[1];
        cpu_state.XMM[cpu_reg].l[2] |= dst[2];
        cpu_state.XMM[cpu_reg].l[3] |= dst[3];

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opXORPS_q_xmm_a16(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_16(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] ^= cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] ^= cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] ^= cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] ^= cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
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
        cpu_state.XMM[cpu_reg].l[0] ^= dst[0];
        cpu_state.XMM[cpu_reg].l[1] ^= dst[1];
        cpu_state.XMM[cpu_reg].l[2] ^= dst[2];
        cpu_state.XMM[cpu_reg].l[3] ^= dst[3];

        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opXORPS_q_xmm_a32(uint32_t fetchdat)
{
    SSE_ENTER();
    fetch_ea_32(fetchdat);
    if (cpu_mod == 3) {
        cpu_state.XMM[cpu_reg].l[0] ^= cpu_state.XMM[cpu_rm].l[0];
        cpu_state.XMM[cpu_reg].l[1] ^= cpu_state.XMM[cpu_rm].l[1];
        cpu_state.XMM[cpu_reg].l[2] ^= cpu_state.XMM[cpu_rm].l[2];
        cpu_state.XMM[cpu_reg].l[3] ^= cpu_state.XMM[cpu_rm].l[3];
        CLOCK_CYCLES(1);
    } else {
        if (cpu_state.eaaddr & 0xf) {
            x86gpf(NULL, 0);
            if (cpu_state.abrt)
                return 1;
        }
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
        cpu_state.XMM[cpu_reg].l[0] ^= dst[0];
        cpu_state.XMM[cpu_reg].l[1] ^= dst[1];
        cpu_state.XMM[cpu_reg].l[2] ^= dst[2];
        cpu_state.XMM[cpu_reg].l[3] ^= dst[3];

        CLOCK_CYCLES(2);
    }
    return 0;
}
