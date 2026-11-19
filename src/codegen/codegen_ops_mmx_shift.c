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
#include "codegen_ops_mmx_shift.h"
#include "codegen_ops_helpers.h"

uint32_t
ropPshift_imm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, UNUSED(uint32_t op_32), uint32_t op_pc)
{
    /* 0F 71/72/73 select the lane width, ModRM.reg selects the operation. */
    static const uint32_t shifts[3][8] = {
        { [2] = UOP_PSRLW_IMM, [4] = UOP_PSRAW_IMM, [6] = UOP_PSLLW_IMM },
        { [2] = UOP_PSRLD_IMM, [4] = UOP_PSRAD_IMM, [6] = UOP_PSLLD_IMM },
        { [2] = UOP_PSRLQ_IMM, [3] = UOP_PSRLDQ_IMM, [6] = UOP_PSLLQ_IMM, [7] = UOP_PSLLDQ_IMM },
    };
    uint32_t shift = shifts[opcode - 0x71][(fetchdat >> 3) & 7];
    if (!shift || (fetchdat & 0xc0) != 0xc0 || (block->flags & CODEBLOCK_NO_IMMEDIATES))
        return 0;
    if (!op_sse_xmm && (shift == UOP_PSRLDQ_IMM || shift == UOP_PSLLDQ_IMM))
        return 0;
    REQUIRE_GUEST_FEATURE(op_sse_xmm ? CPU_FEATURE_SSE2 : CPU_FEATURE_MMX);

    uint8_t count = fastreadb(cs + op_pc + 1);
    if (cpu_state.abrt)
        return 0;
    if (op_sse_xmm)
        uop_SSE_ENTER(ir);
    else
        uop_MMX_ENTER(ir);
    int dest = op_sse_xmm ? IREG_XMM(fetchdat & 7) : IREG_MM(fetchdat & 7);
    codegen_mark_code_present(block, cs + op_pc, 2);
    uop_gen_reg_dst_src_imm(shift, ir, dest, dest, count);
    return op_pc + 2;
}

uint32_t
ropPshift_xmm(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    if (!op_sse_xmm)
        return 0;
    REQUIRE_GUEST_FEATURE(CPU_FEATURE_SSE2);
    uop_SSE_ENTER(ir);
    codegen_mark_code_present(block, cs + op_pc, 1);

    int dest = IREG_XMM((fetchdat >> 3) & 7);
    int count = IREG_XMM_Q(fetchdat & 7);
    if ((fetchdat & 0xc0) != 0xc0) {
        uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
        x86seg *seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
        codegen_check_seg_read(block, ir, seg);
        /* The m128 operand is checked in full. Its low qword supplies the count. */
        CHECK_SEG_LIMITS(block, ir, seg, IREG_eaaddr, 15);
        codegen_check_sse_align(ir, seg);
        count = IREG_temp0_Q;
        uop_MEM_LOAD_REG(ir, count, ireg_seg_base(seg), IREG_eaaddr);
    }
    uop_gen_reg_dst_src2_imm(UOP_PSHIFT_XMM, ir, dest, dest, count, opcode);
    return op_pc + 1;
}
