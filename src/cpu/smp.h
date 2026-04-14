/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          SMP (Symmetric Multiprocessing) — Dual CPU context structure.
 *
 *          This header defines cpu_context_t, which holds the full
 *          per-CPU state snapshot for context switching.  It is kept
 *          separate from cpu.h because it depends on fpu_state_t
 *          (from x87_sf.h / softfloat) which not every translation
 *          unit needs.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef EMU_SMP_H
#define EMU_SMP_H

#include "cpu.h"
#include "x87_sf.h"

/*
 * Per-CPU context: holds a snapshot of ALL per-CPU state so that
 * two CPUs can be maintained simultaneously.
 *
 * The context switch model copies all per-CPU globals in/out of
 * the single set of globals (cpu_state, fpu_state, cr2, gdt, etc.)
 * WITHOUT changing any macros or dynarec offset calculations.
 */
struct cpu_context_t {
    /* Core CPU state (registers, flags, segments, FP stack). */
    cpu_state_t cpu_state;

    /* SoftFloat FPU state. */
    fpu_state_t fpu_state;

    /* x87 instruction/operand pointers (from x87.c). */
    uint32_t x87_pc_off;
    uint32_t x87_op_off;
    uint16_t x87_pc_seg;
    uint16_t x87_op_seg;

    /* MSR state. */
    msr_t    msr;
    uint64_t tsc;

    /* Control registers (cr0 is inside cpu_state.CR0). */
    uint32_t cr2;
    uint32_t cr3;
    uint32_t cr4;

    /* Debug registers. */
    uint32_t dr[8];

    /* Descriptor table registers. */
    x86seg gdt;
    x86seg ldt;
    x86seg idt;
    x86seg tr;
    x86seg _oldds;

    /* Dynarec status. */
#ifdef USE_NEW_DYNAREC
    uint16_t cpu_cur_status;
#else
    uint32_t cpu_cur_status;
#endif

    /* Prefetch cache. */
    uint32_t pccache;
    uint8_t *pccache2;

    /* Segment cache. */
    uint32_t oldds;
    uint32_t oldss;
    uint32_t olddslimit;
    uint32_t oldsslimit;
    uint32_t olddslimitw;
    uint32_t oldsslimitw;

    /* Current opcode. */
    uint8_t opcode;

    /* SMM state. */
    int smi_latched;
    int smm_in_hlt;
    int smi_block;

    /* Dynarec control. */
    int cpu_end_block_after_ins;
    int cpu_block_end;

    /* CPU CR4 mask and features. */
    uint64_t cpu_CR4_mask;

    /* Cyrix CCRs. */
    uint8_t ccr0;
    uint8_t ccr1;
    uint8_t ccr2;
    uint8_t ccr3;
    uint8_t ccr4;
    uint8_t ccr5;
    uint8_t ccr6;
    uint8_t ccr7;

    /* Cyrix state. */
    cyrix_t cyrix;

    /* Translation control. */
    uint8_t do_translate;
    uint8_t do_translate2;

    /* Misc per-CPU state. */
    int cpl_override;
    int in_sys;
    int unmask_a20_in_smm;

    /* Cache. */
    uint32_t _tr_regs[8];
    uint32_t cache_index;
    uint8_t  _cache[2048];

    /* Segment data temp. */
    uint16_t temp_seg_data[4];

    /* APIC pointer for this CPU (opaque, set by APIC init). */
    void *apic;

    /* SMP scheduling state. */
    int halted;        /* CPU is in HLT state */
    int wait_for_sipi; /* AP waiting for Startup IPI */
};

#endif /* EMU_SMP_H */
