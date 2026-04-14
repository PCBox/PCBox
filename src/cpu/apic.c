/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Local APIC (Advanced Programmable Interrupt Controller)
 *          implementation for P6-class CPUs.
 *
 *          Implements the full register set per Intel SDM Vol 3A Ch 10:
 *          - APIC ID, Version, TPR, PPR, EOI
 *          - LDR, DFR, SVR (with enable bit)
 *          - ISR, TMR, IRR (256-bit interrupt vectors)
 *          - ICR with full IPI delivery (Fixed, NMI, INIT, SIPI)
 *          - LVT Timer, LINT0, LINT1, Error, Thermal, Perf
 *          - Timer with one-shot and periodic mode
 *          - Per-CPU APIC instances for SMP support
 *
 *          Virtual wire mode: LINT0 as ExtINT (PIC pass-through),
 *          LINT1 as NMI.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/timer.h>
#include <86box/pic.h>
#include <86box/nmi.h>
#include <86box/apic.h>
#include <86box/ioapic.h>
#include <86box/plat_unused.h>
#include "cpu.h"

/* APIC version: P6 family local APIC, version 0x14, max LVT entry 5 (0-based). */
#define APIC_VERSION 0x00050014

/* Timer divide configuration lookup table.
   Index is the 4-bit DCR value (bits 3,1,0 of the register),
   value is the actual divisor. */
static const int apic_timer_divisors[8] = {
    2, 4, 8, 16, 32, 64, 128, 1
};

#ifdef ENABLE_APIC_LOG
int apic_do_log = ENABLE_APIC_LOG;

static void
apic_log(const char *fmt, ...)
{
    va_list ap;

    if (apic_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define apic_log(fmt, ...)
#endif

/*
 * Local APIC state structure.
 */
struct apic_t {
    /* MMIO mapping. */
    mem_mapping_t mem_mapping;

    /* Base address and MSR. */
    uint32_t base_addr;
    uint64_t msr;

    /* APIC registers. */
    uint32_t id;  /* APIC ID (bits 27:24). */
    uint32_t tpr; /* Task Priority Register. */
    uint32_t ldr; /* Logical Destination Register. */
    uint32_t dfr; /* Destination Format Register. */
    uint32_t svr; /* Spurious Interrupt Vector Register. */

    /* 256-bit vector registers (8 x 32-bit words each). */
    uint32_t isr[8]; /* In-Service Register. */
    uint32_t tmr[8]; /* Trigger Mode Register. */
    uint32_t irr[8]; /* Interrupt Request Register. */

    uint32_t esr; /* Error Status Register (stub). */

    /* ICR (stubbed for Phase 1). */
    uint32_t icr_low;
    uint32_t icr_high;

    /* LVT entries. */
    uint32_t lvt_timer;
    uint32_t lvt_thermal;
    uint32_t lvt_perf;
    uint32_t lvt_lint0;
    uint32_t lvt_lint1;
    uint32_t lvt_error;

    /* Timer. */
    uint32_t   timer_icr; /* Initial Count Register. */
    uint32_t   timer_ccr; /* Current Count Register (shadow). */
    uint32_t   timer_dcr; /* Divide Configuration Register. */
    int        timer_div; /* Actual divisor value. */
    pc_timer_t timer;     /* 86Box timer. */
};

/* Per-CPU APIC state array. */
apic_t *apics[APIC_MAX_CPUS] = { NULL };

/* Global pointer to the BSP's APIC state (backward compat). */
apic_t *apic = NULL;

/*
 * Find the highest-priority set bit in a 256-bit vector register.
 * Returns the bit number (0-255) or -1 if no bits are set.
 * Higher vector numbers have higher priority.
 */
static int
apic_find_highest_bit(const uint32_t *reg)
{
    for (int i = 7; i >= 0; i--) {
        if (reg[i]) {
            /* Find highest set bit in this 32-bit word. */
            int      bit = 31;
            uint32_t val = reg[i];
            while (bit >= 0 && !(val & (1u << bit)))
                bit--;
            return (i * 32) + bit;
        }
    }
    return -1;
}

/*
 * Set a bit in a 256-bit vector register.
 */
static void
apic_set_bit(uint32_t *reg, int bit)
{
    reg[bit >> 5] |= (1u << (bit & 31));
}

/*
 * Clear a bit in a 256-bit vector register.
 */
static void
apic_clear_bit(uint32_t *reg, int bit)
{
    reg[bit >> 5] &= ~(1u << (bit & 31));
}

/*
 * Test a bit in a 256-bit vector register.
 */
static int
apic_test_bit(const uint32_t *reg, int bit)
{
    return !!(reg[bit >> 5] & (1u << (bit & 31)));
}

/*
 * Compute the Processor Priority Register (PPR).
 * PPR = max(TPR, highest ISR vector class).
 * The "class" of a vector is its upper 4 bits (vector >> 4).
 */
static uint32_t
apic_get_ppr(apic_t *dev)
{
    int      highest_isr = apic_find_highest_bit(dev->isr);
    uint32_t isr_class   = (highest_isr >= 0) ? (highest_isr & 0xF0) : 0;
    uint32_t tpr_class   = dev->tpr & 0xF0;

    if (tpr_class >= isr_class)
        return dev->tpr;
    else
        return isr_class;
}

/*
 * Get the timer divisor from the DCR register.
 * DCR bits 3, 1, 0 encode the divisor.
 */
static int
apic_get_timer_divisor(uint32_t dcr)
{
    int idx = ((dcr >> 1) & 4) | (dcr & 3);
    return apic_timer_divisors[idx];
}

/*
 * Timer callback: fires when the APIC timer counts down to zero.
 */
static void
apic_timer_callback(void *priv)
{
    apic_t *dev = (apic_t *) priv;
    static int apic_timer_log_count = 0;
    int        cpu_id = -1;

    for (int i = 0; i < APIC_MAX_CPUS; i++) {
        if (apics[i] == dev) {
            cpu_id = i;
            break;
        }
    }

    if (!(dev->svr & APIC_SVR_ENABLE))
        return;

    /* Fire the timer interrupt if not masked. */
    if (!(dev->lvt_timer & APIC_LVT_MASKED)) {
        int vector = dev->lvt_timer & 0xFF;
        if (apic_timer_log_count < 200) {
            apic_timer_log_count++;
            fprintf(stderr,
                    "APIC-TIMER[%d]: cpu=%d vector=%02X active_cpu=%d svr=%08X lvt=%08X icr=%08X tsc=%" PRIu64 "\n",
                    apic_timer_log_count, cpu_id, vector, active_cpu, dev->svr, dev->lvt_timer,
                    dev->timer_icr, tsc);
        }
        apic_log("APIC: Timer interrupt, vector %02X\n", vector);
        apic_set_bit(dev->irr, vector);
        if (cpu_id >= 0)
            cpu_contexts[cpu_id].halted = 0;
    }

    /* In periodic mode, restart the timer. */
    if (dev->lvt_timer & APIC_LVT_TIMER_PERIODIC) {
        if (dev->timer_icr > 0) {
            /* Calculate period in CPU clock cycles:
               period = ICR * divisor.
               Convert to microseconds for timer_on_auto. */
            double period_us = ((double) dev->timer_icr * (double) dev->timer_div)
                / (cpu_busspeed / 1000000.0);
            timer_on_auto(&dev->timer, period_us);
        }
    }
    /* One-shot mode: timer stays stopped. */
}

/*
 * Start (or restart) the APIC timer.
 */
static void
apic_timer_start(apic_t *dev)
{
    static int apic_timer_start_log_count = 0;
    int        cpu_id = -1;

    for (int i = 0; i < APIC_MAX_CPUS; i++) {
        if (apics[i] == dev) {
            cpu_id = i;
            break;
        }
    }

    if (dev->timer_icr == 0) {
        timer_disable(&dev->timer);
        return;
    }

    /* Calculate period in microseconds.
       Timer ticks at bus clock / divisor.
       Period = ICR * divisor / bus_clock_hz * 1e6. */
    double period_us = ((double) dev->timer_icr * (double) dev->timer_div)
        / (cpu_busspeed / 1000000.0);

    if (apic_timer_start_log_count < 100) {
        apic_timer_start_log_count++;
        fprintf(stderr,
                "APIC-TIMER-START[%d]: cpu=%d icr=%08X div=%d lvt=%08X svr=%08X period_us=%.2f active_cpu=%d\n",
                apic_timer_start_log_count, cpu_id, dev->timer_icr, dev->timer_div,
                dev->lvt_timer, dev->svr, period_us, active_cpu);
    }
    apic_log("APIC: Timer start, ICR=%08X div=%d period=%.2f us\n",
             dev->timer_icr, dev->timer_div, period_us);

    timer_on_auto(&dev->timer, period_us);
}

/*
 * Check if a given APIC ID matches the destination for an IPI.
 *
 * In physical mode (destmod=0): match if dest_id == apic_id.
 *   Special case: dest_id 0xFF = broadcast.
 *
 * In logical mode (destmod=1): match using LDR/DFR.
 *   Flat model (DFR = 0xFFFFFFFF): match if (LDR >> 24) & dest_id != 0.
 *   Cluster model (DFR = 0x0FFFFFFF): match cluster + member bits.
 */
static int
apic_match_dest(apic_t *target, int dest_id, int destmod)
{
    if (!target)
        return 0;

    if (!destmod) {
        /* Physical mode. */
        int target_id = (target->id >> 24) & 0x0F;
        return (dest_id == 0xFF) || (target_id == dest_id);
    } else {
        /* Logical mode. */
        uint8_t target_ldr = (target->ldr >> 24) & 0xFF;

        if ((target->dfr & 0xF0000000) == 0xF0000000) {
            /* Flat model: bitwise AND of destination and LDR. */
            return !!(target_ldr & dest_id);
        } else {
            /* Cluster model: high 4 bits = cluster ID, low 4 bits = member mask. */
            uint8_t dest_cluster = (dest_id >> 4) & 0x0F;
            uint8_t dest_member  = dest_id & 0x0F;
            uint8_t tgt_cluster  = (target_ldr >> 4) & 0x0F;
            uint8_t tgt_member   = target_ldr & 0x0F;
            return (dest_cluster == tgt_cluster) && (tgt_member & dest_member);
        }
    }
}

/*
 * Deliver a Fixed or Lowest Priority IPI to a specific CPU's APIC.
 * Sets the IRR bit and wakes the CPU if halted.
 */
static void
apic_deliver_fixed(int cpu_id, int vector)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS)
        return;

    apic_t *target = apics[cpu_id];
    if (!target)
        return;

    if (vector < 0x10) {
        fprintf(stderr, "SMP: Fixed IPI vector %02X too low (must be >= 0x10)\n", vector);
        apic_log("APIC: IPI vector %02X too low (must be >= 0x10)\n", vector);
        return;
    }

    fprintf(stderr, "SMP: Fixed IPI vector %02X delivered to CPU %d\n", vector, cpu_id);
    apic_log("APIC: Deliver Fixed IPI vector %02X to CPU %d\n", vector, cpu_id);
    apic_set_bit(target->irr, vector);

    /* Wake the CPU if it is halted. */
    cpu_contexts[cpu_id].halted = 0;
}

/*
 * Deliver an INIT IPI to a target CPU.
 * This resets the target CPU to its power-on state and puts it
 * in wait-for-SIPI mode.
 */
static void
apic_deliver_init(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS)
        return;

    fprintf(stderr, "SMP: INIT IPI delivered to CPU %d\n", cpu_id);
    apic_log("APIC: INIT IPI to CPU %d\n", cpu_id);

    /* Reset the target CPU's context to power-on state.
       Per Intel SDM Vol. 3A, Table 9-1: INIT preserves MSRs, the APIC
       state, and the APIC pointer.  Save these before the memset. */
    cpu_context_t *ctx        = &cpu_contexts[cpu_id];
    void          *saved_apic = ctx->apic;
    msr_t          saved_msr  = ctx->msr;
    uint64_t       saved_cr4m = ctx->cpu_CR4_mask;

    /* Zero all CPU execution state. */
    memset(ctx, 0, sizeof(cpu_context_t));

    /* Restore state that persists across INIT. */
    ctx->apic         = saved_apic;
    ctx->msr          = saved_msr;
    ctx->cpu_CR4_mask = saved_cr4m;

    /* FLAGS bit 1 is set after reset; the upper EFLAGS half must stay clear.
       Setting cpu_state.eflags bit 1 would incorrectly enable VM86 mode. */
    ctx->cpu_state.flags  = 0x0002;
    ctx->cpu_state.eflags = 0x00000000;

    /* CR0: CD=1, NW=1, ET=1.  PE=0 = real mode. */
    ctx->cpu_state.CR0.l = 0x60000010;

    /* CS:IP = F000:FFF0 (reset vector).
       For APs after INIT, the CPU enters wait-for-SIPI and doesn't
       execute until SIPI overrides CS:IP. */
    ctx->cpu_state.pc                = 0x0000FFF0;
    ctx->cpu_state.seg_cs.base       = 0xFFFF0000;
    ctx->cpu_state.seg_cs.seg        = 0xF000;
    ctx->cpu_state.seg_cs.limit      = 0xFFFF;
    ctx->cpu_state.seg_cs.limit_low  = 0;
    ctx->cpu_state.seg_cs.limit_high = 0xFFFF;
    ctx->cpu_state.seg_cs.access     = 0x82;
    ctx->cpu_state.seg_cs.ar_high    = 0x10;

    /* DS/ES/SS/FS/GS: base=0, selector=0, limit=0xFFFF, real-mode access. */
    x86seg seg_default;
    memset(&seg_default, 0, sizeof(x86seg));
    seg_default.base       = 0;
    seg_default.seg        = 0;
    seg_default.limit      = 0xFFFF;
    seg_default.limit_low  = 0;
    seg_default.limit_high = 0xFFFF;
    seg_default.access     = 0x82;
    seg_default.ar_high    = 0x10;

    ctx->cpu_state.seg_ds = seg_default;
    ctx->cpu_state.seg_es = seg_default;
    ctx->cpu_state.seg_ss = seg_default;
    ctx->cpu_state.seg_fs = seg_default;
    ctx->cpu_state.seg_gs = seg_default;

    /* IDTR: base=0, limit=0x3FF (real-mode IVT). */
    memset(&ctx->idt, 0, sizeof(x86seg));
    ctx->idt.base  = 0;
    ctx->idt.limit = 0x03FF;

    /* GDTR: base=0, limit=0xFFFF. */
    memset(&ctx->gdt, 0, sizeof(x86seg));
    ctx->gdt.base  = 0;
    ctx->gdt.limit = 0xFFFF;

    /* Debug registers: Intel reset defaults. */
    ctx->dr[6] = 0xFFFF1FF0;
    ctx->dr[7] = 0x00000400;

    /* DFR defaults to all 1s in the APIC. */
    if (saved_apic)
        ((apic_t *) saved_apic)->dfr = 0xFFFFFFFF;

    /* Put CPU in wait-for-SIPI mode. */
    ctx->halted        = 1;
    ctx->wait_for_sipi = 1;
}

/*
 * Deliver a Startup IPI (SIPI) to a target CPU.
 * The vector field specifies the real-mode start address:
 *   CS = vector << 8, IP = 0, CS base = vector * 0x1000.
 */
static void
apic_deliver_sipi(int cpu_id, uint8_t vector)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS)
        return;

    cpu_context_t *ctx = &cpu_contexts[cpu_id];

    /* SIPI is only accepted when the CPU is waiting for SIPI. */
    if (!ctx->wait_for_sipi) {
        fprintf(stderr, "SMP: SIPI to CPU %d IGNORED (not waiting for SIPI)\n", cpu_id);
        apic_log("APIC: SIPI to CPU %d ignored (not waiting for SIPI)\n", cpu_id);
        return;
    }

    fprintf(stderr, "SMP: SIPI delivered to CPU %d, vector=%02X, start at %05X:0000\n",
            cpu_id, vector, (uint32_t) vector << 8);
    apic_log("APIC: SIPI to CPU %d, vector %02X (start at %05X:0000)\n",
             cpu_id, vector, (uint32_t) vector << 8);

    /* Clear wait-for-SIPI and halted flags. */
    ctx->wait_for_sipi = 0;
    ctx->halted        = 0;

    /* Set up real-mode execution at the SIPI vector address.
       CS selector = vector << 8, CS base = vector * 0x1000, IP = 0. */
    ctx->cpu_state.pc = 0;

    /* Set CS segment register for real mode at SIPI vector. */
    ctx->cpu_state.seg_cs.base       = (uint32_t) vector * 0x1000;
    ctx->cpu_state.seg_cs.seg        = (uint16_t) vector << 8;
    ctx->cpu_state.seg_cs.limit      = 0xFFFF;
    ctx->cpu_state.seg_cs.limit_low  = 0;
    ctx->cpu_state.seg_cs.limit_high = 0xFFFF;
    ctx->cpu_state.seg_cs.access     = 0x93; /* Present, DPL=0, code, readable. */
    ctx->cpu_state.seg_cs.ar_high    = 0;

    /* Real mode: CR0.PE = 0, rest of segments at their defaults. */
    ctx->cpu_state.CR0.l = 0x60000010; /* CD=1, NW=1, ET=1, PE=0. */

    /* Set default data segment registers (real mode).
       All data segments: base=0, selector=0, limit=0xFFFF. */
    x86seg seg_default;
    memset(&seg_default, 0, sizeof(x86seg));
    seg_default.base       = 0;
    seg_default.seg        = 0;
    seg_default.limit      = 0xFFFF;
    seg_default.limit_low  = 0;
    seg_default.limit_high = 0xFFFF;
    seg_default.access     = 0x93;
    seg_default.ar_high    = 0;

    ctx->cpu_state.seg_ds = seg_default;
    ctx->cpu_state.seg_es = seg_default;
    ctx->cpu_state.seg_ss = seg_default;
    ctx->cpu_state.seg_fs = seg_default;
    ctx->cpu_state.seg_gs = seg_default;

    /* Stack pointer. */
    ctx->cpu_state.regs[4].l = 0; /* ESP = 0. */

    /* EDX = CPUID signature on reset (model-dependent, use 0 for now). */
    ctx->cpu_state.regs[2].l = 0;

    /* FLAGS bit 1 is set after SIPI; the upper EFLAGS half must stay clear.
       Setting cpu_state.eflags bit 1 would incorrectly enable VM86 mode. */
    ctx->cpu_state.flags  = 0x0002;
    ctx->cpu_state.eflags = 0x00000000;

    /* Ensure IDTR is set for real-mode IVT (base=0, limit=0x3FF). */
    ctx->idt.base  = 0;
    ctx->idt.limit = 0x03FF;

    /* Reset APIC state for the AP (SVR disabled, etc.). */
    apic_t *ap_apic = apics[cpu_id];
    if (ap_apic) {
        /* Keep APIC ID but reset everything else. */
        uint32_t saved_id = ap_apic->id;
        ap_apic->tpr      = 0;
        ap_apic->ldr      = 0;
        ap_apic->dfr      = 0xFFFFFFFF;
        ap_apic->svr      = 0xFF; /* Disabled. */
        memset(ap_apic->isr, 0, sizeof(ap_apic->isr));
        memset(ap_apic->tmr, 0, sizeof(ap_apic->tmr));
        memset(ap_apic->irr, 0, sizeof(ap_apic->irr));
        ap_apic->esr         = 0;
        ap_apic->icr_low     = 0;
        ap_apic->icr_high    = 0;
        ap_apic->lvt_timer   = APIC_LVT_MASKED;
        ap_apic->lvt_thermal = APIC_LVT_MASKED;
        ap_apic->lvt_perf    = APIC_LVT_MASKED;
        ap_apic->lvt_lint0   = APIC_LVT_MASKED | APIC_LVT_DM_EXTINT;
        ap_apic->lvt_lint1   = APIC_LVT_MASKED | APIC_LVT_DM_NMI;
        ap_apic->lvt_error   = APIC_LVT_MASKED;
        ap_apic->timer_icr   = 0;
        ap_apic->timer_ccr   = 0;
        ap_apic->timer_dcr   = 0;
        ap_apic->timer_div   = 2;
        timer_disable(&ap_apic->timer);
        ap_apic->id = saved_id;
    }

    /* Force the sending CPU to yield its remaining time slice so the AP
       gets a chance to run before the BSP continues.  On real hardware
       the AP starts immediately; in our cooperative model we must end
       the current cpu_exec() call.  Zeroing both counters causes both
       the inner (cycles) and outer (cycles_main) loops to exit.
       cpu_block_end ends the current interpreter block in dynarec mode. */
    cycles        = 0;
    cycles_main   = 0;
    cpu_block_end = 1;

    /* Activate fine-grained time slicing for the next N iterations.
       During the AP startup handshake, the BIOS expects BSP and AP
       to interleave at close to instruction-level granularity.  With
       the normal 100K-300K cycle slices, BSP exhausts its entire
       polling loop in one slice before AP has a chance to respond.
       Using 1000-cycle slices for ~5000 iterations (~5M total cycles)
       allows the handshake to complete naturally. */
    smp_fine_slice_countdown = 5000;
    fprintf(stderr, "SMP: SIPI yield — BSP yielding, fine-slice enabled (5000 iters) for AP %d\n",
            cpu_id);

    /* Activate AP instruction-level ring buffer trace. */
    ap_trace_remaining = 50000;
    fprintf(stderr, "SMP: AP trace armed — capturing next %d AP instructions\n",
            ap_trace_remaining);
}

/*
 * Deliver an NMI IPI to a target CPU.
 * For SMP, we set an NMI flag in the target CPU's context.
 * For the active CPU, we can set the global nmi flag directly.
 */
static void
apic_deliver_nmi(int cpu_id)
{
    apic_log("APIC: NMI IPI to CPU %d\n", cpu_id);

    if (cpu_id == active_cpu) {
        nmi = 1;
    } else {
        /* For the non-active CPU, set the NMI flag in its context.
           The smi_latched field is the closest thing we have;
           for now, we just note it and handle it when that CPU runs.
           TODO: Add a proper per-CPU NMI pending flag. */
        cpu_contexts[cpu_id].halted = 0;
        nmi                         = 1; /* Will be processed when CPU switches. */
    }
}

/*
 * Deliver an SMI IPI to a target CPU.
 * SMM state is stored in cpu_state, so inactive CPUs can be primed by
 * updating their saved context and waking them if they were halted.
 */
static void
apic_deliver_smi(int cpu_id)
{
    apic_log("APIC: SMI IPI to CPU %d\n", cpu_id);

    if (cpu_id == active_cpu) {
        smi_line = 1;
    } else {
        cpu_contexts[cpu_id].cpu_state._smi_line = 1;
        cpu_contexts[cpu_id].halted              = 0;
    }
}

/*
 * Process an ICR write — the core IPI delivery mechanism.
 * Called when the CPU writes to ICR Low (offset 0x300).
 * ICR High (offset 0x310) must be written first for the destination.
 */
static void
apic_deliver_ipi(apic_t *dev)
{
    uint32_t icr_low  = dev->icr_low;
    uint32_t icr_high = dev->icr_high;

    int vector    = icr_low & 0xFF;
    int delmod    = (icr_low >> 8) & 7;
    int destmod   = !!(icr_low & APIC_ICR_DESTMOD);
    int level     = !!(icr_low & APIC_ICR_LEVEL);
    int shorthand = (icr_low >> 18) & 3;
    int dest_id   = (icr_high >> 24) & 0xFF;

    int src_cpu = -1;
    for (int i = 0; i < APIC_MAX_CPUS; i++) {
        if (apics[i] == dev) {
            src_cpu = i;
            break;
        }
    }

    fprintf(stderr, "SMP: IPI from CPU %d: vector=%02X delmod=%d destmod=%d "
                    "level=%d shorthand=%d dest=%02X\n",
            src_cpu, vector, delmod, destmod, level, shorthand, dest_id);

    apic_log("APIC: IPI from CPU %d: vector=%02X delmod=%d destmod=%d "
             "level=%d shorthand=%d dest=%02X\n",
             src_cpu, vector, delmod, destmod, level, shorthand, dest_id);

    /* INIT Level De-assert: special case, resets arbitration IDs.
       Broadcast to all CPUs, no actual INIT reset. */
    if (delmod == 5 && !level && (icr_low & APIC_ICR_TRIGGER)) {
        fprintf(stderr, "SMP: INIT Level De-assert (broadcast arb ID reset)\n");
        apic_log("APIC: INIT Level De-assert (broadcast arb ID reset)\n");
        return;
    }

    /* Build the list of target CPUs based on shorthand/destination. */
    for (int cpu_id = 0; cpu_id < APIC_MAX_CPUS; cpu_id++) {
        int deliver = 0;

        if (cpu_id >= num_cpus)
            continue;

        switch (shorthand) {
            case 0: /* No shorthand: use destination field. */
                deliver = apic_match_dest(apics[cpu_id], dest_id, destmod);
                break;
            case 1: /* Self. */
                deliver = (cpu_id == src_cpu);
                break;
            case 2: /* All including self. */
                deliver = 1;
                break;
            case 3: /* All excluding self. */
                deliver = (cpu_id != src_cpu);
                break;
        }

        if (!deliver)
            continue;

        /* Deliver to this CPU based on delivery mode. */
        switch (delmod) {
            case 0: /* Fixed. */
            case 1: /* Lowest Priority (treated as Fixed for now). */
                apic_deliver_fixed(cpu_id, vector);
                break;

            case 2: /* SMI. */
                apic_deliver_smi(cpu_id);
                break;

            case 4: /* NMI. */
                apic_deliver_nmi(cpu_id);
                break;

            case 5: /* INIT. */
                apic_deliver_init(cpu_id);
                break;

            case 6: /* Startup IPI (SIPI). */
                apic_deliver_sipi(cpu_id, (uint8_t) vector);
                break;

            default:
                apic_log("APIC: Unknown IPI delivery mode %d\n", delmod);
                break;
        }
    }
}

/*
 * MMIO read handler for the Local APIC.
 */
static uint8_t
apic_mem_readb(uint32_t addr, void *priv)
{
    /* APIC registers must be accessed as 32-bit aligned. */
    return 0xFF;
}

static uint16_t
apic_mem_readw(uint32_t addr, void *priv)
{
    return 0xFFFF;
}

static uint32_t
apic_mem_readl(uint32_t addr, UNUSED(void *priv))
{
    static int apic_read_log_count = 0;
    apic_t    *dev                 = apic; /* Use active CPU's APIC, not mapping's priv */
    uint32_t   offset              = addr & 0xFFF;

    if (apic_read_log_count < 50) {
        fprintf(stderr, "SMP: APIC read [%03X] addr=%08X\n", offset, addr);
        apic_read_log_count++;
    }
    uint32_t ret = 0;

    if (!dev)
        return 0;

    switch (offset) {
        case APIC_REG_ID:
            ret = dev->id;
            fprintf(stderr, "SMP: APIC ID read: dev=%p id=%08X (active_cpu=%d)\n",
                    (void *) dev, ret, active_cpu);
            break;

        case APIC_REG_VERSION:
            ret = APIC_VERSION;
            break;

        case APIC_REG_TPR:
            ret = dev->tpr;
            break;

        case APIC_REG_APR:
            /* Arbitration Priority Register -- stub, return 0. */
            ret = 0;
            break;

        case APIC_REG_PPR:
            ret = apic_get_ppr(dev);
            break;

        case APIC_REG_LDR:
            ret = dev->ldr;
            break;

        case APIC_REG_DFR:
            ret = dev->dfr;
            break;

        case APIC_REG_SVR:
            ret = dev->svr;
            break;

        /* ISR: 8 registers at 0x100-0x170 */
        case 0x100:
        case 0x110:
        case 0x120:
        case 0x130:
        case 0x140:
        case 0x150:
        case 0x160:
        case 0x170:
            ret = dev->isr[(offset - APIC_REG_ISR_BASE) >> 4];
            break;

        /* TMR: 8 registers at 0x180-0x1F0 */
        case 0x180:
        case 0x190:
        case 0x1A0:
        case 0x1B0:
        case 0x1C0:
        case 0x1D0:
        case 0x1E0:
        case 0x1F0:
            ret = dev->tmr[(offset - APIC_REG_TMR_BASE) >> 4];
            break;

        /* IRR: 8 registers at 0x200-0x270 */
        case 0x200:
        case 0x210:
        case 0x220:
        case 0x230:
        case 0x240:
        case 0x250:
        case 0x260:
        case 0x270:
            ret = dev->irr[(offset - APIC_REG_IRR_BASE) >> 4];
            break;

        case APIC_REG_ESR:
            ret = dev->esr;
            break;

        case APIC_REG_ICR_LOW:
            ret = dev->icr_low;
            break;

        case APIC_REG_ICR_HIGH:
            ret = dev->icr_high;
            break;

        case APIC_REG_LVT_TIMER:
            ret = dev->lvt_timer;
            break;

        case APIC_REG_LVT_THERMAL:
            ret = dev->lvt_thermal;
            break;

        case APIC_REG_LVT_PERF:
            ret = dev->lvt_perf;
            break;

        case APIC_REG_LVT_LINT0:
            ret = dev->lvt_lint0;
            break;

        case APIC_REG_LVT_LINT1:
            ret = dev->lvt_lint1;
            break;

        case APIC_REG_LVT_ERROR:
            ret = dev->lvt_error;
            break;

        case APIC_REG_TIMER_ICR:
            ret = dev->timer_icr;
            break;

        case APIC_REG_TIMER_CCR:
            /* Current Count: compute from remaining timer time. */
            if (timer_is_enabled(&dev->timer) && dev->timer_icr > 0) {
                uint64_t remaining = timer_get_remaining_us(&dev->timer);
                double   tick_us   = (double) dev->timer_div / (cpu_busspeed / 1000000.0);
                if (tick_us > 0.0)
                    ret = (uint32_t) (remaining / tick_us);
                else
                    ret = 0;
            } else {
                ret = 0;
            }
            break;

        case APIC_REG_TIMER_DCR:
            ret = dev->timer_dcr;
            break;

        default:
            apic_log("APIC: Read from unknown register %03X\n", offset);
            ret = 0;
            break;
    }

    apic_log("APIC: Read  [%03X] = %08X\n", offset, ret);
    return ret;
}

/*
 * MMIO write handler for the Local APIC.
 */
static void
apic_mem_writeb(uint32_t addr, uint8_t val, void *priv)
{
    /* APIC registers must be accessed as 32-bit aligned -- ignore byte writes. */
}

static void
apic_mem_writew(uint32_t addr, uint16_t val, void *priv)
{
    /* Ignore word writes. */
}

static void
apic_mem_writel(uint32_t addr, uint32_t val, UNUSED(void *priv))
{
    static int apic_write_log_count = 0;
    apic_t    *dev                  = apic; /* Use active CPU's APIC, not mapping's priv */
    uint32_t   offset               = addr & 0xFFF;

    if (apic_write_log_count < 50) {
        fprintf(stderr, "SMP: APIC write [%03X] = %08X addr=%08X\n", offset, val, addr);
        apic_write_log_count++;
    }

    apic_log("APIC: Write [%03X] = %08X\n", offset, val);

    switch (offset) {
        case APIC_REG_ID:
            /* Only bits 27:24 are writable (APIC ID). */
            dev->id = val & 0x0F000000;
            break;

        case APIC_REG_TPR:
            dev->tpr = val & 0xFF;
            break;

        case APIC_REG_EOI:
            /* EOI: clear the highest-priority bit in the ISR. */
            {
                int highest = apic_find_highest_bit(dev->isr);
                if (highest >= 0) {
                    apic_log("APIC: EOI, clearing ISR bit %d\n", highest);
                    apic_clear_bit(dev->isr, highest);

                    /* Notify the I/O APIC so it can clear Remote IRR
                       for level-triggered interrupts and re-deliver
                       if the line is still asserted. */
                    ioapic_eoi(highest);
                }
            }
            break;

        case APIC_REG_LDR:
            /* Only bits 31:24 are writable. */
            dev->ldr = val & 0xFF000000;
            break;

        case APIC_REG_DFR:
            /* Only bits 31:28 are writable, rest reads as 1. */
            dev->dfr = (val & 0xF0000000) | 0x0FFFFFFF;
            break;

        case APIC_REG_SVR:
            {
                int was_enabled = !!(dev->svr & APIC_SVR_ENABLE);
                dev->svr        = val & 0x1FF; /* Bits 8:0 are writable. */
                int now_enabled = !!(dev->svr & APIC_SVR_ENABLE);

                if (!was_enabled && now_enabled)
                    apic_log("APIC: Software enabled\n");
                else if (was_enabled && !now_enabled) {
                    apic_log("APIC: Software disabled\n");
                    /* Disable timer when APIC is disabled. */
                    timer_disable(&dev->timer);
                }
            }
            break;

        case APIC_REG_ESR:
            /* Writing to ESR clears it (write any value). */
            dev->esr = 0;
            break;

        case APIC_REG_ICR_LOW:
            /* ICR Low write triggers IPI delivery.
               ICR High (destination) must be written first. */
            fprintf(stderr, "SMP: ICR write: low=%08X high=%08X (delmod=%d, dest=%02X)\n",
                    val, dev->icr_high, (val >> 8) & 7, (dev->icr_high >> 24) & 0xFF);
            dev->icr_low = val;
            apic_deliver_ipi(dev);
            break;

        case APIC_REG_ICR_HIGH:
            dev->icr_high = val & 0xFF000000;
            break;

        case APIC_REG_LVT_TIMER:
            dev->lvt_timer = val & 0x000300FF;
            /* Bit 17 = periodic, bit 16 = mask, bits 7:0 = vector. */
            break;

        case APIC_REG_LVT_THERMAL:
            dev->lvt_thermal = val & 0x000107FF;
            break;

        case APIC_REG_LVT_PERF:
            dev->lvt_perf = val & 0x000107FF;
            break;

        case APIC_REG_LVT_LINT0:
            dev->lvt_lint0 = val & 0x0001F7FF;
            apic_log("APIC: LINT0 = %08X (DM=%d, mask=%d)\n",
                     dev->lvt_lint0,
                     (dev->lvt_lint0 >> 8) & 7,
                     !!(dev->lvt_lint0 & APIC_LVT_MASKED));
            break;

        case APIC_REG_LVT_LINT1:
            dev->lvt_lint1 = val & 0x0001F7FF;
            apic_log("APIC: LINT1 = %08X (DM=%d, mask=%d)\n",
                     dev->lvt_lint1,
                     (dev->lvt_lint1 >> 8) & 7,
                     !!(dev->lvt_lint1 & APIC_LVT_MASKED));
            break;

        case APIC_REG_LVT_ERROR:
            dev->lvt_error = val & 0x000100FF;
            break;

        case APIC_REG_TIMER_ICR:
            dev->timer_icr = val;
            /* Writing ICR starts the timer countdown. */
            if (dev->svr & APIC_SVR_ENABLE)
                apic_timer_start(dev);
            break;

        case APIC_REG_TIMER_DCR:
            dev->timer_dcr = val & 0x0B;
            dev->timer_div = apic_get_timer_divisor(dev->timer_dcr);
            apic_log("APIC: Timer divisor = %d\n", dev->timer_div);
            break;

        /* Read-only registers. */
        case APIC_REG_VERSION:
        case APIC_REG_APR:
        case APIC_REG_PPR:
        case APIC_REG_TIMER_CCR:
        case 0x100:
        case 0x110:
        case 0x120:
        case 0x130:
        case 0x140:
        case 0x150:
        case 0x160:
        case 0x170:
        case 0x180:
        case 0x190:
        case 0x1A0:
        case 0x1B0:
        case 0x1C0:
        case 0x1D0:
        case 0x1E0:
        case 0x1F0:
        case 0x200:
        case 0x210:
        case 0x220:
        case 0x230:
        case 0x240:
        case 0x250:
        case 0x260:
        case 0x270:
            /* Ignore writes to read-only registers. */
            break;

        default:
            apic_log("APIC: Write to unknown register %03X = %08X\n", offset, val);
            break;
    }
}

/*
 * Initialize the Local APIC.
 * Called from cpu_set() when the CPU supports APIC (is_p6).
 */
/*
 * Initialize the Local APIC for a specific CPU.
 * cpu_id 0 = BSP; cpu_id 1+ = APs.
 * Called from cpu_set() (for BSP) or cpu_smp_init() (for APs).
 */
void
apic_init_cpu(int cpu_id)
{
    fprintf(stderr, "SMP: apic_init_cpu(%d) called, is_p6=%d\n", cpu_id, is_p6);
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS)
        return;

    /* Free any existing APIC state for this CPU. */
    if (apics[cpu_id]) {
        mem_mapping_disable(&apics[cpu_id]->mem_mapping);
        timer_disable(&apics[cpu_id]->timer);
        free(apics[cpu_id]);
        apics[cpu_id] = NULL;
    }

    apic_t *dev = (apic_t *) malloc(sizeof(apic_t));
    memset(dev, 0, sizeof(apic_t));

    apics[cpu_id] = dev;

    /* Backward compat: apic global points to BSP's APIC. */
    if (cpu_id == 0)
        apic = dev;

    /* Set default base address and MSR.
       All CPUs share the same APIC MMIO page (0xFEE00000).
       Only the BSP gets the BSP flag in its MSR. */
    dev->base_addr = APIC_DEFAULT_BASE;
    dev->msr       = APIC_DEFAULT_BASE | APIC_MSR_ENABLE;
    if (cpu_id == 0)
        dev->msr |= APIC_MSR_BSP;

    /* Keep msr_t.apic_base consistent with the authoritative apic_t.msr
       so diagnostic/trace code reads the correct value. */
    cpu_contexts[cpu_id].msr.apic_base = dev->msr;

    /* APIC ID: bits 27:24, set to cpu_id. */
    dev->id = (uint32_t) cpu_id << 24;

    /* Initialize timer. */
    timer_add(&dev->timer, apic_timer_callback, dev, 0);

    /* Set up MMIO mapping.
       All Local APICs share the same physical address (0xFEE00000).
       Each CPU accesses "its own" APIC at this address.
       In the cooperative model, only the active CPU's APIC is mapped.
       For now, the BSP's mapping is the one that is active.
       When we context-switch, we swap the mapping's priv pointer. */
    if (cpu_id == 0) {
        mem_mapping_add(&dev->mem_mapping,
                        dev->base_addr,
                        0x1000,
                        apic_mem_readb,
                        apic_mem_readw,
                        apic_mem_readl,
                        apic_mem_writeb,
                        apic_mem_writew,
                        apic_mem_writel,
                        NULL,
                        MEM_MAPPING_INTERNAL,
                        dev);
        fprintf(stderr, "SMP: APIC MMIO mapping added at %08X for CPU %d\n",
                dev->base_addr, cpu_id);
    } else {
        /* AP's APIC: no MMIO mapping needed — all CPUs share the BSP's
           mapping, and the handlers use the global `apic` pointer to
           access the active CPU's APIC state. */
    }

    /* Set power-on defaults. */
    dev->tpr = 0;
    dev->ldr = 0;
    dev->dfr = 0xFFFFFFFF;
    dev->svr = 0xFF; /* Disabled. */
    memset(dev->isr, 0, sizeof(dev->isr));
    memset(dev->tmr, 0, sizeof(dev->tmr));
    memset(dev->irr, 0, sizeof(dev->irr));
    dev->esr         = 0;
    dev->icr_low     = 0;
    dev->icr_high    = 0;
    dev->lvt_timer   = APIC_LVT_MASKED;
    dev->lvt_thermal = APIC_LVT_MASKED;
    dev->lvt_perf    = APIC_LVT_MASKED;
    dev->lvt_lint0   = APIC_LVT_MASKED | APIC_LVT_DM_EXTINT;
    dev->lvt_lint1   = APIC_LVT_MASKED | APIC_LVT_DM_NMI;
    dev->lvt_error   = APIC_LVT_MASKED;
    dev->timer_icr   = 0;
    dev->timer_ccr   = 0;
    dev->timer_dcr   = 0;
    dev->timer_div   = 2;

    /* Store the APIC pointer in the cpu_context. */
    cpu_contexts[cpu_id].apic = dev;

    apic_log("APIC: CPU %d APIC initialized, ID=%d, base=%08X%s\n",
             cpu_id, cpu_id, dev->base_addr,
             (cpu_id == 0) ? " (BSP)" : " (AP)");
}

void
apic_init(void)
{
    /* Initialize the BSP's APIC (CPU 0). */
    apic_init_cpu(0);
}

/*
 * Re-add the APIC MMIO mapping after mem_reset().
 *
 * The BSP's APIC is initialized in cpu_set() -> apic_init() which runs
 * BEFORE mem_reset(). mem_reset() zeroes the mapping linked list and
 * _mem_state[] array, orphaning the APIC's mem_mapping_t. This function
 * re-adds the mapping and marks the APIC address range as INTERNAL so
 * the memory system routes CPU accesses to the APIC handlers.
 *
 * Must be called after mem_reset() — typically from cpu_smp_init() or
 * machine init.
 */
void
apic_reset_mapping(void)
{
    if (!apics[0])
        return;

    apic_t *dev = apics[0];

    /* Mark the APIC MMIO range as INTERNAL in the memory state array.
       Without this, the memory system will not route accesses at
       0xFEE00000 to INTERNAL mappings (default after mem_reset is
       all-external / zero). */
    mem_set_mem_state_both(dev->base_addr, 0x1000,
                           MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);

    /* Re-add the BSP's APIC MMIO mapping to the mapping linked list.
       The apic_t struct and its mem_mapping_t still exist in heap, but
       mem_reset() removed them from base_mapping/last_mapping and
       cleared the read_mapping[]/write_mapping[] arrays. */
    mem_mapping_add(&dev->mem_mapping,
                    dev->base_addr,
                    0x1000,
                    apic_mem_readb,
                    apic_mem_readw,
                    apic_mem_readl,
                    apic_mem_writeb,
                    apic_mem_writew,
                    apic_mem_writel,
                    NULL,
                    MEM_MAPPING_INTERNAL,
                    dev);

    fprintf(stderr, "SMP: APIC MMIO mapping re-added at %08X after mem_reset()\n",
            dev->base_addr);
}

/*
 * Reset the Local APIC to power-on defaults.
 */
void
apic_reset(void)
{
    if (!apic)
        return;

    apic_t *dev = apic;

    /* APIC ID: 0 for BSP. */
    dev->id = 0;

    /* TPR: 0. */
    dev->tpr = 0;

    /* LDR: 0. */
    dev->ldr = 0;

    /* DFR: all 1s (flat model). */
    dev->dfr = 0xFFFFFFFF;

    /* SVR: APIC disabled, vector 0xFF.
       BIOS will enable it by setting bit 8. */
    dev->svr = 0xFF;

    /* Clear all vector registers. */
    memset(dev->isr, 0, sizeof(dev->isr));
    memset(dev->tmr, 0, sizeof(dev->tmr));
    memset(dev->irr, 0, sizeof(dev->irr));

    /* ESR: 0. */
    dev->esr = 0;

    /* ICR: 0. */
    dev->icr_low  = 0;
    dev->icr_high = 0;

    /* LVT entries: all masked on reset.
       LINT0: ExtINT mode (virtual wire), masked.
       LINT1: NMI mode, masked. */
    dev->lvt_timer   = APIC_LVT_MASKED;
    dev->lvt_thermal = APIC_LVT_MASKED;
    dev->lvt_perf    = APIC_LVT_MASKED;
    dev->lvt_lint0   = APIC_LVT_MASKED | APIC_LVT_DM_EXTINT;
    dev->lvt_lint1   = APIC_LVT_MASKED | APIC_LVT_DM_NMI;
    dev->lvt_error   = APIC_LVT_MASKED;

    /* Timer: stopped. */
    dev->timer_icr = 0;
    dev->timer_ccr = 0;
    dev->timer_dcr = 0;
    dev->timer_div = 2; /* Default divisor is 2. */
    timer_disable(&dev->timer);

    apic_log("APIC: Reset to defaults\n");
}

/*
 * Shut down the Local APIC.
 */
void
apic_close(void)
{
    for (int i = 0; i < APIC_MAX_CPUS; i++) {
        if (apics[i]) {
            mem_mapping_disable(&apics[i]->mem_mapping);
            timer_disable(&apics[i]->timer);
            free(apics[i]);
            apics[i] = NULL;
        }
    }
    apic = NULL;
}

/*
 * Returns 1 if the Local APIC is enabled.
 * The APIC is enabled when:
 *   1. The MSR global enable bit (bit 11) is set, AND
 *   2. The SVR software enable bit (bit 8) is set.
 */
int
apic_enabled(void)
{
    if (!apic)
        return 0;

    return (apic->msr & APIC_MSR_ENABLE) && (apic->svr & APIC_SVR_ENABLE);
}

/*
 * Check if APIC has a pending interrupt that can be delivered.
 * Returns the vector number (0-255) or -1 if none pending.
 *
 * An interrupt can be delivered when:
 *   1. APIC is enabled
 *   2. There is a pending IRR bit
 *   3. The IRR priority is higher than TPR/PPR
 */
int
apic_get_interrupt(void)
{
    if (!apic || !apic_enabled())
        return -1;

    int highest_irr = apic_find_highest_bit(apic->irr);
    if (highest_irr < 0)
        return -1;

    /* Check priority against PPR. */
    uint32_t ppr = apic_get_ppr(apic);
    if ((highest_irr & 0xF0) <= (ppr & 0xF0))
        return -1;

    /* Accept the interrupt: move from IRR to ISR. */
    apic_clear_bit(apic->irr, highest_irr);
    apic_set_bit(apic->isr, highest_irr);

    /* SMP DIAG: Log APIC interrupt delivery. */
    {
        static int apic_deliver_count = 0;
        if (apic_deliver_count < 100) {
            apic_deliver_count++;
            fprintf(stderr, "APIC-DELIVER[%d]: vector=%02X, cpu=%d, ppr=%02X\n",
                    apic_deliver_count, highest_irr, active_cpu, ppr);
        }
    }

    apic_log("APIC: Delivering vector %02X\n", highest_irr);
    return highest_irr;
}

/*
 * Raise an interrupt in the APIC's IRR for the given vector.
 */
void
apic_set_irr(int vector)
{
    if (!apic || vector < 0 || vector > 255)
        return;

    apic_set_bit(apic->irr, vector);
}

/*
 * Raise an interrupt in a specific CPU's APIC IRR.
 * Used by IPI delivery and I/O APIC routing.
 */
void
apic_set_irr_cpu(int cpu_id, int vector)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS)
        return;

    apic_t *target = apics[cpu_id];
    if (!target || vector < 0 || vector > 255)
        return;

    apic_set_bit(target->irr, vector);

    /* SMP DIAG: Log APIC IRR set from I/O APIC. */
    {
        static int apic_irr_log_count = 0;
        if (apic_irr_log_count < 100) {
            apic_irr_log_count++;
            fprintf(stderr, "APIC-IRR-SET[%d]: cpu=%d, vector=%02X (active_cpu=%d)\n",
                    apic_irr_log_count, cpu_id, vector, active_cpu);
        }
    }

    /* Wake the CPU if it is halted. */
    cpu_contexts[cpu_id].halted = 0;
}

/*
 * Get the APIC state for a specific CPU.
 * Returns NULL if that CPU's APIC is not initialized.
 */
apic_t *
apic_get_cpu(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS)
        return NULL;
    return apics[cpu_id];
}

/*
 * Check if a specific CPU's APIC matches a logical destination.
 * Used by I/O APIC routing to determine which CPUs to deliver to.
 */
int
apic_match_logical_dest(int cpu_id, uint8_t dest)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS)
        return 0;

    apic_t *target = apics[cpu_id];
    if (!target)
        return 0;

    uint8_t ldr_field = (target->ldr >> 24) & 0xFF;

    if ((target->dfr & 0xF0000000) == 0xF0000000) {
        /* Flat model: bitwise AND. */
        return !!(ldr_field & dest);
    } else {
        /* Cluster model. */
        uint8_t dest_cluster = (dest >> 4) & 0x0F;
        uint8_t dest_member  = dest & 0x0F;
        uint8_t tgt_cluster  = (ldr_field >> 4) & 0x0F;
        uint8_t tgt_member   = ldr_field & 0x0F;
        return (dest_cluster == tgt_cluster) && (tgt_member & dest_member);
    }
}

/*
 * Signal LINT0 -- connected to PIC in virtual wire mode.
 *
 * In virtual wire mode, LINT0 is configured as ExtINT,
 * which passes through the 8259 PIC interrupt.
 * The PIC interrupt vector is obtained via picinterrupt()
 * at delivery time.
 */
void
apic_lint0_raise(void)
{
    if (!apic || !apic_enabled())
        return;

    /* Check if LINT0 is masked. */
    if (apic->lvt_lint0 & APIC_LVT_MASKED)
        return;

    int dm = apic->lvt_lint0 & APIC_LVT_DM_MASK;

    switch (dm) {
        case APIC_LVT_DM_EXTINT:
            /* ExtINT mode: the interrupt vector comes from the PIC
               at the time of acknowledgment. We don't set an IRR bit
               here -- instead, the interrupt dispatch code in the
               execution loop will call picinterrupt() directly.
               We just need to signal that an APIC interrupt is pending. */
            break;

        case APIC_LVT_DM_FIXED:
            /* Fixed mode: use the vector from the LVT entry. */
            apic_set_bit(apic->irr, apic->lvt_lint0 & 0xFF);
            break;

        default:
            apic_log("APIC: LINT0 delivery mode %d not handled\n", dm >> 8);
            break;
    }
}

/*
 * Signal LINT1 -- typically NMI.
 */
void
apic_lint1_raise(void)
{
    if (!apic || !apic_enabled())
        return;

    if (apic->lvt_lint1 & APIC_LVT_MASKED)
        return;

    int dm = apic->lvt_lint1 & APIC_LVT_DM_MASK;

    switch (dm) {
        case APIC_LVT_DM_NMI:
            /* NMI: deliver as interrupt vector 2. */
            nmi = 1;
            break;

        case APIC_LVT_DM_FIXED:
            apic_set_bit(apic->irr, apic->lvt_lint1 & 0xFF);
            break;

        default:
            apic_log("APIC: LINT1 delivery mode %d not handled\n", dm >> 8);
            break;
    }
}

/*
 * Read the IA32_APIC_BASE MSR (MSR 0x1B).
 */
uint64_t
apic_read_msr(void)
{
    if (!apic)
        return 0;

    return apic->msr;
}

/*
 * Write the IA32_APIC_BASE MSR (MSR 0x1B).
 * This can change the MMIO base address and enable/disable the APIC.
 */
void
apic_write_msr(uint64_t val)
{
    if (!apic)
        return;

    /* BSP flag (bit 8) is read-only -- preserve original value. */
    val = (val & ~APIC_MSR_BSP) | (apic->msr & APIC_MSR_BSP);

    uint32_t new_base    = (uint32_t) (val & APIC_MSR_BASE_MASK);
    int      was_enabled = !!(apic->msr & APIC_MSR_ENABLE);
    int      now_enabled = !!(val & APIC_MSR_ENABLE);

    apic_log("APIC: MSR write: %016llX (base=%08X, enable=%d)\n",
             (unsigned long long) val, new_base, now_enabled);

    /* Update the stored MSR value. */
    apic->msr = val;

    /* Remap MMIO if base address changed. */
    if (new_base != apic->base_addr) {
        apic_log("APIC: Remapping from %08X to %08X\n",
                 apic->base_addr, new_base);
        mem_mapping_set_addr(&apic->mem_mapping, new_base, 0x1000);
        apic->base_addr = new_base;
    }

    /* Handle enable/disable transitions. */
    if (was_enabled && !now_enabled) {
        apic_log("APIC: Globally disabled via MSR\n");
        mem_mapping_disable(&apic->mem_mapping);
        timer_disable(&apic->timer);
    } else if (!was_enabled && now_enabled) {
        apic_log("APIC: Globally enabled via MSR\n");
        mem_mapping_enable(&apic->mem_mapping);
    }
}

/*
 * Returns non-zero if APIC has a pending interrupt.
 * This is a fast check for block-end decisions in the execution loop.
 */
int
apic_int_pending(void)
{
    if (!apic || !apic_enabled())
        return 0;

    int highest_irr = apic_find_highest_bit(apic->irr);
    if (highest_irr < 0)
        return 0;

    uint32_t ppr = apic_get_ppr(apic);
    return (highest_irr & 0xF0) > (ppr & 0xF0);
}

/* Check if a specific CPU's APIC has a pending interrupt.
   Unlike apic_int_pending() which uses the global 'apic' pointer,
   this checks the APIC for a given cpu_id (used by SMP scheduler). */
int
apic_int_pending_cpu(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS)
        return 0;

    apic_t *dev = apics[cpu_id];
    if (!dev)
        return 0;

    /* Check if the APIC is globally enabled via MSR. */
    if (!(dev->msr & APIC_MSR_ENABLE))
        return 0;

    /* Check if software-enabled via SVR. */
    if (!(dev->svr & APIC_SVR_ENABLE))
        return 0;

    int highest_irr = apic_find_highest_bit(dev->irr);
    if (highest_irr < 0)
        return 0;

    uint32_t ppr = apic_get_ppr(dev);
    return (highest_irr & 0xF0) > (ppr & 0xF0);
}

/*
 * Switch the active APIC MMIO mapping to the given CPU.
 * Disables all other CPUs' APIC MMIO mappings and enables the target's.
 * Called during context switch.
 */
void
apic_switch_cpu(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS)
        return;

    /* Only update the global pointer — do NOT touch mem_mappings.
       All CPUs share the BSP's MMIO mapping; the handlers use the
       global `apic` pointer to access the active CPU's state.
       Disabling/enabling mappings on every context switch was causing
       the BSP to hang during BIOS POST. */
    if (apics[cpu_id])
        apic = apics[cpu_id];
}

/*
 * Returns non-zero if the CPU has an APIC (is_p6 or equivalent).
 */
int
apic_present(void)
{
    return is_p6;
}

/* Accessors for opaque apic_t fields (used by AP trace). */
uint32_t
apic_get_id(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS || apics[cpu_id] == NULL)
        return 0xFFFFFFFF;
    return apics[cpu_id]->id;
}

uint32_t
apic_get_svr(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS || apics[cpu_id] == NULL)
        return 0;
    return apics[cpu_id]->svr;
}

uint32_t
apic_get_tpr(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS || apics[cpu_id] == NULL)
        return 0;
    return apics[cpu_id]->tpr;
}

uint32_t
apic_get_lvt_lint0(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS || apics[cpu_id] == NULL)
        return 0;
    return apics[cpu_id]->lvt_lint0;
}

uint32_t
apic_get_lvt_lint1(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS || apics[cpu_id] == NULL)
        return 0;
    return apics[cpu_id]->lvt_lint1;
}

uint32_t
apic_get_icr_low(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS || apics[cpu_id] == NULL)
        return 0;
    return apics[cpu_id]->icr_low;
}

uint32_t
apic_get_icr_high(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS || apics[cpu_id] == NULL)
        return 0;
    return apics[cpu_id]->icr_high;
}

uint64_t
apic_get_msr_value(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS || apics[cpu_id] == NULL)
        return 0;
    return apics[cpu_id]->msr;
}

int
apic_get_highest_irr(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS || apics[cpu_id] == NULL)
        return -1;
    return apic_find_highest_bit(apics[cpu_id]->irr);
}

int
apic_get_highest_isr(int cpu_id)
{
    if (cpu_id < 0 || cpu_id >= APIC_MAX_CPUS || apics[cpu_id] == NULL)
        return -1;
    return apic_find_highest_bit(apics[cpu_id]->isr);
}
