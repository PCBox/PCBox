#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/machine.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86_flags.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "x87_sf.h"
#include "x87.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_fpu_arith.h"
#include "codegen_ops_helpers.h"

/* FIST/FISTP compiled only strictly between these (codegen_fpu_guard_int_range) */
#if defined __amd64__ || defined _M_X64
#    define FIST_INDEFINITE_GUARD 1
#else
/* the other backends' conversions saturate: range checked first */
#    define FIST_INDEFINITE_GUARD 0
#endif
static const double fist16_bounds[2] = { -32768.0, 32767.0 };
static const double fist32_bounds[2] = { -2147483648.0, 2147483647.0 };
static const double fist64_bounds[2] = { -9223372036854775808.0, 9223372036854775808.0 };

uint32_t
ropFLDs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_PUSH);
    codegen_check_seg_read(block, ir, target_seg);
    CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
    uop_MEM_LOAD_SINGLE(ir, IREG_ST(-1), ireg_seg_base(target_seg), IREG_eaaddr);
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
    fpu_PUSH(block, ir);

    return op_pc + 1;
}
uint32_t
ropFLDd(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_PUSH);
    codegen_check_seg_read(block, ir, target_seg);
    CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
    uop_MEM_LOAD_DOUBLE(ir, IREG_ST(-1), ireg_seg_base(target_seg), IREG_eaaddr);
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
    fpu_PUSH(block, ir);

    return op_pc + 1;
}

/*Narrowing to single precision rounds by RC, which the host conversion
  doesn't follow: leave directed modes to the interpreter, as the arithmetic
  ops do (RC != nearest is part of the block key).*/
uint32_t
ropFSTs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    /* The single is rounded "according to the RC field of the control
       word" (80387 PRM, 4.3.2); the host conversion rounds to nearest, so
       another RC is the interpreter's (codegen_fpu_rc_nearest). */
    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_empty(ir, fetchdat, op_32, op_pc, target_seg); /* an empty ST(0) */
    codegen_check_seg_write(block, ir, target_seg, IREG_eaaddr, 4);
    uop_MEM_STORE_SINGLE(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_ST(0));
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~FPU_SW_C1); /*no rounding to report*/

    return op_pc + 1;
}
uint32_t
ropFSTPs(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    /* The single is rounded "according to the RC field of the control
       word" (80387 PRM, 4.3.2); the host conversion rounds to nearest, so
       another RC is the interpreter's (codegen_fpu_rc_nearest). */
    if (!codegen_fpu_rc_nearest(ir))
        return 0;
    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_empty(ir, fetchdat, op_32, op_pc, target_seg); /* an empty ST(0) */
    codegen_check_seg_write(block, ir, target_seg, IREG_eaaddr, 4);
    uop_MEM_STORE_SINGLE(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_ST(0));
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~FPU_SW_C1); /*no rounding to report*/
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    fpu_POP(block, ir);

    return op_pc + 1;
}
uint32_t
ropFSTd(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_empty(ir, fetchdat, op_32, op_pc, target_seg); /* an empty ST(0) */
    codegen_check_seg_write(block, ir, target_seg, IREG_eaaddr, 8);
    uop_MEM_STORE_DOUBLE(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_ST(0));
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~FPU_SW_C1); /*no rounding to report*/

    return op_pc + 1;
}
uint32_t
ropFSTPd(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_empty(ir, fetchdat, op_32, op_pc, target_seg); /* an empty ST(0) */
    codegen_check_seg_write(block, ir, target_seg, IREG_eaaddr, 8);
    uop_MEM_STORE_DOUBLE(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_ST(0));
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~FPU_SW_C1); /*no rounding to report*/
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    fpu_POP(block, ir);

    return op_pc + 1;
}

uint32_t
ropFILDw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_PUSH);
    codegen_check_seg_read(block, ir, target_seg);
    CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 1);
    uop_MEM_LOAD_REG(ir, IREG_temp0_W, ireg_seg_base(target_seg), IREG_eaaddr);
    uop_MOV_DOUBLE_INT(ir, IREG_ST(-1), IREG_temp0_W);
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
    fpu_PUSH(block, ir);

    return op_pc + 1;
}
uint32_t
ropFILDl(codeblock_t *block, ir_data_t *ir, uint8_t UNUSED(opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_PUSH);
    codegen_check_seg_read(block, ir, target_seg);
    CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 3);
    uop_MEM_LOAD_REG(ir, IREG_temp0, ireg_seg_base(target_seg), IREG_eaaddr);
    uop_MOV_DOUBLE_INT(ir, IREG_ST(-1), IREG_temp0);
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID);
    fpu_PUSH(block, ir);

    return op_pc + 1;
}
uint32_t
ropFILDq(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_stack(ir, fetchdat, op_32, op_pc, target_seg, FPU_GUARD_PUSH);
    codegen_check_seg_read(block, ir, target_seg);
    CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 7);
    uop_MEM_LOAD_REG(ir, IREG_ST_i64(-1), ireg_seg_base(target_seg), IREG_eaaddr);
    uop_MOV_DOUBLE_INT(ir, IREG_ST(-1), IREG_ST_i64(-1));
    uop_MOV_IMM(ir, IREG_tag(-1), TAG_VALID | TAG_UINT64);
    fpu_PUSH(block, ir);

    return op_pc + 1;
}

/*ST(0) rounded to 16 bits in IREG_temp0_W. A result outside -32768..32767
  stores the integer indefinite (0x8000), as the chip does with IE masked, so
  convert to 32 bits and replace an out-of-range value. Straight-line, as the
  register allocator can't merge writes from converging paths. Uses
  IREG_temp0-2.*/
static void
fist_w_value(ir_data_t *ir)
{
    uop_MOV_INT_DOUBLE(ir, IREG_temp0, IREG_ST(0));
    /*temp1 = (temp0 + 0x8000) >> 16, zero only when temp0 fits in 16 bits*/
    uop_ADD_IMM(ir, IREG_temp1, IREG_temp0, 0x8000);
    uop_SHR_IMM(ir, IREG_temp1, IREG_temp1, 16);
    /*temp1 = all ones when out of range, else zero*/
    uop_MOV_IMM(ir, IREG_temp2, 0);
    uop_SUB(ir, IREG_temp2, IREG_temp2, IREG_temp1);
    uop_OR(ir, IREG_temp1, IREG_temp1, IREG_temp2);
    uop_SAR_IMM(ir, IREG_temp1, IREG_temp1, 31);
    /*temp0 ^= (temp0 ^ 0x8000) & mask*/
    uop_XOR_IMM(ir, IREG_temp2, IREG_temp0, 0x8000);
    uop_AND(ir, IREG_temp2, IREG_temp2, IREG_temp1);
    uop_XOR(ir, IREG_temp0, IREG_temp0, IREG_temp2);
}

uint32_t
ropFISTw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_empty(ir, fetchdat, op_32, op_pc, target_seg); /* an empty ST(0) */
    if (!FIST_INDEFINITE_GUARD)
        codegen_fpu_guard_int_range(ir, fetchdat, op_32, op_pc, target_seg, fist16_bounds); /* out of range or a NaN */
    uop_MOV_INT_DOUBLE(ir, IREG_temp0_W, IREG_ST(0));
    if (FIST_INDEFINITE_GUARD)
        codegen_fpu_guard_int_indefinite(ir, fetchdat, op_32, op_pc, target_seg, IREG_temp0_W, 0x8000); /* out of range or a NaN */
    codegen_check_seg_write(block, ir, target_seg, IREG_eaaddr, 2);
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~FPU_SW_C1); /*no rounding to report*/
    /* FIST leaves ST(0) where it is: its tag stays as it was. */

    return op_pc + 1;
}
uint32_t
ropFISTPw(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_empty(ir, fetchdat, op_32, op_pc, target_seg); /* an empty ST(0) */
    if (!FIST_INDEFINITE_GUARD)
        codegen_fpu_guard_int_range(ir, fetchdat, op_32, op_pc, target_seg, fist16_bounds); /* out of range or a NaN */
    uop_MOV_INT_DOUBLE(ir, IREG_temp0_W, IREG_ST(0));
    if (FIST_INDEFINITE_GUARD)
        codegen_fpu_guard_int_indefinite(ir, fetchdat, op_32, op_pc, target_seg, IREG_temp0_W, 0x8000); /* out of range or a NaN */
    codegen_check_seg_write(block, ir, target_seg, IREG_eaaddr, 2);
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_W);
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~FPU_SW_C1); /*no rounding to report*/
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    fpu_POP(block, ir);

    return op_pc + 1;
}
uint32_t
ropFISTl(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_empty(ir, fetchdat, op_32, op_pc, target_seg); /* an empty ST(0) */
    if (!FIST_INDEFINITE_GUARD)
        codegen_fpu_guard_int_range(ir, fetchdat, op_32, op_pc, target_seg, fist32_bounds); /* out of range or a NaN */
    uop_MOV_INT_DOUBLE(ir, IREG_temp0, IREG_ST(0));
    if (FIST_INDEFINITE_GUARD)
        codegen_fpu_guard_int_indefinite(ir, fetchdat, op_32, op_pc, target_seg, IREG_temp0, 0x80000000); /* out of range or a NaN */
    codegen_check_seg_write(block, ir, target_seg, IREG_eaaddr, 4);
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~FPU_SW_C1); /*no rounding to report*/
    /* FIST leaves ST(0) where it is: its tag stays as it was. */

    return op_pc + 1;
}
uint32_t
ropFISTPl(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_empty(ir, fetchdat, op_32, op_pc, target_seg); /* an empty ST(0) */
    if (!FIST_INDEFINITE_GUARD)
        codegen_fpu_guard_int_range(ir, fetchdat, op_32, op_pc, target_seg, fist32_bounds); /* out of range or a NaN */
    uop_MOV_INT_DOUBLE(ir, IREG_temp0, IREG_ST(0));
    if (FIST_INDEFINITE_GUARD)
        codegen_fpu_guard_int_indefinite(ir, fetchdat, op_32, op_pc, target_seg, IREG_temp0, 0x80000000); /* out of range or a NaN */
    codegen_check_seg_write(block, ir, target_seg, IREG_eaaddr, 4);
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0);
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~FPU_SW_C1); /*no rounding to report*/
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    fpu_POP(block, ir);

    return op_pc + 1;
}
uint32_t
ropFISTPq(codeblock_t *block, ir_data_t *ir, uint8_t opcode, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    x86seg *target_seg;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_fpu_guard_empty(ir, fetchdat, op_32, op_pc, target_seg); /* an empty ST(0) */
    codegen_fpu_guard_int_range(ir, fetchdat, op_32, op_pc, target_seg, fist64_bounds); /* out of range or a NaN */
    codegen_check_seg_write(block, ir, target_seg, IREG_eaaddr, 8);
    uop_MOV_INT_DOUBLE_64(ir, IREG_temp0_Q, IREG_ST(0), IREG_ST_i64(0), IREG_tag(0));
    uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_temp0_Q);
    uop_AND_IMM(ir, IREG_NPXS, IREG_NPXS, ~FPU_SW_C1); /*no rounding to report*/
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    fpu_POP(block, ir);

    return op_pc + 1;
}
