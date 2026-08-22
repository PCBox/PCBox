/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Fast dynamic recompiler timing - sacrifices timing accuracy
 *          for speed. Each instruction is counted as 1 cycle (or 0) and
 *          no attempt is made to model pairing, AGI stalls, FPU latencies,
 *          cache, or memory wait states.
 *
 * Authors: Based on existing timing files by Sarah Walker, Miran Grca
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_ops.h"
#include "x87_sf.h"
#include "x87.h"

#ifdef USE_NEW_DYNAREC
#    include "../codegen_new/codegen.h"
#else
#    include "../codegen/codegen.h"
#endif

#include "codegen_timing_common.h"

#ifdef USE_FAST_DYNAREC

/*
 * Fast timing model:
 * - No per-prefix cost
 * - Each opcode = 1 cycle (minimal)
 * - No block start/end overhead
 * - No jump cycle penalty
 *
 * This intentionally sacrifices accuracy:
 * - No U/V pairing
 * - No AGI stall detection
 * - No FPU latency tracking
 * - No cache/memory timing differentiation
 * - No branch prediction modeling
 *
 * The benefit is that codegen_block_cycles stays tiny (or zero),
 * so generated code contains minimal or no cycle accounting,
 * and execution can run at near-native speed, with timers and
 * interrupts checked only coarsely in the outer exec loop.
 */

void
codegen_timing_fast_block_start(void)
{
    /* No state to reset for fast model */
}

void
codegen_timing_fast_start(void)
{
    /* No per-block timing state */
}

void
codegen_timing_fast_prefix(UNUSED(uint8_t prefix), UNUSED(uint32_t fetchdat))
{
    /* Count prefix as 1 cycle for minimal progress, or 0 for max speed.
       Using 0 avoids generating cycle subtraction code when block_cycles
       stays 0, which is fastest. However we need some cycles for the
       accurate path to make progress; fast exec loop overrides this anyway.
       So we count 1 here for compatibility with accurate exec loop,
       but fast exec loop will ignore block_cycles entirely. */
    codegen_block_cycles += 1;
}

void
codegen_timing_fast_opcode(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(int op_32), UNUSED(uint32_t op_pc))
{
    /* Fast model: 1 cycle per opcode. Could be 0 for even more speed,
       but 1 keeps TSC advancing in accurate mode. */
    codegen_block_cycles += 1;
}

void
codegen_timing_fast_block_end(void)
{
    /* Nothing */
}

int
codegen_timing_fast_jump_cycles(void)
{
    return 0;
}

codegen_timing_t codegen_timing_fast = {
    codegen_timing_fast_start,
    codegen_timing_fast_prefix,
    codegen_timing_fast_opcode,
    codegen_timing_fast_block_start,
    codegen_timing_fast_block_end,
    codegen_timing_fast_jump_cycles
};

/*
 * Even faster variant that reports 0 cycles per instruction.
 * This results in no cycle accounting code being emitted at all
 * (codegen_accumulate skips 0 deltas), giving maximum speed.
 * TSC is then advanced coarsely by the fast exec loop.
 */
void
codegen_timing_fast_zero_block_start(void)
{
}

void
codegen_timing_fast_zero_start(void)
{
}

void
codegen_timing_fast_zero_prefix(UNUSED(uint8_t prefix), UNUSED(uint32_t fetchdat))
{
    /* 0 cycles */
}

void
codegen_timing_fast_zero_opcode(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(int op_32), UNUSED(uint32_t op_pc))
{
    /* 0 cycles */
}

void
codegen_timing_fast_zero_block_end(void)
{
}

int
codegen_timing_fast_zero_jump_cycles(void)
{
    return 0;
}

codegen_timing_t codegen_timing_fast_zero = {
    codegen_timing_fast_zero_start,
    codegen_timing_fast_zero_prefix,
    codegen_timing_fast_zero_opcode,
    codegen_timing_fast_zero_block_start,
    codegen_timing_fast_zero_block_end,
    codegen_timing_fast_zero_jump_cycles
};

#endif /* USE_FAST_DYNAREC */
