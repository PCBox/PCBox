static int
opRDTSC(UNUSED(uint32_t fetchdat))
{
    if (!cpu_has_feature(CPU_FEATURE_RDTSC)) {
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 1;
    }
    if ((cr4 & CR4_TSD) && CPL) {
        x86gpf("RDTSC when TSD set and CPL != 0", 0);
        return 1;
    }
#ifdef USE_DYNAREC
    /*
       The block's cycles so far reach the TSC only through update_tsc(), so
       it runs before the read: read first, the value is the one as of the
       last update (the block start or the previous RDTSC), and two RDTSCs
       in one block measure nothing between them.
       A due timer run here can reset the CPU (the 8042 pulse); the nonzero
       return ends the recompiled block, as the OUT ops do.
     */
    if (cpu_use_dynarec) {
        update_tsc();
        if (x86_was_reset)
            return 1;
    }
#endif
    EAX = tsc & 0xffffffff;
    EDX = tsc >> 32;
    CLOCK_CYCLES(1);
    return 0;
}

static int
opRDPMC(UNUSED(uint32_t fetchdat))
{
    /* Willamette has eighteen counters; bit 31 requests a fast low-dword
       read. P6 exposes two counters and does not support that selector bit. */
    int p4 = cpu_s->cpu_type == CPU_PENTIUM4W;
    uint32_t counter = p4 ? ECX & 0x7fffffff : ECX;
    if (counter >= (p4 ? 18u : 2u) || (!(cr4 & CR4_PCE) && (cr0 & 1) && CPL)) {
        x86gpf("RDPMC not allowed", 0);
        return 1;
    }
    /* The 40-bit counter: PerfCtr0/1 (MSR C1h/C2h) on the P6 family and
       the VIA Cyrix III (whose counter 0 is the TSC), CTR0/1 (MSR
       12h/13h) on the others.  */
    if (cpu_s->cpu_type == CPU_CYRIX3S) {
#ifdef USE_DYNAREC
        /* Counter 0 is the TSC: the block's cycles so far first, as RDTSC */
        if (cpu_use_dynarec && !ECX) {
            update_tsc();
            if (x86_was_reset)
                return 1;
        }
#endif
        uint64_t v = ECX ? msr.perfctr[1] : tsc;
        EAX = v & 0xffffffff;
        EDX = (v >> 32) & 0xff;
    } else if (is_p6) {
        /* By counter, not ECX: the P4's fast-read bit 31 would index far past
           the array. A fast read returns the low dword only. */
        EAX = msr.perfctr[counter] & 0xffffffff;
        EDX = (p4 && (ECX & 0x80000000)) ? 0 : ((msr.perfctr[counter] >> 32) & 0xff);
    } else {
        EAX = msr.pmc[ECX] & 0xffffffff;
        EDX = (msr.pmc[ECX] >> 32) & 0xff;
    }
    CLOCK_CYCLES(1);
    return 0;
}
