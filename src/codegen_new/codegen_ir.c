#include <stdint.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/mem.h>
#include <86box/plat_unused.h>

#include "codegen.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_ir.h"
#include "codegen_reg.h"

extern int       has_ea;
static ir_data_t ir_block;

static int codegen_unroll_start;
static int codegen_unroll_count;
static int codegen_unroll_first_instruction;

ir_data_t *
codegen_ir_init(void)
{
    ir_block.wr_pos = 0;

    codegen_unroll_count = 0;

    return &ir_block;
}

void
codegen_ir_set_unroll(int count, int start, int first_instruction)
{
    codegen_unroll_count             = count;
    codegen_unroll_start             = start;
    codegen_unroll_first_instruction = first_instruction;
}

static void
duplicate_uop(ir_data_t *ir, uop_t *uop, int offset)
{
    uop_t *new_uop = uop_alloc_unroll(ir, uop->type);

    if (!ir_reg_is_invalid(uop->src_reg_a))
        new_uop->src_reg_a = codegen_reg_read(uop->src_reg_a.reg);
    if (!ir_reg_is_invalid(uop->src_reg_b))
        new_uop->src_reg_b = codegen_reg_read(uop->src_reg_b.reg);
    if (!ir_reg_is_invalid(uop->src_reg_c))
        new_uop->src_reg_c = codegen_reg_read(uop->src_reg_c.reg);
    if (!ir_reg_is_invalid(uop->dest_reg_a))
        new_uop->dest_reg_a = codegen_reg_write(uop->dest_reg_a.reg, ir->wr_pos - 1);

    new_uop->type     = uop->type;
    new_uop->imm_data = uop->imm_data;
    new_uop->p        = uop->p;
    new_uop->pc       = uop->pc;
#if defined __ARM_EABI__ || defined _ARM_ || defined _M_ARM || defined __aarch64__ || defined _M_ARM64 || defined __loongarch_lp64
    new_uop->is_a16   = uop->is_a16;
#endif

    if (uop->jump_dest_uop != -1) {
        new_uop->jump_dest_uop = uop->jump_dest_uop + offset;
    }
}

#ifdef CODEGEN_BACKEND_HAS_MEM_LOOKUP_REUSE
/* Only these operations preserve the backend's lookup scratch registers.
   Keep this an allowlist: a new uop must not silently inherit that promise.
   -1 preserves the cache; 0 invalidates; 1/2 selects read/write lookups. */
static inline int
codegen_mem_lookup_kind(const uop_t *uop)
{
    if (uop->type & (UOP_TYPE_BARRIER | UOP_TYPE_JUMP_DEST | UOP_TYPE_JUMP))
        return 0;
    switch (uop->type & UOP_MASK) {
        case UOP_MEM_LOAD_REG & UOP_MASK:
            if (uop->is_a16 && IREG_GET_SIZE(uop->dest_reg_a.reg) == IREG_SIZE_DQ)
                return 0;
            return 1;
        case UOP_MEM_LOAD_ABS & UOP_MASK:
        case UOP_MEM_LOAD_SINGLE & UOP_MASK:
        case UOP_MEM_LOAD_DOUBLE & UOP_MASK:
            return 1;
        case UOP_MEM_STORE_REG & UOP_MASK:
            if (uop->is_a16 && IREG_GET_SIZE(uop->src_reg_c.reg) == IREG_SIZE_DQ)
                return 0;
            return 2;
        case UOP_MEM_STORE_ABS & UOP_MASK:
        case UOP_MEM_STORE_IMM_8 & UOP_MASK:
        case UOP_MEM_STORE_IMM_16 & UOP_MASK:
        case UOP_MEM_STORE_IMM_32 & UOP_MASK:
        case UOP_MEM_STORE_SINGLE & UOP_MASK:
        case UOP_MEM_STORE_DOUBLE & UOP_MASK:
            return 2;
        case UOP_ADD_IMM & UOP_MASK:
        case UOP_SUB_IMM & UOP_MASK:
            /* A page-sized stride has no useful locality for this cache. */
            if ((int32_t) uop->imm_data >= 4096 || (int32_t) uop->imm_data <= -4096)
                return 0;
            return uop->type & UOP_TYPE_ORDER_BARRIER ? 0 : -1;
        case UOP_MOV_IMM & UOP_MASK:
        case UOP_MOV & UOP_MASK:
        case UOP_MOVZX & UOP_MASK:
        case UOP_MOVSX & UOP_MASK:
        case UOP_ADD & UOP_MASK:
        case UOP_ADD_LSHIFT & UOP_MASK:
        case UOP_SUB & UOP_MASK:
        case UOP_AND & UOP_MASK:
        case UOP_AND_IMM & UOP_MASK:
        case UOP_OR & UOP_MASK:
        case UOP_OR_IMM & UOP_MASK:
        case UOP_XOR & UOP_MASK:
        case UOP_XOR_IMM & UOP_MASK:
        case UOP_SHL_IMM & UOP_MASK:
        case UOP_SHR_IMM & UOP_MASK:
        case UOP_SAR_IMM & UOP_MASK:
            return uop->type & UOP_TYPE_ORDER_BARRIER ? 0 : -1;
        default:
            return 0;
    }
}
#endif

void
codegen_ir_compile(ir_data_t *ir, codeblock_t *block)
{
    int jump_target_at_end = -1;
    int c;
#ifdef CODEGEN_BACKEND_HAS_MEM_LOOKUP_REUSE
    int mem_lookup = 0;
    const uop_t *last_mem = NULL;
#endif
#ifdef CODEGEN_HAS_SSE
    enum { SSE_UNCHECKED, SSE_CHECKED, SSE_AFTER_MEMORY };
    int sse_entered = SSE_UNCHECKED;
#endif

    if (codegen_unroll_count) {
        int unroll_end;

        codegen_set_loop_start(ir, codegen_unroll_first_instruction);
        unroll_end = ir->wr_pos;

        for (int unroll_count = 1; unroll_count < codegen_unroll_count; unroll_count++) {
            int offset = ir->wr_pos - codegen_unroll_start;
            //                        pclog("Unroll from %i to %i, offset %i - iteration %i\n", codegen_unroll_start, ir->wr_pos, offset, unroll_count);
            for (c = codegen_unroll_start; c < unroll_end; c++) {
                //                                pclog(" Duplicate uop %i\n", c);
                duplicate_uop(ir, &ir->uops[c], offset);
            }
        }
    }

    codegen_reg_mark_as_required();
    codegen_reg_process_dead_list(ir);
    block_write_data = codeblock_allocator_get_ptr(block->head_mem_block);
    block_pos        = 0;
#ifdef CODEGEN_BACKEND_HAS_SELECTIVE_XMM
    codegen_backend_ir_prologue(block);
#else
    codegen_backend_prologue(block);
#endif
#ifdef CODEGEN_BACKEND_HAS_MEM_STUBS
    codegen_backend_mem_begin();
#endif

    for (c = 0; c < ir->wr_pos; c++) {
        uop_t *uop = &ir->uops[c];

        //                pclog("uOP %i : %08x\n", c, uop->type);

#ifdef CODEGEN_HAS_SSE
        /* Coalesce SSE entry checks after loop unrolling, so duplicated
           iterations are checked too. Keep the first check (and its fault PC)
           in each straight-line region; calls and joins end the region.
           Memory helpers can reach devices: capable backends record whether
           a helper ran, so RAM hits only need a cheap conditional recheck.
           Successful inline alignment checks do not change SSE control state.
           Skip the redundant barrier as well as the check, allowing SSE
           values to stay in host registers. */
        if (uop->type & UOP_TYPE_JUMP_DEST)
            sse_entered = SSE_UNCHECKED;
#ifdef CODEGEN_BACKEND_HAS_SSE_RECHECK
        uop->type &= ~UOP_TYPE_SSE_INVALIDATE;
#endif
        if ((uop->type & UOP_MASK) == (UOP_SSE_ENTER & UOP_MASK)) {
            if (sse_entered == SSE_CHECKED)
                continue;
#ifdef CODEGEN_BACKEND_HAS_SSE_RECHECK
            if (sse_entered == SSE_AFTER_MEMORY) {
                /* SSE_ENTER has no register operands or branch bookkeeping.
                   Keep its IR barrier for fault liveness, but emit the guard
                   without flushing the successful RAM path's allocation. */
                codegen_backend_sse_recheck(block, uop);
                sse_entered = SSE_CHECKED;
                continue;
            }
#endif
            sse_entered = SSE_CHECKED;
#ifdef CODEGEN_BACKEND_HAS_SSE_RECHECK
        } else if ((uop->type & UOP_TYPE_MEM) && sse_entered != SSE_UNCHECKED) {
            uop->type |= UOP_TYPE_SSE_INVALIDATE;
            sse_entered = SSE_AFTER_MEMORY;
#endif
        } else if ((uop->type & (UOP_TYPE_BARRIER | UOP_TYPE_ORDER_BARRIER))
#ifdef CODEGEN_BACKEND_HAS_SSE_REGS
                   && (uop->type & UOP_MASK) != (UOP_CHECK_ALIGN & UOP_MASK)
#endif
        )
            sse_entered = SSE_UNCHECKED;
#endif

#ifdef CODEGEN_BACKEND_HAS_MEM_LOOKUP_REUSE
        uop->type &= ~(UOP_TYPE_MEM_REUSE | UOP_TYPE_MEM_SAME_ADDR);
        int lookup = codegen_mem_lookup_kind(uop);
        if (lookup > 0 && lookup == mem_lookup) {
            uop->type |= UOP_TYPE_MEM_REUSE;
            if ((uop->type & UOP_MASK) == (last_mem->type & UOP_MASK)
                && uop->src_reg_a.reg == last_mem->src_reg_a.reg
                && uop->src_reg_a.version == last_mem->src_reg_a.version
                && uop->src_reg_b.reg == last_mem->src_reg_b.reg
                && uop->src_reg_b.version == last_mem->src_reg_b.version
                && uop->imm_data == last_mem->imm_data && uop->is_a16 == last_mem->is_a16)
                uop->type |= UOP_TYPE_MEM_SAME_ADDR;
        }
        if (lookup >= 0) {
            mem_lookup = lookup;
            last_mem = lookup ? uop : NULL;
        }
#endif

        /* Keep the IR barriers for fault-state liveness. Inline SSE checks
           emit their writeback only on the exception path. Joins still flush
           before the branch target so both paths use the backing state. */
        if ((uop->type & UOP_TYPE_BARRIER)
#ifdef CODEGEN_BACKEND_HAS_SSE_REGS
            && ((uop->type & UOP_TYPE_JUMP_DEST)
                || ((uop->type & UOP_MASK) != (UOP_SSE_ENTER & UOP_MASK)
                    && (uop->type & UOP_MASK) != (UOP_CHECK_ALIGN & UOP_MASK)))
#endif
        )
            codegen_reg_flush_invalidate(ir, block);

        if (uop->type & UOP_TYPE_JUMP_DEST) {
            uop_t *uop_dest = uop;

            while (uop_dest->jump_list_next != -1) {
                uop_dest = &ir->uops[uop_dest->jump_list_next];
                codegen_set_jump_dest(block, uop_dest->p);
            }
        }

        if ((uop->type & UOP_MASK) == UOP_INVALID)
            continue;

#ifdef CODEGEN_BACKEND_HAS_MOV_IMM
        if ((uop->type & UOP_MASK) == (UOP_MOV_IMM & UOP_MASK) && reg_is_native_size(uop->dest_reg_a) && !codegen_reg_is_loaded(uop->dest_reg_a) && reg_version[IREG_GET_REG(uop->dest_reg_a.reg)][uop->dest_reg_a.version].refcount <= 0) {
            /*Special case for UOP_MOV_IMM - if destination not already in host register
              and won't be used again then just store directly to memory*/
            codegen_reg_write_imm(block, uop->dest_reg_a, uop->imm_data);
        } else
#endif
            if ((uop->type & UOP_MASK) == (UOP_MOV & UOP_MASK) && reg_version[IREG_GET_REG(uop->src_reg_a.reg)][uop->src_reg_a.version].refcount <= 1 && reg_is_native_size(uop->src_reg_a) && reg_is_native_size(uop->dest_reg_a)) {
            /*Special case for UOP_MOV - if source register won't be used again then
              just rename it to dest register instead of moving*/
            codegen_reg_alloc_register(invalid_ir_reg, uop->src_reg_a, invalid_ir_reg, invalid_ir_reg);
            uop->src_reg_a_real = codegen_reg_alloc_read_reg(block, uop->src_reg_a, NULL);
            codegen_reg_rename(block, uop->src_reg_a, uop->dest_reg_a);
            if (uop->type & UOP_TYPE_ORDER_BARRIER)
                codegen_reg_flush(ir, block);
        } else {
            if (uop->type & UOP_TYPE_PARAMS_REGS) {
                codegen_reg_alloc_register(uop->dest_reg_a, uop->src_reg_a, uop->src_reg_b, uop->src_reg_c);
                if (uop->src_reg_a.reg != IREG_INVALID) {
                    uop->src_reg_a_real = codegen_reg_alloc_read_reg(block, uop->src_reg_a, NULL);
                }
                if (uop->src_reg_b.reg != IREG_INVALID) {
                    uop->src_reg_b_real = codegen_reg_alloc_read_reg(block, uop->src_reg_b, NULL);
                }
                if (uop->src_reg_c.reg != IREG_INVALID) {
                    uop->src_reg_c_real = codegen_reg_alloc_read_reg(block, uop->src_reg_c, NULL);
                }
            }

            if (uop->type & UOP_TYPE_ORDER_BARRIER) {
#ifdef CODEGEN_BACKEND_HAS_MEM_REGS
                if (uop->type & UOP_TYPE_MEM)
                    codegen_reg_flush_mem_dest(block, uop->dest_reg_a);
                else
#endif
                    codegen_reg_flush(ir, block);
            }

            if (uop->type & UOP_TYPE_PARAMS_REGS) {
                if (uop->dest_reg_a.reg != IREG_INVALID) {
                    uop->dest_reg_a_real = codegen_reg_alloc_write_reg(block, uop->dest_reg_a);
                }
            }
#ifndef RELEASE_BUILD
            if (!uop_handlers[uop->type & UOP_MASK])
                fatal("!uop_handlers[uop->type & UOP_MASK] %08x\n", uop->type);
#endif
            uop_handlers[uop->type & UOP_MASK](block, uop);
        }

        if (uop->type & UOP_TYPE_JUMP) {
            if (uop->jump_dest_uop == ir->wr_pos) {
                if (jump_target_at_end == -1)
                    jump_target_at_end = c;
                else {
                    uop_t *uop_dest = &ir->uops[jump_target_at_end];

                    while (uop_dest->jump_list_next != -1)
                        uop_dest = &ir->uops[uop_dest->jump_list_next];

                    uop_dest->jump_list_next = c;
                }
            } else {
                uop_t *uop_dest = &ir->uops[uop->jump_dest_uop];

                while (uop_dest->jump_list_next != -1)
                    uop_dest = &ir->uops[uop_dest->jump_list_next];

                uop_dest->jump_list_next = c;
                ir->uops[uop->jump_dest_uop].type |= UOP_TYPE_JUMP_DEST;
            }
        }
    }

    codegen_reg_flush_invalidate(ir, block);

    if (jump_target_at_end != -1) {
        uop_t *uop_dest = &ir->uops[jump_target_at_end];

        while (1) {
            codegen_set_jump_dest(block, uop_dest->p);
            if (uop_dest->jump_list_next == -1)
                break;
            uop_dest = &ir->uops[uop_dest->jump_list_next];
        }
    }

    codegen_backend_epilogue(block);
#ifdef CODEGEN_BACKEND_HAS_MEM_STUBS
    codegen_backend_mem_finish(block);
#endif
    block_write_data = NULL;
#if 0
    if (has_ea)
        fatal("IR compilation complete\n");
#endif
}
