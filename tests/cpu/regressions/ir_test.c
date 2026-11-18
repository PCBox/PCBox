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
test_branch_merge(codeblock_t *block, int reg)
{
    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    if (reg == IREG_temp0)
        uop_ADD_IMM(ir, reg, IREG_ESI, 1);
    int branch = uop_CMP_IMM_JZ_DEST(ir, IREG_ECX, 0);
    uop_ADD_IMM(ir, reg, IREG_ESI, 7);
    uop_set_jump_dest(ir, branch);
    uop_MOV(ir, IREG_EAX, reg);
    codegen_ir_compile(ir, block);
    codegen_allocator_clean_blocks(block->head_mem_block);

    for (int path = 0; path < 2; path++) {
        ECX = path;
        ESI = 0x12345670;
        EDI = ESI + 1;
        uint32_t expected = ESI + (path ? 7 : 1);
        ((void (*)(void)) block->data)();
        if (EAX != expected) {
            fprintf(stderr, "IR merge of register %x, ECX=%d: EAX=%08x expected %08x\n",
                    reg, path, EAX, expected);
            return 1;
        }
    }
    return 0;
}

static int
test_branch_to_branch(codeblock_t *block, int at_end)
{
    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    int first = uop_CMP_IMM_JZ_DEST(ir, IREG_ECX, 0);
    uop_MOV_IMM(ir, IREG_EAX, 1);
    int other = uop_CMP_IMM_JZ_DEST(ir, IREG_ESI, 0);
    uop_set_jump_dest(ir, first);
    int second = uop_CMP_IMM_JZ_DEST(ir, IREG_EDX, 0);
    uop_MOV_IMM(ir, IREG_EBX, 2);
    uop_set_jump_dest(ir, second);
    uop_set_jump_dest(ir, other);
    if (!at_end)
        uop_MOV_IMM(ir, IREG_EDI, 3);
    codegen_ir_compile(ir, block);
    codegen_allocator_clean_blocks(block->head_mem_block);

    for (int path = 0; path < 8; path++) {
        ECX = path & 1;
        EDX = (path >> 1) & 1;
        ESI = path >> 2;
        EAX = EBX = EDI = 0;
        uint32_t expected = EDX && (!ECX || ESI) ? 2 : 0;
        ((void (*)(void)) block->data)();
        if (EAX != ECX || EBX != expected || EDI != (at_end ? 0 : 3)) {
            fprintf(stderr, "IR branch to branch, path=%d at_end=%d: EAX=%u EBX=%u EDI=%u\n",
                    path, at_end, EAX, EBX, EDI);
            return 1;
        }
    }
    return 0;
}

static int
test_cmov_operands(codeblock_t *block, int dest, int word, int preload)
{
    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    int size = word ? IREG_SIZE_W : IREG_SIZE_L;
    if (preload)
        uop_ADD_IMM(ir, IREG_EBX, IREG_EBX, 0);
    uop_CMOVNZ(ir, dest | size, IREG_EAX | size, IREG_EDX | size, IREG_ECX);
    uop_MOV(ir, IREG_ESI, dest);
    uop_MOVZX(ir, IREG_EDI, dest | IREG_SIZE_BH);
    codegen_ir_compile(ir, block);
    codegen_allocator_clean_blocks(block->head_mem_block);

    for (int path = 0; path < 2; path++) {
        EAX = 0x11223344;
        EDX = 0x55667788;
        ECX = path;
        EBX = 0x99aabbcc;
        uint32_t expected = path ? EDX : EAX;
        if (word)
            expected = (cpu_state.regs[dest].l & 0xffff0000) | (expected & 0xffff);
        ((void (*)(void)) block->data)();
        if (ESI != expected || EDI != ((expected >> 8) & 0xff)) {
            fprintf(stderr, "IR CMOVNZ dest=%x word=%d preloaded=%d condition=%d: result=%08x high byte=%x expected=%08x\n",
                    dest, word, preload, path, ESI, EDI, expected);
            return 1;
        }
    }
    return 0;
}

static int
test_dead_producers(codeblock_t *block)
{
    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    uop_MOV_IMM(ir, IREG_EDI, 17);
    uop_NOP_BARRIER(ir);
    int first = ir->wr_pos;
    uop_ADD_IMM(ir, IREG_temp0, IREG_EDI, 1);
    uop_ADD(ir, IREG_temp1, IREG_temp0, IREG_temp0);
    uop_XOR(ir, IREG_temp2, IREG_temp1, IREG_temp0);
    uop_MOV_IMM(ir, IREG_temp0, 3);
    uop_MOV_IMM(ir, IREG_temp1, 4);
    uop_MOV_IMM(ir, IREG_temp2, 5);
    codegen_ir_compile(ir, block);
    codegen_allocator_clean_blocks(block->head_mem_block);
    EDI = 0;
    ((void (*)(void)) block->data)();
    if (ir->uops[first].type != UOP_INVALID || ir->uops[first + 1].type != UOP_INVALID ||
        ir->uops[first + 2].type != UOP_INVALID || EDI != 17) {
        fprintf(stderr, "IR dead producer chain: %x %x %x, EDI=%u\n",
                ir->uops[first].type, ir->uops[first + 1].type, ir->uops[first + 2].type, EDI);
        return 1;
    }
    return 0;
}

#ifdef CODEGEN_BACKEND_HAS_DIVMOD
static int
test_divmod_results(codeblock_t *block)
{
    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    uop_DIVMOD(ir, IREG_temp0, IREG_EAX, IREG_EDX, IREG_ECX, 32);
    uop_DIV_RESULT(ir, IREG_EAX, 0);
    uop_DIV_RESULT(ir, IREG_EDX, 1);
    uop_MOV_IMM(ir, IREG_temp0, 0);
    codegen_ir_compile(ir, block);
    codegen_allocator_clean_blocks(block->head_mem_block);
    EAX = 73;
    EDX = 0;
    ECX = 8;
    ((void (*)(void)) block->data)();
    if (EAX != 9 || EDX != 1) {
        fprintf(stderr, "IR division with unused status: quotient=%u remainder=%u\n", EAX, EDX);
        return 1;
    }
    return 0;
}
#endif

int
test_ir(codeblock_t *block)
{
    int failed = test_branch_merge(block, IREG_EDI);
    failed |= test_branch_merge(block, IREG_temp0);
    failed |= test_branch_to_branch(block, 0);
    failed |= test_branch_to_branch(block, 1);
    for (int word = 0; word <= 1; word++) {
        failed |= test_cmov_operands(block, IREG_ECX, word, 0);
        failed |= test_cmov_operands(block, IREG_EDX, word, 0);
        failed |= test_cmov_operands(block, IREG_EBX, word, 0);
        failed |= test_cmov_operands(block, IREG_EBX, word, 1);
    }
    failed |= test_dead_producers(block);
#ifdef CODEGEN_BACKEND_HAS_DIVMOD
    failed |= test_divmod_results(block);
#endif
    return failed;
}
