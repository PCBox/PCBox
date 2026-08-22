#ifdef USE_FAST_DYNAREC

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <math.h>

#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include "x86_flags.h"
#include "x86_ops.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "x87_sf.h"
#include "x87.h"
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/timer.h>
#include <86box/apic.h>
#include <86box/device.h>
#include <86box/gdbstub.h>
#include <86box/plat_fallthrough.h>
#include <86box/plat_unused.h>
#ifdef USE_DYNAREC
#    include "codegen.h"
#    ifdef USE_NEW_DYNAREC
#        include "codegen_backend.h"
#    endif
#endif

#include "386_common.h"

#define CPU_BLOCK_END() cpu_block_end = 1

/* These are defined in 386_dynarec.c */
extern int cpu_block_end;
extern int cpu_end_block_after_ins;
extern int cpu_force_interpreter;
extern int cpu_override_dynarec;
extern int32_t cycles_main;

/* Import the accurate dynarec helpers - we reuse their block validation
   and recompilation logic, but with coarse timing */
extern void exec386_dynarec_int(void);
extern void exec386_dynarec_dyn(void);

#ifdef USE_DEBUG_REGS_486
#define CACHE_ON() (!(cr0 & (1 << 30)) && !(cpu_state.flags & T_FLAG) && !(dr[7] & 0xFF))
#else
#define CACHE_ON() (!(cr0 & (1 << 30)) && !(cpu_state.flags & T_FLAG))
#endif

/*
 * Fast dynamic recompiler - sacrifices timing accuracy for speed.
 *
 * This is a NEW optional recompiler (as requested in PCBox roadmap).
 * It reuses the existing block cache and code generation infrastructure
 * but replaces the precise cycle-accurate execution loop with a fast,
 * coarse-grained loop.
 *
 * Accuracy sacrifices:
 * - No per-instruction cycle counting (fast_zero timing emits no cycle code)
 * - TSC advanced by fixed FAST_BLOCK_CYCLES per block, not per instruction
 * - Timer processing batched every BLOCKS_PER_BATCH blocks
 * - No complex cycdiff/delta tracking for interim TSC updates
 * - No U/V pairing, AGI stall, or FPU latency modeling (handled by fast timing)
 * - Interrupts checked per block, not per instruction, and without precise timing
 *
 * Speed gains:
 * - No cycle accounting overhead in generated host code
 * - Fewer branches and memory accesses in outer loop
 * - Larger batches reduce timer heap contention
 * - Simpler TSC handling
 */
void
exec386_dynarec_fast(int32_t cycs)
{
    int vector;
    int tempi;

    const int FAST_BLOCK_CYCLES = 32;
    const int BLOCKS_PER_BATCH  = 128;

    cycles_main += cycs;

    while (cycles_main > 0) {
        int blocks_executed = 0;

        while (blocks_executed < BLOCKS_PER_BATCH && cycles_main > 0) {
            oldcs  = CS;
            oldcpl = CPL;
#ifdef USE_NEW_DYNAREC
            /* new dynarec manages oldpc/op32 internally */
#else
            cpu_state.oldpc = cpu_state.pc;
            cpu_state.op32  = use32;
#endif

            if (cpu_force_interpreter || cpu_override_dynarec || !CACHE_ON()) {
                exec386_dynarec_int();
            } else {
                exec386_dynarec_dyn();
            }

            if (cpu_init) {
                cpu_init = 0;
                resetx86();
            }

            if (cpu_state.abrt) {
                flags_rebuild();
                tempi          = cpu_state.abrt & ABRT_MASK;
                cpu_state.abrt = 0;
                x86_doabrt(tempi);
                if (cpu_state.abrt) {
                    cpu_state.abrt = 0;
                    cpu_state.pc   = cpu_state.oldpc;
#ifndef USE_NEW_DYNAREC
                    CS = oldcs;
#endif
                    pmodeint(8, 0);
                    if (cpu_state.abrt) {
                        cpu_state.abrt = 0;
                        softresetx86();
                        cpu_set_edx();
                    }
                }
            }

            if (new_ne) {
                oldcs           = CS;
                cpu_state.oldpc = cpu_state.pc;
                new_ne          = 0;
                x86_int(16);
            }

            if (smi_line) {
                enter_smm_check(0);
            } else if (nmi && nmi_enable && nmi_mask) {
                oldcs           = CS;
                cpu_state.oldpc = cpu_state.pc;
                x86_int(2);
                nmi_enable = 0;
#ifdef OLD_NMI_BEHAVIOR
                if (nmi_auto_clear) {
                    nmi_auto_clear = 0;
                    nmi            = 0;
                }
#else
                nmi = 0;
#endif
            } else if ((cpu_state.flags & I_FLAG) && pic_pending_int()) {
                vector = picinterrupt();
                if (vector != -1) {
                    oldcs           = CS;
                    cpu_state.oldpc = cpu_state.pc;
                    x86_int(vector);
                }
            }

            tsc += FAST_BLOCK_CYCLES;
            if (current_lapic)
                lapic_timer_advance_ticks(FAST_BLOCK_CYCLES);
            cycles_main -= FAST_BLOCK_CYCLES;
            blocks_executed++;

#ifdef USE_GDBSTUB
            if (gdbstub_instruction())
                return;
#endif
        }

        if (TIMER_VAL_LESS_THAN_VAL(timer_target, (uint64_t) tsc))
            timer_process();
    }
}

#endif /* USE_FAST_DYNAREC */
