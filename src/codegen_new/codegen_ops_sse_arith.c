#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_flags.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_sse_arith.h"
#include "codegen_ops_helpers.h"

typedef enum sse_arith_op_t {
    SSE_ARITH_ADD,
    SSE_ARITH_MUL,
    SSE_ARITH_SUB,
    SSE_ARITH_DIV
} sse_arith_op_t;

static void
uop_sse_arith_packed(ir_data_t *ir, sse_arith_op_t op, int dst_reg, int src_reg_a, int src_reg_b)
{
    if (op_sse_xmm) {
        switch (op) {
            case SSE_ARITH_ADD:
                uop_ADDPD(ir, dst_reg, src_reg_a, src_reg_b);
                break;
            case SSE_ARITH_MUL:
                uop_MULPD(ir, dst_reg, src_reg_a, src_reg_b);
                break;
            case SSE_ARITH_SUB:
                uop_SUBPD(ir, dst_reg, src_reg_a, src_reg_b);
                break;
            case SSE_ARITH_DIV:
                uop_DIVPD(ir, dst_reg, src_reg_a, src_reg_b);
                break;
        }
    } else {
        switch (op) {
            case SSE_ARITH_ADD:
                uop_ADDPS(ir, dst_reg, src_reg_a, src_reg_b);
                break;
            case SSE_ARITH_MUL:
                uop_MULPS(ir, dst_reg, src_reg_a, src_reg_b);
                break;
            case SSE_ARITH_SUB:
                uop_SUBPS(ir, dst_reg, src_reg_a, src_reg_b);
                break;
            case SSE_ARITH_DIV:
                uop_DIVPS(ir, dst_reg, src_reg_a, src_reg_b);
                break;
        }
    }
}

static void
uop_sse_arith_single(ir_data_t *ir, sse_arith_op_t op, int dst_reg, int src_reg_a, int src_reg_b)
{
    switch (op) {
        case SSE_ARITH_ADD:
            uop_ADDSS(ir, dst_reg, src_reg_a, src_reg_b);
            break;
        case SSE_ARITH_MUL:
            uop_MULSS(ir, dst_reg, src_reg_a, src_reg_b);
            break;
        case SSE_ARITH_SUB:
            uop_SUBSS(ir, dst_reg, src_reg_a, src_reg_b);
            break;
        case SSE_ARITH_DIV:
            uop_DIVSS(ir, dst_reg, src_reg_a, src_reg_b);
            break;
    }
}

static void
uop_sse_arith_double(ir_data_t *ir, sse_arith_op_t op, int dst_reg, int src_reg_a, int src_reg_b)
{
    switch (op) {
        case SSE_ARITH_ADD:
            uop_ADDSD(ir, dst_reg, src_reg_a, src_reg_b);
            break;
        case SSE_ARITH_MUL:
            uop_MULSD(ir, dst_reg, src_reg_a, src_reg_b);
            break;
        case SSE_ARITH_SUB:
            uop_SUBSD(ir, dst_reg, src_reg_a, src_reg_b);
            break;
        case SSE_ARITH_DIV:
            uop_DIVSD(ir, dst_reg, src_reg_a, src_reg_b);
            break;
    }
}

static uint32_t
rop_sse_arith_packed(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, sse_arith_op_t op)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (op_sse_xmm && !(cpu_features & CPU_FEATURE_SSE2))
        return 0;

    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_sse_arith_packed(ir, op, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        uop_CHECK_ALIGN(ir);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_sse_arith_packed(ir, op, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

static uint32_t
rop_sse_arith_single(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, sse_arith_op_t op)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_sse_arith_single(ir, op, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_temp0_DQ, IREG_temp0);
        uop_sse_arith_single(ir, op, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

static uint32_t
rop_sse_arith_double(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, sse_arith_op_t op)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_sse_arith_double(ir, op, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_sse_arith_double(ir, op, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_Q);
    }

    return op_pc + 1;
}

uint32_t
ropADDPS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_packed(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_ADD);
}

uint32_t
ropADDSS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_single(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_ADD);
}

uint32_t
ropADDSD(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_double(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_ADD);
}

uint32_t
ropMULPS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_packed(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_MUL);
}

uint32_t
ropMULSS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_single(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_MUL);
}

uint32_t
ropMULSD(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_double(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_MUL);
}

uint32_t
ropSUBPS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_packed(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_SUB);
}

uint32_t
ropSUBSS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_single(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_SUB);
}

uint32_t
ropSUBSD(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_double(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_SUB);
}

uint32_t
ropDIVPS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_packed(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_DIV);
}

uint32_t
ropDIVSS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_single(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_DIV);
}

uint32_t
ropDIVSD(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_arith_double(block, ir, opcode, fetchdat, op_32, op_pc, SSE_ARITH_DIV);
}
