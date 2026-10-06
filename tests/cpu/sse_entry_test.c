/* Exercise the production IR lowering and unrolling with a recording backend.
   This checks entry barriers and fault PCs, not SSE arithmetic semantics. */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/codegen_new/codegen_ir.c"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

cpu_state_t cpu_state;
int cpu_block_end;
uint8_t *block_write_data;
int block_pos;
ir_reg_t invalid_ir_reg = { IREG_INVALID, 0 };
uint8_t reg_last_version[IREG_COUNT];
uint64_t dirty_ir_regs[2];
reg_version_t reg_version[IREG_COUNT][256];
uint16_t reg_dead_list;
int max_version_refcount;

static uint8_t code_buffer[1];
static uint32_t entry_pcs[32];
static int entries, rechecks, arithmetic, full_flushes, order_flushes, patched_jumps;

void fatal(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(1);
}

uint8_t *codeblock_allocator_get_ptr(struct mem_block_t *block) { (void)block; return code_buffer; }
void codegen_backend_prologue(codeblock_t *block) { (void)block; }
#ifdef CODEGEN_BACKEND_HAS_SELECTIVE_XMM
void codegen_backend_ir_prologue(codeblock_t *block) { (void)block; }
#endif
void codegen_backend_epilogue(codeblock_t *block) { (void)block; }
void codegen_backend_mem_begin(void) { }
void codegen_backend_mem_finish(codeblock_t *block) { (void)block; }
void codegen_set_jump_dest(codeblock_t *block, void *p) { (void)block; (void)p; patched_jumps++; }
void codegen_reg_mark_as_required(void) { }
void codegen_reg_process_dead_list(ir_data_t *ir) { (void)ir; }
void codegen_reg_flush(ir_data_t *ir, codeblock_t *block) { (void)ir; (void)block; order_flushes++; }
void codegen_reg_flush_mem_dest(codeblock_t *block, ir_reg_t reg) { (void)block; (void)reg; }
void codegen_reg_flush_invalidate(ir_data_t *ir, codeblock_t *block) { (void)ir; (void)block; full_flushes++; }
int reg_is_native_size(ir_reg_t reg) { (void)reg; return 0; }
int codegen_reg_is_loaded(ir_reg_t reg) { (void)reg; return 0; }
void codegen_reg_write_imm(codeblock_t *block, ir_reg_t reg, uint32_t imm) { (void)block; (void)reg; (void)imm; }
void codegen_reg_rename(codeblock_t *block, ir_reg_t src, ir_reg_t dst) { (void)block; (void)src; (void)dst; }
void codegen_reg_alloc_register(ir_reg_t dst, ir_reg_t a, ir_reg_t b, ir_reg_t c) { (void)dst; (void)a; (void)b; (void)c; }
ir_host_reg_t codegen_reg_alloc_read_reg(codeblock_t *block, ir_reg_t reg, int *idx) { (void)block; (void)reg; (void)idx; return 0; }
ir_host_reg_t codegen_reg_alloc_write_reg(codeblock_t *block, ir_reg_t reg) { (void)block; (void)reg; return 0; }

/* The real hook appends register setup, which does not end an SSE region. */
void codegen_set_loop_start(ir_data_t *ir, int first_instruction)
{
    (void)first_instruction;
    uop_MOV_IMM(ir, IREG_op32, 0x300);
}

static int record_uop(codeblock_t *block, uop_t *uop)
{
    (void)block;
    if ((uop->type & UOP_MASK) == (UOP_SSE_ENTER & UOP_MASK)) {
        CHECK(entries < 32);
        entry_pcs[entries++] = uop->imm_data;
    } else if ((uop->type & UOP_MASK) == (UOP_ADDPS & UOP_MASK))
        arithmetic++;
    return 0;
}

void codegen_backend_sse_recheck(codeblock_t *block, uop_t *uop)
{
    rechecks++;
    record_uop(block, uop);
}

const uOpFn uop_handlers[UOP_MAX] = {
    [UOP_SSE_ENTER & UOP_MASK] = record_uop,
    [UOP_ADDPS & UOP_MASK] = record_uop,
    [UOP_MOV_IMM & UOP_MASK] = record_uop,
    [UOP_CALL_FUNC & UOP_MASK] = record_uop,
    [UOP_CALL_FUNC_RESULT & UOP_MASK] = record_uop,
    [UOP_CALL_INSTRUCTION_FUNC & UOP_MASK] = record_uop,
    [UOP_MEM_LOAD_REG & UOP_MASK] = record_uop,
    [UOP_CHECK_ALIGN & UOP_MASK] = record_uop,
    [UOP_JMP_DEST & UOP_MASK] = record_uop,
};

static ir_data_t *start_block(void)
{
    entries = rechecks = arithmetic = full_flushes = order_flushes = patched_jumps = 0;
    memset(reg_last_version, 0, sizeof(reg_last_version));
    memset(reg_version, 0, sizeof(reg_version));
    dirty_ir_regs[0] = dirty_ir_regs[1] = 0;
    reg_dead_list = max_version_refcount = cpu_block_end = 0;
    return codegen_ir_init();
}

static void add_sse(ir_data_t *ir, uint32_t pc)
{
    cpu_state.oldpc = pc;
    uop_SSE_ENTER(ir);
    uop_ADDPS(ir, IREG_XMM(0), IREG_XMM(0), IREG_XMM(1));
}

static void compile(ir_data_t *ir)
{
    codeblock_t block = { 0 };
    codegen_ir_compile(ir, &block);
}

int main(void)
{
    ir_data_t *ir;

    /* Adjacent arithmetic retains just the first check and its exception PC.
       Repeat with a new block to catch accidental state leaking across blocks. */
    for (int run = 0; run < 2; run++) {
        ir = start_block();
        add_sse(ir, 0x100 + run * 0x100);
        add_sse(ir, 0x103 + run * 0x100);
        uop_MOV_IMM(ir, IREG_EAX, 7);
        add_sse(ir, 0x10b + run * 0x100);
        compile(ir);
        CHECK(entries == 1 && entry_pcs[0] == (uint32_t)(0x100 + run * 0x100));
        CHECK(arithmetic == 3);
        CHECK(full_flushes == 1); /* Only the final block writeback. */
    }

    /* Memory callbacks can change control state. Their following SSE check
       is conditional on helper execution, preserving the RAM-hit region. */
    const uint32_t barriers[] = {
        UOP_CALL_FUNC, UOP_CALL_FUNC_RESULT, UOP_CALL_INSTRUCTION_FUNC,
        UOP_MEM_LOAD_REG
    };
    for (unsigned i = 0; i < sizeof(barriers) / sizeof(barriers[0]); i++) {
        ir = start_block();
        add_sse(ir, 0x100);
        uop_gen(barriers[i], ir);
        add_sse(ir, 0x200);
        add_sse(ir, 0x203);
        compile(ir);
        CHECK(entries == 2 && entry_pcs[0] == 0x100 && entry_pcs[1] == 0x200);
        CHECK(rechecks == !!(barriers[i] & UOP_TYPE_MEM));
        CHECK(arithmetic == 3);
        CHECK(full_flushes == ((barriers[i] & UOP_TYPE_BARRIER) ? 2 : 1));
        CHECK(order_flushes == ((barriers[i] & UOP_TYPE_ORDER_BARRIER) && !(barriers[i] & UOP_TYPE_MEM) ? 1 : 0));
    }

    /* Inline alignment tests retain the entry check, address and fault PC.
       They must not cause an unconditional register flush. */
    ir = start_block();
    add_sse(ir, 0x100);
    cpu_state.oldpc = 0x110;
    uop_CHECK_ALIGN(ir);
    CHECK((ir->uops[ir->wr_pos - 1].type & UOP_MASK) == (UOP_CHECK_ALIGN & UOP_MASK));
    CHECK(ir->uops[ir->wr_pos - 1].src_reg_a.reg == IREG_eaaddr);
    CHECK(ir->uops[ir->wr_pos - 1].imm_data == 0x110);
    add_sse(ir, 0x120);
    codegen_ir_set_unroll(3, 0, 0);
    compile(ir);
    CHECK(entries == 1 && entry_pcs[0] == 0x100);
    CHECK(arithmetic == 6 && full_flushes == 1 && order_flushes == 0);

    /* A branch can bypass the check in the fallthrough path. The destination
       must retain its own check, and the branch fixup must still be emitted. */
    ir = start_block();
    int branch = uop_gen(UOP_JMP_DEST, ir);
    add_sse(ir, 0x100);
    uop_set_jump_dest(ir, branch);
    add_sse(ir, 0x200);
    add_sse(ir, 0x203);
    compile(ir);
    CHECK(entries == 2 && entry_pcs[1] == 0x200);
    CHECK(patched_jumps == 1);
    CHECK(full_flushes == 2); /* Join synchronization and final writeback. */

    /* A join on an ordinary instruction must also invalidate the prior check. */
    ir = start_block();
    branch = uop_gen(UOP_JMP_DEST, ir);
    add_sse(ir, 0x100);
    uop_set_jump_dest(ir, branch);
    uop_MOV_IMM(ir, IREG_EAX, 7);
    add_sse(ir, 0x200);
    compile(ir);
    CHECK(entries == 2 && entry_pcs[1] == 0x200);
    CHECK(patched_jumps == 1);

    /* A register-only unrolled loop can share its check across all copies. */
    ir = start_block();
    add_sse(ir, 0x100);
    add_sse(ir, 0x103);
    codegen_ir_set_unroll(3, 0, 0);
    compile(ir);
    CHECK(entries == 1 && entry_pcs[0] == 0x100);
    CHECK(arithmetic == 6 && full_flushes == 1);

    /* Unrolling must not reuse a pre-loop check after a helper in a previous
       iteration. Each copy retains its first check after that helper. */
    ir = start_block();
    add_sse(ir, 0x100);
    int loop_start = ir->wr_pos;
    add_sse(ir, 0x200);
    add_sse(ir, 0x203);
    uop_gen(UOP_CALL_FUNC, ir);
    codegen_ir_set_unroll(3, loop_start, 0);
    compile(ir);
    CHECK(entries == 3 && entry_pcs[0] == 0x100);
    CHECK(entry_pcs[1] == 0x200 && entry_pcs[2] == 0x200);
    CHECK(arithmetic == 7);

    puts("SSE entry lowering tests passed");
    return 0;
}
