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
#include "codegen_ops_sse_loadstore.h"
#include "codegen_ops_helpers.h"

uint32_t
ropMOVUPS_r_d(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOV(ir, IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropMOVUPS_d_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;
        uop_MOV(ir, IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        uop_MOVZX(ir, IREG_temp0_DQ, IREG_XMM(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropMOVAPS_r_d(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOV(ir, IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        uop_CHECK_ALIGN(ir);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropMOVAPS_d_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;
        uop_MOV(ir, IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        uop_CHECK_ALIGN(ir);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        uop_MOVZX(ir, IREG_temp0_DQ, IREG_XMM(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_DQ);
    }

    return op_pc + 1;
}