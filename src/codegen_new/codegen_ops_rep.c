#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "x86_flags.h"
#include "codegen.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_jit_wrappers.h"
#include "codegen_ops_rep.h"

static int
repstr_movsb_safe_to_recompile(uint32_t op_32, x86seg *source_seg)
{
    uint32_t count;
    uint32_t src_addr;
    uint32_t dst_addr;

    if (!source_seg)
        return 0;

    count = (op_32 & 0x200) ? ECX : CX;
    if (!count)
        return 1;

    if ((source_seg->base == (uint32_t) -1) || (cpu_state.seg_es.base == (uint32_t) -1))
        return 0;

    return 1;
}

uint32_t
ropREP_MOVSB(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), UNUSED(uint32_t fetchdat), uint32_t op_32, uint32_t op_pc)
{
    int      cnt_reg;
    int      src_addr_reg;
    int      dst_addr_reg;
    int      count_zero_jump;
    int      count_done_jump;
    uint32_t restart_pc;
    x86seg  *source_seg = op_ea_seg;
    x86seg  *dest_seg   = &cpu_state.seg_es;

    if (!repstr_movsb_safe_to_recompile(op_32, source_seg))
        return 0;

    restart_pc = cpu_state.oldpc;

    if (op_32 & 0x200) {
        if (!ECX)
            return 0;
        cnt_reg      = IREG_ECX;
        src_addr_reg = IREG_ESI;
        dst_addr_reg = IREG_EDI;
    } else {
        if (!CX)
            return 0;
        cnt_reg      = IREG_CX;
        src_addr_reg = IREG_temp0;
        dst_addr_reg = IREG_temp1;
    }

    count_zero_jump = uop_CMP_IMM_JZ_DEST(ir, cnt_reg, 0);

    uop_MOV_IMM(ir, IREG_pc, restart_pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) source_seg);
    codegen_check_seg_read(block, ir, source_seg);
    codegen_check_seg_write(block, ir, dest_seg);

    if (op_32 & 0x200) {
        CHECK_SEG_LIMITS(block, ir, source_seg, IREG_ESI, 0);
        CHECK_SEG_LIMITS(block, ir, dest_seg, IREG_EDI, 0);
    } else {
        uop_AND_IMM(ir, IREG_temp0, IREG_ESI, 0xffff);
        uop_AND_IMM(ir, IREG_temp1, IREG_EDI, 0xffff);
        CHECK_SEG_LIMITS(block, ir, source_seg, IREG_temp0, 0);
        CHECK_SEG_LIMITS(block, ir, dest_seg, IREG_temp1, 0);
    }

    uop_MEM_LOAD_REG(ir, IREG_temp2_B, ireg_seg_base(source_seg), src_addr_reg);
    uop_MEM_STORE_REG(ir, ireg_seg_base(dest_seg), dst_addr_reg, IREG_temp2_B);

    uop_CALL_FUNC_RESULT(ir, IREG_temp3, jit_DF_SET_01);
    uop_MOV_IMM(ir, IREG_temp2, 1);
    uop_SUB(ir, IREG_temp3, IREG_temp2, IREG_temp3);

    if (op_32 & 0x200) {
        uop_ADD(ir, IREG_ESI, IREG_ESI, IREG_temp3);
        uop_ADD(ir, IREG_EDI, IREG_EDI, IREG_temp3);
    } else {
        uop_ADD(ir, IREG_SI, IREG_SI, IREG_temp3_W);
        uop_ADD(ir, IREG_DI, IREG_DI, IREG_temp3_W);
    }

    uop_SUB_IMM(ir, cnt_reg, cnt_reg, 1);
    uop_NOP_BARRIER(ir);
    count_done_jump = uop_CMP_IMM_JZ_DEST(ir, cnt_reg, 0);
    uop_MOV_IMM(ir, IREG_pc, restart_pc);
    uop_JMP(ir, codegen_exit_rout);

    uop_set_jump_dest(ir, count_done_jump);
    uop_set_jump_dest(ir, count_zero_jump);
    return op_pc;
}
