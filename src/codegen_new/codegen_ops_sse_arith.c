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
#include "codegen_ops_jit_wrappers.h"
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

    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uop_sse_arith_packed(ir, op, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        uop_CHECK_ALIGN(ir);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_sse_arith_packed(ir, op, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

static uint32_t
rop_sse_arith_single(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, sse_arith_op_t op)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
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

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE2);
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

static uint32_t
rop_sse_minmax_packed(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int is_max)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        if (is_max)
            uop_MAXPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
        else
            uop_MINPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        uop_CHECK_ALIGN(ir);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        if (is_max)
            uop_MAXPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
        else
            uop_MINPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

static uint32_t
rop_sse_minmax_single(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int is_max)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        if (is_max)
            uop_MAXSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
        else
            uop_MINSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_temp0_DQ, IREG_temp0);
        if (is_max)
            uop_MAXSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
        else
            uop_MINSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropMINPS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_minmax_packed(block, ir, fetchdat, op_32, op_pc, 0);
}

uint32_t
ropMINSS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_minmax_single(block, ir, fetchdat, op_32, op_pc, 0);
}

uint32_t
ropMAXPS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_minmax_packed(block, ir, fetchdat, op_32, op_pc, 1);
}

uint32_t
ropMAXSS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_minmax_single(block, ir, fetchdat, op_32, op_pc, 1);
}

uint32_t
ropSQRTPS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        uop_SQRTPS(ir, IREG_XMM(dest_reg), IREG_XMM(fetchdat & 7));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        uop_CHECK_ALIGN(ir);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_SQRTPS(ir, IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropSQRTSS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        uop_SQRTSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(fetchdat & 7));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_temp0_DQ, IREG_temp0);
        uop_SQRTSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

static uint32_t
rop_sse_approx_packed(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int rsqrt)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        if (rsqrt)
            uop_RSQRTPS(ir, IREG_XMM(dest_reg), IREG_XMM(fetchdat & 7));
        else
            uop_RCPPS(ir, IREG_XMM(dest_reg), IREG_XMM(fetchdat & 7));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
        uop_CHECK_ALIGN(ir);
        uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
        if (rsqrt)
            uop_RSQRTPS(ir, IREG_XMM(dest_reg), IREG_temp0_DQ);
        else
            uop_RCPPS(ir, IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

static uint32_t
rop_sse_approx_single(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int rsqrt)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        if (rsqrt)
            uop_RSQRTSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(fetchdat & 7));
        else
            uop_RCPSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(fetchdat & 7));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_temp0_DQ, IREG_temp0);
        if (rsqrt)
            uop_RSQRTSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
        else
            uop_RCPSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropRCPPS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_approx_packed(block, ir, fetchdat, op_32, op_pc, 0);
}

uint32_t
ropRCPSS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_approx_single(block, ir, fetchdat, op_32, op_pc, 0);
}

uint32_t
ropRSQRTPS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_approx_packed(block, ir, fetchdat, op_32, op_pc, 1);
}

uint32_t
ropRSQRTSS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_approx_single(block, ir, fetchdat, op_32, op_pc, 1);
}

uint32_t
ropCVTSI2SS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        uop_CVTSI2SS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_32(fetchdat & 7));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_CVTSI2SS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0);
    }

    return op_pc + 1;
}

static uint32_t
rop_sse_single_to_int(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int truncate)
{
    int dest_reg = (fetchdat >> 3) & 7;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        if (truncate)
            uop_CVTTSS2SI(ir, IREG_32(dest_reg), IREG_XMM(fetchdat & 7));
        else
            uop_CVTSS2SI(ir, IREG_32(dest_reg), IREG_XMM(fetchdat & 7));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_temp0_DQ, IREG_temp0);
        if (truncate)
            uop_CVTTSS2SI(ir, IREG_32(dest_reg), IREG_temp0_DQ);
        else
            uop_CVTSS2SI(ir, IREG_32(dest_reg), IREG_temp0_DQ);
    }

    return op_pc + 1;
}

uint32_t
ropCVTTSS2SI(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_single_to_int(block, ir, fetchdat, op_32, op_pc, 1);
}

uint32_t
ropCVTSS2SI(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_single_to_int(block, ir, fetchdat, op_32, op_pc, 0);
}

uint32_t
ropCVTPI2PS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    if ((fetchdat & 0xc0) == 0xc0)
        uop_MMX_ENTER(ir);
    else
        uop_SSE_ENTER(ir);

    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        uop_CVTPI2PS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_MM(fetchdat & 7));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_CVTPI2PS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_Q);
    }

    return op_pc + 1;
}

static uint32_t
rop_sse_packed_to_mmx(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int truncate)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_MMX_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        if (truncate)
            uop_CVTTPS2PI(ir, IREG_MM(dest_reg), IREG_XMM(fetchdat & 7));
        else
            uop_CVTPS2PI(ir, IREG_MM(dest_reg), IREG_XMM(fetchdat & 7));
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
        uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);
        if (truncate)
            uop_CVTTPS2PI(ir, IREG_MM(dest_reg), IREG_temp0_Q);
        else
            uop_CVTPS2PI(ir, IREG_MM(dest_reg), IREG_temp0_Q);
    }

    return op_pc + 1;
}

uint32_t
ropCVTTPS2PI(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_packed_to_mmx(block, ir, fetchdat, op_32, op_pc, 1);
}

uint32_t
ropCVTPS2PI(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_packed_to_mmx(block, ir, fetchdat, op_32, op_pc, 0);
}

static uint32_t
rop_sse_cmp(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int scalar)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (!scalar && op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        int src_reg = fetchdat & 7;
        uint8_t imm = fastreadb(cs + op_pc + 1);
        if (scalar)
            uop_CMPSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg), imm);
        else
            uop_CMPPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg), imm);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        if (scalar) {
            CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
            uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
            uop_MOVZX(ir, IREG_temp0_DQ, IREG_temp0);
            uop_CMPSS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ, fastreadb(cs + op_pc + 1));
        } else {
            CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);
            uop_CHECK_ALIGN(ir);
            uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);
            uop_CMPPS(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ, fastreadb(cs + op_pc + 1));
        }
    }
    codegen_mark_code_present(block, cs + op_pc + 1, 1);

    return op_pc + 2;
}

uint32_t
ropCMPPS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_cmp(block, ir, fetchdat, op_32, op_pc, 0);
}

uint32_t
ropCMPSS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_cmp(block, ir, fetchdat, op_32, op_pc, 1);
}

static uint32_t
rop_sse_comiss(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc, int signaling)
{
    int dest_reg = (fetchdat >> 3) & 7;

    if (op_sse_xmm)
        return 0;

    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE);
    uop_SSE_ENTER(ir);
    uop_CALL_FUNC(ir, jit_flags_rebuild);
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) == 0xc0) {
        uop_COMISS(ir, IREG_flags, IREG_flags, IREG_XMM(dest_reg), IREG_XMM(fetchdat & 7), signaling);
    } else {
        x86seg *target_seg;

        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, target_seg);
        CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
        uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
        uop_MOVZX(ir, IREG_temp0_DQ, IREG_temp0);
        uop_COMISS(ir, IREG_flags, IREG_flags, IREG_XMM(dest_reg), IREG_temp0_DQ, signaling);
    }
    codegen_flags_changed = 0;

    return op_pc + 1;
}

uint32_t
ropUCOMISS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_comiss(block, ir, fetchdat, op_32, op_pc, 0);
}

uint32_t
ropCOMISS(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    return rop_sse_comiss(block, ir, fetchdat, op_32, op_pc, 1);
}
