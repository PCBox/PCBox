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
#include "codegen_ops_mmx_loadstore.h"
#include "codegen_ops_helpers.h"

uint32_t
ropMOVD_r_d(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if(op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVZX(ir, IREG_MM(dest_reg), IREG_32(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_MM(dest_reg), IREG_temp0);
    }

    return op_pc + 1;
}
uint32_t
ropMOVD_d_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    if(op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
    if (cpu_iscyrix && in_smm)
        return 0;

    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;
        uop_MOVZX(ir, IREG_32(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MOVZX(ir, IREG_temp0, IREG_MM(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
    }

    return op_pc + 1;
}

uint32_t
ropMOVQ_r_q(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if(op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOV(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_MM(dest_reg), ireg_seg_base(target_seg), IREG_eaaddr);
    }

    return op_pc + 1;
}

uint32_t
ropMOVQ_q_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    if(op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;
        uop_MOV(ir, IREG_MM(dest_reg), IREG_MM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_MM(src_reg));
    }

    return op_pc + 1;
}

uint32_t
ropPSHUFW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int     dest_reg = (fetchdat >> 3) & 7;
    int     src_reg;
    uint8_t imm;

    if (op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        src_reg = IREG_MM(fetchdat & 7);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        src_reg = IREG_temp0_Q;
    }

    imm = fastreadb(cs + op_pc + 1);
    uop_PSHUFW(ir, IREG_MM(dest_reg), src_reg, imm);
    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPINSRW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int     dest_reg = (fetchdat >> 3) & 7;
    int     src_reg;
    uint8_t imm;

    if (op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        src_reg = IREG_16(fetchdat & 7);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
        uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
        src_reg = IREG_temp0_W;
    }

    imm = fastreadb(cs + op_pc + 1) & 3;
    uop_PINSRW(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), src_reg, imm);
    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPEXTRW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    uint8_t imm;

    if (op_sse_xmm || (fetchdat & 0xc0) != 0xc0)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    imm = fastreadb(cs + op_pc + 1) & 3;
    uop_PEXTRW(ir, IREG_32((fetchdat >> 3) & 7), IREG_MM(fetchdat & 7), imm);
    codegen_mark_code_present(block, cs + op_pc + 1, 1);
    return op_pc + 2;
}

uint32_t
ropPMOVMSKB(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    if (op_sse_xmm || (fetchdat & 0xc0) != 0xc0)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    uop_PMOVMSKB(ir, IREG_32((fetchdat >> 3) & 7), IREG_MM(fetchdat & 7));
    return op_pc + 1;
}
