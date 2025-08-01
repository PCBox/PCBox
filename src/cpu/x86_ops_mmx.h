#define SSATB(val)     (((val) < -128) ? -128 : (((val) > 127) ? 127 : (val)))
#define SSATW(val)     (((val) < -32768) ? -32768 : (((val) > 32767) ? 32767 : (val)))
#define USATB(val)     (((val) < 0) ? 0 : (((val) > 255) ? 255 : (val)))
#define USATW(val)     (((val) < 0) ? 0 : (((val) > 65535) ? 65535 : (val)))

#define MMX_GETREGP(r) MMP[r]
#define MMX_GETREG(r)  *(MMP[r])

#define MMX_SETEXP(r) \
    *(MMEP[r]) = 0xffff

#define MMX_GETSRC()                               \
    if (cpu_mod == 3) {                            \
        src = MMX_GETREG(cpu_rm);                  \
        CLOCK_CYCLES(1);                           \
    } else {                                       \
        SEG_CHECK_READ(cpu_state.ea_seg);          \
        src.q = readmemq(easeg, cpu_state.eaaddr); \
        if (cpu_state.abrt)                        \
            return 1;                              \
        CLOCK_CYCLES(2);                           \
    }

#define MMX_GETSRC_SSE()                           \
    if (cpu_mod == 3) {                            \
        src = MMX_GETREG(cpu_rm);                  \
        CLOCK_CYCLES(1);                           \
    } else {                                       \
        SEG_CHECK_READ(cpu_state.ea_seg);          \
        if (cpu_state.eaaddr & 0xf) {              \
            x86gpf(NULL, 0);                       \
            if (cpu_state.abrt)                    \
                return 1;                          \
        }                                          \
        src.q = readmemq(easeg, cpu_state.eaaddr); \
        if (cpu_state.abrt)                        \
            return 1;                              \
        CLOCK_CYCLES(2);                           \
    }

#define SSE_GETSRC32()                                    \
    if (cpu_mod == 3) {                                   \
        src = cpu_state.XMM[cpu_rm];                      \
        CLOCK_CYCLES(1);                                  \
    } else {                                              \
        SEG_CHECK_READ(cpu_state.ea_seg);                 \
        src.l[0] = readmeml(easeg, cpu_state.eaaddr);     \
        if (cpu_state.abrt)                               \
            return 1;                                     \
        src.l[1] = 0;                                     \
        src.q[1] = 0;                                     \
        CLOCK_CYCLES(2);                                  \
    }

#define SSE_GETSRC64()                                    \
    if (cpu_mod == 3) {                                   \
        src = cpu_state.XMM[cpu_rm];                      \
        CLOCK_CYCLES(1);                                  \
    } else {                                              \
        SEG_CHECK_READ(cpu_state.ea_seg);                 \
        src.q[0] = readmemq(easeg, cpu_state.eaaddr);     \
        if (cpu_state.abrt)                               \
            return 1;                                     \
        src.q[1] = 0;                                     \
        CLOCK_CYCLES(2);                                  \
    }

#define SSE_GETSRC()                                      \
    if (cpu_mod == 3) {                                   \
        src = cpu_state.XMM[cpu_rm];                      \
        CLOCK_CYCLES(1);                                  \
    } else {                                              \
        SEG_CHECK_READ(cpu_state.ea_seg);                 \
        if (cpu_state.eaaddr & 0xf) {                     \
            x86gpf(NULL, 0);                              \
            if (cpu_state.abrt)                           \
                return 1;                                 \
        }                                                 \
        src.q[0] = readmemq(easeg, cpu_state.eaaddr);     \
        if (cpu_state.abrt)                               \
            return 1;                                     \
        src.q[1] = readmemq(easeg, cpu_state.eaaddr + 8); \
        if (cpu_state.abrt)                               \
            return 1;                                     \
        CLOCK_CYCLES(2);                                  \
    }

#define MMX_ENTER()                          \
    if (!cpu_has_feature(CPU_FEATURE_MMX) || (cr0 & 0x4)) { \
        cpu_state.pc = cpu_state.oldpc;      \
        x86illegal();                        \
        return 1;                            \
    }                                        \
    if (cr0 & 0x8) {                         \
        x86_int(7);                          \
        return 1;                            \
    }                                        \
    x87_set_mmx()

#define SSE_ENTER()  \
    if (cr0 & 0x8) { \
        x86_int(7);  \
        return 1;    \
    }                \
    if ((cr0 & 0x4) || !(cr4 & CR4_OSFXSR)) { \
        cpu_state.pc = cpu_state.oldpc;       \
        x86illegal();                         \
        return 1;                             \
    }

static int
opEMMS(UNUSED(uint32_t fetchdat))
{
    if (!cpu_has_feature(CPU_FEATURE_MMX)) {
        cpu_state.pc = cpu_state.oldpc;
        x86illegal();
        return 1;
    }
    if (cr0 & 0xc) {
        x86_int(7);
        return 1;
    }
    x87_emms();
    CLOCK_CYCLES(100); /*Guess*/
    return 0;
}

static struct softfloat_status_t mxcsr_to_softfloat_status_word(void)
{
    struct softfloat_status_t status;
    status.softfloat_exceptionFlags             = 0; // clear exceptions before execution
    status.softfloat_roundingMode               = (cpu_state.mxcsr >> 13) & 3;
    status.softfloat_flush_underflow_to_zero    = ((cpu_state.mxcsr >> 15) & 1) && ((cpu_state.mxcsr >> 11) & 1);
    status.softfloat_suppressException          = 0;
    status.softfloat_exceptionMasks             = (cpu_state.mxcsr >> 7) & 0x3f;
    status.softfloat_denormals_are_zeros        = (cpu_state.mxcsr >> 6) & 1;
    return status;
}

static void softfloat_status_word_to_mxcsr(struct softfloat_status_t status)
{
    cpu_state.mxcsr |= status.softfloat_exceptionFlags & 0x3f;
}