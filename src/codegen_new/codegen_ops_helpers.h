#include "386_common.h"
/*x87.h has no include guard; the FPU translators include it before this.*/
#ifndef TAG_EMPTY
#    include "x87_sf.h"
#    include "x87.h"
#endif
#include "codegen_backend.h"

/* Returning zero from a native opcode translator selects interpreter fallback. */
#define REQUIRE_GUEST_FEATURE(feature)           \
    do {                                         \
        if (!(cpu_features & (feature)))         \
            return 0;                            \
    } while (0)

static inline int
LOAD_SP_WITH_OFFSET(ir_data_t *ir, int offset)
{
    if (stack32) {
        if (offset) {
            uop_ADD_IMM(ir, IREG_eaaddr, IREG_ESP, offset);
            return IREG_eaaddr;
        } else
            return IREG_ESP;
    } else {
        if (offset) {
            uop_ADD_IMM(ir, IREG_eaaddr_W, IREG_SP, offset);
            uop_MOVZX(ir, IREG_eaaddr, IREG_eaaddr_W);
            return IREG_eaaddr;
        } else {
            uop_MOVZX(ir, IREG_eaaddr, IREG_SP);
            return IREG_eaaddr;
        }
    }
}

static inline int
LOAD_SP(ir_data_t *ir)
{
    return LOAD_SP_WITH_OFFSET(ir, 0);
}

static inline void
ADD_SP(ir_data_t *ir, int offset)
{
    if (stack32)
        uop_ADD_IMM(ir, IREG_ESP, IREG_ESP, offset);
    else
        uop_ADD_IMM(ir, IREG_SP, IREG_SP, offset);
}
static inline void
SUB_SP(ir_data_t *ir, int offset)
{
    if (stack32)
        uop_SUB_IMM(ir, IREG_ESP, IREG_ESP, offset);
    else
        uop_SUB_IMM(ir, IREG_SP, IREG_SP, offset);
}

/*A pop frees the popped slots, as x87_pop() does. IREG_tag() is relative to the
  compile-time TOP and dynamic-TOP blocks rebase it at run time, so IREG_tag(0)
  is the popped slot in both modes. Pushes need nothing here: every caller sets
  IREG_tag(-1) itself, and FILD qword relies on its TAG_UINT64 surviving.*/
static inline void
fpu_POP(codeblock_t *block, ir_data_t *ir)
{
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    if (block->flags & CODEBLOCK_STATIC_TOP)
        uop_MOV_IMM(ir, IREG_FPU_TOP, cpu_state.TOP + 1);
    else
        uop_ADD_IMM(ir, IREG_FPU_TOP, IREG_FPU_TOP, 1);
}
static inline void
fpu_POP2(codeblock_t *block, ir_data_t *ir)
{
    uop_MOV_IMM(ir, IREG_tag(0), TAG_EMPTY);
    uop_MOV_IMM(ir, IREG_tag(1), TAG_EMPTY);
    if (block->flags & CODEBLOCK_STATIC_TOP)
        uop_MOV_IMM(ir, IREG_FPU_TOP, cpu_state.TOP + 2);
    else
        uop_ADD_IMM(ir, IREG_FPU_TOP, IREG_FPU_TOP, 2);
}
static inline void
fpu_PUSH(codeblock_t *block, ir_data_t *ir)
{
    if (block->flags & CODEBLOCK_STATIC_TOP)
        uop_MOV_IMM(ir, IREG_FPU_TOP, cpu_state.TOP - 1);
    else
        uop_SUB_IMM(ir, IREG_FPU_TOP, IREG_FPU_TOP, 1);
}

/*Flat DS/SS are part of the block key, so their base and limits need no checks.*/
static inline int
codegen_seg_is_flat(x86seg *seg)
{
    return (seg == &cpu_state.seg_ds && codegen_flat_ds && !(cpu_cur_status & CPU_STATUS_NOTFLATDS)) || (seg == &cpu_state.seg_ss && codegen_flat_ss && !(cpu_cur_status & CPU_STATUS_NOTFLATSS));
}

/*Limit check for an access to seg:addr_reg..addr_reg+end_offset. Exceeding SS's
  limit is #SS(0), any other segment's #GP(0). Uses IREG_temp3.*/
static inline void
CHECK_SEG_LIMITS(UNUSED(codeblock_t *block), ir_data_t *ir, x86seg *seg, int addr_reg, int end_offset)
{
    void *fault_rout = (seg == &cpu_state.seg_ss) ? codegen_ss_rout : codegen_gpf_rout;

    if (codegen_seg_is_flat(seg))
        return;

    uop_CMP_JB(ir, addr_reg, ireg_seg_limit_low(seg), fault_rout);
    if (end_offset) {
        uop_ADD_IMM(ir, IREG_temp3, addr_reg, end_offset);
        uop_CMP_JNBE(ir, IREG_temp3, ireg_seg_limit_high(seg), fault_rout);
    } else
        uop_CMP_JNBE(ir, addr_reg, ireg_seg_limit_high(seg), fault_rout);
}

/*CHECK_SEG_LIMITS() for the part of a 16-bit-addressed operand at
  addr_reg+offset..addr_reg+offset+end_offset, with each end wrapped at 64k as
  the interpreter's LDS/LES/LSS/LFS/LGS a16 forms read it. Uses IREG_temp3.*/
static inline void
CHECK_SEG_LIMITS_A16(UNUSED(codeblock_t *block), ir_data_t *ir, x86seg *seg, int addr_reg, int offset, int end_offset)
{
    void *fault_rout = (seg == &cpu_state.seg_ss) ? codegen_ss_rout : codegen_gpf_rout;

    if (codegen_seg_is_flat(seg))
        return;

    if (offset) {
        uop_ADD_IMM(ir, IREG_temp3, addr_reg, offset);
        uop_AND_IMM(ir, IREG_temp3, IREG_temp3, 0xffff);
        uop_CMP_JB(ir, IREG_temp3, ireg_seg_limit_low(seg), fault_rout);
    } else
        uop_CMP_JB(ir, addr_reg, ireg_seg_limit_low(seg), fault_rout);
    uop_ADD_IMM(ir, IREG_temp3, addr_reg, offset + end_offset);
    uop_AND_IMM(ir, IREG_temp3, IREG_temp3, 0xffff);
    uop_CMP_JNBE(ir, IREG_temp3, ireg_seg_limit_high(seg), fault_rout);
}

/*CHECK_SEG_LIMITS() for an access at a constant offset. Uses IREG_eaaddr and
  IREG_temp3.*/
static inline void
CHECK_SEG_LIMITS_ABS(codeblock_t *block, ir_data_t *ir, x86seg *seg, uint32_t addr, int end_offset)
{
    if (codegen_seg_is_flat(seg))
        return;

    uop_MOV_IMM(ir, IREG_eaaddr, addr);
    CHECK_SEG_LIMITS(block, ir, seg, IREG_eaaddr, end_offset);
}

/*CHECK_SEG_LIMITS() for a len-byte push or pop at SS:addr_reg.*/
static inline void
CHECK_STACK_LIMITS(codeblock_t *block, ir_data_t *ir, int addr_reg, int len)
{
    CHECK_SEG_LIMITS(block, ir, &cpu_state.seg_ss, addr_reg, len - 1);
}

static inline void
LOAD_IMMEDIATE_FROM_RAM_8(UNUSED(codeblock_t *block), ir_data_t *ir, int dest_reg, uint32_t addr)
{
    uop_MOVZX_REG_PTR_8(ir, dest_reg, get_ram_ptr(addr));
}

void LOAD_IMMEDIATE_FROM_RAM_16_unaligned(codeblock_t *block, ir_data_t *ir, int dest_reg, uint32_t addr);
static inline void
LOAD_IMMEDIATE_FROM_RAM_16(codeblock_t *block, ir_data_t *ir, int dest_reg, uint32_t addr)
{
    if ((addr & 0xfff) == 0xfff)
        LOAD_IMMEDIATE_FROM_RAM_16_unaligned(block, ir, dest_reg, addr);
    else
        uop_MOVZX_REG_PTR_16(ir, dest_reg, get_ram_ptr(addr));
}

void LOAD_IMMEDIATE_FROM_RAM_32_unaligned(codeblock_t *block, ir_data_t *ir, int dest_reg, uint32_t addr);
static inline void
LOAD_IMMEDIATE_FROM_RAM_32(codeblock_t *block, ir_data_t *ir, int dest_reg, uint32_t addr)
{
    if ((addr & 0xfff) >= 0xffd)
        LOAD_IMMEDIATE_FROM_RAM_32_unaligned(block, ir, dest_reg, addr);
    else
        uop_MOV_REG_PTR(ir, dest_reg, get_ram_ptr(addr));
}

int codegen_can_unroll_full(codeblock_t *block, ir_data_t *ir, uint32_t next_pc, uint32_t dest_addr);
static inline int
codegen_can_unroll(codeblock_t *block, ir_data_t *ir, uint32_t next_pc, uint32_t dest_addr)
{
    if (block->flags & CODEBLOCK_BYTE_MASK)
        return 0;

    /*Is dest within block?*/
    if (dest_addr > next_pc)
        return 0;
    if ((cs + dest_addr) < block->pc)
        return 0;

    return codegen_can_unroll_full(block, ir, next_pc, dest_addr);
}
