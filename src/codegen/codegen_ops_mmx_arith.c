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
#include "codegen_ops_mmx_arith.h"
#include "codegen_ops_helpers.h"

#define ropParith(func, mmx_feature)                                                               \
    uint32_t rop##func(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),                  \
                       uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                          \
    {                                                                                              \
        int dest_reg = (fetchdat >> 3) & 7;                                                        \
        REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : (mmx_feature));                      \
        if(!op_sse_xmm)                                                                            \
        {                                                                                          \
            uop_MMX_ENTER(ir);                                                                         \
            codegen_mark_code_present(block, cs + op_pc, 1);                                           \
            if ((fetchdat & 0xc0) == 0xc0) {                                                           \
                int src_reg = fetchdat & 7;                                                            \
                uop_##func(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_MM(src_reg));                \
            } else {                                                                                   \
                x86seg *target_seg;                                                                    \
                                                                                                   \
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
                codegen_check_seg_read(block, ir, target_seg);                                         \
                CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);                              \
                uop_MEM_LOAD_REG(ir, IREG_temp0_Q, ireg_seg_base(target_seg), IREG_eaaddr);            \
                uop_##func(ir, IREG_MM(dest_reg), IREG_MM(dest_reg), IREG_temp0_Q);                    \
            } \
        }                                                                                          \
        else \
        { \
            uop_SSE_ENTER(ir); \
            codegen_mark_code_present(block, cs + op_pc, 1); \
            if ((fetchdat & 0xc0) == 0xc0) {                                                           \
                int src_reg = fetchdat & 7;                                                            \
                uop_##func(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_XMM(src_reg));                \
            } else {                                                                                   \
                x86seg *target_seg;                                                                    \
                                                                                                   \
                uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);                                          \
                target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0); \
                codegen_check_seg_read(block, ir, target_seg);                                         \
                CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 15);                              \
                codegen_check_sse_align(ir, target_seg); \
                uop_MEM_LOAD_REG(ir, IREG_temp0_DQ, ireg_seg_base(target_seg), IREG_eaaddr);            \
                uop_##func(ir, IREG_XMM(dest_reg), IREG_XMM(dest_reg), IREG_temp0_DQ);                    \
            } \
        } \
                                                                                                   \
        return op_pc + 1;                                                                          \
    }

/* Both encodings use ModRM.reg as the destination. The unprefixed forms
   operate on MMX registers and the 66-prefixed forms on XMM registers. */
static uint32_t
rop_packed_arith(codeblock_t *block, ir_data_t *ir, uint32_t fetchdat, uint32_t op_32,
                 uint32_t op_pc, uint32_t operation, int mmx_feature, int align_first)
{
    int reg = (fetchdat >> 3) & 7;
    int rm = fetchdat & 7;
    int dest = op_sse_xmm ? IREG_XMM(reg) : IREG_MM(reg);
    int src = op_sse_xmm ? IREG_XMM(rm) : IREG_MM(rm);

    if (op_sse_xmm) {
        REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE2);
        uop_SSE_ENTER(ir);
    } else {
        REQUIRE_GUEST_FEATURE(CPU_FEATURE_MMX);
        REQUIRE_GUEST_FEATURE(mmx_feature);
        uop_MMX_ENTER(ir);
    }
    codegen_mark_code_present(block, cs + op_pc, 1);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        x86seg *seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        /* Match the interpreter's #GP versus #SS priority. */
        if (op_sse_xmm && align_first)
            codegen_check_sse_align(ir, seg);
        codegen_check_seg_read(block, ir, seg);
        CHECK_SEG_LIMITS(block, ir, seg, IREG_eaaddr, op_sse_xmm ? 15 : 7);
        if (op_sse_xmm && !align_first)
            codegen_check_sse_align(ir, seg);
        src = op_sse_xmm ? IREG_temp0_DQ : IREG_temp0_Q;
        uop_MEM_LOAD_REG(ir, src, ireg_seg_base(seg), IREG_eaaddr);
    }
    uop_gen_reg_dst_src2(operation, ir, dest, dest, src);
    if (!op_sse_xmm)
        uop_STORE_PTR_IMM_16(ir, MMEP[reg], 0xffff);
    return op_pc + 1;
}

#define PACKED_ARITH(name, operation, mmx_feature, align_first)                               \
    uint32_t rop##name(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode),             \
                       uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)                     \
    {                                                                                         \
        return rop_packed_arith(block, ir, fetchdat, op_32, op_pc, operation,                 \
                                mmx_feature, align_first);                                    \
    }

// clang-format off
ropParith(PADDB, CPU_FEATURE_MMX)
ropParith(PADDW, CPU_FEATURE_MMX)
ropParith(PADDD, CPU_FEATURE_MMX)
ropParith(PADDSB, CPU_FEATURE_MMX)
ropParith(PADDSW, CPU_FEATURE_MMX)
ropParith(PADDUSB, CPU_FEATURE_MMX)
ropParith(PADDUSW, CPU_FEATURE_MMX)

ropParith(PSUBB, CPU_FEATURE_MMX)
ropParith(PSUBW, CPU_FEATURE_MMX)
ropParith(PSUBD, CPU_FEATURE_MMX)
ropParith(PSUBSB, CPU_FEATURE_MMX)
ropParith(PSUBSW, CPU_FEATURE_MMX)
ropParith(PSUBUSB, CPU_FEATURE_MMX)
ropParith(PSUBUSW, CPU_FEATURE_MMX)

ropParith(PMADDWD, CPU_FEATURE_MMX)
ropParith(PMULHW, CPU_FEATURE_MMX)
ropParith(PMULLW, CPU_FEATURE_MMX)

ropParith(PADDQ, CPU_FEATURE_SSE2)
ropParith(PSUBQ, CPU_FEATURE_SSE2)

PACKED_ARITH(PMINUB,  UOP_PMINUB,  CPU_FEATURE_SSE,  0)
PACKED_ARITH(PMAXUB,  UOP_PMAXUB,  CPU_FEATURE_SSE,  0)
PACKED_ARITH(PAVGB,   UOP_PAVGUSB, CPU_FEATURE_SSE,  1)
PACKED_ARITH(PAVGW,   UOP_PAVGW,   CPU_FEATURE_SSE,  1)
PACKED_ARITH(PMULHUW, UOP_PMULHUW, CPU_FEATURE_SSE,  1)
PACKED_ARITH(PMINSW,  UOP_PMINSW,  CPU_FEATURE_SSE,  1)
PACKED_ARITH(PMAXSW,  UOP_PMAXSW,  CPU_FEATURE_SSE,  1)
PACKED_ARITH(PSADBW,  UOP_PSADBW,  CPU_FEATURE_SSE,  1)
PACKED_ARITH(PMULUDQ, UOP_PMULUDQ, CPU_FEATURE_SSE2, 0)
// clang-format on
