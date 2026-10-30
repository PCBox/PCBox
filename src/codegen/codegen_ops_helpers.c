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
#include "codegen_ir.h"
#include "codegen_ir_defs.h"
#include "codegen_reg.h"
#include "codegen_ops_helpers.h"

void
LOAD_IMMEDIATE_FROM_RAM_16_unaligned(UNUSED(codeblock_t *block), ir_data_t *ir, int dest_reg, uint32_t addr)
{
    /*Word access that crosses two pages. Perform reads from both pages, shift and combine*/
    uop_MOVZX_REG_PTR_8(ir, IREG_temp3_W, get_ram_ptr(addr + 1));
    uop_MOVZX_REG_PTR_8(ir, dest_reg, get_ram_ptr(addr));
    uop_SHL_IMM(ir, IREG_temp3_W, IREG_temp3_W, 8);
    uop_OR(ir, dest_reg, dest_reg, IREG_temp3_W);
}

void
LOAD_IMMEDIATE_FROM_RAM_32_unaligned(UNUSED(codeblock_t *block), ir_data_t *ir, int dest_reg, uint32_t addr)
{
    /*Dword access that crosses two pages. Perform reads from both pages, shift and combine*/
    uop_MOV_REG_PTR(ir, dest_reg, get_ram_ptr(addr & ~3));
    uop_MOV_REG_PTR(ir, IREG_temp3, get_ram_ptr((addr + 4) & ~3));
    uop_SHR_IMM(ir, dest_reg, dest_reg, (addr & 3) * 8);
    uop_SHL_IMM(ir, IREG_temp3, IREG_temp3, (4 - (addr & 3)) * 8);
    uop_OR(ir, dest_reg, dest_reg, IREG_temp3);
}

/*Where the block used a flags op known at compile time
  (codegen_flags_rebuild_known, codegen_cf_inline, codegen_rebuild_c), each
  with the uop that set that op*/
#define FLAGS_KNOWN_MAX 64
static int flags_known_nr;
static int flags_known_site[FLAGS_KNOWN_MAX];
static int flags_known_producer[FLAGS_KNOWN_MAX];

void
codegen_flags_known_reset(void)
{
    flags_known_nr = 0;
}

/*Notes a use of the flags op known here, for codegen_can_unroll_full(); 0 if
  it cannot be noted, and the caller must then not depend on the op*/
int
codegen_flags_known_note(ir_data_t *ir)
{
    int producer;

    for (producer = ir->wr_pos - 1; producer >= 0; producer--) {
        if (IREG_GET_REG(ir->uops[producer].dest_reg_a.reg) == IREG_GET_REG(IREG_flags_op))
            break;
    }
    if ((producer < 0) || (flags_known_nr == FLAGS_KNOWN_MAX))
        return 0;
    flags_known_site[flags_known_nr]     = ir->wr_pos;
    flags_known_producer[flags_known_nr] = producer;
    flags_known_nr++;
    return 1;
}

/*flags_rebuild(), or flags_rebuild_c() if carry_only, as uops when the flags
  op is known here and is one whose flags depend only on flags_res,
  flags_op1, flags_op2 and the carry; returns 0, emitting nothing, otherwise.
  Each flag is the expression CF_SET() .. VF_SET() in x86_flags.h use for
  that op, on the same values, ORed into flags in place. Uses temp1 and
  temp3 only: callers hold operands in temp0 and shift counts in temp2.*/
int
codegen_flags_rebuild_known(ir_data_t *ir, int carry_only)
{
#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
    int      op = codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN;
    int      is_add;
    int      is_sub;
    int      is_incdec;
    uint32_t msb;

    switch (op) {
        case FLAGS_ZN8:
        case FLAGS_ADD8:
        case FLAGS_SUB8:
        case FLAGS_INC8:
        case FLAGS_DEC8:
            msb = 0x80;
            break;
        case FLAGS_ZN16:
        case FLAGS_ADD16:
        case FLAGS_SUB16:
        case FLAGS_INC16:
        case FLAGS_DEC16:
            msb = 0x8000;
            break;
        case FLAGS_ZN32:
        case FLAGS_ADD32:
        case FLAGS_SUB32:
        case FLAGS_INC32:
        case FLAGS_DEC32:
            msb = 0x80000000;
            break;
        default:
            return 0;
    }
    is_add    = (op == FLAGS_ADD8) || (op == FLAGS_ADD16) || (op == FLAGS_ADD32) || (op == FLAGS_INC8) || (op == FLAGS_INC16) || (op == FLAGS_INC32);
    is_sub    = (op == FLAGS_SUB8) || (op == FLAGS_SUB16) || (op == FLAGS_SUB32) || (op == FLAGS_DEC8) || (op == FLAGS_DEC16) || (op == FLAGS_DEC32);
    is_incdec = (op == FLAGS_INC8) || (op == FLAGS_INC16) || (op == FLAGS_INC32) || (op == FLAGS_DEC8) || (op == FLAGS_DEC16) || (op == FLAGS_DEC32);

    /*flags_rebuild_c() leaves CF as it is after INC and DEC*/
    if (is_incdec && carry_only)
        return codegen_flags_known_note(ir);

    /*Room for this sequence's register versions (at most 20 of temp1, 4 of
      temp3, 8 of flags) without passing REG_VERSION_MAX, which only ends the
      block after the current instruction*/
    if ((reg_last_version[IREG_GET_REG(IREG_temp1)] + 20 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_temp3)] + 4 > REG_VERSION_MAX) ||
        (reg_last_version[IREG_GET_REG(IREG_flags)] + 8 > REG_VERSION_MAX) || (reg_last_version[IREG_GET_REG(IREG_flags_op)] + 1 > REG_VERSION_MAX))
        return 0;

    if (!codegen_flags_known_note(ir))
        return 0;

    /*CF into temp1; INC and DEC keep the one in flags*/
    if (op == FLAGS_ADD8 || op == FLAGS_ADD16) {
        uop_ADD(ir, IREG_temp1, IREG_flags_op1, IREG_flags_op2);
        uop_AND_IMM(ir, IREG_temp1, IREG_temp1, (op == FLAGS_ADD8) ? 0x100 : 0x10000);
        uop_SHR_IMM(ir, IREG_temp1, IREG_temp1, (op == FLAGS_ADD8) ? 8 : 16);
    } else if (op == FLAGS_ADD32)
        uop_CMP_SET(ir, IREG_temp1, IREG_flags_res, IREG_flags_op1, UOP_CC_B);
    else if (is_sub && !is_incdec)
        uop_CMP_SET(ir, IREG_temp1, IREG_flags_op1, IREG_flags_op2, UOP_CC_B);

    if (carry_only) {
        uop_AND_IMM(ir, IREG_flags, IREG_flags, (uint16_t) ~C_FLAG);
        if (is_add || is_sub)
            uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);
        return 1;
    }

    if (is_incdec)
        uop_AND_IMM(ir, IREG_flags, IREG_flags, (uint16_t) ~(0x8d5 & ~C_FLAG));
    else {
        uop_AND_IMM(ir, IREG_flags, IREG_flags, (uint16_t) ~0x8d5);
        if (is_add || is_sub)
            uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);
    }

    /*PF: even parity of the low byte of flags_res, as znptable8[]*/
    uop_SHR_IMM(ir, IREG_temp1, IREG_flags_res, 4);
    uop_XOR(ir, IREG_temp1, IREG_temp1, IREG_flags_res);
    uop_SHR_IMM(ir, IREG_temp3, IREG_temp1, 2);
    uop_XOR(ir, IREG_temp1, IREG_temp1, IREG_temp3);
    uop_SHR_IMM(ir, IREG_temp3, IREG_temp1, 1);
    uop_XOR(ir, IREG_temp1, IREG_temp1, IREG_temp3);
    uop_AND_IMM(ir, IREG_temp1, IREG_temp1, 1);
    uop_XOR_IMM(ir, IREG_temp1, IREG_temp1, 1);
    uop_SHL_IMM(ir, IREG_temp1, IREG_temp1, 2);
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);

    /*AF: ((op1 & 0xf) +/- (op2 & 0xf)) & 0x10*/
    if (is_add || is_sub) {
        uop_AND_IMM(ir, IREG_temp1, IREG_flags_op1, 0xf);
        uop_AND_IMM(ir, IREG_temp3, IREG_flags_op2, 0xf);
        if (is_add)
            uop_ADD(ir, IREG_temp1, IREG_temp1, IREG_temp3);
        else
            uop_SUB(ir, IREG_temp1, IREG_temp1, IREG_temp3);
        uop_AND_IMM(ir, IREG_temp1, IREG_temp1, A_FLAG);
        uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);
    }

    /*ZF: !flags_res*/
    uop_TEST_SET(ir, IREG_temp1, IREG_flags_res, UOP_CC_Z);
    uop_SHL_IMM(ir, IREG_temp1, IREG_temp1, 6);
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);

    /*NF: flags_res & msb, moved to bit 7*/
    uop_AND_IMM(ir, IREG_temp1, IREG_flags_res, msb);
    if (msb != 0x80)
        uop_SHR_IMM(ir, IREG_temp1, IREG_temp1, (msb == 0x8000) ? 8 : 24);
    uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);

    /*VF, moved from msb to bit 11. Add: !((op1 ^ op2) & msb) &&
      ((op1 ^ res) & msb). Sub: (op1 ^ op2) & (op1 ^ res) & msb.*/
    if (is_add || is_sub) {
        uop_XOR(ir, IREG_temp1, IREG_flags_op1, IREG_flags_op2);
        if (is_add)
            uop_XOR_IMM(ir, IREG_temp1, IREG_temp1, msb);
        uop_XOR(ir, IREG_temp3, IREG_flags_op1, IREG_flags_res);
        uop_AND(ir, IREG_temp1, IREG_temp1, IREG_temp3);
        uop_AND_IMM(ir, IREG_temp1, IREG_temp1, msb);
        if (msb == 0x80)
            uop_SHL_IMM(ir, IREG_temp1, IREG_temp1, 4);
        else
            uop_SHR_IMM(ir, IREG_temp1, IREG_temp1, (msb == 0x8000) ? 4 : 20);
        uop_OR(ir, IREG_flags, IREG_flags, IREG_temp1_W);
    }

    uop_MOV_IMM(ir, IREG_flags_op, FLAGS_UNKNOWN);
    return 1;
#else
    /*Other backends lack UOP_CMP_SET and UOP_TEST_SET*/
    return 0;
#endif
}

/*CF_SET() as 0 or 1 into dest_reg, for ADC and SBB, when the flags op is
  known here and CF follows from flags_res, flags_op1 and flags_op2 alone:
  none after a ZN operation, op1 below op2 after a SUB, the result below op1
  after an ADD, as SETB works it out (setcc_value). Returns 0, emitting
  nothing, otherwise. It writes dest_reg once, as the call it replaces does.*/
int
codegen_cf_inline(ir_data_t *ir, int dest_reg)
{
#if defined __aarch64__ || defined _M_ARM64 || defined __amd64__ || defined _M_X64
    int op = codegen_flags_changed ? cpu_state.flags_op : FLAGS_UNKNOWN;

    switch (op) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
        case FLAGS_SUB8:
        case FLAGS_SUB16:
        case FLAGS_SUB32:
        case FLAGS_ADD8:
        case FLAGS_ADD16:
        case FLAGS_ADD32:
            break;
        default:
            return 0;
    }

    if (!codegen_flags_known_note(ir))
        return 0;

    switch (op) {
        case FLAGS_ZN8:
        case FLAGS_ZN16:
        case FLAGS_ZN32:
            uop_MOV_IMM(ir, dest_reg, 0);
            break;
        case FLAGS_SUB8:
            uop_CMP_SET(ir, dest_reg, IREG_flags_op1_B, IREG_flags_op2_B, UOP_CC_B);
            break;
        case FLAGS_SUB16:
            uop_CMP_SET(ir, dest_reg, IREG_flags_op1_W, IREG_flags_op2_W, UOP_CC_B);
            break;
        case FLAGS_SUB32:
            uop_CMP_SET(ir, dest_reg, IREG_flags_op1, IREG_flags_op2, UOP_CC_B);
            break;
        case FLAGS_ADD8:
            uop_CMP_SET(ir, dest_reg, IREG_flags_res_B, IREG_flags_op1_B, UOP_CC_B);
            break;
        case FLAGS_ADD16:
            uop_CMP_SET(ir, dest_reg, IREG_flags_res_W, IREG_flags_op1_W, UOP_CC_B);
            break;
        default: /*FLAGS_ADD32*/
            uop_CMP_SET(ir, dest_reg, IREG_flags_res, IREG_flags_op1, UOP_CC_B);
            break;
    }
    return 1;
#else
    /*Other backends lack UOP_CMP_SET*/
    return 0;
#endif
}

#define UNROLL_MAX_REG_REFERENCES 200
#define UNROLL_MAX_UOPS           1000
#define UNROLL_MAX_COUNT          10
int
codegen_can_unroll_full(codeblock_t *block, ir_data_t *ir, UNUSED(uint32_t next_pc), uint32_t dest_addr)
{
    int start;
    int max_unroll;
    int first_instruction;
    int TOP      = -1;
    int flags_op = FLAGS_UNKNOWN;

    /*Check that dest instruction was actually compiled into block*/
    start = codegen_get_instruction_uop(block, dest_addr, &first_instruction, &TOP, &flags_op);

    /*Couldn't find any uOPs corresponding to the destination instruction*/
    if (start == -1) {
        /*Is instruction jumping to itself?*/
        if (dest_addr != cpu_state.oldpc) {
            return 0;
        } else {
            start = ir->wr_pos;
            TOP   = cpu_state.TOP;
        }
    }

    if (TOP != cpu_state.TOP)
        return 0;

    /* Flag consumers may have specialized the loop's incoming lazy state.
     * For example, XOR before an SBB/DEC loop folds the first carry to zero;
     * copying that IR would discard the borrow on subsequent iterations.
     * Keep such a block's first iteration, then dispatch to the loop head.
     * A block compiled from that head starts with unknown flags and can still
     * be unrolled, as can loops whose incoming flag representation is stable. */
    if (flags_op != FLAGS_UNKNOWN
        && (!codegen_flags_changed || flags_op != cpu_state.flags_op))
        return 0;

    /*Not across a flags rebuild worked out from a flags op set before the
      loop: the copies would repeat it after the loop's own flag changes*/
    for (int c = 0; c < flags_known_nr; c++) {
        if ((flags_known_site[c] >= start) && (flags_known_producer[c] < start))
            return 0;
    }

    /*Not across an instruction the interpreter runs in the loop. One that
      only a hand-over path runs, which leaves the block straight after it,
      never returns into the loop*/
    for (int c = start; c < ir->wr_pos; c++) {
        if ((ir->uops[c].type & UOP_MASK) == (UOP_CALL_INSTRUCTION_FUNC & UOP_MASK) &&
            !((c + 1 < ir->wr_pos) && ((ir->uops[c + 1].type & UOP_MASK) == (UOP_JMP & UOP_MASK))))
            return 0;
    }

    max_unroll = UNROLL_MAX_UOPS / ((ir->wr_pos - start) + 6);
    if ((max_version_refcount != 0) && (max_unroll > (UNROLL_MAX_REG_REFERENCES / max_version_refcount)))
        max_unroll = (UNROLL_MAX_REG_REFERENCES / max_version_refcount);
    if (max_unroll > UNROLL_MAX_COUNT)
        max_unroll = UNROLL_MAX_COUNT;
    if (max_unroll <= 1)
        return 0;

    codegen_ir_set_unroll(max_unroll, start, first_instruction);
    /*Each run goes round the loop several times: one run from block->data
      pays for the compile (codeblock_t::runs_left).*/
    block->runs_left = 1;

    return 1;
}
