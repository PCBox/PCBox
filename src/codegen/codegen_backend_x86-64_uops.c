#if defined __amd64__ || defined _M_X64

#    include <stddef.h>
#    include <stdint.h>
#    include <limits.h>
#    include <string.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat_unused.h>

#    include "x86.h"
#    include "x86seg_common.h"
#    include "x86seg.h"
#    include "x87_sf.h"
#    include "x87.h"
#    include "x87_host.h"
#    include "386_common.h"
#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_x86-64_defs.h"
#    include "codegen_backend_x86-64_ops.h"
#    include "codegen_backend_x86-64_ops_helpers.h"
#    include "codegen_backend_x86-64_ops_sse.h"
#    include "codegen_ir_defs.h"
#    include "codegen_allocator.h"
#    include "codegen_backend_x86-64_ops_helpers.h"

#    define STACK_ARG0        (0)
#    define STACK_ARG1        (4)
#    define STACK_ARG2        (8)
#    define STACK_ARG3        (12)
/* Block-local scratch among the IR temporaries' slots (codegen_reg.c), so
   they move up with them by CODEGEN_TEMP_BASE, above the Win64 home area.
   The SSE recheck flag lies between the x87 TOP delta and temp0d, the 16-byte
   scratch between temp2d and temp0dq, and the MXCSR scratch after temp0dq. */
#    define STACK_SSE_RECHECK (36 + CODEGEN_TEMP_BASE)
#    define STACK_TEMP_DQ     (64 + CODEGEN_TEMP_BASE)
#    define STACK_TEMP_MXCSR  (96 + CODEGEN_TEMP_BASE)

_Static_assert(STACK_SSE_RECHECK == IREG_TOP_diff_stack_offset + 4, "the SSE recheck flag follows the x87 TOP delta");
#    ifdef _WIN64
_Static_assert(STACK_TEMP_MXCSR + 4 <= CODEGEN_XMM6_SAVE, "the scratch slots end below the XMM6-XMM15 saves");
#    else
_Static_assert(STACK_TEMP_MXCSR + 4 <= CODEGEN_SYSV_FRAME, "the scratch slots end inside the frame");
#    endif

/*AH-BH exist only for the first four registers; the allocator keeps
  registers accessed by byte out of R8-R15 (HOST_REG_FLAG_NO_BYTE).*/
static inline int
host_reg_get(int reg)
{
    if (IREG_GET_SIZE(reg) == IREG_SIZE_BH) {
        if (IREG_GET_REG(reg) & ~3)
            fatal("x86-64 codegen: high byte of host register %i\n", IREG_GET_REG(reg));
        return IREG_GET_REG(reg) | 4;
    }
    return IREG_GET_REG(reg) & 15;
}
#    define HOST_REG_GET(reg) host_reg_get(reg)

#    define REG_IS_B(size)    (size == IREG_SIZE_B || size == IREG_SIZE_BH)
#    define REG_IS_BH(size)   (size == IREG_SIZE_BH)
#    define REG_IS_D(size)    (size == IREG_SIZE_D)
#    define REG_IS_DQ(size)   (size == IREG_SIZE_DQ)

static inline int
REG_IS_L(int size)
{
    return size == IREG_SIZE_L;
}

static inline int
REG_IS_W(int size)
{
    return size == IREG_SIZE_W;
}

static inline int
REG_IS_Q(int size)
{
    return size == IREG_SIZE_Q;
}

static uint64_t
codegen_div_u64(uint32_t low, uint32_t high)
{
    return ((uint64_t) high << 32) | low;
}

static int64_t
codegen_div_s64(uint32_t low, uint32_t high)
{
    return (int64_t) codegen_div_u64(low, high);
}

static uint32_t
codegen_udiv_check_bits(uint32_t low, uint32_t high, uint32_t divisor, uint32_t bits)
{
    uint64_t max_quotient = (bits == 32) ? UINT32_MAX : ((UINT64_C(1) << bits) - 1);

    if (!divisor)
        return 1;

    return (codegen_div_u64(low, high) / divisor) > max_quotient;
}

/* Called inside an arithmetic uop: live XMM values must survive. The last
   scratch word supplies the quotient width and signedness; the first two
   receive results only after the overflow check has passed. */
static __attribute__((noinline, target("general-regs-only"))) uint32_t
codegen_divmod(uint32_t low, uint32_t high, uint32_t divisor, uint32_t *result)
{
    unsigned bits = result[2] & 0xff;

    if (!divisor)
        return 1;
    if (result[2] & 0x100) {
        int64_t dividend = ((int64_t) (int32_t) high * INT64_C(0x100000000)) + low;
        int32_t divs = (int32_t) divisor;
        int64_t limit = INT64_C(1) << (bits - 1);
        int64_t quotient;

        if (dividend == INT64_MIN && divs == -1)
            return 1;
        quotient = dividend / divs;
        if (quotient < -limit || quotient >= limit)
            return 1;
        result[0] = (uint32_t) quotient;
        result[1] = (uint32_t) (dividend % divs);
    } else {
        uint64_t dividend = ((uint64_t) high << 32) | low;
        uint64_t quotient = dividend / divisor;

        if (quotient > (UINT32_MAX >> (32 - bits)))
            return 1;
        result[0] = (uint32_t) quotient;
        result[1] = (uint32_t) (dividend % divisor);
    }
    return 0;
}

static uint32_t
codegen_idiv_check_bits(uint32_t low, uint32_t high, uint32_t divisor, uint32_t bits)
{
    int64_t dividend = codegen_div_s64(low, high);
    int32_t divs     = (int32_t) divisor;
    int64_t min_quotient;
    int64_t max_quotient;
    int64_t quotient;

    if (!divs)
        return 1;

    switch (bits) {
        case 8:
            min_quotient = -128;
            max_quotient = 127;
            break;
        case 16:
            min_quotient = -32768;
            max_quotient = 32767;
            break;
        default:
            min_quotient = INT32_MIN;
            max_quotient = INT32_MAX;
            break;
    }

    if (dividend == INT64_MIN && divs == -1)
        return 1;

    quotient = dividend / divs;
    return quotient < min_quotient || quotient > max_quotient;
}

static uint32_t
codegen_udiv_check8(uint32_t low, uint32_t high, uint32_t divisor)
{
    return codegen_udiv_check_bits(low, high, divisor, 8);
}

static uint32_t
codegen_udiv_check16(uint32_t low, uint32_t high, uint32_t divisor)
{
    return codegen_udiv_check_bits(low, high, divisor, 16);
}

static uint32_t
codegen_udiv_check32(uint32_t low, uint32_t high, uint32_t divisor)
{
    return codegen_udiv_check_bits(low, high, divisor, 32);
}

static uint32_t
codegen_idiv_check8(uint32_t low, uint32_t high, uint32_t divisor)
{
    return codegen_idiv_check_bits(low, high, divisor, 8);
}

static uint32_t
codegen_idiv_check16(uint32_t low, uint32_t high, uint32_t divisor)
{
    return codegen_idiv_check_bits(low, high, divisor, 16);
}

static uint32_t
codegen_idiv_check32(uint32_t low, uint32_t high, uint32_t divisor)
{
    return codegen_idiv_check_bits(low, high, divisor, 32);
}

static uint32_t
codegen_udiv_quot(uint32_t low, uint32_t high, uint32_t divisor)
{
    if (!divisor)
        return 0;
    return (uint32_t) (codegen_div_u64(low, high) / divisor);
}

static uint32_t
codegen_umod_rem(uint32_t low, uint32_t high, uint32_t divisor)
{
    if (!divisor)
        return 0;
    return (uint32_t) (codegen_div_u64(low, high) % divisor);
}

static uint32_t
codegen_idiv_quot(uint32_t low, uint32_t high, uint32_t divisor)
{
    int64_t dividend = codegen_div_s64(low, high);
    int32_t divs     = (int32_t) divisor;

    if (!divs || (dividend == INT64_MIN && divs == -1))
        return 0;
    return (uint32_t) (dividend / divs);
}

static uint32_t
codegen_imod_rem(uint32_t low, uint32_t high, uint32_t divisor)
{
    int64_t dividend = codegen_div_s64(low, high);
    int32_t divs     = (int32_t) divisor;

    if (!divs || (dividend == INT64_MIN && divs == -1))
        return 0;
    return (uint32_t) (dividend % divs);
}

static void *
codegen_udiv_check_helper(uint32_t bits)
{
    switch (bits) {
        case 8:
            return codegen_udiv_check8;
        case 16:
            return codegen_udiv_check16;
        case 32:
            return codegen_udiv_check32;
        default:
            fatal("codegen_udiv_check_helper - bits=%u\n", bits);
    }

    return NULL;
}

static void *
codegen_idiv_check_helper(uint32_t bits)
{
    switch (bits) {
        case 8:
            return codegen_idiv_check8;
        case 16:
            return codegen_idiv_check16;
        case 32:
            return codegen_idiv_check32;
        default:
            fatal("codegen_idiv_check_helper - bits=%u\n", bits);
    }

    return NULL;
}

static void
host_x86_MOV32_REG_REG_ANY(codeblock_t *block, int dst_reg, int src_reg)
{
    if (!((dst_reg | src_reg) & 8)) {
        host_x86_MOV32_REG_REG(block, dst_reg, src_reg);
        return;
    }

    codegen_alloc_bytes(block, 3);
    codegen_addbyte3(block,
                     0x40 | ((src_reg & 8) ? 0x04 : 0) | ((dst_reg & 8) ? 0x01 : 0),
                     0x89,
                     0xc0 | ((src_reg & 7) << 3) | (dst_reg & 7));
}

static void
host_x86_MOV32_REG_BASE_OFFSET_ANY(codeblock_t *block, int dst_reg, int base_reg, int offset)
{
    if (!((dst_reg | base_reg) & 8)) {
        host_x86_MOV32_REG_BASE_OFFSET(block, dst_reg, base_reg, offset);
        return;
    }

    if (offset < -128 || offset > 127)
        fatal("host_x86_MOV32_REG_BASE_OFFSET_ANY - offset %i\n", offset);

    if (base_reg == REG_RSP) {
        codegen_alloc_bytes(block, 5);
        codegen_addbyte(block, 0x40 | ((dst_reg & 8) ? 0x04 : 0));
        codegen_addbyte4(block, 0x8b, 0x40 | (base_reg & 7) | ((dst_reg & 7) << 3), 0x24, offset);
    } else {
        codegen_alloc_bytes(block, 4);
        codegen_addbyte4(block,
                         0x40 | ((dst_reg & 8) ? 0x04 : 0) | ((base_reg & 8) ? 0x01 : 0),
                         0x8b,
                         0x40 | (base_reg & 7) | ((dst_reg & 7) << 3),
                         offset);
    }
}

static void
codegen_DIV_HELPER_LOAD_ARG(codeblock_t *block, int dst_reg, int src_reg, int saved_eax_offset, int saved_edx_offset)
{
    if (src_reg == REG_EAX)
        host_x86_MOV32_REG_BASE_OFFSET_ANY(block, dst_reg, REG_RSP, saved_eax_offset);
    else if (src_reg == REG_EDX)
        host_x86_MOV32_REG_BASE_OFFSET_ANY(block, dst_reg, REG_RSP, saved_edx_offset);
    else
        host_x86_MOV32_REG_REG_ANY(block, dst_reg, src_reg);
}

static int
codegen_DIV_HELPER(codeblock_t *block, uop_t *uop, void *helper, int divmod)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_reg_c  = HOST_REG_GET(uop->src_reg_c_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);
    int src_size_c = IREG_GET_SIZE(uop->src_reg_c_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b) && REG_IS_L(src_size_c)) {
#    if _WIN64
        const int local_size       = 48;
        const int result_offset    = 32;
        const int saved_edx_offset = 48;
        const int saved_eax_offset = 56;
#    else
        const int local_size       = 16;
        const int result_offset    = 0;
        const int saved_edx_offset = 16;
        const int saved_eax_offset = 24;
#    endif

        if (divmod)
            host_x86_MOV32_BASE_OFFSET_IMM(block, REG_RSP, STACK_TEMP_DQ + 8, uop->imm_data);

        /* These helpers run inside an arithmetic uop, without a register
           barrier. Preserve the allocator's caller-saved integer registers. */
        host_x86_PUSH(block, REG_R10);
        host_x86_PUSH(block, REG_R11);
        host_x86_PUSH(block, REG_RAX);
        host_x86_PUSH(block, REG_RDX);
        host_x86_SUB64_REG_IMM(block, REG_RSP, local_size);
#    if _WIN64
        codegen_DIV_HELPER_LOAD_ARG(block, REG_ECX, src_reg_a, saved_eax_offset, saved_edx_offset);
        codegen_DIV_HELPER_LOAD_ARG(block, REG_EDX, src_reg_b, saved_eax_offset, saved_edx_offset);
        codegen_DIV_HELPER_LOAD_ARG(block, REG_R8, src_reg_c, saved_eax_offset, saved_edx_offset);
        if (divmod) {
            codegen_alloc_bytes(block, 8);
            codegen_addbyte4(block, 0x4c, 0x8d, 0x8c, 0x24); /* LEA R9,[RSP+scratch] */
            codegen_addlong(block, local_size + 32 + STACK_TEMP_DQ);
        }
#    else
        codegen_DIV_HELPER_LOAD_ARG(block, REG_EDI, src_reg_a, saved_eax_offset, saved_edx_offset);
        codegen_DIV_HELPER_LOAD_ARG(block, REG_ESI, src_reg_b, saved_eax_offset, saved_edx_offset);
        codegen_DIV_HELPER_LOAD_ARG(block, REG_EDX, src_reg_c, saved_eax_offset, saved_edx_offset);
        if (divmod) {
            codegen_alloc_bytes(block, 8);
            codegen_addbyte4(block, 0x48, 0x8d, 0x8c, 0x24); /* LEA RCX,[RSP+scratch] */
            codegen_addlong(block, local_size + 32 + STACK_TEMP_DQ);
        }
#    endif
        if (divmod) {
            /* The generic far-call emitter uses R9, our fourth Win64 argument. */
            host_x86_MOV64_REG_IMM(block, REG_RAX, (uintptr_t) helper);
            codegen_alloc_bytes(block, 2);
            codegen_addbyte2(block, 0xff, 0xd0); /* CALL RAX */
        } else
            host_x86_CALL(block, helper);
        host_x86_MOV32_BASE_OFFSET_REG(block, REG_RSP, result_offset, REG_EAX);
        host_x86_MOV32_REG_BASE_OFFSET(block, REG_EDX, REG_RSP, saved_edx_offset);
        host_x86_MOV32_REG_BASE_OFFSET(block, REG_EAX, REG_RSP, saved_eax_offset);
        host_x86_MOV64_REG_BASE_OFFSET(block, REG_R11, REG_RSP, local_size + 16);
        host_x86_MOV64_REG_BASE_OFFSET(block, REG_R10, REG_RSP, local_size + 24);
        host_x86_MOV32_REG_BASE_OFFSET(block, dest_reg, REG_RSP, result_offset);
        host_x86_ADD64_REG_IMM(block, REG_RSP, local_size + 32);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("DIV helper size mismatch: dest_size=%x, src_size_a=%x, src_size_b=%x, src_size_c=%x\n", dest_size, src_size_a, src_size_b, src_size_c);
#    endif
    return 0;
}

static int
codegen_DIVMOD(codeblock_t *block, uop_t *uop)
{
    return codegen_DIV_HELPER(block, uop, codegen_divmod, 1);
}

static int
codegen_DIV_RESULT(codeblock_t *block, uop_t *uop)
{
    host_x86_MOV32_REG_BASE_OFFSET(block, HOST_REG_GET(uop->dest_reg_a_real), REG_RSP,
                                  STACK_TEMP_DQ + 4 * uop->imm_data);
    return 0;
}

static int
codegen_ADD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    /*dest == src_b: moving src_a into dest first would clobber src_b; the
      operation commutes, so take the sources the other way round*/
    if ((dest_reg == src_reg_b) && (dest_reg != src_reg_a)) {
        int t     = src_reg_a;
        src_reg_a = src_reg_b;
        src_reg_b = t;
    }

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_LEA_REG_REG(block, dest_reg, src_reg_a, src_reg_b);
        else
            host_x86_ADD32_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
        host_x86_ADD16_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
        host_x86_ADD8_REG_REG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_ADD_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (dest_reg != src_reg)
            host_x86_LEA_REG_IMM(block, dest_reg, src_reg, uop->imm_data);
        else
            host_x86_ADD32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_ADD16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_ADD8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ADD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_ADD_LSHIFT(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b = HOST_REG_GET(uop->src_reg_b_real);

    if (!uop->imm_data) {
        if (dest_reg == src_reg_a)
            host_x86_ADD32_REG_REG(block, dest_reg, src_reg_b);
        else
            host_x86_LEA_REG_REG(block, dest_reg, src_reg_a, src_reg_b);
    } else if (uop->imm_data < 4)
        host_x86_LEA_REG_REG_SHIFT(block, dest_reg, src_reg_a, src_reg_b, uop->imm_data);
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_ADD_LSHIFT - shift out of range %i\n", uop->imm_data);
#    endif
    return 0;
}

static int
codegen_IMUL_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_x86_IMUL32_REG_IMM(block, dest_reg, src_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_x86_IMUL16_REG_IMM(block, dest_reg, src_reg, (int16_t)uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("IMUL_IMM size mismatch: dest_size=%x, src_size=%x\n", dest_size, src_size);
#    endif
    return 0;
}

static int
codegen_UDIV_CHECK(codeblock_t *block, uop_t *uop)
{
    return codegen_DIV_HELPER(block, uop, codegen_udiv_check_helper(uop->imm_data), 0);
}

static int
codegen_IDIV_CHECK(codeblock_t *block, uop_t *uop)
{
    return codegen_DIV_HELPER(block, uop, codegen_idiv_check_helper(uop->imm_data), 0);
}

static int
codegen_UDIV(codeblock_t *block, uop_t *uop)
{
    return codegen_DIV_HELPER(block, uop, codegen_udiv_quot, 0);
}

static int
codegen_UMOD(codeblock_t *block, uop_t *uop)
{
    return codegen_DIV_HELPER(block, uop, codegen_umod_rem, 0);
}

static int
codegen_IDIV(codeblock_t *block, uop_t *uop)
{
    return codegen_DIV_HELPER(block, uop, codegen_idiv_quot, 0);
}

static int
codegen_IMOD(codeblock_t *block, uop_t *uop)
{
    return codegen_DIV_HELPER(block, uop, codegen_imod_rem, 0);
}

static int
codegen_AND(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    /*dest == src_b: moving src_a into dest first would clobber src_b; the
      operation commutes, so take the sources the other way round*/
    if ((dest_reg == src_reg_b) && (dest_reg != src_reg_a)) {
        int t     = src_reg_a;
        src_reg_a = src_reg_b;
        src_reg_b = t;
    }

    if (((REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) ||
        (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PAND_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
        host_x86_AND32_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
        host_x86_AND16_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
        host_x86_AND8_REG_REG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("AND %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_AND_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_AND32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_AND16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_AND8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("AND_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_ANDN(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
#if 0
    int src_reg_a = HOST_REG_GET(uop->src_reg_a_real);
#endif
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (((REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) ||
        (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PANDN_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ANDN %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

/*An exit that adds add to cycles first (uop->imm_data2), with no entry for it
  (codegen_exit_dest): the add in its own arm, which the path that goes on
  branches over. Every exit is an order barrier at least, so cycles is in
  memory. EXIT_ARM_SIZE keeps the branch over the arm and the arm in one
  code chunk: room for the branch, the longest add and the longest jump.*/
#    define EXIT_ARM_SIZE 32

static void
codegen_exit_add(codeblock_t *block, int add)
{
    host_x86_ADD32_ABS_IMM(block, &cpu_state._cycles, add);
}

static void
codegen_exit_arm(codeblock_t *block, int add, void *route)
{
    codegen_exit_add(block, add);
    host_x86_JMP(block, route);
}

/*A lazy site's exit (codegen_lazy_site): its branches go to a stub after the
  block that makes the site's stores, then leaves as the exit would.
  Identical stubs are emitted once.*/
#    define LAZY_STUB_PATCHES 2
typedef struct lazy_stub_t {
    uint32_t                  *patch[LAZY_STUB_PATCHES];
    int                        nr_patches;
    const codegen_lazy_list_t *list;
    void                      *route;
    int                        add;
    void                      *addr;
} lazy_stub_t;

static lazy_stub_t lazy_stubs[CODEGEN_LAZY_SITES_MAX];
static int         nr_lazy_stubs;

static void
codegen_lazy_exit(int add, uint32_t *patch, void *route)
{
    lazy_stub_t *stub;

    /*One exit per lazy uop, and no more lazy uops with stores than lists*/
    if (nr_lazy_stubs == CODEGEN_LAZY_SITES_MAX)
        fatal("codegen_lazy_exit - out of stubs\n");
    stub             = &lazy_stubs[nr_lazy_stubs++];
    stub->patch[0]   = patch;
    stub->nr_patches = 1;
    stub->list       = codegen_lazy_site;
    stub->route      = route;
    stub->add        = add;
}

/*Another branch, its rel32 at patch, to the exit codegen_lazy_exit just
  recorded*/
static void
codegen_lazy_exit_also(uint32_t *patch)
{
    lazy_stub_t *stub = &lazy_stubs[nr_lazy_stubs - 1];

    if (stub->nr_patches == LAZY_STUB_PATCHES)
        fatal("codegen_lazy_exit_also - too many branches\n");
    stub->patch[stub->nr_patches++] = patch;
}

static int
lazy_stubs_match(const lazy_stub_t *a, const lazy_stub_t *b)
{
    if ((a->route != b->route) || (a->add != b->add) || (a->list->nr != b->list->nr))
        return 0;
    for (int i = 0; i < a->list->nr; i++) {
        const codegen_lazy_store_t *sa = &a->list->stores[i];
        const codegen_lazy_store_t *sb = &b->list->stores[i];

        if ((sa->ireg != sb->ireg) || (sa->host_reg != sb->host_reg) || (sa->imm != sb->imm))
            return 0;
    }
    return 1;
}

void
codegen_lazy_stubs_emit(codeblock_t *block)
{
    for (int i = 0; i < nr_lazy_stubs; i++) {
        lazy_stub_t *stub = &lazy_stubs[i];
        int          j;

        for (j = 0; j < i; j++) {
            if (lazy_stubs_match(stub, &lazy_stubs[j]))
                break;
        }
        if (j < i)
            stub->addr = lazy_stubs[j].addr;
        else {
            void *dest = codegen_exit_dest(stub->route, stub->add);

            codegen_alloc(block, 16);
            stub->addr = &block_write_data[block_pos];
            codegen_reg_lazy_stores(block, stub->list);
            if (dest)
                host_x86_JMP(block, dest);
            else {
                codegen_alloc(block, EXIT_ARM_SIZE);
                codegen_exit_arm(block, stub->add, stub->route);
            }
        }
        for (j = 0; j < stub->nr_patches; j++)
            *stub->patch[j] = (uintptr_t) stub->addr - ((uintptr_t) stub->patch[j] + 4);
    }
    nr_lazy_stubs = 0;
}

/*A conditional branch to route from branch (host_x86_JNZ_long and the like),
  charging what add adds on the way. Without an entry the branch with the
  opposite condition, inverse, jumps over the arm. Returns where the exit
  starts, for another branch to the same exit, or NULL for a lazy site's
  exit, which takes the other branch with codegen_lazy_exit_also.*/
static void *
codegen_exit_Jcc_add(codeblock_t *block, int add, void *route, uint32_t *(*branch)(codeblock_t *block), uint32_t *(*inverse)(codeblock_t *block))
{
    void     *dest = codegen_exit_dest(route, add);
    uint32_t *jump_p;

    if (codegen_lazy_site && codegen_lazy_site->nr) {
        codegen_lazy_exit(add, branch(block), route);
        return NULL;
    }
    if (dest) {
        jump_p  = branch(block);
        *jump_p = (uintptr_t) dest - ((uintptr_t) jump_p + 4);
        return dest;
    }
    codegen_alloc(block, EXIT_ARM_SIZE);
    jump_p = inverse(block);
    dest   = &block_write_data[block_pos];
    codegen_exit_arm(block, add, route);
    *jump_p = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) jump_p + 4);

    return dest;
}

/*The same, charging what uop->imm_data2 adds*/
static void *
codegen_exit_Jcc(codeblock_t *block, uop_t *uop, void *route, uint32_t *(*branch)(codeblock_t *block), uint32_t *(*inverse)(codeblock_t *block))
{
    return codegen_exit_Jcc_add(block, uop->imm_data2, route, branch, inverse);
}

static int
codegen_CALL_FUNC(codeblock_t *block, uop_t *uop)
{
    host_x86_CALL(block, uop->p);

    return 0;
}

/*An order barrier leaves the cached registers loaded, and EAX and EDX are
  not preserved by the callee, so they are saved around the call. Two pushes
  keep RSP 16-byte aligned; Windows also needs the 32-byte home area below
  them, so the callee does not overwrite the saved values.*/
static int
codegen_CALL_FUNC_ORDERED(codeblock_t *block, uop_t *uop)
{
    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_CALL(block, uop->p);
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);

    return 0;
}

/*As codegen_CALL_FUNC_ORDERED. The result goes through ESI, which is not
  allocated, so that EAX and EDX can be put back before dest_reg is set; if
  dest_reg is one of them, its old contents were written back before the
  call and are not needed.*/
static int
codegen_CALL_FUNC_RESULT_ORDERED(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);

    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_CALL(block, uop->p);
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_MOV32_REG_REG(block, REG_ESI, REG_EAX);
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);
    host_x86_MOV32_REG_REG(block, dest_reg, REG_ESI);

    return 0;
}

static int
codegen_CALL_FUNC_RESULT(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);

#    ifdef RECOMPILER_DEBUG
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    if (!REG_IS_L(dest_size))
        fatal("CALL_FUNC_RESULT %02x\n", uop->dest_reg_a_real);
#    endif
    host_x86_CALL(block, uop->p);
    host_x86_MOV32_REG_REG(block, dest_reg, REG_EAX);

    return 0;
}

static int
codegen_CALL_INSTRUCTION_FUNC(codeblock_t *block, uop_t *uop)
{
#    if _WIN64
    host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data);
#    else
    host_x86_MOV32_REG_IMM(block, REG_EDI, uop->imm_data);
#    endif
    host_x86_CALL(block, uop->p);
    host_x86_TEST32_REG(block, REG_EAX, REG_EAX);
    codegen_exit_Jcc(block, uop, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);

    return 0;
}

/*As codegen_CALL_INSTRUCTION_FUNC, with EAX and EDX kept on the stack
  around the call as codegen_CALL_FUNC_ORDERED keeps them; the result is
  tested in ESI once they are back*/
static int
codegen_CALL_INSTRUCTION_FUNC_ORDERED(codeblock_t *block, uop_t *uop)
{
    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x20);
    host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data);
#    else
    host_x86_MOV32_REG_IMM(block, REG_EDI, uop->imm_data);
#    endif
    host_x86_CALL(block, uop->p);
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_MOV32_REG_REG(block, REG_ESI, REG_EAX);
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);
    host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
    codegen_exit_Jcc(block, uop, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);

    return 0;
}

/*The REP routine keeps RBX, RBP and R12-R15 but not the cached EAX and EDX,
  which are saved around it as for an ordered call. Its result goes through
  ESI, which is not allocated, so the saved registers can be put back first.*/
static int
codegen_CALL_REP_STRING(codeblock_t *block, uop_t *uop)
{
    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x20);
    host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data);
#    else
    host_x86_MOV32_REG_IMM(block, REG_EDI, uop->imm_data);
#    endif
    host_x86_CALL(block, uop->p);
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x20);
#    endif
    host_x86_MOV32_REG_REG(block, REG_ESI, REG_EAX);
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);
    host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
    codegen_exit_Jcc(block, uop, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);

    return 0;
}

/*Leave the block for an SMI the instruction raised, with pc set after it,
  taken from the immediate for SMI_EXIT_IMM. The branch over the exit is
  the only cost while none is pending.*/
static int
codegen_SMI_EXIT_IMM(codeblock_t *block, uop_t *uop)
{
    void     *dest = codegen_exit_dest(codegen_exit_rout, uop->imm_data2);
    uint32_t *branch_offset;

    host_x86_MOVZX_REG_ABS_32_8(block, REG_ECX, &cpu_state._smi_line);
    host_x86_TEST32_REG(block, REG_ECX, REG_ECX);
    branch_offset = host_x86_JZ_long(block);
    if (uop->type & UOP_TYPE_PARAMS_IMM)
        host_x86_MOV32_ABS_IMM(block, &cpu_state.pc, uop->imm_data);
    /*Charging what the block's end would (uop->imm_data2)*/
    if (dest)
        host_x86_JMP(block, dest);
    else
        codegen_exit_arm(block, uop->imm_data2, codegen_exit_rout);
    *branch_offset = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 4;

    return 0;
}

static int
codegen_CMP_IMM_JZ(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_CMP32_REG_IMM(block, src_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_IMM_JZ %02x\n", uop->src_reg_a_real);
#    endif
    codegen_exit_Jcc(block, uop, uop->p, host_x86_JZ_long, host_x86_JNZ_long);

    return 0;
}

static int
codegen_CMP_IMM_JNZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_CMP32_REG_IMM(block, src_reg, uop->imm_data);
    } else if (REG_IS_W(src_size)) {
        host_x86_CMP16_REG_IMM(block, src_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_IMM_JNZ_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNZ_long(block);

    return 0;
}
static int
codegen_CMP_IMM_JZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_CMP32_REG_IMM(block, src_reg, uop->imm_data);
    } else if (REG_IS_W(src_size)) {
        host_x86_CMP16_REG_IMM(block, src_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_IMM_JZ_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JZ_long(block);

    return 0;
}
static int
codegen_TEST_IMM_JZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    /*A word register's upper half is not part of its value: the immediate
      tests the low 16 bits only*/
    if (REG_IS_L(src_size))
        host_x86_TEST32_REG_IMM(block, src_reg, uop->imm_data);
    else if (REG_IS_W(src_size))
        host_x86_TEST32_REG_IMM(block, src_reg, uop->imm_data & 0xffff);
    else
        fatal("TEST_IMM_JZ_DEST %02x\n", uop->src_reg_a_real);

    uop->p = host_x86_JZ_long(block);

    return 0;
}

static int
codegen_CMP_JB(codeblock_t *block, uop_t *uop)
{
    int       src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int       src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int       src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int       src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JB %02x\n", uop->src_reg_a_real);
#    endif
    codegen_exit_Jcc(block, uop, uop->p, host_x86_JB_long, host_x86_JNB_long);

    return 0;
}
static int
codegen_CMP_JNBE(codeblock_t *block, uop_t *uop)
{
    int       src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int       src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int       src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int       src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNBE %02x\n", uop->src_reg_a_real);
#    endif
    codegen_exit_Jcc(block, uop, uop->p, host_x86_JNBE_long, host_x86_JBE_long);

    return 0;
}

static int
codegen_CMP_JNWITHIN(codeblock_t *block, uop_t *uop)
{
    int       src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int       lo_reg   = HOST_REG_GET(uop->src_reg_b_real);
    int       hi_reg   = HOST_REG_GET(uop->src_reg_c_real);
    int       src_size = IREG_GET_SIZE(uop->src_reg_a_real);
    int       lo_size  = IREG_GET_SIZE(uop->src_reg_b_real);
    int       hi_size  = IREG_GET_SIZE(uop->src_reg_c_real);
    uint32_t *jump_p;
    void     *exit_p;

    if (REG_IS_L(src_size) && REG_IS_L(lo_size) && REG_IS_L(hi_size)) {
        host_x86_CMP32_REG_REG(block, src_reg, lo_reg);
        jump_p = host_x86_JB_long(block);
        if (uop->imm_data) {
            host_x86_LEA_REG_IMM(block, REG_ECX, src_reg, uop->imm_data);
            host_x86_CMP32_REG_REG(block, REG_ECX, hi_reg);
        } else
            host_x86_CMP32_REG_REG(block, src_reg, hi_reg);
        /*Both branches leave through one exit, the second one's arm when it
          has one*/
        exit_p = codegen_exit_Jcc(block, uop, uop->p, host_x86_JNBE_long, host_x86_JBE_long);
        if (exit_p)
            *jump_p = (uintptr_t) exit_p - ((uintptr_t) jump_p + 4);
        else
            codegen_lazy_exit_also(jump_p);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNWITHIN %02x %02x %02x\n", uop->src_reg_a_real, uop->src_reg_b_real, uop->src_reg_c_real);
#    endif

    return 0;
}

/*src_reg + imm carries out of 32 bits exactly when src_reg > ~imm, unsigned,
  which holds for imm 0 as well*/
static int
codegen_CMN_IMM_JC(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_CMP32_REG_IMM(block, src_reg, ~uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMN_IMM_JC %02x\n", uop->src_reg_a_real);
#    endif
    codegen_exit_Jcc(block, uop, uop->p, host_x86_JNBE_long, host_x86_JBE_long);

    return 0;
}

/*The low limit test, then the carry test of CMN_IMM_JC*/
static int
codegen_CMP_JNWITHIN_TOP(codeblock_t *block, uop_t *uop)
{
    int       src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int       lo_reg   = HOST_REG_GET(uop->src_reg_b_real);
    int       src_size = IREG_GET_SIZE(uop->src_reg_a_real);
    int       lo_size  = IREG_GET_SIZE(uop->src_reg_b_real);
    uint32_t *jump_p;
    void     *exit_p;

    if (REG_IS_L(src_size) && REG_IS_L(lo_size)) {
        host_x86_CMP32_REG_REG(block, src_reg, lo_reg);
        jump_p = host_x86_JB_long(block);
        host_x86_CMP32_REG_IMM(block, src_reg, ~uop->imm_data);
        /*Both branches leave through one exit, the second one's arm when it
          has one*/
        exit_p  = codegen_exit_Jcc(block, uop, uop->p, host_x86_JNBE_long, host_x86_JBE_long);
        *jump_p = (uintptr_t) exit_p - ((uintptr_t) jump_p + 4);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNWITHIN_TOP %02x %02x\n", uop->src_reg_a_real, uop->src_reg_b_real);
#    endif

    return 0;
}

/*COMISD sets CF, ZF and PF for an unordered pair, so JBE and JNBE take a NaN
  as outside*/
static int
codegen_FCMP_JWITHIN_DEST(codeblock_t *block, uop_t *uop)
{
    int       src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int       lo_reg   = HOST_REG_GET(uop->src_reg_b_real);
    int       hi_reg   = HOST_REG_GET(uop->src_reg_c_real);
    int       src_size = IREG_GET_SIZE(uop->src_reg_a_real);
    int       lo_size  = IREG_GET_SIZE(uop->src_reg_b_real);
    int       hi_size  = IREG_GET_SIZE(uop->src_reg_c_real);
    uint32_t *outside;

    if (REG_IS_D(src_size) && REG_IS_D(lo_size) && REG_IS_D(hi_size)) {
        host_x86_COMISD_XREG_XREG(block, hi_reg, src_reg);
        outside = host_x86_JBE_long(block);
        host_x86_COMISD_XREG_XREG(block, src_reg, lo_reg);
        uop->p   = host_x86_JNBE_long(block);
        *outside = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) outside) - 4;
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("FCMP_JWITHIN_DEST %02x %02x %02x\n", uop->src_reg_a_real, uop->src_reg_b_real, uop->src_reg_c_real);
#    endif

    return 0;
}

/*The exponent window of x87_divide_fast, 553 <= e <= 1493 for both operands,
  a memory operand of the single kind held to the single's normal range
  (e >= 897: a denormal single raises DE, which the software divide does);
  a quotient with a zero fraction, a power of two, goes there as well*/
static int
codegen_FDIV_JSLOW_DEST(codeblock_t *block, uop_t *uop)
{
    int       num_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int       den_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int       q_reg    = HOST_REG_GET(uop->src_reg_c_real);
    int       num_size = IREG_GET_SIZE(uop->src_reg_a_real);
    int       den_size = IREG_GET_SIZE(uop->src_reg_b_real);
    int       q_size   = IREG_GET_SIZE(uop->src_reg_c_real);
    int       kind     = (uop->imm_data >> 11) & 3;
    int       lo_num   = ((uop->imm_data & 0x200) && (kind == 1)) ? 897 : 553;
    int       lo_den   = ((uop->imm_data & 0x400) && (kind == 1)) ? 897 : 553;
    uint32_t *fail_num;
    uint32_t *fail_den;
    uint32_t *fail_q;
    uint32_t *ok;

    if (!(REG_IS_D(num_size) && REG_IS_D(den_size) && REG_IS_D(q_size)))
        fatal("FDIV_JSLOW_DEST %02x %02x %02x\n", uop->src_reg_a_real, uop->src_reg_b_real, uop->src_reg_c_real);

    host_x86_MOVQ_REG_XREG(block, REG_RCX, num_reg);
    host_x86_SHR64_IMM(block, REG_RCX, 52);
    host_x86_AND32_REG_IMM(block, REG_ECX, 0x7ff);
    host_x86_SUB32_REG_IMM(block, REG_ECX, lo_num);
    host_x86_CMP32_REG_IMM(block, REG_ECX, 1493 - lo_num);
    fail_num = host_x86_JNBE_long(block);
    host_x86_MOVQ_REG_XREG(block, REG_RCX, den_reg);
    host_x86_SHR64_IMM(block, REG_RCX, 52);
    host_x86_AND32_REG_IMM(block, REG_ECX, 0x7ff);
    host_x86_SUB32_REG_IMM(block, REG_ECX, lo_den);
    host_x86_CMP32_REG_IMM(block, REG_ECX, 1493 - lo_den);
    fail_den = host_x86_JNBE_long(block);
    host_x86_MOVQ_REG_XREG(block, REG_RCX, q_reg);
    host_x86_MOV64_REG_IMM(block, REG_RSI, 0x000fffffffffffffull);
    host_x86_TEST64_REG(block, REG_RSI, REG_RCX);
    fail_q = host_x86_JZ_long(block);
    ok     = host_x86_JMP_long(block);

    *fail_num = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_num + 4);
    *fail_den = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_den + 4);
    *fail_q   = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_q + 4);
    uop->p    = host_x86_JMP_long(block);
    *ok       = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) ok + 4);

    return 0;
}

/*The status bits of a divide finished from the host quotient
  (x87_divide_fast): r = num - q * den is exact inside the window, PE is
  r != 0 and C1 a rounding up in magnitude, r and num of opposite signs. r is
  Dekker's product, the operations of x87_divide_fast's path without FMA in
  its order, worked in XMM_TEMP and four allocated XMM registers the uop does
  not read: all 128 bits of each wait below the frame and are put back before
  the first branch, RSP with them. Not the low halves only: a guest XMM
  register is a 128-bit value, and Win64 keeps XMM6 and XMM7 allocated across
  the order barrier, so one of them can hold it here. At 24 bits the quotient rounds
  once more, to single: a single-precision midpoint with r != 0, or a single
  that is not normal, is the software divide's; when the single differs from
  the quotient the flags are that rounding's, C1 for the larger magnitude. PE
  raised while unmasked is the software divide's too, which raises it as the
  interpreter does.*/
static int
codegen_FDIV_FLAGS_JSLOW_DEST(codeblock_t *block, uop_t *uop)
{
    int       dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int       q_reg     = HOST_REG_GET(uop->src_reg_a_real);
    int       num_reg   = HOST_REG_GET(uop->src_reg_b_real);
    int       den_reg   = HOST_REG_GET(uop->src_reg_c_real);
    int       dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int       q_size    = IREG_GET_SIZE(uop->src_reg_a_real);
    int       num_size  = IREG_GET_SIZE(uop->src_reg_b_real);
    int       den_size  = IREG_GET_SIZE(uop->src_reg_c_real);
    int       pc24      = uop->imm_data & 0x2000;
    int       w[5];
    int       nr_w      = 1;
    uint32_t *fail_mid  = NULL;
    uint32_t *fail_sub  = NULL;
    uint32_t *fail_pe;
    uint32_t *same;
    uint32_t *below;
    uint32_t *ok;

    if (!(REG_IS_L(dest_size) && REG_IS_D(q_size) && REG_IS_D(num_size) && REG_IS_D(den_size)))
        fatal("FDIV_FLAGS_JSLOW_DEST %02x %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real, uop->src_reg_c_real);

    w[0] = REG_XMM_TEMP;
    for (int c = REG_XMM1; (c <= REG_XMM7) && (nr_w < 5); c++) {
        if ((c != q_reg) && (c != num_reg) && (c != den_reg))
            w[nr_w++] = c;
    }
    /*Nothing below addresses the stack until the restore*/
    host_x86_SUB64_REG_IMM(block, REG_RSP, 64);
    for (int c = 1; c < 5; c++)
        host_x86_MOVDQU_BASE_OFFSET_XREG(block, REG_RSP, (c - 1) * 16, w[c]);

    /* split = 2^27 + 1 */
    host_x86_MOV64_REG_IMM(block, REG_RCX, 0x41a0000002000000ull);
    host_x86_MOVQ_XREG_REG(block, w[0], REG_RCX);
    host_x86_MOVQ_XREG_XREG(block, w[1], q_reg);
    host_x86_MULSD_XREG_XREG(block, w[1], w[0]);
    host_x86_MOVQ_XREG_XREG(block, w[2], w[1]);
    host_x86_SUBSD_XREG_XREG(block, w[2], q_reg);
    host_x86_SUBSD_XREG_XREG(block, w[1], w[2]); /* qh */
    host_x86_MOVQ_XREG_XREG(block, w[2], q_reg);
    host_x86_SUBSD_XREG_XREG(block, w[2], w[1]); /* ql */
    host_x86_MOVQ_XREG_XREG(block, w[3], den_reg);
    host_x86_MULSD_XREG_XREG(block, w[3], w[0]);
    host_x86_MOVQ_XREG_XREG(block, w[4], w[3]);
    host_x86_SUBSD_XREG_XREG(block, w[4], den_reg);
    host_x86_SUBSD_XREG_XREG(block, w[3], w[4]); /* bh */
    host_x86_MOVQ_XREG_XREG(block, w[0], den_reg);
    host_x86_SUBSD_XREG_XREG(block, w[0], w[3]); /* bl */
    host_x86_MOVQ_XREG_XREG(block, w[4], q_reg);
    host_x86_MULSD_XREG_XREG(block, w[4], den_reg); /* p */
    host_x86_MOVQ_REG_XREG(block, REG_RSI, w[0]);
    host_x86_MOVQ_XREG_XREG(block, w[0], w[1]);
    host_x86_MULSD_XREG_XREG(block, w[0], w[3]);
    host_x86_SUBSD_XREG_XREG(block, w[0], w[4]); /* qh * bh - p */
    host_x86_MOVQ_REG_XREG(block, REG_RDI, w[4]);
    host_x86_MOVQ_XREG_REG(block, w[4], REG_RSI);
    host_x86_MULSD_XREG_XREG(block, w[1], w[4]);
    host_x86_ADDSD_XREG_XREG(block, w[0], w[1]); /* + qh * bl */
    host_x86_MULSD_XREG_XREG(block, w[3], w[2]);
    host_x86_ADDSD_XREG_XREG(block, w[0], w[3]); /* + ql * bh */
    host_x86_MULSD_XREG_XREG(block, w[2], w[4]);
    host_x86_ADDSD_XREG_XREG(block, w[0], w[2]); /* + ql * bl = e */
    host_x86_MOVQ_XREG_REG(block, w[3], REG_RDI);
    host_x86_MOVQ_XREG_XREG(block, w[1], num_reg);
    host_x86_SUBSD_XREG_XREG(block, w[1], w[3]);
    host_x86_SUBSD_XREG_XREG(block, w[1], w[0]); /* r = (num - p) - e */
    host_x86_MOVQ_REG_XREG(block, REG_RCX, w[1]);
    for (int c = 1; c < 5; c++)
        host_x86_MOVDQU_XREG_BASE_OFFSET(block, w[c], REG_RSP, (c - 1) * 16);
    host_x86_ADD64_REG_IMM(block, REG_RSP, 64);

    host_x86_MOVQ_REG_XREG(block, REG_RSI, num_reg);
    host_x86_SHR64_IMM(block, REG_RSI, 63);
    host_x86_MOV64_REG_REG(block, REG_RDI, REG_RCX);
    host_x86_SHR64_IMM(block, REG_RDI, 63);
    host_x86_XOR32_REG_REG(block, REG_EDI, REG_ESI);
    host_x86_SHL32_IMM(block, REG_EDI, 9);
    host_x86_OR32_REG_IMM(block, REG_EDI, FPU_SW_Precision);
    if (pc24) {
        /* zero below the single's midpoint bit: fraction bits 28..0 = 1 0...0 */
        host_x86_MOVD_REG_XREG(block, REG_R8, q_reg);
        host_x86_AND32_REG_IMM(block, REG_R8, 0x1fffffff);
        host_x86_XOR32_REG_IMM(block, REG_R8, 0x10000000);
        host_x86_MOV32_REG_IMM(block, REG_R9, 1);
    }
    host_x86_MOV64_REG_IMM(block, REG_RSI, 0x7fffffffffffffffull);
    host_x86_MOV32_REG_IMM(block, dest_reg, 0);
    host_x86_TEST64_REG(block, REG_RSI, REG_RCX);
    host_x86_CMOVNZ32_REG_REG(block, dest_reg, REG_EDI);
    if (pc24) {
        /* with r == 0 the midpoint is no concern: R9 stays 1 */
        host_x86_CMOVNZ32_REG_REG(block, REG_R9, REG_R8);
        host_x86_TEST32_REG(block, REG_R9, REG_R9);
        fail_mid = host_x86_JZ_long(block);
        host_x86_CVTSD2SS_XREG_XREG(block, REG_XMM_TEMP, q_reg);
        host_x86_MOVD_REG_XREG(block, REG_ECX, REG_XMM_TEMP);
        host_x86_SHR32_IMM(block, REG_ECX, 23);
        host_x86_AND32_REG_IMM(block, REG_ECX, 0xff);
        host_x86_SUB32_REG_IMM(block, REG_ECX, 1);
        host_x86_CMP32_REG_IMM(block, REG_ECX, 253);
        fail_sub = host_x86_JNBE_long(block);
        host_x86_CVTSS2SD_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
        host_x86_MOVQ_REG_XREG(block, REG_RSI, q_reg);
        host_x86_SHR64_IMM(block, REG_RSI, 63);
        host_x86_COMISD_XREG_XREG(block, REG_XMM_TEMP, q_reg);
        same = host_x86_JZ_long(block);
        host_x86_MOV32_REG_IMM(block, dest_reg, FPU_SW_Precision);
        below = host_x86_JB_long(block);
        host_x86_XOR32_REG_IMM(block, REG_ESI, 1);
        *below = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) below + 4);
        /* C1: the single above a positive quotient or below a negative one */
        host_x86_SHL32_IMM(block, REG_ESI, 9);
        host_x86_OR32_REG_REG(block, dest_reg, REG_ESI);
        *same = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) same + 4);
    }
    host_x86_MOVZX_REG_ABS_32_16(block, REG_ECX, &cpu_state.npxc);
    host_x86_XOR32_REG_IMM(block, REG_ECX, FPU_SW_Precision);
    host_x86_AND32_REG_REG(block, REG_ECX, dest_reg);
    host_x86_AND32_REG_IMM(block, REG_ECX, FPU_SW_Precision);
    fail_pe = host_x86_JNZ_long(block);
    ok      = host_x86_JMP_long(block);

    if (fail_mid)
        *fail_mid = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_mid + 4);
    if (fail_sub)
        *fail_sub = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_sub + 4);
    *fail_pe = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) fail_pe + 4);
    uop->p   = host_x86_JMP_long(block);
    *ok      = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) ok + 4);

    return 0;
}

static int
codegen_CMP_JNB_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNB_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNB_long(block);

    return 0;
}
static int
codegen_CMP_JNBE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNBE_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNBE_long(block);

    return 0;
}
static int
codegen_CMP_JNL_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNL_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNL_long(block);

    return 0;
}
static int
codegen_CMP_JNLE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNLE_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNLE_long(block);

    return 0;
}
static int
codegen_CMP_JNO_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNO_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNO_long(block);

    return 0;
}
static int
codegen_CMP_JNZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JNZ_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNZ_long(block);

    return 0;
}
static int
codegen_CMP_JB_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JB_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JB_long(block);

    return 0;
}
static int
codegen_CMP_JBE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JBE_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JBE_long(block);

    return 0;
}
static int
codegen_CMP_JL_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JL_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JL_long(block);

    return 0;
}
static int
codegen_CMP_JLE_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JLE_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JLE_long(block);

    return 0;
}
static int
codegen_CMP_JO_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JO_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JO_long(block);

    return 0;
}
static int
codegen_CMP_JZ_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMP_JZ_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JZ_long(block);

    return 0;
}

static void
codegen_CMP_REGS(codeblock_t *block, uop_t *uop)
{
    int a = HOST_REG_GET(uop->src_reg_a_real);
    int b = HOST_REG_GET(uop->src_reg_b_real);
    int size = IREG_GET_SIZE(uop->src_reg_a_real);
    if (REG_IS_B(size))
        host_x86_CMP8_REG_REG(block, a, b);
    else if (REG_IS_W(size))
        host_x86_CMP16_REG_REG(block, a, b);
    else
        host_x86_CMP32_REG_REG(block, a, b);
}

static int
codegen_CMP_ULT(codeblock_t *block, uop_t *uop)
{
    int dest = HOST_REG_GET(uop->dest_reg_a_real);
    codegen_CMP_REGS(block, uop);
    /* Compare before writing dest, which may alias either operand. MOV
       clears the upper bits without changing CF; XOR would destroy it. */
    host_x86_MOV32_REG_IMM(block, dest, 0);
    codegen_alloc_bytes(block, 4);
    if (dest >= 4)
        codegen_addbyte(block, 0x40 | (dest >> 3)); /* REX for low-byte registers */
    codegen_addbyte3(block, 0x0f, uop->imm_data ? 0x93 : 0x92, 0xc0 | (dest & 7)); /* SETAE / SETB */
    return 0;
}

static int
codegen_CMP_SLT(codeblock_t *block, uop_t *uop)
{
    int dest = HOST_REG_GET(uop->dest_reg_a_real);
    codegen_CMP_REGS(block, uop);
    /* MOV preserves SF/OF, including when dest aliases an input. */
    host_x86_MOV32_REG_IMM(block, dest, 0);
    codegen_alloc_bytes(block, 4);
    if (dest >= 4)
        codegen_addbyte(block, 0x40 | (dest >> 3));
    codegen_addbyte3(block, 0x0f, uop->imm_data ? 0x9d : 0x9c, 0xc0 | (dest & 7)); /* SETGE / SETL */
    return 0;
}

static int
codegen_OVERFLOW(codeblock_t *block, uop_t *uop)
{
    int dest = HOST_REG_GET(uop->dest_reg_a_real);
    int a = HOST_REG_GET(uop->src_reg_a_real);
    int b = HOST_REG_GET(uop->src_reg_b_real);
    int size = uop->imm_data & 12;

    if (size == 4) {
        /* Read low bytes from dword allocations. Byte allocations would force
           both lazy operands into the scarce legacy byte registers. */
        if (!(uop->imm_data & 2)) {
            host_x86_MOV32_REG_REG(block, REG_ECX, a);
            a = REG_ECX;
        }
        codegen_alloc_bytes(block, 3);
        if (a >= 4 || b >= 4)
            codegen_addbyte(block, 0x40 | ((b & 8) >> 1) | ((a & 8) >> 3));
        codegen_addbyte2(block, (uop->imm_data & 2) ? 0x38 : 0x00,
                        0xc0 | ((b & 7) << 3) | (a & 7)); /* CMP / ADD r/m8, r8 */
    } else if (uop->imm_data & 2) {
        if (size == 8) host_x86_CMP16_REG_REG(block, a, b);
        else host_x86_CMP32_REG_REG(block, a, b);
    } else {
        /* ECX is backend scratch; neither lazy operand may be modified. */
        if (size == 8) {
            host_x86_MOV16_REG_REG(block, REG_ECX, a);
            host_x86_ADD16_REG_REG(block, REG_ECX, b);
        } else {
            host_x86_MOV32_REG_REG(block, REG_ECX, a);
            host_x86_ADD32_REG_REG(block, REG_ECX, b);
        }
    }
    host_x86_MOV32_REG_IMM(block, dest, 0); /* Preserve OF. */
    codegen_alloc_bytes(block, 4);
    if (dest >= 4)
        codegen_addbyte(block, 0x40 | (dest >> 3));
    codegen_addbyte3(block, 0x0f, (uop->imm_data & 1) ? 0x91 : 0x90, 0xc0 | (dest & 7)); /* SETNO / SETO */
    return 0;
}

static int
codegen_PARITY(codeblock_t *block, uop_t *uop)
{
    int dest = HOST_REG_GET(uop->dest_reg_a_real);
    int src = HOST_REG_GET(uop->src_reg_a_real);
    /* Host PF only depends on the low byte, even with a dword TEST. */
    host_x86_TEST32_REG(block, src, src);
    host_x86_MOV32_REG_IMM(block, dest, 0);
    codegen_alloc_bytes(block, 4);
    if (dest >= 4)
        codegen_addbyte(block, 0x40 | (dest >> 3));
    codegen_addbyte3(block, 0x0f, uop->imm_data ? 0x9b : 0x9a, 0xc0 | (dest & 7)); /* SETNP / SETP */
    return 0;
}

static int
codegen_PARITY_JUMP(codeblock_t *block, uop_t *uop)
{
    int src = HOST_REG_GET(uop->src_reg_a_real);
    host_x86_TEST32_REG(block, src, src);
    codegen_alloc_bytes(block, 6);
    codegen_addbyte2(block, 0x0f, uop->imm_data ? 0x8b : 0x8a); /* JNP / JP */
    codegen_addlong(block, 0);
    uop->p = &block_write_data[block_pos - 4];
    return 0;
}

static int
codegen_CMP_Z(codeblock_t *block, uop_t *uop)
{
    int dest = HOST_REG_GET(uop->dest_reg_a_real);
    int src = HOST_REG_GET(uop->src_reg_a_real);
    host_x86_TEST32_REG(block, src, src);
    /* Dest may alias src. Clear it after TEST, preserving ZF for SETcc. */
    host_x86_MOV32_REG_IMM(block, dest, 0);
    codegen_alloc_bytes(block, 4);
    if (dest >= 4)
        codegen_addbyte(block, 0x40 | (dest >> 3));
    codegen_addbyte3(block, 0x0f, uop->imm_data ? 0x95 : 0x94, 0xc0 | (dest & 7)); /* SETNE / SETE */
    return 0;
}

static int
codegen_CMOV_Z(codeblock_t *block, uop_t *uop)
{
    int dest = HOST_REG_GET(uop->dest_reg_a_real);
    int old = HOST_REG_GET(uop->src_reg_a_real);
    int src = HOST_REG_GET(uop->src_reg_b_real);
    int cond = HOST_REG_GET(uop->src_reg_c_real);
    int word = REG_IS_W(IREG_GET_SIZE(uop->dest_reg_a_real));
    int invert = !!uop->imm_data;

    /* Test before touching dest, which can also hold the condition. */
    host_x86_TEST32_REG(block, cond, cond);
    if (dest == src) {
        /* Dest already holds the taken value; select old on the other arm. */
        src = old;
        invert = !invert;
    } else if (dest != old) {
        if (word) host_x86_MOV16_REG_REG(block, dest, old);
        else host_x86_MOV32_REG_REG(block, dest, old);
    }
    codegen_alloc_bytes(block, 5);
    if (word)
        codegen_addbyte(block, 0x66);
    if ((dest | src) & 8)
        codegen_addbyte(block, 0x40 | ((dest & 8) >> 1) | ((src & 8) >> 3));
    codegen_addbyte3(block, 0x0f, invert ? 0x45 : 0x44, 0xc0 | ((dest & 7) << 3) | (src & 7));
    return 0;
}

static int
codegen_CMOVNZ(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int old_reg    = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg    = HOST_REG_GET(uop->src_reg_b_real);
    int cond_reg   = HOST_REG_GET(uop->src_reg_c_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int old_size   = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size   = IREG_GET_SIZE(uop->src_reg_b_real);
    int cond_size  = IREG_GET_SIZE(uop->src_reg_c_real);

    if (!REG_IS_L(cond_size))
        fatal("CMOVNZ cond %02x\n", uop->src_reg_c_real);

    if (REG_IS_L(dest_size) && REG_IS_L(old_size) && REG_IS_L(src_size)) {
        if (dest_reg != old_reg)
            host_x86_MOV32_REG_REG(block, dest_reg, old_reg);
        host_x86_TEST32_REG(block, cond_reg, cond_reg);
        host_x86_CMOVNZ32_REG_REG(block, dest_reg, src_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(old_size) && REG_IS_W(src_size)) {
        if (dest_reg != old_reg)
            host_x86_MOV16_REG_REG(block, dest_reg, old_reg);
        host_x86_TEST32_REG(block, cond_reg, cond_reg);
        host_x86_CMOVNZ16_REG_REG(block, dest_reg, src_reg);
    } else if ((REG_IS_D(dest_size) && REG_IS_D(old_size) && REG_IS_D(src_size))
               || (REG_IS_Q(dest_size) && REG_IS_Q(old_size) && REG_IS_Q(src_size))) {
        /*There is no SSE2 conditional move, so branch over the copy instead.
          MOVQ leaves the flags alone, so the TEST can be hoisted above it.*/
        uint32_t *branch_offset;

        host_x86_TEST32_REG(block, cond_reg, cond_reg);
        if (dest_reg == src_reg) {
            /*The destination already holds the source, so restore the old
              value when the condition is false instead*/
            branch_offset = host_x86_JNZ_long(block);
            if (dest_reg != old_reg)
                host_x86_MOVQ_XREG_XREG(block, dest_reg, old_reg);
        } else {
            if (dest_reg != old_reg)
                host_x86_MOVQ_XREG_XREG(block, dest_reg, old_reg);
            branch_offset = host_x86_JZ_long(block);
            host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg);
        }
        *branch_offset = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 4;
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMOVNZ %02x %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real, uop->src_reg_c_real);
#    endif

    return 0;
}

static int
codegen_FABS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_MOV64_REG_IMM(block, REG_RCX, 0x7fffffffffffffff);
        host_x86_MOVQ_XREG_REG(block, REG_XMM_TEMP, REG_RCX);
        host_x86_PAND_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FABS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_FCHS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_MOV64_REG_IMM(block, REG_RCX, 0x8000000000000000);
        host_x86_MOVQ_XREG_REG(block, REG_XMM_TEMP, REG_RCX);
        host_x86_PXOR_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FCHS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_FP_CONTROL_SET(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_W(src_size)) {
        /*new_fp_control = the host MXCSR with the x87 RC in its RC field*/
        host_x86_MOVZX_REG_32_16(block, REG_ECX, src_reg);
        host_x86_AND32_REG_IMM(block, REG_ECX, 0x0c00);
        host_x86_SHL32_IMM(block, REG_ECX, 3);
        host_x86_MOV32_REG_ABS(block, REG_ESI, &cpu_state.old_fp_control);
        host_x86_AND32_REG_IMM(block, REG_ESI, ~0x6000);
        host_x86_OR32_REG_REG(block, REG_ECX, REG_ESI);
        host_x86_MOV32_ABS_REG(block, &cpu_state.new_fp_control, REG_ECX);
        host_x86_MOV32_ABS_IMM(block, &fpu_ferr, 0);
        host_x86_MOV32_ABS_IMM(block, &fpu_ignne, 0);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FP_CONTROL_SET %02x\n", uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_FSQRT(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        host_x86_SQRTSD_XREG_XREG(block, dest_reg, src_reg_a);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FSQRT %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_FROUND_S(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a)) {
        host_x86_CVTSD2SS_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_CVTSS2SD_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FROUND_S %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_FTST(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_W(dest_size) && REG_IS_D(src_size_a)) {
        host_x86_PXOR_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
        if (dest_reg != REG_EAX)
            host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
        host_x86_XOR32_REG_REG(block, REG_EAX, REG_EAX);
        host_x86_COMISD_XREG_XREG(block, src_reg_a, REG_XMM_TEMP);
        host_x86_LAHF(block);
        host_x86_AND16_REG_IMM(block, REG_EAX, FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3);
        if (dest_reg != REG_EAX) {
            host_x86_MOV16_REG_REG(block, dest_reg, REG_EAX);
            host_x86_MOV32_REG_REG(block, REG_EAX, REG_ECX);
        }
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FTST %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_FADD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a) {
        host_x86_ADDSD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_FCOM(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_W(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        if (dest_reg != REG_EAX)
            host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
        host_x86_XOR32_REG_REG(block, REG_EAX, REG_EAX);
        host_x86_COMISD_XREG_XREG(block, src_reg_a, src_reg_b);
        host_x86_LAHF(block);
        host_x86_AND16_REG_IMM(block, REG_EAX, FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3);
        if (dest_reg != REG_EAX) {
            host_x86_MOV16_REG_REG(block, dest_reg, REG_EAX);
            host_x86_MOV32_REG_REG(block, REG_EAX, REG_ECX);
        }
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FCOM %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_FDIV(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a) {
        host_x86_DIVSD_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_DIVSD_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FDIV %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_FMUL(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a) {
        host_x86_MULSD_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_MULSD_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FMUL %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_FSUB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b) && dest_reg == src_reg_a) {
        host_x86_SUBSD_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_D(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_SUBSD_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FSUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_FP_ENTER(codeblock_t *block, uop_t *uop)
{
    uint32_t *branch_offset;

    host_x86_MOV32_REG_ABS(block, REG_ECX, &cr0);
    host_x86_TEST32_REG_IMM(block, REG_ECX, 0xc);
    branch_offset = host_x86_JZ_long(block);
    host_x86_MOV32_ABS_IMM(block, &cpu_state.oldpc, uop->imm_data);
    /*The cycles accumulated so far, charged on this exit only (the uop is an
      order barrier, so cycles is in memory)*/
    if (uop->imm_data2) {
        host_x86_MOV32_REG_ABS(block, REG_ECX, &cpu_state._cycles);
        host_x86_ADD32_REG_IMM(block, REG_ECX, uop->imm_data2);
        host_x86_MOV32_ABS_REG(block, &cpu_state._cycles, REG_ECX);
    }
#    if _WIN64
    host_x86_MOV32_REG_IMM(block, REG_ECX, 7);
#    else
    host_x86_MOV32_REG_IMM(block, REG_EDI, 7);
#    endif
    host_x86_CALL(block, x86_int);
    host_x86_JMP(block, codegen_exit_rout);
    *branch_offset = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 4;

    return 0;
}
static int
codegen_MMX_ENTER(codeblock_t *block, uop_t *uop)
{
    uint32_t *branch_offset;

    host_x86_MOV32_REG_ABS(block, REG_ECX, &cr0);
    host_x86_TEST32_REG_IMM(block, REG_ECX, 0x4);
    branch_offset = host_x86_JZ_long(block);
    host_x86_MOV32_ABS_IMM(block, &cpu_state.oldpc, uop->imm_data);
    host_x86_CALL(block, x86illegal);
    host_x86_JMP(block, codegen_exit_rout);
    *branch_offset = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 4;
    
    host_x86_TEST32_REG_IMM(block, REG_ECX, 0x8);
    branch_offset = host_x86_JZ_long(block);
    host_x86_MOV32_ABS_IMM(block, &cpu_state.oldpc, uop->imm_data);
    /*As codegen_FP_ENTER (the uop is a full barrier)*/
    if (uop->imm_data2) {
        host_x86_MOV32_REG_ABS(block, REG_ECX, &cpu_state._cycles);
        host_x86_ADD32_REG_IMM(block, REG_ECX, uop->imm_data2);
        host_x86_MOV32_ABS_REG(block, &cpu_state._cycles, REG_ECX);
    }
    host_x86_CALL(block, codegen_mmx_unavailable);
    host_x86_JMP(block, codegen_exit_rout);
    *branch_offset = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 4;

    /* A pending x87 exception faults before any MMX state is changed. */
    host_x86_MOVZX_REG_ABS_32_16(block, REG_ECX, &cpu_state.npxs);
    host_x86_TEST32_REG_IMM(block, REG_ECX, 0x80);
    uint32_t *no_pending = host_x86_JZ_long(block);
    host_x86_MOV32_REG_ABS(block, REG_ECX, &cr0);
    host_x86_TEST32_REG_IMM(block, REG_ECX, 0x20);
    uint32_t *native_exception = host_x86_JNZ_long(block);
    host_x86_MOV32_REG_ABS(block, REG_ECX, &fpu_ignne);
    host_x86_TEST32_REG_IMM(block, REG_ECX, 1);
    uint32_t *ignored = host_x86_JNZ_long(block);
    *native_exception = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) native_exception) - 4;
    codegen_reg_flush_conditional(block, invalid_ir_reg);
    host_x86_MOV32_ABS_IMM(block, &cpu_state.oldpc, uop->imm_data);
    host_x86_CALL(block, codegen_mmx_check);
    host_x86_JMP(block, codegen_exit_rout);
    *no_pending = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) no_pending) - 4;
    host_x86_MOV32_ABS_IMM(block, &fpu_ignne, 0);
    *ignored = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) ignored) - 4;

    return 0;
}

static int
codegen_MMX_COMMIT(codeblock_t *block, UNUSED(uop_t *uop))
{
    host_x86_MOV32_ABS_IMM(block, &cpu_state.tag[0], 0x01010101);
    host_x86_MOV32_ABS_IMM(block, &cpu_state.tag[4], 0x01010101);
    host_x86_MOV32_ABS_IMM(block, &cpu_state.TOP, 0);
    host_x86_MOV8_ABS_IMM(block, &cpu_state.ismmx, 1);

    return 0;
}

/*The fault paths of SSE_ENTER and CHECK_ALIGN. The checks skip the IR
  barrier's flush, so that a successful one leaves the cached registers live;
  their fault path writes back the state instead: the pending immediates and
  every dirty register, SIMD ones included. Inline, that writeback sat between
  each check and the code after it. It goes behind the block now, with the
  state captured at the check (codegen_reg_capture), and the check is a test
  and a branch. Nothing runs between a check's branch and its stub, so every
  host register still holds what the capture says. The stubs of a block that
  would be the same code (an unrolled loop's copies of one check) are emitted
  once; a block with more checks than slots keeps the rest inline.*/
#    define FAULT_STUB_MAX 256

typedef struct fault_stub_t {
    codegen_reg_state_t state;
    void               *helper;
    void               *route;
    uint32_t            oldpc;
    int                 nr_branches;
    uint32_t           *branch[2];
    void               *addr;
} fault_stub_t;

static fault_stub_t fault_stubs[FAULT_STUB_MAX];
static int          nr_fault_stubs;

/*Start a check's fault path: it stores oldpc for the fault, calls helper,
  if any, and leaves through route*/
static fault_stub_t *
fault_stub_begin(fault_stub_t *local, uint32_t oldpc, void *helper, void *route)
{
    fault_stub_t *stub = (nr_fault_stubs < FAULT_STUB_MAX) ? &fault_stubs[nr_fault_stubs++] : local;

    codegen_reg_capture(&stub->state, invalid_ir_reg, 1, 0);
    stub->helper      = helper;
    stub->route       = route;
    stub->oldpc       = oldpc;
    stub->nr_branches = 0;
    return stub;
}

static void
fault_stub_branch(fault_stub_t *stub, uint32_t *branch)
{
    stub->branch[stub->nr_branches++] = branch;
}

static void
fault_stub_patch(const fault_stub_t *stub, void *addr)
{
    for (int i = 0; i < stub->nr_branches; i++)
        *stub->branch[i] = (uintptr_t) addr - ((uintptr_t) stub->branch[i] + 4);
}

static void
fault_stub_body(codeblock_t *block, fault_stub_t *stub)
{
    codegen_alloc(block, 16);
    stub->addr = &block_write_data[block_pos];
    codegen_reg_emit_writes(block, &stub->state);
    host_x86_MOV32_ABS_IMM(block, &cpu_state.oldpc, stub->oldpc);
    if (stub->helper)
        host_x86_CALL(block, stub->helper);
    host_x86_JMP(block, stub->route);
}

/*After the check's branches. A stub without a slot goes here, jumped over*/
static void
fault_stub_end(codeblock_t *block, fault_stub_t *stub, fault_stub_t *local)
{
    uint32_t *over;

    if (stub != local)
        return;
    over = host_x86_JMP_long(block);
    fault_stub_body(block, stub);
    fault_stub_patch(stub, stub->addr);
    *over = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) over + 4);
}

void
codegen_fault_stubs_reset(void)
{
    nr_fault_stubs = 0;
}

/*After the block's last exit*/
void
codegen_fault_stubs_emit(codeblock_t *block)
{
    for (int i = 0; i < nr_fault_stubs; i++) {
        fault_stub_t *stub = &fault_stubs[i];
        int           j;

        for (j = 0; j < i; j++) {
            const fault_stub_t *other = &fault_stubs[j];

            if (other->oldpc == stub->oldpc && other->helper == stub->helper && other->route == stub->route
                && !memcmp(&other->state, &stub->state, sizeof(stub->state)))
                break;
        }
        if (j < i)
            stub->addr = fault_stubs[j].addr;
        else
            fault_stub_body(block, stub);
        fault_stub_patch(stub, stub->addr);
    }
    nr_fault_stubs = 0;
}

/*#UD with CR0.EM set or CR4.OSFXSR clear, then #NM with CR0.TS set: two
  tests on the way through, and the helper tells the faults apart*/
static int
codegen_SSE_ENTER(codeblock_t *block, uop_t *uop)
{
    fault_stub_t  local;
    fault_stub_t *stub = fault_stub_begin(&local, uop->imm_data, codegen_sse_unavailable, codegen_exit_rout);

    host_x86_MOV32_REG_ABS(block, REG_ECX, &cr0);
    host_x86_TEST32_REG_IMM(block, REG_ECX, 0xc);
    fault_stub_branch(stub, host_x86_JNZ_long(block));
    host_x86_MOV32_REG_ABS(block, REG_ECX, &cr4);
    host_x86_TEST32_REG_IMM(block, REG_ECX, CR4_OSFXSR);
    fault_stub_branch(stub, host_x86_JZ_long(block));
    fault_stub_end(block, stub, &local);

    host_x86_MOV32_BASE_OFFSET_IMM(block, REG_RSP, STACK_SSE_RECHECK, 0);
    return 0;
}

void
codegen_backend_sse_recheck(codeblock_t *block, uop_t *uop)
{
    /* A dominating successful SSE_ENTER initialized this slot. If a helper
       changed control state, fault at this SSE instruction, not the memory op. */
    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, STACK_SSE_RECHECK);
    host_x86_TEST32_REG(block, REG_ECX, REG_ECX);
    uint32_t *checked = host_x86_JZ_long(block);
    codegen_SSE_ENTER(block, uop);
    codegen_set_jump_dest(block, checked);
}

static int
codegen_CHECK_ALIGN(codeblock_t *block, uop_t *uop)
{
    fault_stub_t  local;
    fault_stub_t *stub = fault_stub_begin(&local, uop->imm_data, NULL, codegen_gpf_rout);

    host_x86_TEST32_REG_IMM(block, uop->src_reg_a_real, 15);
    fault_stub_branch(stub, host_x86_JNZ_long(block));
    fault_stub_end(block, stub, &local);
    return 0;
}

static int
codegen_JMP(codeblock_t *block, uop_t *uop)
{
    void *dest;

#    ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    /*An unconditional jump out of the block is an ordinary hand-off (UOP_JMP
      writes registers back), so it can link like the block's end. Fault
      paths reach codegen_exit_rout by conditional branches or direct jumps
      instead.*/
    if (codegen_block_link && (uop->p == codegen_exit_rout)) {
        if (uop->imm_data2)
            codegen_exit_add(block, uop->imm_data2);
        codegen_link_exit(block);
        return 0;
    }
#    endif
    dest = codegen_exit_dest(uop->p, uop->imm_data2);
    if (!dest) {
        codegen_exit_add(block, uop->imm_data2);
        dest = uop->p;
    }
    host_x86_JMP(block, dest);

    return 0;
}

static int
codegen_LOAD_FUNC_ARG0(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
#    if _WIN64
        host_x86_MOV32_REG_REG(block, REG_ECX, src_reg);
#    else
        host_x86_MOV32_REG_REG(block, REG_EDI, src_reg);
#    endif
    } else if (REG_IS_W(src_size)) {
#    if _WIN64
        host_x86_MOVZX_REG_32_16(block, REG_ECX, src_reg);
#    else
        host_x86_MOVZX_REG_32_16(block, REG_EDI, src_reg);
#    endif
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_LOAD_FUNC_ARG0 %02x\n", uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG1(UNUSED(codeblock_t *block), UNUSED(uop_t *uop))
{
#    ifdef RECOMPILER_DEBUG
    fatal("codegen_LOAD_FUNC_ARG1 %02x\n", uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG2(UNUSED(codeblock_t *block), UNUSED(uop_t *uop))
{
#    ifdef RECOMPILER_DEBUG
    fatal("codegen_LOAD_FUNC_ARG2 %02x\n", uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG3(UNUSED(codeblock_t *block), UNUSED(uop_t *uop))
{
#    ifdef RECOMPILER_DEBUG
    fatal("codegen_LOAD_FUNC_ARG3 %02x\n", uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_LOAD_FUNC_ARG0_IMM(codeblock_t *block, uop_t *uop)
{
#    if _WIN64
    host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data);
#    else
    host_x86_MOV32_REG_IMM(block, REG_EDI, uop->imm_data);
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG1_IMM(codeblock_t *block, uop_t *uop)
{
#    if _WIN64
    host_x86_MOV32_REG_IMM(block, REG_EDX, uop->imm_data);
#    else
    host_x86_MOV32_REG_IMM(block, REG_ESI, uop->imm_data);
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG2_IMM(codeblock_t *block, uop_t *uop)
{
#    if _WIN64
    host_x86_MOV32_REG_IMM(block, REG_R8, uop->imm_data);
#    else
    host_x86_MOV32_REG_IMM(block, REG_EDX, uop->imm_data);
#    endif
    return 0;
}
static int
codegen_LOAD_FUNC_ARG3_IMM(UNUSED(codeblock_t *block), UNUSED(uop_t *uop))
{
#    ifdef RECOMPILER_DEBUG
    fatal("codegen_LOAD_FUNC_ARG3_IMM\n");
#    endif
    return 0;
}

static int
codegen_LOAD_SEG(codeblock_t *block, uop_t *uop)
{
    int src_reg = HOST_REG_GET(uop->src_reg_a_real);

#    ifdef RECOMPILER_DEBUG
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);
    if (!REG_IS_W(src_size))
        fatal("LOAD_SEG %02x %p\n", uop->src_reg_a_real, uop->p);
#    endif
#    if _WIN64
    host_x86_MOV16_REG_REG(block, REG_CX, src_reg);
    host_x86_MOV64_REG_IMM(block, REG_EDX, (uint64_t) uop->p);
#    else
    host_x86_MOV16_REG_REG(block, REG_DI, src_reg);
    host_x86_MOV64_REG_IMM(block, REG_ESI, (uint64_t) uop->p);
#    endif
    host_x86_CALL(block, (void *) loadseg);
    host_x86_TEST32_REG(block, REG_EAX, REG_EAX);
    codegen_exit_Jcc(block, uop, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);

    return 0;
}

static int mem_slow_count, mem_slow_active;

/* These registers are fixed by the memory ABI. Reserve the whole lookup at
   once: separate emitter calls add noticeable cost when compiling RAM code. */
static uint32_t *
codegen_MEM_READ_PAGE(codeblock_t *block, uintptr_t *lookup, int refill)
{
    codegen_alloc_bytes(block, refill ? 27 : 31);
    if (refill)
        codegen_addbyte3(block, 0x45, 0x8b, 0xc1);       /* MOV R8D,R9D */
    else {
        codegen_addbyte3(block, 0x44, 0x8b, 0xc6);       /* MOV R8D,ESI */
        codegen_addbyte4(block, 0x41, 0xc1, 0xe8, 12);   /* SHR R8D,12 */
    }
    codegen_addbyte2(block, 0x49, 0xb9);                /* MOV R9,lookup */
    codegen_addquad(block, (uintptr_t) lookup);
    codegen_addbyte4(block, 0x4b, 0x8b, 0x3c, 0xc1);   /* MOV RDI,[R9+R8*8] */
    codegen_addbyte4(block, 0x48, 0x83, 0xff, 0xff);   /* CMP RDI,-1 */
    codegen_addbyte2(block, 0x0f, 0x84);                /* JE missing */
    codegen_addlong(block, 0);
    return (uint32_t *) &block_write_data[block_pos - 4];
}

/* R8D is the cached guest page and RDI its host bias. R9 remains scratch
   for absolute CPU-state loads and the lookup-table address. Only reuse within
   an IR region whose emitters preserve R8/RDI; read/write tables never mix. */
static uint32_t *
codegen_MEM_LOOKUP(codeblock_t *block, uop_t *uop, uintptr_t *lookup, void **lookup_resume)
{
    *lookup_resume = NULL;
    if (mem_slow_active && (uop->type & UOP_TYPE_MEM_REUSE)) {
        if (uop->type & UOP_TYPE_MEM_SAME_ADDR) {
            /* An unchanged address only needs the callback invalidation check. */
            codegen_alloc_bytes(block, 10);
            codegen_addbyte4(block, 0x41, 0x83, 0xf8, 0xff); /* CMP R8D,-1 */
            codegen_addbyte2(block, 0x0f, 0x84);             /* JE refill */
        } else {
            codegen_alloc_bytes(block, 16);
            codegen_addbyte3(block, 0x44, 0x8b, 0xce);      /* MOV R9D,ESI */
            codegen_addbyte4(block, 0x41, 0xc1, 0xe9, 12);  /* SHR R9D,12 */
            codegen_addbyte3(block, 0x45, 0x3b, 0xc8);      /* CMP R9D,R8D */
            codegen_addbyte2(block, 0x0f, 0x85);            /* JNE refill */
        }
        codegen_addlong(block, 0);
        /* A cache hit falls through. Emit the table lookup after the block. */
        *lookup_resume = &block_write_data[block_pos];
        return (uint32_t *) &block_write_data[block_pos - 4];
    }
    return codegen_MEM_READ_PAGE(block, lookup, 0);
}

/* ESI stays the linear address; ECX/XMM0 retain the existing data ABI. */
static uint32_t *
codegen_MEM_SCALAR_LOOKUP(codeblock_t *block, uop_t *uop, uintptr_t *lookup, int size, uint32_t **unaligned, void **lookup_resume)
{
    uint32_t *miss = codegen_MEM_LOOKUP(block, uop, lookup, lookup_resume);
    *unaligned = NULL;
    if (size > 1) {
        /* Aligned accesses cannot cross a page. Keep their fall-through path
           to one alignment test; handle unaligned RAM hits out of line. */
        host_x86_TEST32_REG_IMM(block, REG_ESI, size - 1);
        *unaligned = host_x86_JNZ_long(block);
    }
    return miss;
}

typedef struct mem_slow_site_t {
    codegen_mem_reg_state_t state;
    uint32_t *unaligned, *miss, *page_cross;
    void *access, *resume, *helper, *stub, *lookup_resume;
    int data_reg;
    int size, cycles_reg, sse_invalidate, owner, users, same_address;
} mem_slow_site_t;

static mem_slow_site_t mem_slow_sites[UOP_NR_MAX];
/* Preserve the cheap contiguous search for early matches. Index only stubs
   not found in that bounded prefix, so common repeated accesses need no hash.
   Entries store owner + 1; zero is empty. At most half the table is occupied. */
static uint16_t mem_slow_index[UOP_NR_MAX * 2];
enum { MEM_SLOW_LINEAR_SITES = 16 };

static inline int
codegen_MEM_SAME_STUB(const mem_slow_site_t *site, const mem_slow_site_t *other)
{
    return site->helper == other->helper && site->size == other->size
           && site->cycles_reg == other->cycles_reg && site->sse_invalidate == other->sse_invalidate
           && site->data_reg == other->data_reg
           && site->state.write_mask == other->state.write_mask
           && site->state.reload_mask == other->state.reload_mask
           && !memcmp(site->state.regs, other->state.regs, sizeof(site->state.regs));
}

static inline unsigned
codegen_MEM_STUB_HASH(const mem_slow_site_t *site)
{
    /* Hash exactly the compatibility key, never struct padding, continuations
       or final allocator state. Equality is still checked on every collision. */
    uintptr_t helper = (uintptr_t) site->helper;
    uint32_t hash = (uint32_t) helper ^ (uint32_t) (helper >> 32);
    hash = (hash ^ site->size) * 16777619u;
    hash = (hash ^ (uint32_t) site->cycles_reg) * 16777619u;
    hash = (hash ^ site->sse_invalidate) * 16777619u;
    hash = (hash ^ site->data_reg) * 16777619u;
    hash = (hash ^ site->state.write_mask) * 16777619u;
    hash = (hash ^ site->state.reload_mask) * 16777619u;
    /* Scalar word loads avoid a SIMD stack temporary (and Win64 frame
       realignment) in the surrounding emitter. Do not read snapshot padding. */
    unsigned n = 0;
    for (; n + 4 <= sizeof(site->state.regs); n += 4) {
        uint32_t word;
        memcpy(&word, &site->state.regs[n], sizeof(word));
        hash = (hash ^ word) * 16777619u;
    }
    uint32_t tail = 0;
    for (unsigned shift = 0; n < sizeof(site->state.regs); n++, shift += 8)
        tail |= (uint32_t) site->state.regs[n] << shift;
    hash = (hash ^ tail) * 16777619u;
    return hash ^ (hash >> 16);
}

static inline void
jump_here(uint32_t *jump_p)
{
    *jump_p = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) jump_p + 4);
}

static int
codegen_FCOM_SIGNED_ZERO(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_W(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        uint32_t *not_neg_zero_p;
        uint32_t *not_pos_zero_p;

        if (dest_reg != REG_EAX)
            host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
        host_x86_XOR32_REG_REG(block, REG_EAX, REG_EAX);
        host_x86_COMISD_XREG_XREG(block, src_reg_a, src_reg_b);
        host_x86_LAHF(block);
        host_x86_AND16_REG_IMM(block, REG_EAX, FPU_SW_C0 | FPU_SW_C2 | FPU_SW_C3);
        host_x86_MOVQ_REG_XREG(block, REG_RSI, src_reg_a);
        host_x86_MOV64_REG_IMM(block, REG_RDI, 0x8000000000000000ULL);
        host_x86_CMP64_REG_REG(block, REG_RSI, REG_RDI);
        not_neg_zero_p = host_x86_JNZ_long(block);
        host_x86_MOVQ_REG_XREG(block, REG_RSI, src_reg_b);
        host_x86_TEST64_REG(block, REG_RSI, REG_RSI);
        not_pos_zero_p = host_x86_JNZ_long(block);
        host_x86_MOV32_REG_IMM(block, REG_EAX, FPU_SW_C0);
        jump_here(not_neg_zero_p);
        jump_here(not_pos_zero_p);
        if (dest_reg != REG_EAX) {
            host_x86_MOV16_REG_REG(block, dest_reg, REG_EAX);
            host_x86_MOV32_REG_REG(block, REG_EAX, REG_ECX);
        }
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FCOM_SIGNED_ZERO %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_FCOM_CHKCOP(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_W(dest_size) && REG_IS_D(src_size_a) && REG_IS_D(src_size_b)) {
        uint32_t *not_image_a_p;
        uint32_t *not_image_b_p;

        if (uop->imm_data)
            codegen_FCOM_SIGNED_ZERO(block, uop);
        else
            codegen_FCOM(block, uop);
        /* C3 when a is the first image and b the first plus 2. */
        host_x86_MOVQ_REG_XREG(block, REG_RSI, src_reg_a);
        host_x86_MOV64_REG_IMM(block, REG_RDI, 0x3fec1a6ff866a936ULL);
        host_x86_CMP64_REG_REG(block, REG_RSI, REG_RDI);
        not_image_a_p = host_x86_JNZ_long(block);
        host_x86_MOVQ_REG_XREG(block, REG_RSI, src_reg_b);
        host_x86_ADD64_REG_IMM(block, REG_RDI, 2);
        host_x86_CMP64_REG_REG(block, REG_RSI, REG_RDI);
        not_image_b_p = host_x86_JNZ_long(block);
        host_x86_MOV16_REG_IMM(block, dest_reg, FPU_SW_C3);
        jump_here(not_image_a_p);
        jump_here(not_image_b_p);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("codegen_FCOM_CHKCOP %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

/*Guest memory accesses. The common case - the page is in the running
  processor's lookup table and the access doesn't cross a size boundary - is
  done inline, the same way the load/store routines start. Anything else
  calls the routine, which looks the page up again and takes its slow path.
  In: ESI = address (and for stores ECX or XMM_TEMP = data). Clobbers RCX,
  RSI, RDI and R8.*/
enum {
    MEM_B,
    MEM_W,
    MEM_L,
    MEM_Q,
    MEM_SINGLE,
    MEM_DOUBLE,
    MEM_DQ
};

static inline int
mem_access_size(int kind)
{
    static const int sizes[] = { 1, 2, 4, 8, 4, 8, 16 };

    return sizes[kind];
}

/*The slow path of an inline load or store (misaligned, or no lookup entry):
  the routine call and the abort exit. It is emitted behind the block, after
  its last exit, so the fast paths take fewer code lines; it comes back with
  a jump. Its abort exit is the access's lazy site exit as it would be
  inline: the host registers hold the same values there. A block with more
  accesses than slots keeps the rest inline.*/
#    define MEM_SLOW_MAX 256

typedef struct mem_slow_t {
    uint32_t            *misaligned_p;
    uint32_t            *miss_p;
    void                *ret;
    void                *rout;
    codegen_lazy_list_t *lazy;
    int                  kind;
    int                  dest_reg;
    int                  store;
    int                  add;
    /*MEM_DQ: the address's registers and displacement, and the registers the
      slow path stores before the helper and loads after it*/
    int                  seg_reg;
    int                  addr_reg;
    int                  is_a16;
    int                  sse_invalidate;
    uint32_t             imm;
    codegen_reg_state_t  state;
} mem_slow_t;

static mem_slow_t mem_slow[MEM_SLOW_MAX];
static int        nr_mem_slow;

/*A 128-bit access's slow path (misaligned_p is the page crossing branch):
  the helper for each half. It can change the guest's state (cycles, CR0 and
  CR4 through a device) and it clobbers the SIMD registers, so the registers
  are stored before it and loaded again after, as they were at the access.
  Both halves leave through one exit, the access's lazy site's, whose stub
  stores what the uop deferred, among it the oldpc a #PF takes as its EIP*/
static void
codegen_mem_slow_dq(codeblock_t *block, const mem_slow_t *slow)
{
    uint32_t  imm = slow->imm + 8;
    uint32_t *fault_p;
    void     *exit_p;

    if (slow->misaligned_p)
        jump_here(slow->misaligned_p);
    if (slow->miss_p)
        jump_here(slow->miss_p);
    /*The next SSE instruction rechecks CR0 and CR4 after a helper*/
    if (slow->sse_invalidate)
        host_x86_MOV32_BASE_OFFSET_IMM(block, REG_RSP, STACK_SSE_RECHECK, 1);
    codegen_reg_emit_writes(block, &slow->state);
    if (slow->store) {
        host_x86_MOVDQU_BASE_OFFSET_XREG(block, REG_RSP, STACK_TEMP_DQ, slow->dest_reg);
        host_x86_MOVQ_XREG_BASE_OFFSET(block, REG_XMM_TEMP, REG_RSP, STACK_TEMP_DQ);
    }
    host_x86_CALL(block, slow->rout);
    host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
    fault_p = host_x86_JNZ_long(block);
    if (!slow->store)
        host_x86_MOVQ_BASE_OFFSET_XREG(block, REG_RSP, STACK_TEMP_DQ, REG_XMM_TEMP);

    host_x86_LEA_REG_REG(block, REG_ESI, slow->seg_reg, slow->addr_reg);
    if (imm) {
        host_x86_ADD32_REG_IMM(block, REG_ESI, imm);
        if (slow->is_a16)
            host_x86_AND32_REG_IMM(block, REG_ESI, 0x0000ffff);
    }
    if (slow->store)
        host_x86_MOVQ_XREG_BASE_OFFSET(block, REG_XMM_TEMP, REG_RSP, STACK_TEMP_DQ + 8);
    host_x86_CALL(block, slow->rout);
    host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
    exit_p = codegen_exit_Jcc_add(block, slow->add, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);
    if (exit_p)
        *fault_p = (uintptr_t) exit_p - ((uintptr_t) fault_p + 4);
    else
        codegen_lazy_exit_also(fault_p);
    if (!slow->store)
        host_x86_MOVQ_BASE_OFFSET_XREG(block, REG_RSP, STACK_TEMP_DQ + 8, REG_XMM_TEMP);

    codegen_reg_emit_reloads(block, &slow->state);
    /*No guest page has this tag. Never carry a mapping across a callback*/
    host_x86_MOV32_REG_IMM(block, REG_R8, UINT32_MAX);
    if (!slow->store)
        host_x86_MOVDQU_XREG_BASE_OFFSET(block, slow->dest_reg, REG_RSP, STACK_TEMP_DQ);
}

static void
codegen_mem_slow(codeblock_t *block, const mem_slow_t *slow)
{
    if (slow->kind == MEM_DQ) {
        codegen_mem_slow_dq(block, slow);
        return;
    }
    if (slow->misaligned_p)
        jump_here(slow->misaligned_p);
    jump_here(slow->miss_p);
    host_x86_MOV32_REG_REG(block, REG_ESI, slow->store ? REG_EDI : REG_ECX);
    host_x86_CALL(block, slow->rout);
    host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
    codegen_exit_Jcc_add(block, slow->add, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);
    if (!slow->store) {
        switch (slow->kind) {
            case MEM_B:
                host_x86_MOV8_REG_REG(block, slow->dest_reg, REG_ECX);
                break;
            case MEM_W:
                host_x86_MOV16_REG_REG(block, slow->dest_reg, REG_ECX);
                break;
            case MEM_L:
                host_x86_MOV32_REG_REG(block, slow->dest_reg, REG_ECX);
                break;
            default:
                host_x86_MOVQ_XREG_XREG(block, slow->dest_reg, REG_XMM_TEMP);
                break;
        }
    }
}

/*The fast path ends here: the slow path goes behind the block, or inline
  with a jump over it*/
static void
codegen_mem_slow_add(codeblock_t *block, uop_t *uop, uint32_t *misaligned_p, uint32_t *miss_p, int kind, int dest_reg, int store, void *rout)
{
    mem_slow_t  inline_slow;
    mem_slow_t *slow = (nr_mem_slow < MEM_SLOW_MAX) ? &mem_slow[nr_mem_slow++] : &inline_slow;
    uint32_t   *done_p;

    slow->misaligned_p = misaligned_p;
    slow->miss_p       = miss_p;
    slow->rout         = rout;
    slow->lazy         = codegen_lazy_site;
    slow->kind         = kind;
    slow->dest_reg     = dest_reg;
    slow->store        = store;
    slow->add          = uop->imm_data2;
    if (slow != &inline_slow) {
        slow->ret = &block_write_data[block_pos];
        return;
    }
    done_p = host_x86_JMP_long(block);
    codegen_mem_slow(block, slow);
    jump_here(done_p);
}

void
codegen_mem_slow_reset(void)
{
    nr_mem_slow = 0;
}

/*After the block's last exit, and before the lazy stubs, which the abort
  exits add to*/
void
codegen_mem_slow_emit(codeblock_t *block)
{
    for (int c = 0; c < nr_mem_slow; c++) {
        codegen_lazy_site = mem_slow[c].lazy;
        codegen_mem_slow(block, &mem_slow[c]);
        host_x86_JMP(block, mem_slow[c].ret);
    }
    codegen_lazy_site = NULL;
    nr_mem_slow       = 0;
}

/*A 128-bit access, ESI = address. A RAM hit within one page is a MOVDQU and
  falls through; anything else takes the slow path behind the block
  (codegen_mem_slow_dq). 16-bit addressing has only the slow path, inline*/
static void
codegen_mem_dq(codeblock_t *block, uop_t *uop, int data_reg, int seg_reg, int addr_reg, int store, uintptr_t *lookup, void *rout)
{
    mem_slow_t  inline_slow;
    mem_slow_t *slow;
    uint32_t   *page_cross_p = NULL;
    uint32_t   *miss_p       = NULL;

    if (!uop->is_a16) {
        void *lookup_resume;

        /*(ESI & 0xfff) > 0xff0: it crosses into the next page*/
        host_x86_MOV32_REG_REG(block, REG_ECX, REG_ESI);
        host_x86_AND32_REG_IMM(block, REG_ECX, 0x00000fff);
        host_x86_CMP32_REG_IMM(block, REG_ECX, 0x00000ff0);
        page_cross_p = host_x86_JA_long(block);
        miss_p       = codegen_MEM_LOOKUP(block, uop, lookup, &lookup_resume);
        if (store)
            host_x86_MOVDQU_BASE_INDEX_XREG(block, REG_RDI, REG_RSI, data_reg);
        else
            host_x86_MOVDQU_XREG_BASE_INDEX(block, data_reg, REG_RDI, REG_RSI);
    }

    slow = (!uop->is_a16 && (nr_mem_slow < MEM_SLOW_MAX)) ? &mem_slow[nr_mem_slow++] : &inline_slow;
    slow->misaligned_p   = page_cross_p;
    slow->miss_p         = miss_p;
    slow->rout           = rout;
    slow->lazy           = codegen_lazy_site;
    slow->kind           = MEM_DQ;
    slow->dest_reg       = data_reg;
    slow->store          = store;
    slow->add            = uop->imm_data2;
    slow->seg_reg        = seg_reg;
    slow->addr_reg       = addr_reg;
    slow->is_a16         = uop->is_a16;
    slow->sse_invalidate = !!(uop->type & UOP_TYPE_SSE_INVALIDATE);
    slow->imm            = uop->imm_data;
    codegen_reg_capture(&slow->state, uop->dest_reg_a, 0, 1);
    if (slow != &inline_slow) {
        slow->ret = &block_write_data[block_pos];
        return;
    }
    if (uop->is_a16)
        codegen_mem_slow_dq(block, slow);
    else {
        uint32_t *done_p = host_x86_JMP_long(block);

        codegen_mem_slow_dq(block, slow);
        jump_here(done_p);
    }
}

static void
codegen_mem_load(codeblock_t *block, uop_t *uop, int kind, int dest_reg, void *rout)
{
    uint32_t *misaligned_p = NULL;
    uint32_t *miss_p;

    host_x86_MOV32_REG_REG(block, REG_ECX, REG_ESI);
    host_x86_SHR32_IMM(block, REG_ESI, 12);
    host_x86_MOV64_REG_TABLE_INDEX(block, REG_RSI, REG_RDI, readlookup2, REG_RSI);
    if (mem_access_size(kind) != 1) {
        host_x86_TEST8_REG_IMM(block, REG_ECX, mem_access_size(kind) - 1);
        misaligned_p = host_x86_JNZ_long(block);
    }
    host_x86_CMP64_REG_IMM(block, REG_RSI, (uint32_t) -1);
    miss_p = host_x86_JZ_long(block);
    switch (kind) {
        case MEM_B:
            host_x86_MOV8_REG_BASE_INDEX(block, dest_reg, REG_RSI, REG_RCX);
            break;
        case MEM_W:
            host_x86_MOV16_REG_BASE_INDEX(block, dest_reg, REG_RSI, REG_RCX);
            break;
        case MEM_L:
            host_x86_MOV32_REG_BASE_INDEX(block, dest_reg, REG_RSI, REG_RCX);
            break;
        case MEM_SINGLE:
            host_x86_CVTSS2SD_XREG_BASE_INDEX(block, dest_reg, REG_RSI, REG_RCX);
            break;
        default:
            host_x86_MOVQ_XREG_BASE_INDEX(block, dest_reg, REG_RSI, REG_RCX);
            break;
    }
    codegen_mem_slow_add(block, uop, misaligned_p, miss_p, kind, dest_reg, 0, rout);
}

static void
codegen_mem_store(codeblock_t *block, uop_t *uop, int kind, void *rout)
{
    uint32_t *misaligned_p = NULL;
    uint32_t *miss_p;

    host_x86_MOV32_REG_REG(block, REG_EDI, REG_ESI);
    host_x86_SHR32_IMM(block, REG_ESI, 12);
    host_x86_MOV64_REG_TABLE_INDEX(block, REG_RSI, REG_R8, writelookup2, REG_RSI);
    if (mem_access_size(kind) != 1) {
        host_x86_TEST8_REG_IMM(block, REG_EDI, mem_access_size(kind) - 1);
        misaligned_p = host_x86_JNZ_long(block);
    }
    host_x86_CMP64_REG_IMM(block, REG_RSI, (uint32_t) -1);
    miss_p = host_x86_JZ_long(block);
    switch (kind) {
        case MEM_B:
            host_x86_MOV8_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
            break;
        case MEM_W:
            host_x86_MOV16_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
            break;
        case MEM_L:
            host_x86_MOV32_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
            break;
        case MEM_SINGLE:
            host_x86_MOVD_BASE_INDEX_XREG(block, REG_RSI, REG_RDI, REG_XMM_TEMP);
            break;
        default:
            host_x86_MOVQ_BASE_INDEX_XREG(block, REG_RSI, REG_RDI, REG_XMM_TEMP);
            break;
    }
    codegen_mem_slow_add(block, uop, misaligned_p, miss_p, kind, -1, 1, rout);
}

static int
codegen_MEM_LOAD_ABS(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    host_x86_LEA_REG_IMM(block, REG_ESI, seg_reg, uop->imm_data);
    if (REG_IS_B(dest_size)) {
        codegen_mem_load(block, uop, MEM_B, dest_reg, codegen_mem_load_byte);
    } else if (REG_IS_W(dest_size)) {
        codegen_mem_load(block, uop, MEM_W, dest_reg, codegen_mem_load_word);
    } else if (REG_IS_L(dest_size)) {
        codegen_mem_load(block, uop, MEM_L, dest_reg, codegen_mem_load_long);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MEM_LOAD_ABS - %02x\n", uop->dest_reg_a_real);
#    endif

    return 0;
}
static int
codegen_MEM_LOAD_REG(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data) {
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
        if (uop->is_a16) {
            host_x86_AND32_REG_IMM(block, REG_ESI, 0x0000ffff);
        }
    }
    if (REG_IS_DQ(dest_size)) {
        codegen_mem_dq(block, uop, dest_reg, seg_reg, addr_reg, 0, readlookup2, codegen_mem_load_quad);
        return 0;
    }
    if (REG_IS_B(dest_size)) {
        codegen_mem_load(block, uop, MEM_B, dest_reg, codegen_mem_load_byte);
    } else if (REG_IS_W(dest_size)) {
        codegen_mem_load(block, uop, MEM_W, dest_reg, codegen_mem_load_word);
    } else if (REG_IS_L(dest_size)) {
        codegen_mem_load(block, uop, MEM_L, dest_reg, codegen_mem_load_long);
    } else if (REG_IS_Q(dest_size)) {
        codegen_mem_load(block, uop, MEM_Q, dest_reg, codegen_mem_load_quad);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MEM_LOAD_REG - %02x\n", uop->dest_reg_a_real);
#    endif

    return 0;
}
static int
codegen_MEM_LOAD_REG_EA(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (REG_IS_W(dest_size))
        host_x86_CALL(block, codegen_mem_load_word_ea);
    else if (REG_IS_L(dest_size))
        host_x86_CALL(block, codegen_mem_load_long_ea);
    else
        fatal("MEM_LOAD_REG_EA - %02x\n", uop->dest_reg_a_real);
    host_x86_TEST32_REG(block, REG_ESI, REG_ESI);
    codegen_exit_Jcc(block, uop, codegen_exit_rout, host_x86_JNZ_long, host_x86_JZ_long);
    if (REG_IS_W(dest_size))
        host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
    else
        host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_MEM_LOAD_SINGLE(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

#    ifdef RECOMPILER_DEBUG
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    if (!REG_IS_D(dest_size))
        fatal("MEM_LOAD_SINGLE - %02x\n", uop->dest_reg_a_real);
#    endif
    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data)
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
    codegen_mem_load(block, uop, MEM_SINGLE, dest_reg, codegen_mem_load_single);

    return 0;
}
static int
codegen_MEM_LOAD_DOUBLE(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

#    ifdef RECOMPILER_DEBUG
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    if (!REG_IS_D(dest_size))
        fatal("MEM_LOAD_DOUBLE - %02x\n", uop->dest_reg_a_real);
#    endif
    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data)
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
    codegen_mem_load(block, uop, MEM_DOUBLE, dest_reg, codegen_mem_load_double);

    return 0;
}

static int
codegen_MEM_STORE_ABS(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_b_real);

    host_x86_LEA_REG_IMM(block, REG_ESI, seg_reg, uop->imm_data);
    if (REG_IS_B(src_size)) {
        host_x86_MOV8_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_B, codegen_mem_store_byte);
    } else if (REG_IS_W(src_size)) {
        host_x86_MOV16_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_W, codegen_mem_store_word);
    } else if (REG_IS_L(src_size)) {
        host_x86_MOV32_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_L, codegen_mem_store_long);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MEM_STORE_ABS - %02x\n", uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_MEM_STORE_IMM_8(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    host_x86_MOV8_REG_IMM(block, REG_ECX, uop->imm_data);
    codegen_mem_store(block, uop, MEM_B, codegen_mem_store_byte);

    return 0;
}
static int
codegen_MEM_STORE_IMM_16(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data & 0xffff); /*only CX is stored*/
    codegen_mem_store(block, uop, MEM_W, codegen_mem_store_word);

    return 0;
}
static int
codegen_MEM_STORE_IMM_32(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    host_x86_MOV32_REG_IMM(block, REG_ECX, uop->imm_data);
    codegen_mem_store(block, uop, MEM_L, codegen_mem_store_long);

    return 0;
}

static int
codegen_MEM_STORE_REG(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_c_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_c_real);

    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data) {
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
        if (uop->is_a16) {
            host_x86_AND32_REG_IMM(block, REG_ESI, 0x0000ffff);
        }
    }
    if (REG_IS_DQ(src_size)) {
        codegen_mem_dq(block, uop, src_reg, seg_reg, addr_reg, 1, writelookup2, codegen_mem_store_quad);
        return 0;
    }
    if (REG_IS_B(src_size)) {
        host_x86_MOV8_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_B, codegen_mem_store_byte);
    } else if (REG_IS_W(src_size)) {
        host_x86_MOV16_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_W, codegen_mem_store_word);
    } else if (REG_IS_L(src_size)) {
        host_x86_MOV32_REG_REG(block, REG_ECX, src_reg);
        codegen_mem_store(block, uop, MEM_L, codegen_mem_store_long);
    } else if (REG_IS_Q(src_size)) {
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg);
        codegen_mem_store(block, uop, MEM_Q, codegen_mem_store_quad);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MEM_STORE_REG - %02x\n", uop->src_reg_b_real);
#    endif

    return 0;
}
static int
codegen_JMP_DEST(codeblock_t *block, uop_t *uop)
{
    uop->p = host_x86_JMP_long(block);

    return 0;
}

static int
codegen_MEM_STORE_SINGLE(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_c_real);

#    ifdef RECOMPILER_DEBUG
    int src_size = IREG_GET_SIZE(uop->src_reg_c_real);
    if (!REG_IS_D(src_size))
        fatal("MEM_STORE_SINGLE - %02x\n", uop->src_reg_b_real);
#    endif
    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data)
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
    host_x86_CVTSD2SS_XREG_XREG(block, REG_XMM_TEMP, src_reg);
    codegen_mem_store(block, uop, MEM_SINGLE, codegen_mem_store_single);

    return 0;
}
static int
codegen_MEM_STORE_DOUBLE(codeblock_t *block, uop_t *uop)
{
    int seg_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int addr_reg = HOST_REG_GET(uop->src_reg_b_real);
    int src_reg  = HOST_REG_GET(uop->src_reg_c_real);

#    ifdef RECOMPILER_DEBUG
    int src_size = IREG_GET_SIZE(uop->src_reg_c_real);
    if (!REG_IS_D(src_size))
        fatal("MEM_STORE_DOUBLE - %02x\n", uop->src_reg_b_real);
#    endif
    host_x86_LEA_REG_REG(block, REG_ESI, seg_reg, addr_reg);
    if (uop->imm_data)
        host_x86_ADD32_REG_IMM(block, REG_ESI, uop->imm_data);
    host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg);
    codegen_mem_store(block, uop, MEM_DOUBLE, codegen_mem_store_double);

    return 0;
}

static int
codegen_MOV(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
    } else if (REG_IS_D(dest_size) && REG_IS_D(src_size)) {
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg);
    } else if (REG_IS_Q(dest_size) && REG_IS_Q(src_size)) {
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg);
    } else if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size)) {
        host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOV %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_MOVSS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_MOVSS_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOVSS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_MOVSD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && (REG_IS_DQ(src_size_b) || REG_IS_Q(src_size_b))) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_MOVSD_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_DQ(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) {
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_Q(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b)) {
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOVSD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

void
codegen_backend_sse_mxcsr_begin(codeblock_t *block)
{
    /* Accumulate sticky exceptions across a straight-line native SSE run.
       Clear host status on entry and keep host exceptions masked. */
    host_x86_MOV32_REG_ABS(block, REG_ECX, &cpu_state.mxcsr);
    /* Only all-masked blocks reach this path, so FTZ is effective here. */
    host_x86_AND32_REG_IMM(block, REG_ECX, 0xe040);
    host_x86_OR32_REG_IMM(block, REG_ECX, 0x1f80);
    host_x86_MOV32_BASE_OFFSET_REG(block, REG_RSP, STACK_TEMP_MXCSR, REG_ECX);
    host_x86_LDMXCSR_BASE_OFFSET(block, REG_RSP, STACK_TEMP_MXCSR);
}

void
codegen_backend_sse_mxcsr_end(codeblock_t *block)
{
    /* Guest exception flags are sticky; restoring the host MXCSR first
       would discard the flags raised by the native operation. */
    host_x86_STMXCSR_BASE_OFFSET(block, REG_RSP, STACK_TEMP_MXCSR);
    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, STACK_TEMP_MXCSR);
    host_x86_AND32_REG_IMM(block, REG_ECX, 0x3f);
    host_x86_MOV32_REG_ABS(block, REG_ESI, &cpu_state.mxcsr);
    host_x86_OR32_REG_REG(block, REG_ECX, REG_ESI);
    host_x86_MOV32_ABS_REG(block, &cpu_state.mxcsr, REG_ECX);
    host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
}

static int
codegen_ADDPS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_ADDPS_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ADDPS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_ADDPD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_ADDPD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ADDPD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_ADDSS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_ADDSS_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ADDSS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_ADDSD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && (REG_IS_DQ(src_size_b) || REG_IS_Q(src_size_b))) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_ADDSD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ADDSD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_SSE_ARITH(codeblock_t *block, uop_t *uop, void (*host_func)(codeblock_t *, int, int), UNUSED(const char *name), int allow_q_src)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && (REG_IS_DQ(src_size_b) || (allow_q_src && REG_IS_Q(src_size_b)))) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_func(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("%s %02x %02x %02x\n", name, uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_MULPS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_MULPS_XREG_XREG, "MULPS", 0);
}

static int
codegen_MULPD(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_MULPD_XREG_XREG, "MULPD", 0);
}

static int
codegen_MULSS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_MULSS_XREG_XREG, "MULSS", 0);
}

static int
codegen_MULSD(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_MULSD_XREG_XREG, "MULSD", 1);
}

static int
codegen_SUBPS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_SUBPS_XREG_XREG, "SUBPS", 0);
}

static int
codegen_SUBPD(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_SUBPD_XREG_XREG, "SUBPD", 0);
}

static int
codegen_SUBSS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_SUBSS_XREG_XREG, "SUBSS", 0);
}

static int
codegen_SUBSD(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_SUBSD_XREG_XREG, "SUBSD", 1);
}

static int
codegen_DIVPS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_DIVPS_XREG_XREG, "DIVPS", 0);
}

static int
codegen_DIVPD(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_DIVPD_XREG_XREG, "DIVPD", 0);
}

static int
codegen_DIVSS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_DIVSS_XREG_XREG, "DIVSS", 0);
}

static int
codegen_DIVSD(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_DIVSD_XREG_XREG, "DIVSD", 1);
}

static int
codegen_SHUFPS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_SHUFPS_XREG_XREG(block, dest_reg, src_reg_b, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SHUFPS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_MOVHLPS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && (REG_IS_DQ(src_size_b) || REG_IS_Q(src_size_b))) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_MOVHLPS_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOVHLPS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_MOVLHPS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && (REG_IS_DQ(src_size_b) || REG_IS_Q(src_size_b))) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_MOVLHPS_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOVLHPS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_MOVMSKPS(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_DQ(src_size))
        host_x86_MOVMSKPS_REG_XREG(block, dest_reg, src_reg);
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOVMSKPS %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_MINPS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_MINPS_XREG_XREG, "MINPS", 0);
}

static int
codegen_MINSS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_MINSS_XREG_XREG, "MINSS", 0);
}

static int
codegen_MAXPS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_MAXPS_XREG_XREG, "MAXPS", 0);
}

static int
codegen_MAXSS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_MAXSS_XREG_XREG, "MAXSS", 0);
}

static int
codegen_SQRTPS(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size))
    {
        host_x86_SQRTPS_XREG_XREG(block, dest_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SQRTPS %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_SQRTSS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_SQRTSS_XREG_XREG, "SQRTSS", 0);
}

static int
codegen_RCPPS(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size))
    {
        host_x86_RCPPS_XREG_XREG(block, dest_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("RCPPS %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_RCPSS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_RCPSS_XREG_XREG, "RCPSS", 0);
}

static int
codegen_RSQRTPS(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size))
    {
        host_x86_RSQRTPS_XREG_XREG(block, dest_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("RSQRTPS %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_RSQRTSS(codeblock_t *block, uop_t *uop)
{
    return codegen_SSE_ARITH(block, uop, host_x86_RSQRTSS_XREG_XREG, "RSQRTSS", 0);
}

static int
codegen_CVTSI2SS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_CVTSI2SS_XREG_REG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CVTSI2SS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_CVTTSS2SI(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_DQ(src_size)) {
        host_x86_CVTTSS2SI_REG_XREG(block, dest_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CVTTSS2SI %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_CVTSS2SI(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_DQ(src_size)) {
        host_x86_CVTSS2SI_REG_XREG(block, dest_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CVTSS2SI %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_CVTPI2PS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_Q(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_CVTDQ2PS_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
        host_x86_MOVSD_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CVTPI2PS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_CVTTPS2PI(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && (REG_IS_DQ(src_size) || REG_IS_Q(src_size))) {
        /* Only the low two lanes belong to CVTTPS2PI. Upper lanes must not
           raise exceptions in the four-lane host conversion. */
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg);
        host_x86_CVTTPS2DQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CVTTPS2PI %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_CVTPS2PI(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && (REG_IS_DQ(src_size) || REG_IS_Q(src_size))) {
        /* Discard inactive lanes before collecting host exception status. */
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg);
        host_x86_CVTPS2DQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CVTPS2PI %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_CMPPS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_CMPPS_XREG_XREG(block, dest_reg, src_reg_b, uop->imm_data & 7);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMPPS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_CMPSS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOVDQA_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_CMPSS_XREG_XREG(block, dest_reg, src_reg_b, uop->imm_data & 7);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("CMPSS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_COMISS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int flags_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_b_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_c_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int flags_size = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_b_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_c_real);

    if (REG_IS_W(dest_size) && REG_IS_W(flags_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b)) {
        host_x86_MOV32_REG_REG(block, REG_ECX, flags_reg);
        host_x86_AND32_REG_IMM(block, REG_ECX, 0xffff & ~(V_FLAG | A_FLAG | N_FLAG | Z_FLAG | P_FLAG | C_FLAG));
        host_x86_PUSH(block, REG_RAX);
        if (uop->imm_data)
            host_x86_COMISS_XREG_XREG(block, src_reg_a, src_reg_b);
        else
            host_x86_UCOMISS_XREG_XREG(block, src_reg_a, src_reg_b);
        host_x86_LAHF(block);
        host_x86_SHR32_IMM(block, REG_EAX, 8);
        host_x86_AND32_REG_IMM(block, REG_EAX, C_FLAG | P_FLAG | Z_FLAG);
        host_x86_OR32_REG_REG(block, REG_ECX, REG_EAX);
        host_x86_POP(block, REG_RAX);
        host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("COMISS %02x %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real, uop->src_reg_c_real);
#    endif
    return 0;
}

static int
codegen_MOV_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_L(dest_size)) {
        host_x86_MOV32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size)) {
        host_x86_MOV16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size)) {
        host_x86_MOV8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOV_IMM %02x\n", uop->dest_reg_a_real);
#    endif
    return 0;
}
static int
codegen_MOV_PTR(codeblock_t *block, uop_t *uop)
{
    int dest_reg = HOST_REG_GET(uop->dest_reg_a_real);

    host_x86_MOV64_REG_IMM(block, dest_reg, (uint64_t) uop->p);
    return 0;
}
static int
codegen_MOV_REG_PTR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_L(dest_size)) {
        host_x86_MOV32_REG_ABS(block, dest_reg, uop->p);
    } else if (REG_IS_Q(dest_size) || REG_IS_D(dest_size)) {
        /*The pointer may be anywhere in the address space*/
        host_x86_MOV64_REG_IMM(block, REG_ECX, (uint64_t) (uintptr_t) uop->p);
        host_x86_MOVQ_XREG_BASE_OFFSET(block, dest_reg, REG_ECX, 0);
    } else
        fatal("MOV_REG_PTR %02x\n", uop->dest_reg_a_real);

    return 0;
}
static int
codegen_MOVZX_REG_PTR_8(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_L(dest_size)) {
        host_x86_MOVZX_REG_ABS_32_8(block, dest_reg, uop->p);
    } else if (REG_IS_W(dest_size)) {
        host_x86_MOVZX_REG_ABS_16_8(block, dest_reg, uop->p);
    } else if (REG_IS_B(dest_size)) {
        host_x86_MOV8_REG_ABS(block, dest_reg, uop->p);
    } else
        fatal("MOVZX_REG_PTR_8 %02x\n", uop->dest_reg_a_real);

    return 0;
}
static int
codegen_MOVZX_REG_PTR_16(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if (REG_IS_L(dest_size)) {
        host_x86_MOVZX_REG_ABS_32_16(block, dest_reg, uop->p);
    } else if (REG_IS_W(dest_size)) {
        host_x86_MOV16_REG_ABS(block, dest_reg, uop->p);
    } else
        fatal("MOVZX_REG_PTR_16 %02x\n", uop->dest_reg_a_real);

    return 0;
}
static int
codegen_MOVSX(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_W(src_size)) {
        host_x86_MOVSX_REG_32_16(block, dest_reg, src_reg);
    } else if (REG_IS_BH(src_size) && (dest_reg & 8)) {
        /*AH-BH and R8-R15 can't share an instruction: extend into ECX*/
        host_x86_MOVSX_REG_32_8(block, REG_ECX, src_reg);
        if (REG_IS_L(dest_size))
            host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);
        else
            host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
    } else if (REG_IS_L(dest_size) && REG_IS_B(src_size)) {
        host_x86_MOVSX_REG_32_8(block, dest_reg, src_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_B(src_size)) {
        host_x86_MOVSX_REG_16_8(block, dest_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOVSX %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_MOVZX(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if ((REG_IS_Q(dest_size) || REG_IS_DQ(dest_size)) && REG_IS_L(src_size)) {
        host_x86_MOVD_XREG_REG(block, dest_reg, src_reg);
    } else if (REG_IS_L(dest_size) && (REG_IS_Q(src_size) || REG_IS_DQ(src_size))) {
        host_x86_MOVD_REG_XREG(block, dest_reg, src_reg);
    } else if (REG_IS_L(dest_size) && REG_IS_W(src_size)) {
        host_x86_MOVZX_REG_32_16(block, dest_reg, src_reg);
    } else if (REG_IS_BH(src_size) && (dest_reg & 8)) {
        /*AH-BH and R8-R15 can't share an instruction: extend into ECX*/
        host_x86_MOVZX_REG_32_8(block, REG_ECX, src_reg);
        if (REG_IS_L(dest_size))
            host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);
        else
            host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
    } else if (REG_IS_L(dest_size) && REG_IS_B(src_size)) {
        host_x86_MOVZX_REG_32_8(block, dest_reg, src_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_B(src_size)) {
        host_x86_MOVZX_REG_16_8(block, dest_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOVZX %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_MOV_DOUBLE_INT(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_D(dest_size) && REG_IS_L(src_size)) {
        host_x86_CVTSI2SD_XREG_REG(block, dest_reg, src_reg);
    } else if (REG_IS_D(dest_size) && REG_IS_W(src_size)) {
        host_x86_MOVSX_REG_32_16(block, REG_ECX, src_reg);
        host_x86_CVTSI2SD_XREG_REG(block, dest_reg, REG_ECX);
    } else if (REG_IS_D(dest_size) && REG_IS_Q(src_size)) {
        host_x86_MOVQ_REG_XREG(block, REG_RCX, src_reg);
        host_x86_CVTSI2SD_XREG_REG64(block, dest_reg, REG_RCX);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOV_DOUBLE_INT %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_MOV_INT_DOUBLE(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_D(src_size)) {
        host_x86_LDMXCSR(block, &cpu_state.new_fp_control);
        host_x86_CVTSD2SI_REG_XREG(block, dest_reg, src_reg);
        host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
    } else if (REG_IS_W(dest_size) && REG_IS_D(src_size)) {
        host_x86_LDMXCSR(block, &cpu_state.new_fp_control);
        host_x86_CVTSD2SI_REG_XREG(block, REG_ECX, src_reg);
        /* A word overflow stores the integer indefinite, not the low
           word of the dword conversion. This also catches NaN/infinity. */
        host_x86_MOVSX_REG_32_16(block, REG_ESI, REG_ECX);
        host_x86_CMP32_REG_REG(block, REG_ESI, REG_ECX);
        uint32_t *in_range = host_x86_JZ_long(block);
        host_x86_MOV32_REG_IMM(block, REG_ECX, 0x8000);
        *in_range = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) in_range) - 4;
        host_x86_MOV16_REG_REG(block, dest_reg, REG_ECX);
        host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOV_INT_DOUBLE %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_MOV_INT_DOUBLE_64(codeblock_t *block, uop_t *uop)
{
    int dest_reg    = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg     = HOST_REG_GET(uop->src_reg_a_real);
    int src_64_reg  = HOST_REG_GET(uop->src_reg_b_real);
    int tag_reg     = HOST_REG_GET(uop->src_reg_c_real);
    int dest_size   = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size    = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_64_size = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_D(src_size) && REG_IS_Q(src_64_size)) {
        uint32_t *branch_offset;

        /*If TAG_UINT64 is set then the source is MM[]. Otherwise it is a double in ST()*/
        host_x86_MOVQ_XREG_XREG(block, dest_reg, src_64_reg);
        host_x86_TEST8_REG(block, tag_reg, tag_reg);
        branch_offset = host_x86_JS_long(block);

        host_x86_LDMXCSR(block, &cpu_state.new_fp_control);
        host_x86_CVTSD2SI_REG64_XREG(block, REG_RCX, src_reg);
        host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
        host_x86_MOVQ_XREG_REG(block, dest_reg, REG_RCX);

        *branch_offset = (uint32_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 4;
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("MOV_INT_DOUBLE_64 %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_NOP(UNUSED(codeblock_t *block), UNUSED(uop_t *uop))
{
    return 0;
}

static int
codegen_OR(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    /*dest == src_b: moving src_a into dest first would clobber src_b; the
      operation commutes, so take the sources the other way round*/
    if ((dest_reg == src_reg_b) && (dest_reg != src_reg_a)) {
        int t     = src_reg_a;
        src_reg_a = src_reg_b;
        src_reg_b = t;
    }
    
    if (((REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) ||
        (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_POR_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
        host_x86_OR32_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
        host_x86_OR16_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
        host_x86_OR8_REG_REG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("OR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_OR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_x86_OR32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_x86_OR16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_x86_OR8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("OR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_PACKSSWB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PACKSSWB_XREG_XREG(block, dest_reg, src_reg_b);
    }
    if ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PACKSSWB_XREG_XREG_SSE(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PACKSSWB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PACKSSDW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PACKSSDW_XREG_XREG(block, dest_reg, src_reg_b);
    }
    if ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PACKSSDW_XREG_XREG_SSE(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PACKSSDW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PACKUSWB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PACKUSWB_XREG_XREG(block, dest_reg, src_reg_b);
    }
    if ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PACKUSWB_XREG_XREG_SSE(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PACKUSWB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_PADDB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDQ(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDQ_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDSB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDSB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDSW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDSW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDUSB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDUSB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDUSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PADDUSW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PADDUSW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PADDUSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_PCMPEQB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPEQB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPEQB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PCMPEQW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPEQW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPEQW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PCMPEQD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPEQD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPEQD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PCMPGTB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPGTB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPGTB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PCMPGTW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPGTW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPGTW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PCMPGTD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PCMPGTD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PCMPGTD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_PF2ID(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a)) {
        host_x86_LDMXCSR(block, &cpu_state.trunc_fp_control);
        host_x86_CVTPS2DQ_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_LDMXCSR(block, &cpu_state.old_fp_control);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PF2ID %02x %02x\n", uop->dest_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PFADD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_ADDPS_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFADD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFCMPEQ(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_CMPPS_XREG_XREG(block, dest_reg, src_reg_b, CMPPS_EQ);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFCMPEQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFCMPGE(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_CMPPS_XREG_XREG(block, dest_reg, src_reg_b, CMPPS_NLT);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFCMPGE %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFCMPGT(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_CMPPS_XREG_XREG(block, dest_reg, src_reg_b, CMPPS_NLE);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFCMPGT %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFMAX(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        /*MAXPS returns its second operand on a tie or a NaN; the interpreter
          keeps the destination unless the source is greater, which is MAXPS
          with the operands the other way round*/
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MAXPS_XREG_XREG(block, REG_XMM_TEMP, dest_reg);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFMAX %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFMIN(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        /*As PFMAX: the destination unless the source is smaller*/
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MINPS_XREG_XREG(block, REG_XMM_TEMP, dest_reg);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFMIN %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFMUL(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_MULPS_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFMUL %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PFRCP(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a)) {
        /*TODO: This could be improved (use RCPSS + iteration)*/
        host_x86_MOV32_REG_IMM(block, REG_ECX, 1);
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_CVTSI2SS_XREG_REG(block, dest_reg, REG_ECX);
        host_x86_DIVSS_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
        host_x86_UNPCKLPS_XREG_XREG(block, dest_reg, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFRCP %02x %02x\n", uop->dest_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PFRSQRT(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a)) {
        /*1.0 / sqrt(x) in double precision, rounded to single once, as the
          interpreter does it; in single precision it rounds twice*/
        host_x86_CVTSS2SD_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_SQRTSD_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
        host_x86_MOV32_REG_IMM(block, REG_ECX, 1);
        host_x86_CVTSI2SD_XREG_REG(block, dest_reg, REG_ECX);
        host_x86_DIVSD_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
        host_x86_CVTSD2SS_XREG_XREG(block, dest_reg, dest_reg);
        host_x86_UNPCKLPS_XREG_XREG(block, dest_reg, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFRSQRT %02x %02x\n", uop->dest_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PFSUB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_SUBPS_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) {
        host_x86_MOVQ_XREG_XREG(block, REG_XMM_TEMP, src_reg_a);
        host_x86_SUBPS_XREG_XREG(block, REG_XMM_TEMP, src_reg_b);
        host_x86_MOVQ_XREG_XREG(block, dest_reg, REG_XMM_TEMP);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PFSUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PI2FD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a)) {
        host_x86_CVTDQ2PS_XREG_XREG(block, dest_reg, src_reg_a);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PI2FD %02x %02x\n", uop->dest_reg_a_real);
#    endif
    return 0;
}

static int
codegen_PMADDWD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PMADDWD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PMULHW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PMULHW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PMULHW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PMULHW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PMULLW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PMULLW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PMULLW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_MMX_BINARY(codeblock_t *block, uop_t *uop, void (*host_func)(codeblock_t *, int, int), UNUSED(const char *name))
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) {
        if (dest_reg == src_reg_a)
            host_func(block, dest_reg, src_reg_b);
        else if (dest_reg == src_reg_b)
            host_func(block, dest_reg, src_reg_a);
        else {
            host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg_a);
            host_func(block, dest_reg, src_reg_b);
        }
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("%s %02x %02x %02x\n", name, uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_PAVGUSB(codeblock_t *block, uop_t *uop)
{
    return codegen_MMX_BINARY(block, uop, host_x86_PAVGB_XREG_XREG, "PAVGUSB");
}

static int
codegen_PAVGW(codeblock_t *block, uop_t *uop)
{
    return codegen_MMX_BINARY(block, uop, host_x86_PAVGW_XREG_XREG, "PAVGW");
}

static int
codegen_PMINUB(codeblock_t *block, uop_t *uop)
{
    return codegen_MMX_BINARY(block, uop, host_x86_PMINUB_XREG_XREG, "PMINUB");
}

static int
codegen_PMAXUB(codeblock_t *block, uop_t *uop)
{
    return codegen_MMX_BINARY(block, uop, host_x86_PMAXUB_XREG_XREG, "PMAXUB");
}

static int
codegen_PMULHUW(codeblock_t *block, uop_t *uop)
{
    return codegen_MMX_BINARY(block, uop, host_x86_PMULHUW_XREG_XREG, "PMULHUW");
}

static int
codegen_PMINSW(codeblock_t *block, uop_t *uop)
{
    return codegen_MMX_BINARY(block, uop, host_x86_PMINSW_XREG_XREG, "PMINSW");
}

static int
codegen_PMAXSW(codeblock_t *block, uop_t *uop)
{
    return codegen_MMX_BINARY(block, uop, host_x86_PMAXSW_XREG_XREG, "PMAXSW");
}

static int
codegen_PSADBW(codeblock_t *block, uop_t *uop)
{
    return codegen_MMX_BINARY(block, uop, host_x86_PSADBW_XREG_XREG, "PSADBW");
}

static int
codegen_PSHUFW(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size))
        host_x86_PSHUFW_XREG_XREG(block, dest_reg, src_reg, uop->imm_data);
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSHUFW %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_PINSRW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOVQ_XREG_XREG(block, dest_reg, src_reg_a);
        host_x86_PINSRW_XREG_REG(block, dest_reg, src_reg_b, uop->imm_data & 3);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PINSRW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_PEXTRW(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_Q(src_size))
        host_x86_PEXTRW_REG_XREG(block, dest_reg, src_reg, uop->imm_data & 3);
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PEXTRW %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_PMOVMSKB(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_Q(src_size)) {
        host_x86_PMOVMSKB_REG_XREG(block, dest_reg, src_reg);
        host_x86_AND32_REG_IMM(block, dest_reg, 0xff);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PMOVMSKB %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_PSLLW_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if ((REG_IS_Q(dest_size)) || (REG_IS_DQ(dest_size))) {
        host_x86_PSLLW_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSLLW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSLLD_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if ((REG_IS_Q(dest_size)) || (REG_IS_DQ(dest_size))) {
        host_x86_PSLLD_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSLLD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSLLQ_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if ((REG_IS_Q(dest_size)) || (REG_IS_DQ(dest_size))) {
        host_x86_PSLLQ_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSLLQ_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSRAW_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if ((REG_IS_Q(dest_size)) || (REG_IS_DQ(dest_size))) {
        host_x86_PSRAW_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSRAW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSRAD_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if ((REG_IS_Q(dest_size)) || (REG_IS_DQ(dest_size))) {
        host_x86_PSRAD_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSRAD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSRLW_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if ((REG_IS_Q(dest_size)) || (REG_IS_DQ(dest_size))) {
        host_x86_PSRLW_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSRLW_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSRLD_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if ((REG_IS_Q(dest_size)) || (REG_IS_DQ(dest_size))) {
        host_x86_PSRLD_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSRLD_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_PSRLQ_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);

    if ((REG_IS_Q(dest_size)) || (REG_IS_DQ(dest_size))) {
        host_x86_PSRLQ_XREG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSRLQ_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_PSUBB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBQ(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBQ_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBSB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBSB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBSW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBSW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBUSB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBUSB_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBUSB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PSUBUSW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) || ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PSUBUSW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PSUBUSW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_PUNPCKHBW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKHBW_XREG_XREG(block, dest_reg, src_reg_b);
    }
    if ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKHBW_XREG_XREG_SSE(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKHBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PUNPCKHWD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKHWD_XREG_XREG(block, dest_reg, src_reg_b);
    }
    if ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKHWD_XREG_XREG_SSE(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKHWD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PUNPCKHDQ(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if ((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKHDQ_XREG_XREG(block, dest_reg, src_reg_b);
    }
    if ((REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b)) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKHDQ_XREG_XREG_SSE(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKHDQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PUNPCKLBW(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) ||
        (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKLBW_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKLBW %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PUNPCKLWD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) ||
        (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKLWD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKLWD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_PUNPCKLDQ(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (((REG_IS_Q(dest_size) && REG_IS_Q(src_size_b)) ||
         (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PUNPCKLDQ_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("PUNPCKLDQ %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_UNPCKLPS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_UNPCKLPS_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("UNPCKLPS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_UNPCKLPD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_UNPCKLPD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("UNPCKLPD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_UNPCKHPS(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_UNPCKHPS_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("UNPCKHPS %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_UNPCKHPD(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_b) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_UNPCKHPD_XREG_XREG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("UNPCKHPD %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}

static int
codegen_ROL(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL32_CL(block, dest_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL16_CL(block, dest_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL8_CL(block, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ROL %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_ROL_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL32_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL16_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_ROL8_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ROL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_ROR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR32_CL(block, dest_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR16_CL(block, dest_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR8_CL(block, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ROR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_ROR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR32_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR16_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_ROR8_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("ROR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_SAR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR32_CL(block, dest_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR16_CL(block, dest_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR8_CL(block, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SAR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_SAR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR32_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR16_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SAR8_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SAR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_SHL(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL32_CL(block, dest_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL16_CL(block, dest_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL8_CL(block, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SHL %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_SHL_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL32_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL16_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SHL8_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SHL_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_SHR(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int shift_reg = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    host_x86_MOV32_REG_REG(block, REG_ECX, shift_reg);
    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR32_CL(block, dest_reg);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR16_CL(block, dest_reg);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR8_CL(block, dest_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SHR %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}
static int
codegen_SHR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR32_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR16_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (uop->dest_reg_a_real != uop->src_reg_a_real)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SHR8_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SHR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_STORE_PTR_IMM(codeblock_t *block, uop_t *uop)
{
#    ifdef RECOMPILER_DEBUG
    if (((uintptr_t) uop->p) >> 32)
        fatal("STORE_PTR_IMM 64-bit addr\n");
#    endif
    host_x86_MOV32_ABS_IMM(block, uop->p, uop->imm_data);
    return 0;
}
static int
codegen_STORE_PTR_IMM_8(codeblock_t *block, uop_t *uop)
{
#    ifdef RECOMPILER_DEBUG
    if (((uintptr_t) uop->p) >> 32)
        fatal("STORE_PTR_IMM_8 64-bit addr\n");
#    endif
    host_x86_MOV8_ABS_IMM(block, uop->p, uop->imm_data);
    return 0;
}

static int
codegen_SUB(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    /*dest == src_b: moving src_a into dest first would clobber src_b, so
      src_b goes to the scratch register first*/
    if ((dest_reg == src_reg_b) && (dest_reg != src_reg_a)) {
        if (REG_IS_L(src_size_b))
            host_x86_MOV32_REG_REG(block, REG_ECX, src_reg_b);
        else if (REG_IS_W(src_size_b))
            host_x86_MOV16_REG_REG(block, REG_ECX, src_reg_b);
        else
            host_x86_MOV8_REG_REG(block, REG_ECX, src_reg_b);
        src_reg_b = REG_ECX;
    }

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
        host_x86_SUB32_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
        host_x86_SUB16_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
        host_x86_SUB8_REG_REG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SUB %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_SUB_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
        host_x86_SUB32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg);
        host_x86_SUB16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        if (dest_reg != src_reg)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg);
        host_x86_SUB8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("SUB_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

static int
codegen_TEST_JNS_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_TEST32_REG(block, src_reg, src_reg);
    } else if (REG_IS_W(src_size)) {
        host_x86_TEST16_REG(block, src_reg, src_reg);
    } else if (REG_IS_B(src_size)) {
        host_x86_TEST8_REG(block, src_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("TEST_JNS_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JNS_long(block);

    return 0;
}
static int
codegen_TEST_JS_DEST(codeblock_t *block, uop_t *uop)
{
    int src_reg  = HOST_REG_GET(uop->src_reg_a_real);
    int src_size = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_TEST32_REG(block, src_reg, src_reg);
    } else if (REG_IS_W(src_size)) {
        host_x86_TEST16_REG(block, src_reg, src_reg);
    } else if (REG_IS_B(src_size)) {
        host_x86_TEST8_REG(block, src_reg, src_reg);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("TEST_JS_DEST %02x\n", uop->src_reg_a_real);
#    endif
    uop->p = host_x86_JS_long(block);

    return 0;
}

/*The condition goes through CL, which the allocator never hands out, so the
  destination can be any host register*/
static int
codegen_CMP_SET(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        host_x86_CMP32_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        host_x86_CMP16_REG_REG(block, src_reg_a, src_reg_b);
    } else if (REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        host_x86_CMP8_REG_REG(block, src_reg_a, src_reg_b);
    } else
        fatal("CMP_SET %02x %02x\n", uop->src_reg_a_real, uop->src_reg_b_real);
    if (!REG_IS_L(dest_size))
        fatal("CMP_SET dest %02x\n", uop->dest_reg_a_real);

    host_x86_SETcc_REG(block, uop->imm_data, REG_ECX);
    host_x86_MOVZX_REG_32_8(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_TEST_SET(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(src_size)) {
        host_x86_TEST32_REG(block, src_reg, src_reg);
    } else if (REG_IS_W(src_size)) {
        host_x86_TEST16_REG(block, src_reg, src_reg);
    } else if (REG_IS_B(src_size)) {
        host_x86_TEST8_REG(block, src_reg, src_reg);
    } else
        fatal("TEST_SET %02x\n", uop->src_reg_a_real);
    if (!REG_IS_L(dest_size))
        fatal("TEST_SET dest %02x\n", uop->dest_reg_a_real);

    host_x86_SETcc_REG(block, uop->imm_data, REG_ECX);
    host_x86_MOVZX_REG_32_8(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_BSWAP(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg   = HOST_REG_GET(uop->src_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (!REG_IS_L(dest_size) || !REG_IS_L(src_size))
        fatal("BSWAP %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);

    if (dest_reg != src_reg)
        host_x86_MOV32_REG_REG(block, dest_reg, src_reg);
    host_x86_BSWAP32_REG(block, dest_reg);

    return 0;
}
static int
codegen_IMUL(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg == src_reg_b) {
            host_x86_IMUL32_REG_REG(block, dest_reg, src_reg_a);
        } else {
            if (dest_reg != src_reg_a)
                host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
            host_x86_IMUL32_REG_REG(block, dest_reg, src_reg_b);
        }
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg == src_reg_b) {
            host_x86_IMUL16_REG_REG(block, dest_reg, src_reg_a);
        } else {
            if (dest_reg != src_reg_a)
                host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
            host_x86_IMUL16_REG_REG(block, dest_reg, src_reg_b);
        }
    }

    return 0;
}
static int
codegen_IMUL_OV(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (!REG_IS_L(dest_size) || !REG_IS_L(src_size_a) || !REG_IS_L(src_size_b))
        fatal("IMUL_OV %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    /*The product is thrown away; OF says whether it fit*/
    host_x86_MOV32_REG_REG(block, REG_ECX, src_reg_a);
    host_x86_IMUL32_REG_REG(block, REG_ECX, src_reg_b);
    host_x86_SETcc_REG(block, UOP_CC_O, REG_ECX);
    host_x86_MOVZX_REG_32_8(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_MUL_HI_common(codeblock_t *block, uop_t *uop, int is_signed)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (!REG_IS_L(dest_size) || !REG_IS_L(src_size_a) || !REG_IS_L(src_size_b))
        fatal("MUL_HI %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    /*Both operands widened to 64 bits in RCX and RSI, which the allocator
      never hands out; the low 64 bits of that product are the whole product*/
    if (is_signed) {
        host_x86_MOVSXD_REG_REG(block, REG_RCX, src_reg_a);
        host_x86_MOVSXD_REG_REG(block, REG_RSI, src_reg_b);
    } else {
        host_x86_MOV32_REG_REG(block, REG_ECX, src_reg_a);
        host_x86_MOV32_REG_REG(block, REG_ESI, src_reg_b);
    }
    host_x86_IMUL64_REG_REG(block, REG_RCX, REG_RSI);
    host_x86_SHR64_IMM(block, REG_RCX, 32);
    host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_MUL_HI(codeblock_t *block, uop_t *uop)
{
    return codegen_MUL_HI_common(block, uop, 0);
}
static int
codegen_IMUL_HI(codeblock_t *block, uop_t *uop)
{
    return codegen_MUL_HI_common(block, uop, 1);
}


static int
codegen_UMUL(codeblock_t *block, uop_t *uop)
{
    /* Signedness only affects the high half; guest flags are handled by IR. */
    return codegen_IMUL(block, uop);
}

static int
codegen_UMUL_HI(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        int mul_reg = src_reg_b;

        host_x86_PUSH(block, REG_RAX);
        host_x86_PUSH(block, REG_RDX);
        if ((src_reg_b == REG_EAX) && (src_reg_a != REG_EAX)) {
            mul_reg = (src_reg_a == REG_ECX) ? REG_EDX : REG_ECX;
            host_x86_MOV32_REG_REG(block, mul_reg, REG_EAX);
        }
        if (src_reg_a != REG_EAX)
            host_x86_MOV32_REG_REG(block, REG_EAX, src_reg_a);
        host_x86_MUL32_REG(block, mul_reg);

        if (dest_reg == REG_EDX) {
            host_x86_ADD64_REG_IMM(block, REG_RSP, 8);
            host_x86_POP(block, REG_RAX);
        } else if (dest_reg == REG_EAX) {
            host_x86_MOV32_REG_REG(block, REG_EAX, REG_EDX);
            host_x86_POP(block, REG_RDX);
            host_x86_ADD64_REG_IMM(block, REG_RSP, 8);
        } else {
            host_x86_MOV32_REG_REG(block, dest_reg, REG_EDX);
            host_x86_POP(block, REG_RDX);
            host_x86_POP(block, REG_RAX);
        }
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("UMUL_HI size mismatch: dest_size=%x, src_size_a=%x, src_size_b=%x\n", dest_size, src_size_a, src_size_b);
#    endif
    return 0;
}

static int
codegen_BS_common(codeblock_t *block, uop_t *uop, int reverse)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    if (!REG_IS_L(dest_size) || !REG_IS_L(src_size_a) || !REG_IS_L(src_size_b))
        fatal("BSF/BSR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);

    /*The scan's result when the source is zero is not relied on: the old
      value is kept unless ZF says a bit was found*/
    if (reverse)
        host_x86_BSR32_REG_REG(block, REG_ESI, src_reg_a);
    else
        host_x86_BSF32_REG_REG(block, REG_ESI, src_reg_a);
    host_x86_MOV32_REG_REG(block, REG_ECX, src_reg_b);
    host_x86_CMOVNZ32_REG_REG(block, REG_ECX, REG_ESI);
    host_x86_MOV32_REG_REG(block, dest_reg, REG_ECX);

    return 0;
}
static int
codegen_BSF(codeblock_t *block, uop_t *uop)
{
    return codegen_BS_common(block, uop, 0);
}
static int
codegen_BSR(codeblock_t *block, uop_t *uop)
{
    return codegen_BS_common(block, uop, 1);
}

static int
codegen_XOR(codeblock_t *block, uop_t *uop)
{
    int dest_reg   = HOST_REG_GET(uop->dest_reg_a_real);
    int src_reg_a  = HOST_REG_GET(uop->src_reg_a_real);
    int src_reg_b  = HOST_REG_GET(uop->src_reg_b_real);
    int dest_size  = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size_a = IREG_GET_SIZE(uop->src_reg_a_real);
    int src_size_b = IREG_GET_SIZE(uop->src_reg_b_real);

    /*dest == src_b: moving src_a into dest first would clobber src_b; the
      operation commutes, so take the sources the other way round*/
    if ((dest_reg == src_reg_b) && (dest_reg != src_reg_a)) {
        int t     = src_reg_a;
        src_reg_a = src_reg_b;
        src_reg_b = t;
    }

    if (((REG_IS_Q(dest_size) && REG_IS_Q(src_size_a) && REG_IS_Q(src_size_b)) ||
        (REG_IS_DQ(dest_size) && REG_IS_DQ(src_size_a) && REG_IS_DQ(src_size_b))) && uop->dest_reg_a_real == uop->src_reg_a_real) {
        host_x86_PXOR_XREG_XREG(block, dest_reg, src_reg_b);
    } else if (REG_IS_L(dest_size) && REG_IS_L(src_size_a) && REG_IS_L(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV32_REG_REG(block, dest_reg, src_reg_a);
        host_x86_XOR32_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size_a) && REG_IS_W(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV16_REG_REG(block, dest_reg, src_reg_a);
        host_x86_XOR16_REG_REG(block, dest_reg, src_reg_b);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size_a) && REG_IS_B(src_size_b)) {
        if (dest_reg != src_reg_a)
            host_x86_MOV8_REG_REG(block, dest_reg, src_reg_a);
        host_x86_XOR8_REG_REG(block, dest_reg, src_reg_b);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("XOR %02x %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real, uop->src_reg_b_real);
#    endif
    return 0;
}
static int
codegen_XOR_IMM(codeblock_t *block, uop_t *uop)
{
    int dest_reg  = HOST_REG_GET(uop->dest_reg_a_real);
    int dest_size = IREG_GET_SIZE(uop->dest_reg_a_real);
    int src_size  = IREG_GET_SIZE(uop->src_reg_a_real);

    if (REG_IS_L(dest_size) && REG_IS_L(src_size)) {
        host_x86_XOR32_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_W(dest_size) && REG_IS_W(src_size)) {
        host_x86_XOR16_REG_IMM(block, dest_reg, uop->imm_data);
    } else if (REG_IS_B(dest_size) && REG_IS_B(src_size)) {
        host_x86_XOR8_REG_IMM(block, dest_reg, uop->imm_data);
    }
#    ifdef RECOMPILER_DEBUG
    else
        fatal("XOR_IMM %02x %02x\n", uop->dest_reg_a_real, uop->src_reg_a_real);
#    endif
    return 0;
}

#    include "codegen_backend_x86-64_x87h.c"

const uOpFn uop_handlers[UOP_MAX] = {
    [UOP_DIVMOD & UOP_MASK] = codegen_DIVMOD,
    [UOP_DIV_RESULT & UOP_MASK] = codegen_DIV_RESULT,
    [UOP_CMP_SLT & UOP_MASK] = codegen_CMP_SLT,
    [UOP_CMP_Z & UOP_MASK] = codegen_CMP_Z,
    [UOP_OVERFLOW & UOP_MASK] = codegen_OVERFLOW,
    [UOP_CMOV_Z & UOP_MASK] = codegen_CMOV_Z,
    [UOP_PARITY & UOP_MASK] = codegen_PARITY,
    [UOP_PARITY_JUMP & UOP_MASK] = codegen_PARITY_JUMP,
    [UOP_CMP_ULT & UOP_MASK] = codegen_CMP_ULT,
    [UOP_CALL_FUNC & UOP_MASK] = codegen_CALL_FUNC,
    [UOP_CALL_FUNC_ORDERED &
        UOP_MASK]
    = codegen_CALL_FUNC_ORDERED,
    [UOP_CALL_FUNC_RESULT_ORDERED &
        UOP_MASK]
    = codegen_CALL_FUNC_RESULT_ORDERED,
    [UOP_CALL_INSTRUCTION_FUNC_ORDERED &
        UOP_MASK]
    = codegen_CALL_INSTRUCTION_FUNC_ORDERED,
    [UOP_CALL_REP_STRING &
        UOP_MASK]
    = codegen_CALL_REP_STRING,
    [UOP_SMI_EXIT_IMM &
        UOP_MASK]
    = codegen_SMI_EXIT_IMM,
    [UOP_SMI_EXIT &
        UOP_MASK]
    = codegen_SMI_EXIT_IMM,
    [UOP_CALL_FUNC_RESULT &
        UOP_MASK]
    = codegen_CALL_FUNC_RESULT,
    [UOP_CALL_INSTRUCTION_FUNC &
        UOP_MASK]
    = codegen_CALL_INSTRUCTION_FUNC,

    [UOP_JMP &
        UOP_MASK]
    = codegen_JMP,
    [UOP_JMP_DEST &
        UOP_MASK]
    = codegen_JMP_DEST,
#    if X87_HOST_AVAILABLE
    [UOP_X87H &
        UOP_MASK]
    = codegen_X87H,
    [UOP_X87H_DONE &
        UOP_MASK]
    = codegen_X87H_DONE,
#    endif

    [UOP_LOAD_SEG &
        UOP_MASK]
    = codegen_LOAD_SEG,

    [UOP_LOAD_FUNC_ARG_0 &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG0,
    [UOP_LOAD_FUNC_ARG_1 &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG1,
    [UOP_LOAD_FUNC_ARG_2 &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG2,
    [UOP_LOAD_FUNC_ARG_3 &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG3,

    [UOP_LOAD_FUNC_ARG_0_IMM &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG0_IMM,
    [UOP_LOAD_FUNC_ARG_1_IMM &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG1_IMM,
    [UOP_LOAD_FUNC_ARG_2_IMM &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG2_IMM,
    [UOP_LOAD_FUNC_ARG_3_IMM &
        UOP_MASK]
    = codegen_LOAD_FUNC_ARG3_IMM,

    [UOP_STORE_P_IMM &
        UOP_MASK]
    = codegen_STORE_PTR_IMM,
    [UOP_STORE_P_IMM_8 &
        UOP_MASK]
    = codegen_STORE_PTR_IMM_8,

    [UOP_MEM_LOAD_ABS &
        UOP_MASK]
    = codegen_MEM_LOAD_ABS,
    [UOP_MEM_LOAD_REG &
        UOP_MASK]
    = codegen_MEM_LOAD_REG,
    [UOP_MEM_LOAD_REG_EA &
        UOP_MASK]
    = codegen_MEM_LOAD_REG_EA,
    [UOP_MEM_LOAD_SINGLE &
        UOP_MASK]
    = codegen_MEM_LOAD_SINGLE,
    [UOP_MEM_LOAD_DOUBLE &
        UOP_MASK]
    = codegen_MEM_LOAD_DOUBLE,

    [UOP_MEM_STORE_ABS &
        UOP_MASK]
    = codegen_MEM_STORE_ABS,
    [UOP_MEM_STORE_REG &
        UOP_MASK]
    = codegen_MEM_STORE_REG,
    [UOP_MEM_STORE_IMM_8 &
        UOP_MASK]
    = codegen_MEM_STORE_IMM_8,
    [UOP_MEM_STORE_IMM_16 &
        UOP_MASK]
    = codegen_MEM_STORE_IMM_16,
    [UOP_MEM_STORE_IMM_32 &
        UOP_MASK]
    = codegen_MEM_STORE_IMM_32,
    [UOP_MEM_STORE_SINGLE &
        UOP_MASK]
    = codegen_MEM_STORE_SINGLE,
    [UOP_MEM_STORE_DOUBLE &
        UOP_MASK]
    = codegen_MEM_STORE_DOUBLE,

    [UOP_MOV &
        UOP_MASK]
    = codegen_MOV,
    [UOP_MOV_PTR &
        UOP_MASK]
    = codegen_MOV_PTR,
    [UOP_MOV_IMM &
        UOP_MASK]
    = codegen_MOV_IMM,
    [UOP_MOVSX &
        UOP_MASK]
    = codegen_MOVSX,
    [UOP_MOVZX &
        UOP_MASK]
    = codegen_MOVZX,
    [UOP_MOVSS &
        UOP_MASK]
    = codegen_MOVSS,
    [UOP_MOVSD &
        UOP_MASK]
    = codegen_MOVSD,
    [UOP_MOV_DOUBLE_INT &
        UOP_MASK]
    = codegen_MOV_DOUBLE_INT,
    [UOP_MOV_INT_DOUBLE &
        UOP_MASK]
    = codegen_MOV_INT_DOUBLE,
    [UOP_MOV_INT_DOUBLE_64 &
        UOP_MASK]
    = codegen_MOV_INT_DOUBLE_64,
    [UOP_MOV_REG_PTR &
        UOP_MASK]
    = codegen_MOV_REG_PTR,
    [UOP_MOVZX_REG_PTR_8 &
        UOP_MASK]
    = codegen_MOVZX_REG_PTR_8,
    [UOP_MOVZX_REG_PTR_16 &
        UOP_MASK]
    = codegen_MOVZX_REG_PTR_16,

    [UOP_ADD &
        UOP_MASK]
    = codegen_ADD,
    [UOP_ADD_IMM &
        UOP_MASK]
    = codegen_ADD_IMM,
    [UOP_IMUL_IMM &
        UOP_MASK]
    = codegen_IMUL_IMM,
    [UOP_UMUL &
        UOP_MASK]
    = codegen_UMUL,
    [UOP_UMUL_HI &
        UOP_MASK]
    = codegen_UMUL_HI,
    [UOP_UDIV_CHECK &
        UOP_MASK]
    = codegen_UDIV_CHECK,
    [UOP_IDIV_CHECK &
        UOP_MASK]
    = codegen_IDIV_CHECK,
    [UOP_UDIV &
        UOP_MASK]
    = codegen_UDIV,
    [UOP_UMOD &
        UOP_MASK]
    = codegen_UMOD,
    [UOP_IDIV &
        UOP_MASK]
    = codegen_IDIV,
    [UOP_IMOD &
        UOP_MASK]
    = codegen_IMOD,
    [UOP_ADD_LSHIFT &
        UOP_MASK]
    = codegen_ADD_LSHIFT,
    [UOP_AND &
        UOP_MASK]
    = codegen_AND,
    [UOP_ANDN &
        UOP_MASK]
    = codegen_ANDN,
    [UOP_AND_IMM &
        UOP_MASK]
    = codegen_AND_IMM,
    [UOP_OR &
        UOP_MASK]
    = codegen_OR,
    [UOP_OR_IMM &
        UOP_MASK]
    = codegen_OR_IMM,
    [UOP_SUB &
        UOP_MASK]
    = codegen_SUB,
    [UOP_SUB_IMM &
        UOP_MASK]
    = codegen_SUB_IMM,
    [UOP_XOR &
        UOP_MASK]
    = codegen_XOR,
    [UOP_XOR_IMM &
        UOP_MASK]
    = codegen_XOR_IMM,

    [UOP_SAR &
        UOP_MASK]
    = codegen_SAR,
    [UOP_SAR_IMM &
        UOP_MASK]
    = codegen_SAR_IMM,
    [UOP_SHL &
        UOP_MASK]
    = codegen_SHL,
    [UOP_SHL_IMM &
        UOP_MASK]
    = codegen_SHL_IMM,
    [UOP_SHR &
        UOP_MASK]
    = codegen_SHR,
    [UOP_SHR_IMM &
        UOP_MASK]
    = codegen_SHR_IMM,
    [UOP_ROL &
        UOP_MASK]
    = codegen_ROL,
    [UOP_ROL_IMM &
        UOP_MASK]
    = codegen_ROL_IMM,
    [UOP_ROR &
        UOP_MASK]
    = codegen_ROR,
    [UOP_ROR_IMM &
        UOP_MASK]
    = codegen_ROR_IMM,

    [UOP_CMP_IMM_JZ &
        UOP_MASK]
    = codegen_CMP_IMM_JZ,

    [UOP_CMP_JB &
        UOP_MASK]
    = codegen_CMP_JB,
    [UOP_CMP_JNBE &
        UOP_MASK]
    = codegen_CMP_JNBE,
    [UOP_FCMP_JWITHIN_DEST &
        UOP_MASK]
    = codegen_FCMP_JWITHIN_DEST,
    [UOP_FCMP_JWITHIN_DEST_COLD &
        UOP_MASK]
    = codegen_FCMP_JWITHIN_DEST,
    [UOP_CMP_JNWITHIN &
        UOP_MASK]
    = codegen_CMP_JNWITHIN,
    [UOP_CMN_IMM_JC &
        UOP_MASK]
    = codegen_CMN_IMM_JC,
    [UOP_CMP_JNWITHIN_TOP &
        UOP_MASK]
    = codegen_CMP_JNWITHIN_TOP,

    [UOP_CMP_JNB_DEST &
        UOP_MASK]
    = codegen_CMP_JNB_DEST,
    [UOP_CMP_JNBE_DEST &
        UOP_MASK]
    = codegen_CMP_JNBE_DEST,
    [UOP_CMP_JNL_DEST &
        UOP_MASK]
    = codegen_CMP_JNL_DEST,
    [UOP_CMP_JNLE_DEST &
        UOP_MASK]
    = codegen_CMP_JNLE_DEST,
    [UOP_CMP_JNO_DEST &
        UOP_MASK]
    = codegen_CMP_JNO_DEST,
    [UOP_CMP_JNZ_DEST &
        UOP_MASK]
    = codegen_CMP_JNZ_DEST,
    [UOP_CMP_JB_DEST &
        UOP_MASK]
    = codegen_CMP_JB_DEST,
    [UOP_CMP_JBE_DEST &
        UOP_MASK]
    = codegen_CMP_JBE_DEST,
    [UOP_CMP_JL_DEST &
        UOP_MASK]
    = codegen_CMP_JL_DEST,
    [UOP_CMP_JLE_DEST &
        UOP_MASK]
    = codegen_CMP_JLE_DEST,
    [UOP_CMP_JO_DEST &
        UOP_MASK]
    = codegen_CMP_JO_DEST,
    [UOP_CMP_JZ_DEST &
        UOP_MASK]
    = codegen_CMP_JZ_DEST,
    [UOP_CMOVNZ &
        UOP_MASK]
    = codegen_CMOVNZ,

    [UOP_CMP_IMM_JNZ_DEST &
        UOP_MASK]
    = codegen_CMP_IMM_JNZ_DEST,
    [UOP_CMP_IMM_JZ_DEST &
        UOP_MASK]
    = codegen_CMP_IMM_JZ_DEST,
    [UOP_CMP_IMM_JZ_DEST_COLD &
        UOP_MASK]
    = codegen_CMP_IMM_JZ_DEST,
    [UOP_TEST_IMM_JZ_DEST_COLD &
        UOP_MASK]
    = codegen_TEST_IMM_JZ_DEST,

    [UOP_TEST_JNS_DEST &
        UOP_MASK]
    = codegen_TEST_JNS_DEST,
    [UOP_TEST_JS_DEST &
        UOP_MASK]
    = codegen_TEST_JS_DEST,
    [UOP_CMP_SET &
        UOP_MASK]
    = codegen_CMP_SET,
    [UOP_TEST_SET &
        UOP_MASK]
    = codegen_TEST_SET,
    [UOP_BSWAP &
        UOP_MASK]
    = codegen_BSWAP,
    [UOP_IMUL &
        UOP_MASK]
    = codegen_IMUL,
    [UOP_IMUL_OV &
        UOP_MASK]
    = codegen_IMUL_OV,
    [UOP_MUL_HI &
        UOP_MASK]
    = codegen_MUL_HI,
    [UOP_IMUL_HI &
        UOP_MASK]
    = codegen_IMUL_HI,
    [UOP_BSF &
        UOP_MASK]
    = codegen_BSF,
    [UOP_BSR &
        UOP_MASK]
    = codegen_BSR,

    [UOP_FP_ENTER &
        UOP_MASK]
    = codegen_FP_ENTER,
    [UOP_FP_ENTER_ORDERED &
        UOP_MASK]
    = codegen_FP_ENTER,
    [UOP_FP_CONTROL_SET &
        UOP_MASK]
    = codegen_FP_CONTROL_SET,
    [UOP_MMX_ENTER &
        UOP_MASK]
    = codegen_MMX_ENTER,
    [UOP_MMX_COMMIT & UOP_MASK] = codegen_MMX_COMMIT,

    [UOP_FADD &
        UOP_MASK]
    = codegen_FADD,
    [UOP_FCOM &
        UOP_MASK]
    = codegen_FCOM,
    [UOP_FCOM_SIGNED_ZERO &
        UOP_MASK]
    = codegen_FCOM_SIGNED_ZERO,
    [UOP_FCOM_CHKCOP &
        UOP_MASK]
    = codegen_FCOM_CHKCOP,
    [UOP_FDIV &
        UOP_MASK]
    = codegen_FDIV,
    [UOP_FDIV_JSLOW_DEST &
        UOP_MASK]
    = codegen_FDIV_JSLOW_DEST,
    [UOP_FDIV_FLAGS_JSLOW_DEST &
        UOP_MASK]
    = codegen_FDIV_FLAGS_JSLOW_DEST,
    [UOP_FMUL &
        UOP_MASK]
    = codegen_FMUL,
    [UOP_FSUB &
        UOP_MASK]
    = codegen_FSUB,

    [UOP_FABS &
        UOP_MASK]
    = codegen_FABS,
    [UOP_FCHS &
        UOP_MASK]
    = codegen_FCHS,
    [UOP_FSQRT &
        UOP_MASK]
    = codegen_FSQRT,
    [UOP_FROUND_S &
        UOP_MASK]
    = codegen_FROUND_S,
    [UOP_FTST &
        UOP_MASK]
    = codegen_FTST,

    [UOP_PACKSSWB &
        UOP_MASK]
    = codegen_PACKSSWB,
    [UOP_PACKSSDW &
        UOP_MASK]
    = codegen_PACKSSDW,
    [UOP_PACKUSWB &
        UOP_MASK]
    = codegen_PACKUSWB,

    [UOP_PADDB &
        UOP_MASK]
    = codegen_PADDB,
    [UOP_PADDW &
        UOP_MASK]
    = codegen_PADDW,
    [UOP_PADDD &
        UOP_MASK]
    = codegen_PADDD,
    [UOP_PADDQ &
        UOP_MASK]
    = codegen_PADDQ,
    [UOP_PADDSB &
        UOP_MASK]
    = codegen_PADDSB,
    [UOP_PADDSW &
        UOP_MASK]
    = codegen_PADDSW,
    [UOP_PADDUSB &
        UOP_MASK]
    = codegen_PADDUSB,
    [UOP_PADDUSW &
        UOP_MASK]
    = codegen_PADDUSW,

    [UOP_PCMPEQB &
        UOP_MASK]
    = codegen_PCMPEQB,
    [UOP_PCMPEQW &
        UOP_MASK]
    = codegen_PCMPEQW,
    [UOP_PCMPEQD &
        UOP_MASK]
    = codegen_PCMPEQD,
    [UOP_PCMPGTB &
        UOP_MASK]
    = codegen_PCMPGTB,
    [UOP_PCMPGTW &
        UOP_MASK]
    = codegen_PCMPGTW,
    [UOP_PCMPGTD &
        UOP_MASK]
    = codegen_PCMPGTD,

    [UOP_PF2ID &
        UOP_MASK]
    = codegen_PF2ID,
    [UOP_PFADD &
        UOP_MASK]
    = codegen_PFADD,
    [UOP_PFCMPEQ &
        UOP_MASK]
    = codegen_PFCMPEQ,
    [UOP_PFCMPGE &
        UOP_MASK]
    = codegen_PFCMPGE,
    [UOP_PFCMPGT &
        UOP_MASK]
    = codegen_PFCMPGT,
    [UOP_PFMAX &
        UOP_MASK]
    = codegen_PFMAX,
    [UOP_PFMIN &
        UOP_MASK]
    = codegen_PFMIN,
    [UOP_PFMUL &
        UOP_MASK]
    = codegen_PFMUL,
    [UOP_PFRCP &
        UOP_MASK]
    = codegen_PFRCP,
    [UOP_PFRSQRT &
        UOP_MASK]
    = codegen_PFRSQRT,
    [UOP_PFSUB &
        UOP_MASK]
    = codegen_PFSUB,
    [UOP_PI2FD &
        UOP_MASK]
    = codegen_PI2FD,

    [UOP_PMADDWD &
        UOP_MASK]
    = codegen_PMADDWD,
    [UOP_PMULHW &
        UOP_MASK]
    = codegen_PMULHW,
    [UOP_PMULLW &
        UOP_MASK]
    = codegen_PMULLW,
    [UOP_PAVGUSB &
        UOP_MASK]
    = codegen_PAVGUSB,
    [UOP_PAVGW &
        UOP_MASK]
    = codegen_PAVGW,
    [UOP_PMINUB &
        UOP_MASK]
    = codegen_PMINUB,
    [UOP_PMAXUB &
        UOP_MASK]
    = codegen_PMAXUB,
    [UOP_PMULHUW &
        UOP_MASK]
    = codegen_PMULHUW,
    [UOP_PMINSW &
        UOP_MASK]
    = codegen_PMINSW,
    [UOP_PMAXSW &
        UOP_MASK]
    = codegen_PMAXSW,
    [UOP_PSADBW &
        UOP_MASK]
    = codegen_PSADBW,
    [UOP_PSHUFW &
        UOP_MASK]
    = codegen_PSHUFW,
    [UOP_PINSRW &
        UOP_MASK]
    = codegen_PINSRW,
    [UOP_PEXTRW &
        UOP_MASK]
    = codegen_PEXTRW,
    [UOP_PMOVMSKB &
        UOP_MASK]
    = codegen_PMOVMSKB,

    [UOP_PSLLW_IMM &
        UOP_MASK]
    = codegen_PSLLW_IMM,
    [UOP_PSLLD_IMM &
        UOP_MASK]
    = codegen_PSLLD_IMM,
    [UOP_PSLLQ_IMM &
        UOP_MASK]
    = codegen_PSLLQ_IMM,
    [UOP_PSRAW_IMM &
        UOP_MASK]
    = codegen_PSRAW_IMM,
    [UOP_PSRAD_IMM &
        UOP_MASK]
    = codegen_PSRAD_IMM,
    [UOP_PSRLW_IMM &
        UOP_MASK]
    = codegen_PSRLW_IMM,
    [UOP_PSRLD_IMM &
        UOP_MASK]
    = codegen_PSRLD_IMM,
    [UOP_PSRLQ_IMM &
        UOP_MASK]
    = codegen_PSRLQ_IMM,

    [UOP_PSUBB &
        UOP_MASK]
    = codegen_PSUBB,
    [UOP_PSUBW &
        UOP_MASK]
    = codegen_PSUBW,
    [UOP_PSUBD &
        UOP_MASK]
    = codegen_PSUBD,
    [UOP_PSUBQ &
        UOP_MASK]
    = codegen_PSUBQ,
    [UOP_PSUBSB &
        UOP_MASK]
    = codegen_PSUBSB,
    [UOP_PSUBSW &
        UOP_MASK]
    = codegen_PSUBSW,
    [UOP_PSUBUSB &
        UOP_MASK]
    = codegen_PSUBUSB,
    [UOP_PSUBUSW &
        UOP_MASK]
    = codegen_PSUBUSW,

    [UOP_PUNPCKHBW &
        UOP_MASK]
    = codegen_PUNPCKHBW,
    [UOP_PUNPCKHWD &
        UOP_MASK]
    = codegen_PUNPCKHWD,
    [UOP_PUNPCKHDQ &
        UOP_MASK]
    = codegen_PUNPCKHDQ,
    [UOP_PUNPCKLBW &
        UOP_MASK]
    = codegen_PUNPCKLBW,
    [UOP_PUNPCKLWD &
        UOP_MASK]
    = codegen_PUNPCKLWD,
    [UOP_PUNPCKLDQ &
        UOP_MASK]
    = codegen_PUNPCKLDQ,

    [UOP_UNPCKLPS &
        UOP_MASK]
    = codegen_UNPCKLPS,
    [UOP_UNPCKLPD &
        UOP_MASK]
    = codegen_UNPCKLPD,

    [UOP_UNPCKHPS &
        UOP_MASK]
    = codegen_UNPCKHPS,
    [UOP_UNPCKHPD &
        UOP_MASK]
    = codegen_UNPCKHPD,

    [UOP_ADDPS &
        UOP_MASK]
    = codegen_ADDPS,
    [UOP_ADDPD &
        UOP_MASK]
    = codegen_ADDPD,
    [UOP_ADDSS &
        UOP_MASK]
    = codegen_ADDSS,
    [UOP_ADDSD &
        UOP_MASK]
    = codegen_ADDSD,
    [UOP_MULPS &
        UOP_MASK]
    = codegen_MULPS,
    [UOP_MULPD &
        UOP_MASK]
    = codegen_MULPD,
    [UOP_MULSS &
        UOP_MASK]
    = codegen_MULSS,
    [UOP_MULSD &
        UOP_MASK]
    = codegen_MULSD,
    [UOP_SUBPS &
        UOP_MASK]
    = codegen_SUBPS,
    [UOP_SUBPD &
        UOP_MASK]
    = codegen_SUBPD,
    [UOP_SUBSS &
        UOP_MASK]
    = codegen_SUBSS,
    [UOP_SUBSD &
        UOP_MASK]
    = codegen_SUBSD,
    [UOP_DIVPS &
        UOP_MASK]
    = codegen_DIVPS,
    [UOP_DIVPD &
        UOP_MASK]
    = codegen_DIVPD,
    [UOP_DIVSS &
        UOP_MASK]
    = codegen_DIVSS,
    [UOP_DIVSD &
        UOP_MASK]
    = codegen_DIVSD,

    [UOP_SHUFPS &
        UOP_MASK]
    = codegen_SHUFPS,
    [UOP_MOVHLPS &
        UOP_MASK]
    = codegen_MOVHLPS,
    [UOP_MOVLHPS &
        UOP_MASK]
    = codegen_MOVLHPS,
    [UOP_MOVMSKPS &
        UOP_MASK]
    = codegen_MOVMSKPS,
    [UOP_MINPS &
        UOP_MASK]
    = codegen_MINPS,
    [UOP_MINSS &
        UOP_MASK]
    = codegen_MINSS,
    [UOP_MAXPS &
        UOP_MASK]
    = codegen_MAXPS,
    [UOP_MAXSS &
        UOP_MASK]
    = codegen_MAXSS,
    [UOP_SQRTPS &
        UOP_MASK]
    = codegen_SQRTPS,
    [UOP_SQRTSS &
        UOP_MASK]
    = codegen_SQRTSS,
    [UOP_CMPPS &
        UOP_MASK]
    = codegen_CMPPS,
    [UOP_CMPSS &
        UOP_MASK]
    = codegen_CMPSS,
    [UOP_COMISS &
        UOP_MASK]
    = codegen_COMISS,
    [UOP_RCPPS &
        UOP_MASK]
    = codegen_RCPPS,
    [UOP_RCPSS &
        UOP_MASK]
    = codegen_RCPSS,
    [UOP_RSQRTPS &
        UOP_MASK]
    = codegen_RSQRTPS,
    [UOP_RSQRTSS &
        UOP_MASK]
    = codegen_RSQRTSS,
    [UOP_CVTSI2SS &
        UOP_MASK]
    = codegen_CVTSI2SS,
    [UOP_CVTTSS2SI &
        UOP_MASK]
    = codegen_CVTTSS2SI,
    [UOP_CVTSS2SI &
        UOP_MASK]
    = codegen_CVTSS2SI,
    [UOP_CVTPI2PS &
        UOP_MASK]
    = codegen_CVTPI2PS,
    [UOP_CVTTPS2PI &
        UOP_MASK]
    = codegen_CVTTPS2PI,
    [UOP_CVTPS2PI &
        UOP_MASK]
    = codegen_CVTPS2PI,

    [UOP_SSE_ENTER &
        UOP_MASK]
    = codegen_SSE_ENTER,

    [UOP_CHECK_ALIGN &
        UOP_MASK]
    = codegen_CHECK_ALIGN,

    [UOP_NOP_BARRIER &
        UOP_MASK]
    = codegen_NOP
};

void
codegen_direct_read_8(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOV8_REG_ABS(block, host_reg, p);
}
void
codegen_direct_read_16(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOV16_REG_ABS(block, host_reg, p);
}
void
codegen_direct_read_32(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOV32_REG_ABS(block, host_reg, p);
}
void
codegen_direct_read_64(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOVQ_XREG_ABS(block, host_reg, p);
}
void
codegen_direct_read_128(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOVDQU_XREG_ABS(block, host_reg, p);
}
void
codegen_direct_read_pointer(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOV64_REG_ABS(block, host_reg, p);
}
void
codegen_direct_read_double(codeblock_t *block, int host_reg, void *p)
{
    host_x86_MOVQ_XREG_ABS(block, host_reg, p);
}
void
codegen_direct_read_st_8(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOV8_REG_ABS_REG_REG_SHIFT(block, host_reg, offset, REG_RBP, REG_ECX, 0);
}
void
codegen_direct_read_st_64(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOVQ_XREG_ABS_REG_REG_SHIFT(block, host_reg, offset, REG_RBP, REG_ECX, 3);
}
void
codegen_direct_read_st_double(codeblock_t *block, int host_reg, void *base, int reg_idx)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOVQ_XREG_ABS_REG_REG_SHIFT(block, host_reg, offset, REG_RBP, REG_ECX, 3);
}

void
codegen_direct_write_8(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOV8_ABS_REG(block, p, host_reg);
}
void
codegen_direct_write_16(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOV16_ABS_REG(block, p, host_reg);
}
void
codegen_direct_write_32(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOV32_ABS_REG(block, p, host_reg);
}
void
codegen_direct_write_64(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOVQ_ABS_XREG(block, p, host_reg);
}
void
codegen_direct_write_128(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOVDQU_ABS_XREG(block, p, host_reg);
}
void
codegen_direct_write_pointer(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOV64_ABS_REG(block, p, host_reg);
}
void
codegen_direct_write_double(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOVQ_ABS_XREG(block, p, host_reg);
}
void
codegen_direct_write_st_8(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOV8_ABS_REG_REG_SHIFT_REG(block, offset, REG_RBP, REG_ECX, 0, host_reg);
}
void
codegen_direct_write_st_64(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOVQ_ABS_REG_REG_SHIFT_XREG(block, offset, REG_RBP, REG_ECX, 3, host_reg);
}
void
codegen_direct_write_st_double(codeblock_t *block, void *base, int reg_idx, int host_reg)
{
    int offset = (uintptr_t) base - (((uintptr_t) &cpu_state) + 128);

    host_x86_MOV32_REG_BASE_OFFSET(block, REG_ECX, REG_RSP, IREG_TOP_diff_stack_offset);
    host_x86_ADD32_REG_IMM(block, REG_ECX, reg_idx);
    host_x86_AND32_REG_IMM(block, REG_ECX, 7);
    host_x86_MOVQ_ABS_REG_REG_SHIFT_XREG(block, offset, REG_RBP, REG_ECX, 3, host_reg);
}

void
codegen_direct_write_ptr(codeblock_t *block, void *p, int host_reg)
{
    host_x86_MOV64_ABS_REG(block, p, host_reg);
}

void
codegen_direct_read_16_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_x86_MOV16_REG_BASE_OFFSET(block, host_reg, REG_ESP, stack_offset);
}
void
codegen_direct_read_32_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_x86_MOV32_REG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}
void
codegen_direct_read_64_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_x86_MOVQ_XREG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}
void
codegen_direct_read_pointer_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_x86_MOV64_REG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}
void
codegen_direct_read_double_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_x86_MOVQ_XREG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}
void
codegen_direct_read_128_stack(codeblock_t *block, int host_reg, int stack_offset)
{
    host_x86_MOVDQU_XREG_BASE_OFFSET(block, host_reg, REG_RSP, stack_offset);
}

void
codegen_direct_write_32_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_x86_MOV32_BASE_OFFSET_REG(block, REG_RSP, stack_offset, host_reg);
}
void
codegen_direct_write_64_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_x86_MOVQ_BASE_OFFSET_XREG(block, REG_RSP, stack_offset, host_reg);
}
void
codegen_direct_write_pointer_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_x86_MOV64_BASE_OFFSET_REG(block, REG_RSP, stack_offset, host_reg);
}
void
codegen_direct_write_double_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_x86_MOVQ_BASE_OFFSET_XREG(block, REG_RSP, stack_offset, host_reg);
}
void
codegen_direct_write_128_stack(codeblock_t *block, int stack_offset, int host_reg)
{
    host_x86_MOVDQU_BASE_OFFSET_XREG(block, REG_RSP, stack_offset, host_reg);
}

void
codegen_set_jump_dest(UNUSED(codeblock_t *block), void *p)
{
    *(uint32_t *) p = (uintptr_t) &block_write_data[block_pos] - ((uintptr_t) p + 4);
}

void
codegen_direct_write_8_imm(codeblock_t *block, void *p, uint8_t imm_data)
{
    host_x86_MOV8_ABS_IMM(block, p, imm_data);
}
void
codegen_direct_write_16_imm(codeblock_t *block, void *p, uint16_t imm_data)
{
    host_x86_MOV16_ABS_IMM(block, p, imm_data);
}
void
codegen_direct_write_32_imm(codeblock_t *block, void *p, uint32_t imm_data)
{
    host_x86_MOV32_ABS_IMM(block, p, imm_data);
}
void
codegen_direct_write_32_imm_stack(codeblock_t *block, int stack_offset, uint32_t imm_data)
{
    host_x86_MOV32_BASE_OFFSET_IMM(block, REG_ESP, stack_offset, imm_data);
}

#endif
