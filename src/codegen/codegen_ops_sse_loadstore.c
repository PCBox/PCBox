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
ropMOVUPS_r_d(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE((opcode == 0x6f || op_sse_xmm) ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
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
ropMOVUPS_d_r(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE((opcode == 0x7f || op_sse_xmm) ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
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
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
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

/* F3/F2 0F D6 transfer between the MMX and XMM register files. */
static uint32_t
rop_mmx_xmm_move(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_pc, int to_xmm)
{
    int dest = (fetchdat >> 3) & 7;
    int src = fetchdat & 7;

    if ((fetchdat & 0xc0) != 0xc0)
        return 0;
    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE2);
    REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
    uop_SSE_ENTER(ir);
    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if (to_xmm) {
        uop_MOVZX(ir, IREG_XMM(dest), IREG_MM(src));
    } else {
        uop_MOV(ir, IREG_MM(dest), IREG_XMM_Q(src));
        uop_STORE_PTR_IMM_16(ir, MMEP[dest], 0xffff);
    }
    return op_pc + 1;
}

uint32_t
ropMOVQ2DQ(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    return rop_mmx_xmm_move(block, ir, fetchdat, op_pc, 1);
}

uint32_t
ropMOVDQ2Q(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    return rop_mmx_xmm_move(block, ir, fetchdat, op_pc, 0);
}

/* F3 0F 7E loads the low qword, 66 0F D6 stores it. Both register
   forms clear the high qword, including moves to the same register. */
uint32_t
ropMOVQ_xmm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int reg = (fetchdat >> 3) & 7;
    int rm = fetchdat & 7;
    int store = opcode == 0xd6;

    if (store && !op_sse_xmm)
        return 0;
    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE2);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        uop_MOVZX(ir, IREG_XMM(store ? rm : reg), IREG_XMM_Q(store ? reg : rm));
    } else {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        x86seg *seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        if (store) {
            codegen_check_seg_write(block, ir, seg);
            CHECK_SEG_LIMITS(block, ir, seg, IREG_eaaddr, 7);
            uop_MEM_STORE_REG(ir, ireg_seg_base(seg), IREG_eaaddr, IREG_XMM_Q(reg));
        } else {
            codegen_check_seg_read(block, ir, seg);
            CHECK_SEG_LIMITS(block, ir, seg, IREG_eaaddr, 7);
            uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(seg), IREG_eaaddr);
            uop_MOVZX(ir, IREG_XMM(reg), IREG_temp0_Q);
        }
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
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
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
ropUNPCKLPS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (opcode == 0x6c && !op_sse_xmm)
        return 0;
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
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        codegen_check_sse_align(ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        if (op_sse_xmm)
            uop_UNPCKLPD(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
        else
            uop_UNPCKLPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropUNPCKHPS(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (opcode == 0x6d && !op_sse_xmm)
        return 0;
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
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        codegen_check_sse_align(ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        if (op_sse_xmm)
            uop_UNPCKHPD(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
        else
            uop_UNPCKHPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropMOVAPS_r_d(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
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
        if (opcode == 0x6f) /* MOVDQA */
            codegen_check_sse_align(ir, target_seg);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        if (opcode != 0x6f)
            codegen_check_sse_align(ir, target_seg);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOV(ir, IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropMOVAPS_d_r(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int src_reg = (fetchdat >> 3) & 7;

    /* 0F E7 without 66 is the MMX MOVNTQ. */
    if (opcode == 0xe7 && !op_sse_xmm)
        return 0;
    if ((opcode == 0x2b || opcode == 0xe7) && (fetchdat & 0xc0) == 0xc0)
        return 0;

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
        if (opcode == 0x7f || opcode == 0xe7) /* MOVDQA / MOVNTDQ */
            codegen_check_sse_align(ir, target_seg);
        codegen_check_seg_write(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        if (opcode != 0x7f && opcode != 0xe7)
            codegen_check_sse_align(ir, target_seg);
        uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_XMM(src_reg));
    }

    return op_pc + 1;
}

uint32_t
ropMOVLPS_r_q(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (op_sse_xmm && (fetchdat & 0xc0) == 0xc0)
        return 0;

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
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

    if ((fetchdat & 0xc0) == 0xc0)
        return 0;

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
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

    if (op_sse_xmm && (fetchdat & 0xc0) == 0xc0)
        return 0;

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
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

    if ((fetchdat & 0xc0) == 0xc0)
        return 0;

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
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
    if ((fetchdat & 0xc0) != 0xc0)
        return 0;

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    int dest = IREG_32((fetchdat >> 3) & 7);
    int src = IREG_XMM(fetchdat & 7);
    if (op_sse_xmm)
        uop_MOVMSKPD(ir, dest, src);
    else
        uop_MOVMSKPS(ir, dest, src);
    return op_pc + 1;
}

static uint32_t
rop_shuffle(codeblock_t *block, ir_data_t *ir, uint32_t operation, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest = IREG_XMM((fetchdat >> 3) & 7);
    int src = IREG_XMM(fetchdat & 7);
    x86seg *seg = NULL;
    int word_shuffle = operation == UOP_PSHUFW || operation == UOP_PSHUFHW;

    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        return 0;

    REQUIRE_GUEST_FEATURE(operation == UOP_SHUFPS ? CPU_FEATURE_SSE : CPU_FEATURE_SSE2);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    }
    /* PSHUFLW/PSHUFHW fetch the selector before accessing the memory operand. */
    if (word_shuffle)
        codegen_mark_code_present(block, cs + op_pc + 1, 1);
    if (seg) {
        codegen_check_seg_read(block, ir, seg);
        CHECK_SEG_LIMITS(block, ir, seg, IREG_eaaddr, 15);
        codegen_check_sse_align(ir, seg);
        src = IREG_temp0_DQ;
        uop_MEM_LOAD_REG(ir, src, ireg_seg_base(seg), IREG_eaaddr);
    }

    uint8_t imm = fastreadb(cs + op_pc + 1);
    if (!word_shuffle)
        codegen_mark_code_present(block, cs + op_pc + 1, 1);
    if (operation == UOP_SHUFPS || operation == UOP_SHUFPD)
        uop_gen_reg_dst_src2_imm(operation, ir, dest, dest, src, imm);
    else
        uop_gen_reg_dst_src1_imm(operation, ir, dest, src, imm);
    return op_pc + 2;
}

uint32_t
ropSHUF_xmm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    uint32_t operation = opcode == 0x70 ? UOP_PSHUFD : (op_sse_xmm ? UOP_SHUFPD : UOP_SHUFPS);
    return rop_shuffle(block, ir, operation, fetchdat, op_32, op_pc);
}

uint32_t
ropPSHUFLW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_shuffle(block, ir, UOP_PSHUFW, fetchdat, op_32, op_pc);
}

uint32_t
ropPSHUFHW(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_shuffle(block, ir, UOP_PSHUFHW, fetchdat, op_32, op_pc);
}

uint32_t
ropMASKMOVDQU(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    if (!op_sse_xmm || (fetchdat & 0xc0) != 0xc0)
        return 0;
    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE2);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    if (op_32 & 0x200)
        uop_MOV(ir, IREG_eaaddr, IREG_EDI);
    else
        uop_MOVZX(ir, IREG_eaaddr, IREG_DI);
    codegen_check_seg_write(block, ir, op_ea_seg);
    CHECK_SEG_LIMITS(block, ir, op_ea_seg, IREG_eaaddr, 15);
    uop_PMOVMSKB(ir, IREG_temp1, IREG_XMM(fetchdat & 7));
    int empty = uop_CMP_IMM_JZ_DEST(ir, IREG_temp1, 0);
    uop_ADD(ir, IREG_temp0, ireg_seg_base(op_ea_seg), IREG_eaaddr);
    uop_LOAD_FUNC_ARG_REG(ir, 0, IREG_temp0);
    uop_LOAD_FUNC_ARG_IMM(ir, 1, fetchdat & 0xff);
    uop_CALL_FUNC_RESULT(ir, IREG_temp0, x86_maskmovdqu_store);
    codegen_exit_on_one(ir, IREG_temp0);
    uop_set_jump_dest(ir, empty);
    return op_pc + 1;
}
