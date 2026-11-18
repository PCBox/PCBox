/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdint.h>
#include <stdio.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>
#include "codegen.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_ir.h"

static int
test_partial_spill(codeblock_t *block, int pressure)
{
    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();

    /* Give the spill slot a known old value, then change the whole register.
       The word write after eviction needs the upper half of the new value. */
    uop_MOV_IMM(ir, IREG_temp0, 0x12345678);
    uop_NOP_BARRIER(ir);
    uop_MOV(ir, IREG_EDI, IREG_temp0);
    uop_ADD_IMM(ir, IREG_temp0, IREG_ESI, 1);
    if (pressure) {
        /* These three guest registers occupy all byte-capable host slots. */
        uop_ADD(ir, IREG_EAX, IREG_ECX, IREG_EDX);
    } else
        uop_NOP_BARRIER(ir);
    uop_MOV_IMM(ir, IREG_temp0_W, 0xabcd);
    uop_MOV(ir, IREG_EBX, IREG_temp0);

    codegen_ir_compile(ir, block);
    codegen_allocator_clean_blocks(block->head_mem_block);
    ESI = 0x4567ffff;
    ((void (*)(void)) block->data)();
    if (EDI != 0x12345678 || EBX != 0x4568abcd) {
        fprintf(stderr, "Partial write after %s: EDI=%08x EBX=%08x\n",
                pressure ? "register pressure" : "barrier", EDI, EBX);
        return 1;
    }
    return 0;
}

#ifdef CODEGEN_BACKEND_HAS_MEM_REGS
static int
test_captured_spill(codeblock_t *block)
{
    codegen_mem_reg_state_t state = { .write_mask = 1, .reload_mask = 1 };
    codegen_reg_reset();
    block_write_data = block->data;
    codegen_backend_prologue(block);
    state.regs[0] = IREG_EAX;
    codegen_reg_sync_mem(block, &state, 1, 0);
    /* The capture says this temporary is live at the memory operation.
       The allocator has already consumed its reads when the stub is emitted. */
    state.regs[0] = IREG_temp0;
    codegen_reg_sync_mem(block, &state, 0, 0);
    state.regs[0] = IREG_EBX;
    codegen_reg_sync_mem(block, &state, 1, 0);
    state.regs[0] = IREG_temp0;
    codegen_reg_sync_mem(block, &state, 1, 0);
    state.regs[0] = IREG_EDX;
    codegen_reg_sync_mem(block, &state, 0, 0);
    codegen_backend_epilogue(block);
    codegen_allocator_clean_blocks(block->head_mem_block);
    EAX = 0xdeadbeef;
    EBX = EDX = 0;
    ((void (*)(void)) block->data)();
    if (EDX != EAX) {
        fprintf(stderr, "Captured spill: EDX=%08x expected %08x\n", EDX, EAX);
        return 1;
    }
    return 0;
}
#endif

int
test_register_allocation(codeblock_t *block)
{
    int failed = test_partial_spill(block, 0) || test_partial_spill(block, 1);
#ifdef CODEGEN_BACKEND_HAS_MEM_REGS
    failed |= test_captured_spill(block);
#endif
    return failed;
}
