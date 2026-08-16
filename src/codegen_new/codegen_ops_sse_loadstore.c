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

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
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
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropMOVUPS_d_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
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
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_XMM(src_reg));
    }

    return op_pc + 1;
}

uint32_t
ropMOVSS_r_d(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_XMM(dest_reg), IREG_temp0);
    }

    return op_pc + 1;
}

uint32_t
ropMOVSS_d_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;
        uop_MOVSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MOVZX(ir, IREG_temp0, IREG_XMM(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
    }

    return op_pc + 1;
}

uint32_t
ropMOVSD_r_d(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE2);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVSD(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVSD(ir, IREG_XMM(dest_reg), IREG_temp0_Q, IREG_temp0_Q);
    }

    return op_pc + 1;
}

uint32_t
ropMOVSD_d_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE2);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int dest_reg = fetchdat & 7;
        uop_MOVSD(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MOVSD(ir, IREG_temp0_Q, IREG_XMM(src_reg), IREG_XMM(src_reg));
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_Q);
    }

    return op_pc + 1;
}

uint32_t
ropUNPCKLPS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        if (op_sse_xmm)
            uop_UNPCKLPD(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
        else
            uop_UNPCKLPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        uop_CHECK_ALIGN(ir);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        if (op_sse_xmm)
            uop_UNPCKLPD(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
        else
            uop_UNPCKLPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropUNPCKHPS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        if (op_sse_xmm)
            uop_UNPCKHPD(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
        else
            uop_UNPCKHPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        uop_CHECK_ALIGN(ir);
        codegen_check_seg_read(block, ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        if (op_sse_xmm)
            uop_UNPCKHPD(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
        else
            uop_UNPCKHPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropMOVAPS_r_d(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
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
        uop_CHECK_ALIGN(ir);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropMOVAPS_d_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
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
        uop_CHECK_ALIGN(ir);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_XMM(src_reg));
    }

    return op_pc + 1;
}

uint32_t
ropMOVLPS_r_q(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVHLPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVSD(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_Q);
    }

    return op_pc + 1;
}

uint32_t
ropMOVLPS_q_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int     src_reg = (fetchdat >> 3) & 7;
    x86seg *target_seg;

    if (op_sse_xmm || (fetchdat & 0xc0) == 0xc0)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_XMM_Q(src_reg));

    return op_pc + 1;
}

uint32_t
ropMOVHPS_r_q(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_MOVLHPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVLHPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_Q);
    }

    return op_pc + 1;
}

uint32_t
ropMOVHPS_q_r(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int     src_reg = (fetchdat >> 3) & 7;
    x86seg *target_seg;

    if (op_sse_xmm || (fetchdat & 0xc0) == 0xc0)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_check_seg_write(block, ir, target_seg);
    CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
    uop_MOVHLPS(ir, IREG_temp0_DQ, IREG_XMM(src_reg), IREG_XMM(src_reg));
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_DQ_LO_Q);

    return op_pc + 1;
}

uint32_t
ropMOVMSKPS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    if (op_sse_xmm || (fetchdat & 0xc0) != 0xc0)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    uop_MOVMSKPS(ir, IREG_32((fetchdat >> 3) & 7), IREG_XMM(fetchdat & 7));
    return op_pc + 1;
}

uint32_t
ropSHUFPS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int     dest_reg = (fetchdat >> 3) & 7;
    x86seg *target_seg;

    if (op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        uop_SHUFPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(fetchdat & 7), fastreadb(cs + op_pc + 1));
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        uop_CHECK_ALIGN(ir);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SHUFPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ, fastreadb(cs + op_pc + 1));
    }
    codegen_mark_code_present(block, cs + op_pc + 1, 1);

    return op_pc + 2;
}
