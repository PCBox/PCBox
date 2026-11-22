/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Coarse block timing for the fast dynamic recompiler.
 *
 * Authors: Based on existing timing files by Sarah Walker, Miran Grca
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2020 Miran Grca.
 */

#include <stdio.h>
#include <stdint.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include "cpu.h"
#include "codegen.h"

/* A linked block or an unrolled iteration can run without returning to
   the dispatcher. Put the coarse charge in the block's timing accumulator
   so those paths also consume the slice and advance device timers. */
static void
fast_block_start(void)
{
    codegen_block_cycles = 128;
}

static void
fast_noop(void)
{
}

static void
fast_prefix(UNUSED(uint8_t prefix), UNUSED(uint32_t fetchdat))
{
}

static void
fast_opcode(UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), UNUSED(int op_32), UNUSED(uint32_t op_pc))
{
}

static int
fast_jump_cycles(void)
{
    return 0;
}

codegen_timing_t codegen_timing_fast = {
    .start       = fast_noop,
    .prefix      = fast_prefix,
    .opcode      = fast_opcode,
    .block_start = fast_block_start,
    .block_end   = fast_noop,
    .jump_cycles = fast_jump_cycles
};
