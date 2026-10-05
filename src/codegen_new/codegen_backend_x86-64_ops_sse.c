#if defined __amd64__ || defined _M_X64

#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat_unused.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_x86-64_defs.h"
#    include "codegen_backend_x86-64_ops_sse.h"
#    include "codegen_backend_x86-64_ops_helpers.h"

static inline uint8_t
rex(int w, int reg, int index, int rm)
{
    return 0x40 | (w ? 0x08 : 0) | ((reg & 8) ? 0x04 : 0) |
           ((index & 8) ? 0x02 : 0) | ((rm & 8) ? 0x01 : 0);
}

static inline int
rex_needed(int w, int reg, int index, int rm)
{
    return w || ((reg | index | rm) & 8);
}

static inline void
add_rex_if_needed(codeblock_t *block, int w, int reg, int index, int rm)
{
    if (rex_needed(w, reg, index, rm))
        codegen_addbyte(block, rex(w, reg, index, rm));
}

static inline uint8_t
modrm_reg_reg(int reg, int rm)
{
    return 0xc0 | ((reg & 7) << 3) | (rm & 7);
}

/*All emitters below take XMM (and GPR) numbers 0-15. The mandatory prefix
  (0x66/0xf2/0xf3, or 0 for none) must come before REX, which must
  immediately precede the 0x0f escape.*/
static inline void
sse_prefix_rex(codeblock_t *block, uint8_t prefix, int w, int reg, int index, int rm)
{
    if (prefix)
        codegen_addbyte(block, prefix);
    add_rex_if_needed(block, w, reg, index, rm);
}

/*[prefix] [REX] 0F opcode ModRM(reg, rm)*/
static inline void
sse_op_reg_reg_w(codeblock_t *block, uint8_t prefix, int w, uint8_t opcode, int reg, int rm)
{
    codegen_alloc_bytes(block, 5);
    sse_prefix_rex(block, prefix, w, reg, 0, rm);
    codegen_addbyte3(block, 0x0f, opcode, modrm_reg_reg(reg, rm));
}
static inline void
sse_op_reg_reg(codeblock_t *block, uint8_t prefix, uint8_t opcode, int reg, int rm)
{
    sse_op_reg_reg_w(block, prefix, 0, opcode, reg, rm);
}
static inline void
sse_op_reg_reg_imm(codeblock_t *block, uint8_t prefix, uint8_t opcode, int reg, int rm, uint8_t imm)
{
    codegen_alloc_bytes(block, 6);
    sse_prefix_rex(block, prefix, 0, reg, 0, rm);
    codegen_addbyte4(block, 0x0f, opcode, modrm_reg_reg(reg, rm), imm);
}

/*[prefix] [REX] 0F opcode ModRM [SIB] disp - operand at [base_reg + offset]*/
static inline void
sse_op_base_offset(codeblock_t *block, uint8_t prefix, uint8_t opcode, int reg, int base_reg, int32_t offset)
{
    int disp8 = (offset >= -128 && offset <= 127);

    codegen_alloc_bytes(block, 10);
    sse_prefix_rex(block, prefix, 0, reg, 0, base_reg);
    codegen_addbyte3(block, 0x0f, opcode, (disp8 ? 0x40 : 0x80) | ((reg & 7) << 3) | (base_reg & 7));
    if ((base_reg & 7) == REG_RSP)
        codegen_addbyte(block, 0x24); /*SIB - base RSP/R12, no index*/
    if (disp8)
        codegen_addbyte(block, offset);
    else
        codegen_addlong(block, offset);
}

/*Operand at p. cpu_state is addressed relative to RBP, anything else needs a
  32-bit absolute address*/
static inline void
sse_op_abs(codeblock_t *block, uint8_t prefix, uint8_t opcode, int reg, void *p)
{
    intptr_t offset = (intptr_t) ((uintptr_t) p - (((uintptr_t) &cpu_state) + 128));

    if (offset >= INT32_MIN && offset <= INT32_MAX) {
        sse_op_base_offset(block, prefix, opcode, reg, REG_RBP, (int32_t) offset);
        return;
    }
    if ((uintptr_t) p >> 32)
        fatal("sse_op_abs - out of range %p\n", p);
    codegen_alloc_bytes(block, 9);
    sse_prefix_rex(block, prefix, 0, reg, 0, 0);
    codegen_addbyte4(block, 0x0f, opcode, 0x04 | ((reg & 7) << 3), 0x25); /*[disp32]*/
    codegen_addlong(block, (uint32_t) (uintptr_t) p);
}

/*Operand at [base_reg + idx_reg]*/
static inline void
sse_op_base_index(codeblock_t *block, uint8_t prefix, uint8_t opcode, int reg, int base_reg, int idx_reg)
{
    codegen_alloc_bytes(block, 7);
    sse_prefix_rex(block, prefix, 0, reg, idx_reg, base_reg);
    if ((base_reg & 7) == REG_RBP) {
        /*RBP/R13 can only be a base with a displacement*/
        codegen_addbyte3(block, 0x0f, opcode, 0x44 | ((reg & 7) << 3));
        codegen_addbyte2(block, (base_reg & 7) | ((idx_reg & 7) << 3), 0);
    } else {
        codegen_addbyte3(block, 0x0f, opcode, 0x04 | ((reg & 7) << 3));
        codegen_addbyte(block, (base_reg & 7) | ((idx_reg & 7) << 3));
    }
}

/*Operand at addr[base_reg + idx_reg << shift]*/
static inline void
sse_op_abs_index(codeblock_t *block, uint8_t prefix, uint8_t opcode, int reg, uint32_t addr, int base_reg, int idx_reg, int shift)
{
    uint8_t sib = (base_reg & 7) | ((idx_reg & 7) << 3) | (shift << 6);

    codegen_alloc_bytes(block, 10);
    sse_prefix_rex(block, prefix, 0, reg, idx_reg, base_reg);
    if (addr < 0x80 || addr >= 0xffffff80) {
        codegen_addbyte3(block, 0x0f, opcode, 0x44 | ((reg & 7) << 3));
        codegen_addbyte2(block, sib, addr & 0xff);
    } else {
        codegen_addbyte3(block, 0x0f, opcode, 0x84 | ((reg & 7) << 3));
        codegen_addbyte(block, sib);
        codegen_addlong(block, addr);
    }
}

void
host_x86_ADDPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x58, dst_reg, src_reg); /*ADDPS dst_reg, src_reg*/
}
void
host_x86_ADDPD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x58, dst_reg, src_reg); /*ADDPD dst_reg, src_reg*/
}
void
host_x86_ADDSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf2, 0x58, dst_reg, src_reg); /*ADDSD dst_reg, src_reg*/
}
void
host_x86_ADDSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x58, dst_reg, src_reg); /*ADDSS dst_reg, src_reg*/
}

void
host_x86_CMPPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg, int type)
{
    sse_op_reg_reg_imm(block, 0, 0xc2, dst_reg, src_reg, type); /*CMPPS dst_reg, src_reg, type*/
}
void
host_x86_CMPSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg, int type)
{
    sse_op_reg_reg_imm(block, 0xf3, 0xc2, dst_reg, src_reg, type); /*CMPSS dst_reg, src_reg, type*/
}

void
host_x86_COMISS_XREG_XREG(codeblock_t *block, int src_reg_a, int src_reg_b)
{
    sse_op_reg_reg(block, 0, 0x2f, src_reg_a, src_reg_b); /*COMISS src_reg_a, src_reg_b*/
}
void
host_x86_UCOMISS_XREG_XREG(codeblock_t *block, int src_reg_a, int src_reg_b)
{
    sse_op_reg_reg(block, 0, 0x2e, src_reg_a, src_reg_b); /*UCOMISS src_reg_a, src_reg_b*/
}

void
host_x86_COMISD_XREG_XREG(codeblock_t *block, int src_reg_a, int src_reg_b)
{
    sse_op_reg_reg(block, 0x66, 0x2e, src_reg_a, src_reg_b); /*UCOMISD src_reg_a, src_reg_b*/
}

void
host_x86_CVTDQ2PS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x5b, dst_reg, src_reg); /*CVTDQ2PS dst_reg, src_reg*/
}
void
host_x86_CVTPS2DQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x5b, dst_reg, src_reg); /*CVTPS2DQ dst_reg, src_reg*/
}

void
host_x86_CVTTPS2DQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x5b, dst_reg, src_reg); /*CVTTPS2DQ dst_reg, src_reg*/
}

void
host_x86_CVTSD2SI_REG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf2, 0x2d, dst_reg, src_reg); /*CVTSD2SI dst_reg, src_reg*/
}
void
host_x86_CVTSD2SI_REG64_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg_w(block, 0xf2, 1, 0x2d, dst_reg, src_reg); /*CVTSD2SI dst_reg, src_reg*/
}
void
host_x86_CVTSD2SS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf2, 0x5a, dst_reg, src_reg); /*CVTSD2SS dst_reg, src_reg*/
}

void
host_x86_CVTSS2SI_REG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x2d, dst_reg, src_reg); /*CVTSS2SI dst_reg, src_reg*/
}
void
host_x86_CVTTSS2SI_REG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x2c, dst_reg, src_reg); /*CVTTSS2SI dst_reg, src_reg*/
}

void
host_x86_CVTSI2SD_XREG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf2, 0x2a, dst_reg, src_reg); /*CVTSI2SD dst_reg, src_reg*/
}
void
host_x86_CVTSI2SS_XREG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x2a, dst_reg, src_reg); /*CVTSI2SS dst_reg, src_reg*/
}
void
host_x86_CVTSI2SD_XREG_REG64(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg_w(block, 0xf2, 1, 0x2a, dst_reg, src_reg); /*CVTSI2SD dst_reg, src_reg*/
}

void
host_x86_CVTSS2SD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x5a, dst_reg, src_reg); /*CVTSS2SD dst_reg, src_reg*/
}
void
host_x86_CVTSS2SD_XREG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int idx_reg)
{
    sse_op_base_index(block, 0xf3, 0x5a, dst_reg, base_reg, idx_reg); /*CVTSS2SD XMMx, [base_reg + idx_reg]*/
}

void
host_x86_DIVSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf2, 0x5e, dst_reg, src_reg); /*DIVSD dst_reg, src_reg*/
}
void
host_x86_DIVPD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x5e, dst_reg, src_reg); /*DIVPD dst_reg, src_reg*/
}
void
host_x86_DIVPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x5e, dst_reg, src_reg); /*DIVPS dst_reg, src_reg*/
}
void
host_x86_DIVSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x5e, dst_reg, src_reg); /*DIVSS dst_reg, src_reg*/
}

void
host_x86_LDMXCSR(codeblock_t *block, void *p)
{
    int offset = (uintptr_t) p - (((uintptr_t) &cpu_state) + 128);

    if (offset >= -128 && offset < 127) {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block, 0x0f, 0xae, 0x50 | REG_EBP, offset); /*LDMXCSR offset[EBP]*/
    } else if (offset < (1ULL << 32)) {
        codegen_alloc_bytes(block, 7);
        codegen_addbyte3(block, 0x0f, 0xae, 0x90 | REG_EBP); /*LDMXCSR offset[EBP]*/
        codegen_addlong(block, offset);
    } else {
        fatal("host_x86_LDMXCSR - out of range %p\n", p);
    }
}

void
host_x86_LDMXCSR_BASE_OFFSET(codeblock_t *block, int base_reg, int offset)
{
    if (offset >= -128 && offset <= 127) {
        if ((base_reg & 7) == REG_RSP) {
            codegen_alloc_bytes(block, 6);
            add_rex_if_needed(block, 0, 0, 0, base_reg);
            codegen_addbyte3(block, 0x0f, 0xae, 0x50 | (base_reg & 7));
            codegen_addbyte2(block, 0x24, offset);
        } else {
            codegen_alloc_bytes(block, 5);
            add_rex_if_needed(block, 0, 0, 0, base_reg);
            codegen_addbyte4(block, 0x0f, 0xae, 0x50 | (base_reg & 7), offset);
        }
    } else
        fatal("LDMXCSR_BASE_OFFSET - offset %i\n", offset);
}

void
host_x86_MAXSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf2, 0x5f, dst_reg, src_reg); /*MAXSD dst_reg, src_reg*/
}

void
host_x86_MOVD_BASE_INDEX_XREG(codeblock_t *block, int base_reg, int idx_reg, int src_reg)
{
    sse_op_base_index(block, 0x66, 0x7e, src_reg, base_reg, idx_reg); /*MOVD [base_reg + idx_reg], XMMx*/
}
void
host_x86_MOVD_REG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x7e, src_reg, dst_reg); /*MOVD dst_reg, src_reg*/
}
void
host_x86_MOVD_XREG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int idx_reg)
{
    sse_op_base_index(block, 0x66, 0x6e, dst_reg, base_reg, idx_reg); /*MOVD XMMx, [base_reg + idx_reg]*/
}
void
host_x86_MOVD_XREG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x6e, dst_reg, src_reg); /*MOVD dst_reg, src_reg*/
}

void
host_x86_MOVQ_ABS_XREG(codeblock_t *block, void *p, int src_reg)
{
    sse_op_abs(block, 0x66, 0xd6, src_reg, p); /*MOVQ [p], src_reg*/
}
void
host_x86_MOVQ_ABS_REG_REG_SHIFT_XREG(codeblock_t *block, uint32_t addr, int src_reg_a, int src_reg_b, int shift, int src_reg)
{
    sse_op_abs_index(block, 0x66, 0xd6, src_reg, addr, src_reg_a, src_reg_b, shift); /*MOVQ addr[src_reg_a + src_reg_b << shift], XMMx*/
}

void
host_x86_MOVQ_BASE_INDEX_XREG(codeblock_t *block, int base_reg, int idx_reg, int src_reg)
{
    sse_op_base_index(block, 0x66, 0xd6, src_reg, base_reg, idx_reg); /*MOVQ [base_reg + idx_reg], XMMx*/
}
void
host_x86_MOVQ_BASE_OFFSET_XREG(codeblock_t *block, int base_reg, int offset, int src_reg)
{
    sse_op_base_offset(block, 0x66, 0xd6, src_reg, base_reg, offset); /*MOVQ [base_reg + offset], XMMx*/
}

void
host_x86_MOVQ_XREG_ABS(codeblock_t *block, int dst_reg, void *p)
{
    sse_op_abs(block, 0xf3, 0x7e, dst_reg, p); /*MOVQ dst_reg, [p]*/
}
void
host_x86_MOVQ_XREG_ABS_REG_REG_SHIFT(codeblock_t *block, int dst_reg, uint32_t addr, int src_reg_a, int src_reg_b, int shift)
{
    sse_op_abs_index(block, 0xf3, 0x7e, dst_reg, addr, src_reg_a, src_reg_b, shift); /*MOVQ XMMx, addr[src_reg_a + src_reg_b << shift]*/
}
void
host_x86_MOVQ_XREG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int idx_reg)
{
    sse_op_base_index(block, 0xf3, 0x7e, dst_reg, base_reg, idx_reg); /*MOVQ XMMx, [base_reg + idx_reg]*/
}
void
host_x86_MOVQ_XREG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    sse_op_base_offset(block, 0xf3, 0x7e, dst_reg, base_reg, offset); /*MOVQ XMMx, [base_reg + offset]*/
}

void
host_x86_MOVQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x7e, dst_reg, src_reg); /*MOVQ dst_reg, src_reg*/
}

void
host_x86_MOVDQA_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x6f, dst_reg, src_reg); /*MOVDQA dst_reg, src_reg*/
}

void
host_x86_MOVSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x10, dst_reg, src_reg); /*MOVSS dst_reg, src_reg*/
}

void
host_x86_MOVSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf2, 0x10, dst_reg, src_reg); /*MOVSD dst_reg, src_reg*/
}

/*cpu_state.XMM is not guaranteed to be 16 byte aligned, so use MOVDQU for
  memory accesses*/
void
host_x86_MOVDQU_XREG_ABS(codeblock_t *block, int dst_reg, void *p)
{
    sse_op_abs(block, 0xf3, 0x6f, dst_reg, p); /*MOVDQU dst_reg, [p]*/
}
void
host_x86_MOVDQU_ABS_XREG(codeblock_t *block, void *p, int src_reg)
{
    sse_op_abs(block, 0xf3, 0x7f, src_reg, p); /*MOVDQU [p], src_reg*/
}
void
host_x86_MOVDQU_XREG_BASE_INDEX(codeblock_t *block, int dst_reg, int base_reg, int idx_reg)
{
    sse_op_base_index(block, 0xf3, 0x6f, dst_reg, base_reg, idx_reg); /*MOVDQU dst_reg, [base_reg + idx_reg]*/
}
void
host_x86_MOVDQU_BASE_INDEX_XREG(codeblock_t *block, int base_reg, int idx_reg, int src_reg)
{
    sse_op_base_index(block, 0xf3, 0x7f, src_reg, base_reg, idx_reg); /*MOVDQU [base_reg + idx_reg], src_reg*/
}
void
host_x86_MOVDQU_XREG_BASE_OFFSET(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    sse_op_base_offset(block, 0xf3, 0x6f, dst_reg, base_reg, offset); /*MOVDQU dst_reg, [base_reg + offset]*/
}
void
host_x86_MOVDQU_BASE_OFFSET_XREG(codeblock_t *block, int base_reg, int offset, int src_reg)
{
    sse_op_base_offset(block, 0xf3, 0x7f, src_reg, base_reg, offset); /*MOVDQU [base_reg + offset], src_reg*/
}

void
host_x86_MOVQ_REG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg_w(block, 0x66, 1, 0x7e, src_reg, dst_reg); /*MOVQ dst_reg, src_reg*/
}
void
host_x86_MOVQ_XREG_REG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg_w(block, 0x66, 1, 0x6e, dst_reg, src_reg); /*MOVQ dst_reg, src_reg*/
}

void
host_x86_MOVHLPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x12, dst_reg, src_reg); /*MOVHLPS dst_reg, src_reg*/
}
void
host_x86_MOVLHPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x16, dst_reg, src_reg); /*MOVLHPS dst_reg, src_reg*/
}
void
host_x86_MOVMSKPS_REG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x50, dst_reg, src_reg); /*MOVMSKPS dst_reg, src_reg*/
}

void
host_x86_MAXPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x5f, dst_reg, src_reg); /*MAXPS dst_reg, src_reg*/
}
void
host_x86_MAXSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x5f, dst_reg, src_reg); /*MAXSS dst_reg, src_reg*/
}
void
host_x86_MINPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x5d, dst_reg, src_reg); /*MINPS dst_reg, src_reg*/
}
void
host_x86_MINSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x5d, dst_reg, src_reg); /*MINSS dst_reg, src_reg*/
}

void
host_x86_MULPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x59, dst_reg, src_reg); /*MULPS dst_reg, src_reg*/
}
void
host_x86_MULPD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x59, dst_reg, src_reg); /*MULPD dst_reg, src_reg*/
}
void
host_x86_MULSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf2, 0x59, dst_reg, src_reg); /*MULSD dst_reg, src_reg*/
}
void
host_x86_MULSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x59, dst_reg, src_reg); /*MULSS dst_reg, src_reg*/
}

void
host_x86_PACKSSWB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x63, dst_reg, src_reg);           /*PACKSSWB dst_reg, src_reg*/
    sse_op_reg_reg_imm(block, 0x66, 0x70, dst_reg, dst_reg, 0x88); /*PSHUFD dst_reg, dst_reg, 0x88 (move bits 64-95 to 32-63)*/
}
void
host_x86_PACKSSDW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x6b, dst_reg, src_reg);           /*PACKSSDW dst_reg, src_reg*/
    sse_op_reg_reg_imm(block, 0x66, 0x70, dst_reg, dst_reg, 0x88); /*PSHUFD dst_reg, dst_reg, 0x88 (move bits 64-95 to 32-63)*/
}
void
host_x86_PACKUSWB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x67, dst_reg, src_reg);           /*PACKUSWB dst_reg, src_reg*/
    sse_op_reg_reg_imm(block, 0x66, 0x70, dst_reg, dst_reg, 0x88); /*PSHUFD dst_reg, dst_reg, 0x88 (move bits 64-95 to 32-63)*/
}

void
host_x86_PACKSSWB_XREG_XREG_SSE(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x63, dst_reg, src_reg); /*PACKSSWB dst_reg, src_reg*/
}
void
host_x86_PACKSSDW_XREG_XREG_SSE(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x6b, dst_reg, src_reg); /*PACKSSDW dst_reg, src_reg*/
}
void
host_x86_PACKUSWB_XREG_XREG_SSE(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x67, dst_reg, src_reg); /*PACKUSWB dst_reg, src_reg*/
}

void
host_x86_PADDB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xfc, dst_reg, src_reg); /*PADDB dst_reg, src_reg*/
}
void
host_x86_PADDW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xfd, dst_reg, src_reg); /*PADDW dst_reg, src_reg*/
}
void
host_x86_PADDD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xfe, dst_reg, src_reg); /*PADDD dst_reg, src_reg*/
}
void
host_x86_PADDQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xd4, dst_reg, src_reg); /*PADDQ dst_reg, src_reg*/
}
void
host_x86_PADDSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xec, dst_reg, src_reg); /*PADDSB dst_reg, src_reg*/
}
void
host_x86_PADDSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xed, dst_reg, src_reg); /*PADDSW dst_reg, src_reg*/
}
void
host_x86_PADDUSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xdc, dst_reg, src_reg); /*PADDUSB dst_reg, src_reg*/
}
void
host_x86_PADDUSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xdd, dst_reg, src_reg); /*PADDUSW dst_reg, src_reg*/
}

void
host_x86_PAVGB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xe0, dst_reg, src_reg); /*PAVGB dst_reg, src_reg*/
}
void
host_x86_PAVGW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xe3, dst_reg, src_reg); /*PAVGW dst_reg, src_reg*/
}

void
host_x86_PAND_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xdb, dst_reg, src_reg); /*PAND dst_reg, src_reg*/
}
void
host_x86_PANDN_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xdf, dst_reg, src_reg); /*PANDN dst_reg, src_reg*/
}
void
host_x86_POR_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xeb, dst_reg, src_reg); /*POR dst_reg, src_reg*/
}
void
host_x86_PXOR_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xef, dst_reg, src_reg); /*PXOR dst_reg, src_reg*/
}

void
host_x86_PCMPEQB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x74, dst_reg, src_reg); /*PCMPEQB dst_reg, src_reg*/
}
void
host_x86_PCMPEQW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x75, dst_reg, src_reg); /*PCMPEQW dst_reg, src_reg*/
}
void
host_x86_PCMPEQD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x76, dst_reg, src_reg); /*PCMPEQD dst_reg, src_reg*/
}
void
host_x86_PCMPGTB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x64, dst_reg, src_reg); /*PCMPGTB dst_reg, src_reg*/
}
void
host_x86_PCMPGTW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x65, dst_reg, src_reg); /*PCMPGTW dst_reg, src_reg*/
}
void
host_x86_PCMPGTD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x66, dst_reg, src_reg); /*PCMPGTD dst_reg, src_reg*/
}

void
host_x86_PMADDWD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xf5, dst_reg, src_reg); /*PMADDWD dst_reg, src_reg*/
}
void
host_x86_PMULHW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xe5, dst_reg, src_reg); /*PMULHW dst_reg, src_reg*/
}
void
host_x86_PMULLW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xd5, dst_reg, src_reg); /*PMULLW dst_reg, src_reg*/
}

void
host_x86_PEXTRW_REG_XREG(codeblock_t *block, int dst_reg, int src_reg, int word)
{
    sse_op_reg_reg_imm(block, 0x66, 0xc5, dst_reg, src_reg, word); /*PEXTRW dst_reg, src_reg, word*/
}
void
host_x86_PINSRW_XREG_REG(codeblock_t *block, int dst_reg, int src_reg, int word)
{
    sse_op_reg_reg_imm(block, 0x66, 0xc4, dst_reg, src_reg, word); /*PINSRW dst_reg, src_reg, word*/
}
void
host_x86_PMAXSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xee, dst_reg, src_reg); /*PMAXSW dst_reg, src_reg*/
}
void
host_x86_PMAXUB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xde, dst_reg, src_reg); /*PMAXUB dst_reg, src_reg*/
}
void
host_x86_PMINSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xea, dst_reg, src_reg); /*PMINSW dst_reg, src_reg*/
}
void
host_x86_PMINUB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xda, dst_reg, src_reg); /*PMINUB dst_reg, src_reg*/
}
void
host_x86_PMOVMSKB_REG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xd7, dst_reg, src_reg); /*PMOVMSKB dst_reg, src_reg*/
}
void
host_x86_PMULHUW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xe4, dst_reg, src_reg); /*PMULHUW dst_reg, src_reg*/
}

/*Shift by immediate - the ModRM reg field holds the opcode extension*/
void
host_x86_PSLLW_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    sse_op_reg_reg_imm(block, 0x66, 0x71, 6, dst_reg, shift); /*PSLLW dst_reg, imm*/
}
void
host_x86_PSLLD_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    sse_op_reg_reg_imm(block, 0x66, 0x72, 6, dst_reg, shift); /*PSLLD dst_reg, imm*/
}
void
host_x86_PSLLQ_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    sse_op_reg_reg_imm(block, 0x66, 0x73, 6, dst_reg, shift); /*PSLLQ dst_reg, imm*/
}
void
host_x86_PSRAW_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    sse_op_reg_reg_imm(block, 0x66, 0x71, 4, dst_reg, shift); /*PSRAW dst_reg, imm*/
}
void
host_x86_PSRAD_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    sse_op_reg_reg_imm(block, 0x66, 0x72, 4, dst_reg, shift); /*PSRAD dst_reg, imm*/
}
void
host_x86_PSRAQ_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    sse_op_reg_reg_imm(block, 0x66, 0x73, 4, dst_reg, shift); /*PSRAQ dst_reg, imm*/
}
void
host_x86_PSRLW_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    sse_op_reg_reg_imm(block, 0x66, 0x71, 2, dst_reg, shift); /*PSRLW dst_reg, imm*/
}
void
host_x86_PSRLD_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    sse_op_reg_reg_imm(block, 0x66, 0x72, 2, dst_reg, shift); /*PSRLD dst_reg, imm*/
}
void
host_x86_PSRLQ_XREG_IMM(codeblock_t *block, int dst_reg, int shift)
{
    sse_op_reg_reg_imm(block, 0x66, 0x73, 2, dst_reg, shift); /*PSRLQ dst_reg, imm*/
}

void
host_x86_PSUBB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xf8, dst_reg, src_reg); /*PSUBB dst_reg, src_reg*/
}
void
host_x86_PSUBW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xf9, dst_reg, src_reg); /*PSUBW dst_reg, src_reg*/
}
void
host_x86_PSUBD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xfa, dst_reg, src_reg); /*PSUBD dst_reg, src_reg*/
}
void
host_x86_PSUBQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xfb, dst_reg, src_reg); /*PSUBQ dst_reg, src_reg*/
}
void
host_x86_PSUBSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xe8, dst_reg, src_reg); /*PSUBSB dst_reg, src_reg*/
}
void
host_x86_PSUBSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xe9, dst_reg, src_reg); /*PSUBSW dst_reg, src_reg*/
}
void
host_x86_PSUBUSB_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xd8, dst_reg, src_reg); /*PSUBUSB dst_reg, src_reg*/
}
void
host_x86_PSUBUSW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xd9, dst_reg, src_reg); /*PSUBUSW dst_reg, src_reg*/
}

void
host_x86_PUNPCKHBW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x60, dst_reg, src_reg);           /*PUNPCKLBW dst_reg, src_reg*/
    sse_op_reg_reg_imm(block, 0x66, 0x70, dst_reg, dst_reg, 0xee); /*PSHUFD dst_reg, dst_reg, 0xee (move top 64-bits to low 64-bits)*/
}

void
host_x86_PUNPCKHBW_XREG_XREG_SSE(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x68, dst_reg, src_reg); /*PUNPCKHBW dst_reg, src_reg*/
}

void
host_x86_PUNPCKHWD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x61, dst_reg, src_reg);           /*PUNPCKLWD dst_reg, src_reg*/
    sse_op_reg_reg_imm(block, 0x66, 0x70, dst_reg, dst_reg, 0xee); /*PSHUFD dst_reg, dst_reg, 0xee (move top 64-bits to low 64-bits)*/
}

void
host_x86_PUNPCKHWD_XREG_XREG_SSE(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x69, dst_reg, src_reg); /*PUNPCKHWD dst_reg, src_reg*/
}

void
host_x86_PUNPCKHDQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x62, dst_reg, src_reg);           /*PUNPCKLDQ dst_reg, src_reg*/
    sse_op_reg_reg_imm(block, 0x66, 0x70, dst_reg, dst_reg, 0xee); /*PSHUFD dst_reg, dst_reg, 0xee (move top 64-bits to low 64-bits)*/
}

void
host_x86_PUNPCKHDQ_XREG_XREG_SSE(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x6a, dst_reg, src_reg); /*PUNPCKHDQ dst_reg, src_reg*/
}
void
host_x86_PUNPCKLBW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x60, dst_reg, src_reg); /*PUNPCKLBW dst_reg, src_reg*/
}
void
host_x86_PUNPCKLWD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x61, dst_reg, src_reg); /*PUNPCKLWD dst_reg, src_reg*/
}
void
host_x86_PUNPCKLDQ_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x62, dst_reg, src_reg); /*PUNPCKLDQ dst_reg, src_reg*/
}

void
host_x86_RCPPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x53, dst_reg, src_reg); /*RCPPS dst_reg, src_reg*/
}
void
host_x86_RCPSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x53, dst_reg, src_reg); /*RCPSS dst_reg, src_reg*/
}
void
host_x86_RSQRTPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x52, dst_reg, src_reg); /*RSQRTPS dst_reg, src_reg*/
}
void
host_x86_RSQRTSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x52, dst_reg, src_reg); /*RSQRTSS dst_reg, src_reg*/
}

void
host_x86_SQRTSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf2, 0x51, dst_reg, src_reg); /*SQRTSD dst_reg, src_reg*/
}
void
host_x86_SQRTPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x51, dst_reg, src_reg); /*SQRTPS dst_reg, src_reg*/
}
void
host_x86_SQRTSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x51, dst_reg, src_reg); /*SQRTSS dst_reg, src_reg*/
}

void
host_x86_SHUFPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg, int imm)
{
    sse_op_reg_reg_imm(block, 0, 0xc6, dst_reg, src_reg, imm); /*SHUFPS dst_reg, src_reg, imm*/
}

void
host_x86_PSHUFW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg, int imm)
{
    sse_op_reg_reg_imm(block, 0xf2, 0x70, dst_reg, src_reg, imm); /*PSHUFLW dst_reg, src_reg, imm*/
}

void
host_x86_PSADBW_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0xf6, dst_reg, src_reg); /*PSADBW dst_reg, src_reg*/
}

void
host_x86_SUBPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x5c, dst_reg, src_reg); /*SUBPS dst_reg, src_reg*/
}
void
host_x86_SUBPD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x5c, dst_reg, src_reg); /*SUBPD dst_reg, src_reg*/
}
void
host_x86_SUBSD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf2, 0x5c, dst_reg, src_reg); /*SUBSD dst_reg, src_reg*/
}
void
host_x86_SUBSS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0xf3, 0x5c, dst_reg, src_reg); /*SUBSS dst_reg, src_reg*/
}

void
host_x86_UNPCKLPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x14, dst_reg, src_reg); /*UNPCKLPS dst_reg, src_reg*/
}

void
host_x86_UNPCKLPD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x14, dst_reg, src_reg); /*UNPCKLPD dst_reg, src_reg*/
}

void
host_x86_UNPCKHPS_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0, 0x15, dst_reg, src_reg); /*UNPCKHPS dst_reg, src_reg*/
}

void
host_x86_UNPCKHPD_XREG_XREG(codeblock_t *block, int dst_reg, int src_reg)
{
    sse_op_reg_reg(block, 0x66, 0x15, dst_reg, src_reg); /*UNPCKHPD dst_reg, src_reg*/
}

#endif
