/* SPDX-License-Identifier: GPL-2.0-or-later */
static int
opUCOMISS_xmm_xmm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && sse_xmm)
        return opUCOMISD_xmm_xmm_a16(fetchdat);

    flags_rebuild();
    fetch_ea_16(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    if (cpu_mod == 3) {
        if (isunordered(cpu_state_high.XMM[cpu_reg].f[0], cpu_state_high.XMM[cpu_rm].f[0])) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] > cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (cpu_state_high.XMM[cpu_reg].f[0] < cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] == cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(1);
    } else {
        uint32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float src_real;
        src_real = *(float *) &src;
        if (isunordered(cpu_state_high.XMM[cpu_reg].f[0], src_real)) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] > src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (cpu_state_high.XMM[cpu_reg].f[0] < src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] == src_real) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opUCOMISS_xmm_xmm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && sse_xmm)
        return opUCOMISD_xmm_xmm_a32(fetchdat);

    flags_rebuild();
    fetch_ea_32(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    if (cpu_mod == 3) {
        if (isunordered(cpu_state_high.XMM[cpu_reg].f[0], cpu_state_high.XMM[cpu_rm].f[0])) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] > cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (cpu_state_high.XMM[cpu_reg].f[0] < cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] == cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(1);
    } else {
        uint32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float src_real;
        src_real = *(float *) &src;
        if (isunordered(cpu_state_high.XMM[cpu_reg].f[0], src_real)) {
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] > src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (cpu_state_high.XMM[cpu_reg].f[0] < src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] == src_real) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opCOMISS_xmm_xmm_a16(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && sse_xmm)
        return opCOMISD_xmm_xmm_a16(fetchdat);

    flags_rebuild();
    fetch_ea_16(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    cpu_state_high.mxcsr &= ~1;
    if (cpu_mod == 3) {
        if (isunordered(cpu_state_high.XMM[cpu_reg].f[0], cpu_state_high.XMM[cpu_rm].f[0])) {
            cpu_state_high.mxcsr |= 1;
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
            if(!(cpu_state_high.mxcsr & 0x80))
            {
                if (cr4 & CR4_OSXMMEXCPT)
                    x86_int(0x13);
                ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
            }
        } else if (cpu_state_high.XMM[cpu_reg].f[0] > cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (cpu_state_high.XMM[cpu_reg].f[0] < cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] == cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(1);
    } else {
        uint32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float src_real;
        src_real = *(float *) &src;
        if (isunordered(cpu_state_high.XMM[cpu_reg].f[0], src_real)) {
            cpu_state_high.mxcsr |= 1;
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
            if(!(cpu_state_high.mxcsr & 0x80))
            {
                if (cr4 & CR4_OSXMMEXCPT)
                    x86_int(0x13);
                ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
            }
        } else if (cpu_state_high.XMM[cpu_reg].f[0] > src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (cpu_state_high.XMM[cpu_reg].f[0] < src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] == src_real) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}

static int
opCOMISS_xmm_xmm_a32(uint32_t fetchdat)
{
    if ((cpu_features & CPU_FEATURE_SSE2) && sse_xmm)
        return opCOMISD_xmm_xmm_a32(fetchdat);

    flags_rebuild();
    fetch_ea_32(fetchdat);
    cpu_state.flags &= ~(V_FLAG | A_FLAG | N_FLAG);
    cpu_state_high.mxcsr &= ~1;
    if (cpu_mod == 3) {
        if (isunordered(cpu_state_high.XMM[cpu_reg].f[0], cpu_state_high.XMM[cpu_rm].f[0])) {
            cpu_state_high.mxcsr |= 1;
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
            if(!(cpu_state_high.mxcsr & 0x80))
            {
                if (cr4 & CR4_OSXMMEXCPT)
                    x86_int(0x13);
                ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
            }
        } else if (cpu_state_high.XMM[cpu_reg].f[0] > cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (cpu_state_high.XMM[cpu_reg].f[0] < cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] == cpu_state_high.XMM[cpu_rm].f[0]) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(1);
    } else {
        uint32_t src;

        SEG_CHECK_READ(cpu_state.ea_seg);
        src = readmeml(easeg, cpu_state.eaaddr);
        if (cpu_state.abrt)
            return 1;
        float src_real;
        src_real = *(float *) &src;
        if (isunordered(cpu_state_high.XMM[cpu_reg].f[0], src_real)) {
            cpu_state_high.mxcsr |= 1;
            cpu_state.flags |= Z_FLAG | P_FLAG | C_FLAG;
            if(!(cpu_state_high.mxcsr & 0x80))
            {
                if (cr4 & CR4_OSXMMEXCPT)
                    x86_int(0x13);
                ILLEGAL_ON(!(cr4 & CR4_OSXMMEXCPT));
            }
        } else if (cpu_state_high.XMM[cpu_reg].f[0] > src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG | C_FLAG);
        } else if (cpu_state_high.XMM[cpu_reg].f[0] < src_real) {
            cpu_state.flags &= ~(Z_FLAG | P_FLAG);
            cpu_state.flags |= C_FLAG;
        } else if (cpu_state_high.XMM[cpu_reg].f[0] == src_real) {
            cpu_state.flags &= ~(P_FLAG | C_FLAG);
            cpu_state.flags |= Z_FLAG;
        }
        CLOCK_CYCLES(2);
    }
    return 0;
}
