/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Local APIC (Advanced Programmable
 *          Interrupt Controller) implementation.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef EMU_APIC_H
#define EMU_APIC_H

#include <stdint.h>

/* Default APIC MMIO base address. */
#define APIC_DEFAULT_BASE 0xFEE00000

/* APIC register offsets. */
#define APIC_REG_ID          0x020
#define APIC_REG_VERSION     0x030
#define APIC_REG_TPR         0x080
#define APIC_REG_APR         0x090
#define APIC_REG_PPR         0x0A0
#define APIC_REG_EOI         0x0B0
#define APIC_REG_LDR         0x0D0
#define APIC_REG_DFR         0x0E0
#define APIC_REG_SVR         0x0F0
#define APIC_REG_ISR_BASE    0x100
#define APIC_REG_TMR_BASE    0x180
#define APIC_REG_IRR_BASE    0x200
#define APIC_REG_ESR         0x280
#define APIC_REG_ICR_LOW     0x300
#define APIC_REG_ICR_HIGH    0x310
#define APIC_REG_LVT_TIMER   0x320
#define APIC_REG_LVT_THERMAL 0x330
#define APIC_REG_LVT_PERF    0x340
#define APIC_REG_LVT_LINT0   0x350
#define APIC_REG_LVT_LINT1   0x360
#define APIC_REG_LVT_ERROR   0x370
#define APIC_REG_TIMER_ICR   0x380
#define APIC_REG_TIMER_CCR   0x390
#define APIC_REG_TIMER_DCR   0x3E0

/* SVR bits. */
#define APIC_SVR_ENABLE (1 << 8)

/* LVT delivery modes (bits 10:8). */
#define APIC_LVT_DM_FIXED  0x000
#define APIC_LVT_DM_SMI    0x200
#define APIC_LVT_DM_NMI    0x400
#define APIC_LVT_DM_INIT   0x500
#define APIC_LVT_DM_EXTINT 0x700
#define APIC_LVT_DM_MASK   0x700

/* LVT bits. */
#define APIC_LVT_MASKED         (1 << 16)
#define APIC_LVT_TIMER_PERIODIC (1 << 17)

/* ICR delivery modes (bits 10:8). */
#define APIC_ICR_DM_FIXED  0x000
#define APIC_ICR_DM_LOWPRI 0x100
#define APIC_ICR_DM_SMI    0x200
#define APIC_ICR_DM_NMI    0x400
#define APIC_ICR_DM_INIT   0x500
#define APIC_ICR_DM_SIPI   0x600
#define APIC_ICR_DM_MASK   0x700

/* ICR destination shorthand (bits 19:18). */
#define APIC_ICR_DSH_NONE   0x00000 /* No shorthand. */
#define APIC_ICR_DSH_SELF   0x40000 /* Self. */
#define APIC_ICR_DSH_ALL    0x80000 /* All including self. */
#define APIC_ICR_DSH_ALLNOT 0xC0000 /* All excluding self. */
#define APIC_ICR_DSH_MASK   0xC0000

/* ICR bits. */
#define APIC_ICR_DESTMOD (1 << 11) /* Destination mode: 0=physical, 1=logical. */
#define APIC_ICR_LEVEL   (1 << 14) /* Level: 0=de-assert, 1=assert. */
#define APIC_ICR_TRIGGER (1 << 15) /* Trigger mode: 0=edge, 1=level. */

/* IA32_APIC_BASE MSR bits. */
#define APIC_MSR_BSP       (1 << 8)
#define APIC_MSR_ENABLE    (1 << 11)
#define APIC_MSR_BASE_MASK 0x000FFFFF000ULL

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration. */
typedef struct apic_t apic_t;

/* Maximum number of CPUs supported. */
#define APIC_MAX_CPUS 2

/* Initialize the Local APIC for a specific CPU.
   cpu_id 0 = BSP; called from cpu_set() / cpu_smp_init(). */
extern void apic_init_cpu(int cpu_id);

/* Initialize the Local APIC for the BSP (backward compat wrapper). */
extern void apic_init(void);

/* Re-add the APIC MMIO mapping after mem_reset() has cleared it.
   Must be called after mem_reset() for P6-class CPUs. */
extern void apic_reset_mapping(void);

/* Reset the Local APIC to power-on defaults. */
extern void apic_reset(void);

/* Shut down and free the Local APIC. */
extern void apic_close(void);

/* Returns 1 if the Local APIC is enabled (SVR bit 8 set and
   MSR global enable set). */
extern int apic_enabled(void);

/* Check if the APIC has a pending interrupt that can be delivered.
   Returns the vector number (0-255) or -1 if none. */
extern int apic_get_interrupt(void);

/* Raise an interrupt in the APIC's IRR for the given vector (current CPU). */
extern void apic_set_irr(int vector);

/* Raise an interrupt in a specific CPU's APIC IRR.
   Used by IPI delivery and I/O APIC routing. */
extern void apic_set_irr_cpu(int cpu_id, int vector);

/* Signal LINT0 (connected to PIC in virtual wire mode). */
extern void apic_lint0_raise(void);

/* Signal LINT1 (NMI). */
extern void apic_lint1_raise(void);

/* Read/write the IA32_APIC_BASE MSR.
   Returns the current value or sets a new value. */
extern uint64_t apic_read_msr(void);
extern void     apic_write_msr(uint64_t val);

/* Returns non-zero if APIC is present (CPU supports it). */
extern int apic_present(void);

/* Returns non-zero if APIC has a pending interrupt (fast check
   for block-end decisions in the execution loop). */
extern int apic_int_pending(void);

/* Returns non-zero if a specific CPU's APIC has a pending interrupt.
   Used by the SMP scheduler to decide whether to wake a halted CPU. */
extern int apic_int_pending_cpu(int cpu_id);

/* Get the APIC state for a specific CPU.
   Returns NULL if that CPU's APIC is not initialized. */
extern apic_t *apic_get_cpu(int cpu_id);

/* Check if a specific CPU's APIC matches the given logical destination.
   Returns 1 if the APIC is a target for this logical destination.
   Used by I/O APIC routing. */
extern int apic_match_logical_dest(int cpu_id, uint8_t dest);

/* Switch the active APIC MMIO mapping to the given CPU.
   Called during context switch to ensure the correct APIC is mapped. */
extern void apic_switch_cpu(int cpu_id);

/* Pointer to the BSP's APIC state (backward compat; == apics[0]). */
extern apic_t *apic;

/* Array of per-CPU APIC states. */
extern apic_t *apics[APIC_MAX_CPUS];

/* Accessors for opaque apic_t fields (used by AP trace in 386_dynarec.c). */
extern uint32_t apic_get_id(int cpu_id);
extern uint32_t apic_get_svr(int cpu_id);
extern uint32_t apic_get_tpr(int cpu_id);
extern uint32_t apic_get_lvt_lint0(int cpu_id);
extern uint32_t apic_get_lvt_lint1(int cpu_id);
extern uint32_t apic_get_icr_low(int cpu_id);
extern uint32_t apic_get_icr_high(int cpu_id);
extern uint64_t apic_get_msr_value(int cpu_id);
extern int      apic_get_highest_irr(int cpu_id);
extern int      apic_get_highest_isr(int cpu_id);

#ifdef __cplusplus
}
#endif

#endif /* EMU_APIC_H */
