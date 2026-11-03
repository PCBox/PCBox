/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          The host-x87 FPU's compiled instructions.
 *
 *          The common x87 instructions are done inline by the block
 *          (UOP_X87H, codegen_backend_x86-64_x87h.c), with the memory operand
 *          read into cpu_state first. When the plain case does not hold
 *          (an exception unmasked or pending, the stack not as the
 *          instruction needs it) the inline code changes nothing and jumps to
 *          a call of the softfloat handler for the instruction, set up as
 *          codegen_generate_call() sets up any handler. A store's value is
 *          written by the block after the instruction, and its flags and pop
 *          given only then (UOP_X87H_DONE), so a store that faults leaves the
 *          FPU as it was.
 *
 *          Any other form, and every instruction this mode leaves alone, goes
 *          through codegen_generate_call() to its handler as in softfloat
 *          mode.
 *
 * Authors: Michael Pratte, <mpratte@makefox.group>
 *
 *          Copyright 2026 Michael Pratte.
 */
#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "386_common.h"
#include "x87_sf.h"
#include "x87.h"
#include "x87_host.h"
#include "codegen.h"
#include "codegen_accumulate.h"
#include "codegen_ir.h"
#include "codegen_ops.h"
#include "codegen_ops_helpers.h"
#include "codegen_ops_x87h.h"

#if X87_HOST_AVAILABLE

RecompOpFn recomp_opcodes_x87h[8][512];

/* The block calls the handler: codegen_generate_call()'s setup, all of it,
   since the block runs this only sometimes (codegen_x87h_forget()). The
   inline code that sent it here changed nothing. */
static void
x87h_call_handler(ir_data_t *ir, uint32_t fetchdat, uint32_t op_32, x86seg *ea_seg, uint32_t pc)
{
    uop_MOV_IMM(ir, IREG_rm_mod_reg, cpu_rm | (cpu_mod << 8) | (cpu_reg << 16));
    uop_MOV_IMM(ir, IREG_pc, pc);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    uop_MOV_IMM(ir, IREG_op32, op_32);
    uop_MOV_PTR(ir, IREG_ea_seg, (void *) ea_seg);
    uop_MOV_IMM(ir, IREG_ssegs, op_ssegs);
    uop_CALL_INSTRUCTION_FUNC(ir, codegen_fpu_handler, fetchdat);
    codegen_x87h_forget();
}

/* The instruction, inline (codegen_backend_x86-64_x87h.c). Returns the jump
   to the handler's call, taken when the plain case does not hold. */
static int
x87h_inline(ir_data_t *ir, uint32_t desc)
{
    uop_X87H(ir, desc);
    return ir->wr_pos - 1;
}

/* The fast path is done: past the handler to the next instruction. */
static void
x87h_slow_path(ir_data_t *ir, int jump_slow, uint32_t fetchdat, uint32_t op_32, x86seg *ea_seg, uint32_t pc)
{
    int jump_done = uop_JMP_DEST(ir);

    uop_set_jump_dest(ir, jump_slow);
    uop_NOP_BARRIER(ir);
    x87h_call_handler(ir, fetchdat, op_32, ea_seg, pc);
    uop_NOP_BARRIER(ir);
    uop_set_jump_dest(ir, jump_done);
    uop_NOP_BARRIER(ir);
}

static int
x87h_operand_reg(int size)
{
    switch (size) {
        case 2:
            return IREG_x87h_W;
        case 4:
            return IREG_x87h_L;
        default:
            return IREG_x87h_q;
    }
}

static uint32_t
x87h_rop_mem(codeblock_t *block, ir_data_t *ir, int esc, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    const int digit = (fetchdat >> 3) & 7;
    int       size;
    const int kind  = x87h_mem_kind(esc, digit, &size);
    x86seg   *target_seg;
    int       jump_slow;

    if (kind == X87H_NONE)
        return 0;

    uop_FP_ENTER(ir);
    uop_MOV_IMM(ir, IREG_oldpc, cpu_state.oldpc);
    op_pc--;
    target_seg = codegen_generate_ea(ir, op_ea_seg, fetchdat, op_ssegs, &op_pc, op_32, 0);
    codegen_accumulate_flush(ir);

    switch (kind) {
        case X87H_ARITH:
        case X87H_COMPARE:
        case X87H_LOAD:
            codegen_check_seg_read(block, ir, target_seg);
            CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, size - 1);
            uop_MEM_LOAD_REG(ir, x87h_operand_reg(size), ireg_seg_base(target_seg), IREG_eaaddr);
            jump_slow = x87h_inline(ir, X87H_DESC(X87H_OP_MEM, esc, digit, 0, 0));
            break;

        case X87H_COPY_LOAD:
            codegen_check_seg_read(block, ir, target_seg);
            CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 9);
            uop_MEM_LOAD_REG(ir, IREG_x87h_q, ireg_seg_base(target_seg), IREG_eaaddr);
            uop_MEM_LOAD_REG_OFFSET(ir, IREG_x87h_W, ireg_seg_base(target_seg), IREG_eaaddr, 8);
            jump_slow = x87h_inline(ir, X87H_DESC(X87H_OP_MEM, esc, digit, 0, 0));
            break;

        /* The limit is checked as the handler checks it, when the value is
           written: a store the handler does not make (an unmasked
           exception) faults on nothing else. */
        case X87H_STORE:
            codegen_check_seg_write(block, ir, target_seg);
            CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, size - 1);
            jump_slow = x87h_inline(ir, X87H_DESC(X87H_OP_MEM, esc, digit, 0, 0));
            uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, x87h_operand_reg(size));
            uop_X87H_DONE(ir, X87H_DESC(X87H_OP_MEM, esc, digit, 0, 0));
            break;

        case X87H_COPY_STORE:
        default:
            codegen_check_seg_write(block, ir, target_seg);
            CHECK_SEG_LIMITS(block, ir, target_seg, IREG_eaaddr, 9);
            jump_slow = x87h_inline(ir, X87H_DESC(X87H_OP_MEM, esc, digit, 0, 0));
            uop_MEM_STORE_REG(ir, ireg_seg_base(target_seg), IREG_eaaddr, IREG_x87h_q);
            uop_MEM_STORE_REG_OFFSET(ir, ireg_seg_base(target_seg), IREG_eaaddr, 8, IREG_x87h_W);
            uop_X87H_DONE(ir, X87H_DESC(X87H_OP_MEM, esc, digit, 0, 0));
            break;
    }
    x87h_slow_path(ir, jump_slow, fetchdat, op_32, target_seg, op_pc);

    return op_pc + 1;
}

/* A register form's X87H_DESC(), or 0 to leave it to
   codegen_generate_call(). */
static uint32_t
x87h_reg_desc(int esc, uint8_t modrm)
{
    const int digit = (modrm >> 3) & 7;
    const int i     = modrm & 7;

    switch (esc) {
        case 0: /* D8 */
            if ((digit == 2) || (digit == 3)) /* FCOM(P) ST(i) */
                return X87H_DESC(X87H_OP_FCOM_REG, 0, 0, i, digit == 3);
            return X87H_DESC(X87H_OP_ARITH_REG, 0, digit, i, 0);

        case 1: /* D9 */
            if (digit == 0)
                return X87H_DESC(X87H_OP_FLD_ST, 1, 0, i, 0);
            if (digit == 1)
                return X87H_DESC(X87H_OP_FXCH, 1, 0, i, 0);
            switch (modrm) {
                case 0xe0: /* FCHS */
                case 0xe1: /* FABS */
                    return X87H_DESC(X87H_OP_SIGN, 1, 0, 0, modrm & 1);
                case 0xe8: /* FLD1 */
                    return X87H_DESC(X87H_OP_CONST, 1, 0, 0, 1);
                case 0xee: /* FLDZ */
                    return X87H_DESC(X87H_OP_CONST, 1, 0, 0, 0);
                case 0xfa: /* FSQRT */
                    return X87H_DESC(X87H_OP_FSQRT, 1, 0, 0, 0);
                default:
                    return 0;
            }

        case 2: /* DA */
            if (modrm == 0xe9) /* FUCOMPP */
                return X87H_DESC(X87H_OP_FCOM_REG, 0, 0, 1, 2 | 4);
            return 0;

        case 4: /* DC */
            if ((digit == 2) || (digit == 3))
                return 0;
            return X87H_DESC(X87H_OP_ARITH_REG, 4, digit, i, 0);

        case 5: /* DD */
            if ((digit == 2) || (digit == 3)) /* FST(P) ST(i) */
                return X87H_DESC(X87H_OP_FST_ST, 5, 0, i, digit == 3);
            if ((digit == 4) || (digit == 5)) /* FUCOM(P) ST(i) */
                return X87H_DESC(X87H_OP_FCOM_REG, 0, 0, i, (digit == 5) | 4);
            return 0;

        case 6: /* DE */
            if (modrm == 0xd9) /* FCOMPP */
                return X87H_DESC(X87H_OP_FCOM_REG, 0, 0, 1, 2);
            if ((digit == 2) || (digit == 3))
                return 0;
            return X87H_DESC(X87H_OP_ARITH_REG, 6, digit, i, 0);

        default:
            return 0;
    }
}

static uint32_t
x87h_rop_reg(UNUSED(codeblock_t *block), ir_data_t *ir, int esc, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    const uint8_t modrm = fetchdat & 0xff;
    uint32_t      desc;
    int           jump_slow;

    if ((esc == 7) && (modrm == 0xe0)) { /* FNSTSW AX: the status word, stack top included */
        uop_FP_ENTER_NOWAIT(ir); /* no-wait: no pending exception taken */
        uop_MOVZX_REG_PTR_16(ir, IREG_temp0, &fpu_state.swd);
        uop_AND_IMM(ir, IREG_temp0, IREG_temp0, ~0x3800u);
        uop_MOVZX_REG_PTR_8(ir, IREG_temp1, &fpu_state.tos);
        uop_AND_IMM(ir, IREG_temp1, IREG_temp1, 7);
        uop_SHL_IMM(ir, IREG_temp1, IREG_temp1, 11);
        uop_OR(ir, IREG_temp0, IREG_temp0, IREG_temp1);
        uop_MOV(ir, IREG_AX, IREG_temp0_W);
        return op_pc;
    }

    desc = x87h_reg_desc(esc, modrm);
    if (desc == 0)
        return 0;

    cpu_mod = 3;
    cpu_reg = (modrm >> 3) & 7;
    cpu_rm  = modrm & 7;

    uop_FP_ENTER(ir);
    codegen_accumulate_flush(ir);
    jump_slow = x87h_inline(ir, desc);
    x87h_slow_path(ir, jump_slow, fetchdat, op_32, op_ea_seg, op_pc - 1);

    return op_pc;
}

static uint32_t
x87h_rop(codeblock_t *block, ir_data_t *ir, int esc, uint32_t fetchdat, uint32_t op_32, uint32_t op_pc)
{
    if ((fetchdat & 0xc0) == 0xc0)
        return x87h_rop_reg(block, ir, esc, fetchdat, op_32, op_pc);

    return x87h_rop_mem(block, ir, esc, fetchdat, op_32, op_pc);
}

#    define X87H_ROP(esc)                                                                                                       \
        static uint32_t ropX87H_##esc(codeblock_t *block, ir_data_t *ir, UNUSED(uint8_t opcode), uint32_t fetchdat, \
                                      uint32_t op_32, uint32_t op_pc)                                               \
        {                                                                                                           \
            return x87h_rop(block, ir, esc, fetchdat, op_32, op_pc);                                               \
        }

X87H_ROP(0)
X87H_ROP(1)
X87H_ROP(2)
X87H_ROP(3)
X87H_ROP(4)
X87H_ROP(5)
X87H_ROP(6)
X87H_ROP(7)

void
codegen_x87h_init(void)
{
    static const RecompOpFn rops[8] = {
        ropX87H_0, ropX87H_1, ropX87H_2, ropX87H_3, ropX87H_4, ropX87H_5, ropX87H_6, ropX87H_7
    };

    for (int esc = 0; esc < 8; esc++) {
        for (int c = 0; c < 512; c++)
            recomp_opcodes_x87h[esc][c] = rops[esc];
    }
}

#endif /* X87_HOST_AVAILABLE */
