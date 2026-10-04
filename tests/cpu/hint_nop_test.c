/* Test the production hint decoder, code masks, and loop-unrolling gate.
   Opcode handlers stand in for CPU selection; no guest data memory is mapped. */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include "cpu.h"
#include "x86.h"
#include "x86seg_common.h"
#include "x86seg.h"
#include "../../src/codegen_new/codegen.h"
#include "../../src/codegen_new/codegen_ir.h"
#include "../../src/codegen_new/codegen_ops_misc.h"
#include "../../src/codegen_new/codegen_ops_helpers.h"

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "%s:%d: %s failed\n", __FILE__, __LINE__, #condition); \
        exit(1); \
    } \
} while (0)

cpu_state_t cpu_state;
int max_version_refcount;
static int hint_handler(uint32_t data) { (void) data; return 0; }
static int illegal_handler(uint32_t data) { (void) data; return 1; }

#define HINT_TABLE(base) \
    [base + 0x18] = hint_handler, [base + 0x19] = hint_handler, \
    [base + 0x1a] = hint_handler, [base + 0x1b] = hint_handler, \
    [base + 0x1c] = hint_handler, [base + 0x1d] = hint_handler, \
    [base + 0x1e] = hint_handler, [base + 0x1f] = hint_handler
const OpFn dynarec_ops_pentium3_0f[1024] = {
    HINT_TABLE(0), HINT_TABLE(0x100), HINT_TABLE(0x200), HINT_TABLE(0x300)
};
static OpFn selected_opcodes[1024];
const OpFn *x86_dynarec_opcodes_0f = selected_opcodes;

static int unroll_count;
int codegen_get_instruction_uop(codeblock_t *block, uint32_t pc, int *first, int *top)
{
    (void) block;
    (void) pc;
    *first = 0;
    *top = cpu_state.TOP;
    return 0;
}
void codegen_ir_set_unroll(int count, int start, int first)
{
    CHECK(start == 0 && first == 0);
    unroll_count = count;
}

/* Addressing-form lengths, including ModRM but excluding SIB. The SIB's
   no-base form contributes disp32 only in the mod=00 row. */
static const uint8_t lengths16[4][8] = {
    {1, 1, 1, 1, 1, 1, 3, 1},
    {2, 2, 2, 2, 2, 2, 2, 2},
    {3, 3, 3, 3, 3, 3, 3, 3},
    {1, 1, 1, 1, 1, 1, 1, 1}
};
static const uint8_t lengths32[4][8] = {
    {1, 1, 1, 1, 1, 5, 1, 1},
    {2, 2, 2, 2, 2, 2, 2, 2},
    {5, 5, 5, 5, 5, 5, 5, 5},
    {1, 1, 1, 1, 1, 1, 1, 1}
};

static ir_data_t ir;
static unsigned int cases;

static void check_hint(unsigned int opcode, unsigned int op32, unsigned int modrm,
                       unsigned int sib, uint32_t pc, unsigned int flags)
{
    const int mod = modrm >> 6;
    const int rm = modrm & 7;
    int length = (op32 & 0x200) ? lengths32[mod][rm] : lengths16[mod][rm];
    if ((op32 & 0x200) && mod != 3 && rm == 4) {
        length++;
        if (mod == 0 && (sib & 7) == 5)
            length += 4;
    }
    codeblock_t block = {0};
    block.pc = cpu_state.seg_cs.base + pc - 2;
    block.flags = flags;
    ir.block = &block;
    ir.wr_pos = 1;
    ir.uops[0].type = UOP_ADDPS;

    CHECK(ropHINT_NOP(&block, &ir, opcode, modrm | (sib << 8) | 0xffff0000,
                      op32, pc) == pc + length);
    CHECK(ir.wr_pos == 1 && ir.uops[0].type == UOP_ADDPS);
    uint64_t expected[2] = {0, 0};
    for (int byte = 0; byte < length; byte++) {
        uint32_t address = cpu_state.seg_cs.base + pc + byte;
        int shift = (flags & CODEBLOCK_BYTE_MASK) ? 0 : 6;
        uint32_t boundary = (flags & CODEBLOCK_BYTE_MASK) ? 63 : 4095;
        int page = !!((address ^ block.pc) & ~boundary);
        expected[page] |= UINT64_C(1) << ((address >> shift) & 63);
    }
    CHECK(block.page_mask == expected[0] && block.page_mask2 == expected[1]);

    /* The same instruction must fall back when the selected CPU assigns
       this opcode differently, without consuming bytes or modifying IR. */
    unsigned int index = opcode | op32;
    selected_opcodes[index] = illegal_handler;
    codeblock_t before = block;
    CHECK(ropHINT_NOP(&block, &ir, opcode, modrm | (sib << 8), op32, pc) == 0);
    CHECK(memcmp(&block, &before, sizeof(block)) == 0 && ir.wr_pos == 1);
    selected_opcodes[index] = hint_handler;
    cases++;
}

int main(void)
{
    memcpy(selected_opcodes, dynarec_ops_pentium3_0f, sizeof(selected_opcodes));
    cpu_state.seg_cs.base = 0x10000;
    cpu_state.seg_ds.base = UINT32_MAX; /* Hints must not check the operand. */
    cpu_state._cycles = 123;
    cpu_state_t before = cpu_state;
    const uint32_t pcs[] = {16, 63, 65, 4095, 4097};
    const unsigned int flags[] = {0, CODEBLOCK_BYTE_MASK,
                                   CODEBLOCK_BYTE_MASK | CODEBLOCK_NO_IMMEDIATES};
    for (unsigned int op32 = 0; op32 <= 0x300; op32 += 0x100)
        for (unsigned int opcode = 0x18; opcode <= 0x1f; opcode++)
            for (unsigned int modrm = 0; modrm < 256; modrm++) {
                int sibs = ((op32 & 0x200) && (modrm >> 6) != 3 && (modrm & 7) == 4) ? 256 : 1;
                for (int sib = 0; sib < sibs; sib++)
                    for (unsigned int p = 0; p < sizeof(pcs) / sizeof(pcs[0]); p++)
                        for (unsigned int f = 0; f < sizeof(flags) / sizeof(flags[0]); f++)
                            check_hint(opcode, op32, modrm, sib, pcs[p], flags[f]);
            }
    CHECK(memcmp(&cpu_state, &before, sizeof(cpu_state)) == 0);

    codeblock_t block = {0};
    ir.block = &block;
    ir.wr_pos = 1;
    ir.uops[0].type = UOP_ADDPS;
    CHECK(ropHINT_NOP(&block, &ir, 0x18, 0x123405, 0x300, 16) == 21);
    CHECK(codegen_can_unroll_full(&block, &ir, 32, 0));
    CHECK(unroll_count > 1);
    ir.uops[ir.wr_pos++].type = UOP_CALL_INSTRUCTION_FUNC;
    CHECK(!codegen_can_unroll_full(&block, &ir, 32, 0));
    printf("Hint NOP tests passed (%u decoding/gating/mask cases, loop unrolling)\n", cases);
    return 0;
}
