/* Execute the production translators, allocator and x64 backend. Share the
   benchmark's RAM/code arena so correctness and timing exercise the same JIT,
   but use independent arithmetic oracles and explicit fault/ABI assertions. */
#define CPU_FASTPATH_TEST
#define main cpu_microbench_main
#include "cpu_microbench.c"
#undef main
#include "../../src/codegen_new/codegen_ops_branch.h"

typedef void (*jit_fn)(void);
typedef uint32_t (*translator)(codeblock_t *, ir_data_t *, uint8_t, uint32_t, uint32_t, uint32_t);
static unsigned checks;
static uint32_t random_state = 0x9e3779b9;
int stack32;
void loadcsjmp(uint16_t seg, uint32_t old_pc) { (void) seg; (void) old_pc; fatal("unexpected far jump\n"); }

static uint32_t random_u32(void)
{
    random_state ^= random_state << 13;
    random_state ^= random_state >> 17;
    random_state ^= random_state << 5;
    return random_state;
}

static ir_data_t *begin_test(const char *name)
{
    active_case = name;
    bench_case_t empty = {0};
    reset_state(&empty, 0);
    test_fault_access = 0;
    helper_calls = 0;
    verifying = 1;
    next_chunk = first_codegen_chunk;
    memset(&bench_block, 0, sizeof(bench_block));
    bench_block.flags = CODEBLOCK_HAS_FPU | CODEBLOCK_STATIC_TOP;
    start_code();
    codegen_reg_reset();
    codegen_flags_changed = 0;
    return codegen_ir_init();
}

static jit_fn finish_test(ir_data_t *ir)
{
    codegen_ir_compile(ir, &bench_block);
    flush_code();
    return (jit_fn) bench_block.data;
}

/* Do not derive expected carry using the same bit trick as the translator. */
static unsigned prepare_flags(int op, unsigned bits, uint32_t a, uint32_t b, unsigned carry)
{
    uint32_t mask = UINT32_MAX >> (32 - bits);
    a &= mask;
    b &= mask;
    cpu_state.flags_op = op;
    cpu_state.flags_op1 = a;
    cpu_state.flags_op2 = b;
    cpu_state.flags = 0x202 | carry;
    if (op == FLAGS_ADD8 || op == FLAGS_ADD16 || op == FLAGS_ADD32) {
        cpu_state.flags_res = (a + b) & mask;
        return ((uint64_t) a + b) > mask;
    }
    if (op == FLAGS_SUB8 || op == FLAGS_SUB16 || op == FLAGS_SUB32) {
        cpu_state.flags_res = (a - b) & mask;
        return a < b;
    }
    if (op == FLAGS_ADC32) {
        cpu_state.flags_res = a + b + carry;
        return (uint64_t) a + b + carry > UINT32_MAX;
    }
    if (op == FLAGS_SBC32) {
        cpu_state.flags_res = a - b - carry;
        return (uint64_t) a < (uint64_t) b + carry;
    }
    cpu_state.flags_res = a;
    return op == FLAGS_ZN8 || op == FLAGS_ZN16 || op == FLAGS_ZN32 ? 0 : carry;
}

static void test_conditions(void)
{
    const int ops[] = { FLAGS_ADD8, FLAGS_ADD16, FLAGS_ADD32, FLAGS_SUB8, FLAGS_SUB16, FLAGS_SUB32,
                       FLAGS_ZN8, FLAGS_ZN16, FLAGS_ZN32, FLAGS_INC8, FLAGS_INC16, FLAGS_INC32,
                       FLAGS_DEC8, FLAGS_DEC16, FLAGS_DEC32, FLAGS_ADC32, FLAGS_SBC32, FLAGS_UNKNOWN };
    const uint32_t edge[] = {0, 1, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff,
                            0x10000, 0x7fffffff, 0x80000000, 0xfffffffe, 0xffffffff};
    for (unsigned op = 0; op < sizeof(ops) / sizeof(ops[0]); op++)
        for (int known = 0; known <= 1; known++) {
            ir_data_t *ir = begin_test("carry/conditions");
            codegen_flags_changed = known;
            cpu_state.flags_op = ops[op];
            setcc_gen_B(ir, 0);
            uop_MOV(ir, IREG_EAX, IREG_temp0);
            setcc_gen_B(ir, 1);
            uop_MOV(ir, IREG_EBX, IREG_temp0);
            setcc_gen_BE(ir, 0);
            uop_MOV(ir, IREG_ECX, IREG_temp0);
            setcc_gen_BE(ir, 1);
            uop_MOV(ir, IREG_EDX, IREG_temp0);
            jit_fn entry = finish_test(ir);
            unsigned bits = op < 15 ? 8u << (op % 3) : 32;
            unsigned count = bits == 8 && op < 6 ? 65536 : 8192;
            for (unsigned i = 0; i < count; i++) {
                uint32_t a = bits == 8 && op < 6 ? i >> 8 : i < 196 ? edge[i / 14] : random_u32();
                uint32_t b = bits == 8 && op < 6 ? i & 255 : i < 196 ? edge[i % 14] : random_u32();
                unsigned carry = prepare_flags(ops[op], bits, a, b, i & 1);
                uint32_t old_a = cpu_state.flags_op1, old_b = cpu_state.flags_op2, old_r = cpu_state.flags_res;
                uint16_t old_flags = cpu_state.flags;
                unsigned zero = ops[op] == FLAGS_UNKNOWN ? !!(old_flags & Z_FLAG) : old_r == 0;
                entry();
                if (EAX != carry || EBX != !carry || ECX != (carry || zero) || EDX != !(carry || zero))
                    fatal("op=%d known=%d a=%08x b=%08x: got %u/%u/%u/%u, CF=%u ZF=%u\n",
                          ops[op], known, a, b, EAX, EBX, ECX, EDX, carry, zero);
                CHECK(cpu_state.flags_op == ops[op] && cpu_state.flags == old_flags);
                CHECK(cpu_state.flags_op1 == old_a && cpu_state.flags_op2 == old_b && cpu_state.flags_res == old_r);
                checks++;
            }
        }
}

static void test_adc_sbb(void)
{
    const translator reg_ops[2][3] = {{ropADC_b_rm, ropADC_w_rm, ropADC_l_rm},
                                      {ropSBB_b_rm, ropSBB_w_rm, ropSBB_l_rm}};
    const translator group_ops[] = {rop80, rop81_w, rop81_l};
    /* Every scratch-register arrangement: register/high byte, immediate,
       memory, and mutable immediate with a live memory operand. */
    for (int subtract = 0; subtract <= 1; subtract++)
        for (unsigned width = 0; width < 3; width++)
            for (int form = 0; form < 5; form++)
                for (int known = 0; known <= 1; known++) {
                    unsigned bits = 8u << width;
                    uint32_t mask = UINT32_MAX >> (32 - bits);
                    ir_data_t *ir = begin_test("carry/ADC-SBB operands");
                    int high = !width && form == 1;
                    int dynamic = form == 4;
                    int mem = form >= 3;
                    unsigned reg = high ? 7 : 3; /* BH or BL/BX/EBX */
                    if (dynamic) bench_block.flags |= CODEBLOCK_NO_IMMEDIATES;
                    cpu_state.flags_op = FLAGS_SUB32;
                    codegen_flags_changed = known;
                    uint32_t immediate = 0x87654321;
                    memcpy(instruction_bytes + 0x102, &immediate, 4);
                    if (form < 2)
                        reg_ops[subtract][width](&bench_block, ir, 0, 0xc0 | (reg << 3) | 2, 0x300, 0x101);
                    else
                        group_ops[width](&bench_block, ir, 0, (subtract ? 0x18 : 0x10) | (mem ? 0 : 0xc3), 0x300, 0x101);
                    jit_fn entry = finish_test(ir);
                    for (unsigned i = 0; i < 2048; i++) {
                        uint32_t a = random_u32(), b = form < 2 ? random_u32() : immediate;
                        if (dynamic && width) {
                            b = random_u32();
                            memcpy(instruction_bytes + 0x102, &b, 4);
                        }
                        a &= mask;
                        b &= mask;
                        unsigned carry = prepare_flags(FLAGS_SUB32, 32, i & 1 ? 0 : UINT32_MAX, 1, 0);
                        EAX = 0x4000;
                        EBX = high ? (0xa5b6005a | (a << 8)) : ((0xa5b65a00 & ~mask) | a);
                        EDX = b;
                        uint32_t old_ebx = EBX;
                        memcpy(memory + EAX, &a, bits / 8);
                        uint32_t expected = (subtract ? a - b - carry : a + b + carry) & mask;
                        entry();
                        CHECK(!cpu_state.abrt);
                        uint32_t actual = 0;
                        if (mem) memcpy(&actual, memory + EAX, bits / 8);
                        else actual = high ? BH : EBX & mask;
                        if (actual != expected)
                            fatal("sub=%d bits=%u form=%d known=%d a=%x b=%x cf=%u: %x != %x\n",
                                  subtract, bits, form, known, a, b, carry, actual, expected);
                        uint32_t changed = high ? 0xff00 : mask;
                        CHECK(mem ? EBX == old_ebx : (EBX & ~changed) == (old_ebx & ~changed));
                        CHECK(cpu_state.flags_op1 == a && cpu_state.flags_op2 == b && cpu_state.flags_res == expected);
                        CHECK(!!CF_SET() == (subtract ? (uint64_t) a < (uint64_t) b + carry : (uint64_t) a + b + carry > mask));
                        checks++;
                    }
                }
}

static void test_signed_conditions(void)
{
    const uint32_t edge[] = {0, 1, 0x7fffffff, 0x80000000, 0x80000001, 0xfffffffe, 0xffffffff};
    for (unsigned width = 0; width < 3; width++)
    for (int decrement = 0; decrement < 2; decrement++)
        for (int known = 0; known < 2; known++)
            for (int cond = 12; cond < 16; cond++)
                for (int move = 0; move < 3; move++) {
                    ir_data_t *ir = begin_test("signed conditions");
                    cpu_state.flags_op = decrement ? FLAGS_DEC8 + width : FLAGS_SUB8 + width;
                    codegen_flags_changed = known;
                    if (move) bench_cmov[move - 1][cond](&bench_block, ir, 0, 0xda, 0x300, 0x101);
                    else bench_setcc[cond](&bench_block, ir, 0, 0xc3, 0x300, 0x101);
                    jit_fn entry = finish_test(ir);
                    unsigned bits = 8u << width;
                    uint32_t mask = UINT32_MAX >> (32 - bits);
                    for (unsigned i = 0; i < (width ? 4145u : 65536u); i++) {
                        uint32_t a = (width ? (i < 49 ? edge[i / 7] : random_u32()) : i >> 8) & mask;
                        uint32_t b = decrement ? 1 : (width ? (i < 49 ? edge[i % 7] : random_u32()) : i) & mask;
                        prepare_flags(FLAGS_SUB8 + width, bits, a, b, 1);
                        cpu_state.flags_op = decrement ? FLAGS_DEC8 + width : FLAGS_SUB8 + width;
                        EDX = 0xabcdef12; EBX = 0x12345678;
                        int32_t sa = bits == 8 ? (int8_t) a : bits == 16 ? (int16_t) a : (int32_t) a;
                        int32_t sb = bits == 8 ? (int8_t) b : bits == 16 ? (int16_t) b : (int32_t) b;
                        unsigned condition = (cond < 14 ? sa < sb : sa <= sb) ^ (cond & 1);
                        uint32_t expected = move == 2 ? (condition ? EDX : EBX)
                            : move == 1 ? (condition ? (EBX & 0xffff0000) | (EDX & 0xffff) : EBX)
                            : (EBX & 0xffffff00) | condition;
                        entry();
                        CHECK(EBX == expected && EDX == 0xabcdef12);
                        CHECK(cpu_state.flags_op1 == a && cpu_state.flags_op2 == b && cpu_state.flags_res == ((a - b) & mask));
                        CHECK(cpu_state.flags & C_FLAG);
                        checks++;
                    }
                }
#ifdef CODEGEN_BACKEND_HAS_CMP_SLT
    for (int d = 0; d < CODEGEN_HOST_REGS; d++)
        for (int a = 0; a < CODEGEN_HOST_REGS; a++)
            for (int b = 0; b < CODEGEN_HOST_REGS; b++)
                for (int invert = 0; invert < 2; invert++) {
                    begin_test("signed compare aliases");
                    int dest = codegen_host_reg_list[d].reg;
                    int lhs = codegen_host_reg_list[a].reg, rhs = codegen_host_reg_list[b].reg;
                    codegen_backend_prologue(&bench_block);
                    host_x86_MOV32_REG_IMM(&bench_block, lhs, 0x80000000u);
                    host_x86_MOV32_REG_IMM(&bench_block, rhs, 0x7fffffffu);
                    uop_t op = {.dest_reg_a_real = dest | IREG_SIZE_L, .src_reg_a_real = lhs | IREG_SIZE_L,
                                .src_reg_b_real = rhs | IREG_SIZE_L, .imm_data = invert};
                    codegen_CMP_SLT(&bench_block, &op);
                    host_x86_MOV32_ABS_REG(&bench_block, &EAX, dest);
                    codegen_backend_epilogue(&bench_block);
                    flush_code();
                    ((jit_fn) bench_block.data)();
                    CHECK(EAX == (unsigned) ((lhs != rhs) ^ invert));
                    checks++;
                }
#endif
}

/* Signed arithmetic in a wider type provides an independent OF oracle. */
static void test_overflow_conditions(void)
{
    const int groups[] = {FLAGS_ADD8, FLAGS_SUB8, FLAGS_INC8, FLAGS_DEC8, FLAGS_ADC8, FLAGS_SBC8};
    const uint32_t edge[] = {0, 1, 0x7f, 0x80, 0xff, 0x7fff, 0x8000, 0xffff,
                            0x7fffffff, 0x80000000, UINT32_MAX};
    for (unsigned g = 0; g <= 6; g++)
        for (unsigned width = 0; width < 3; width++)
            for (int known = 0; known < 2; known++)
                for (int invert = 0; invert < 2; invert++)
                    for (int form = 0; form < 5; form++) {
                        int op = g == 6 ? FLAGS_UNKNOWN : groups[g] + width;
                        ir_data_t *ir = begin_test("overflow conditions");
                        codegen_flags_changed = known;
                        cpu_state.flags_op = op;
                        if (form < 2)
                            bench_setcc[invert](&bench_block, ir, 0, form ? 0xc7 : 0xc3, 0x300, 0x101);
                        else if (form < 4)
                            bench_cmov[form - 2][invert](&bench_block, ir, 0, 0xda, 0x300, 0x101);
                        else {
                            instruction_bytes[0x101] = 0x20;
                            if (invert) ropJNO_8(&bench_block, ir, 0, 0, 0x300, 0x101);
                            else ropJO_8(&bench_block, ir, 0, 0, 0x300, 0x101);
                            uop_MOV_IMM(ir, IREG_pc, 0x102);
                        }
                        jit_fn entry = finish_test(ir);
                        unsigned bits = 8u << width;
                        uint32_t mask = UINT32_MAX >> (32 - bits);
                        unsigned count = !width && g < 2 ? 65536 : 2048;
                        for (unsigned i = 0; i < count; i++) {
                            uint32_t a = (count == 65536 ? i >> 8 : i < 121 ? edge[i / 11] : random_u32()) & mask;
                            uint32_t b = (g == 2 || g == 3 ? 1 : count == 65536 ? i : i < 121 ? edge[i % 11] : random_u32()) & mask;
                            unsigned carry = g == 4 || g == 5 ? i & 1 : 0;
                            int64_t sa = bits == 8 ? (int8_t) a : bits == 16 ? (int16_t) a : (int32_t) a;
                            int64_t sb = bits == 8 ? (int8_t) b : bits == 16 ? (int16_t) b : (int32_t) b;
                            int64_t result = g & 1 ? sa - sb - carry : sa + sb + carry;
                            unsigned overflow = result < -(INT64_C(1) << (bits - 1)) || result >= (INT64_C(1) << (bits - 1));
                            if (g == 6) overflow = i & 1;
                            unsigned condition = overflow ^ invert;
                            cpu_state.flags_op = op;
                            cpu_state.flags_op1 = a;
                            cpu_state.flags_op2 = b;
                            cpu_state.flags_res = (uint32_t) result & mask;
                            cpu_state.flags = 0x203 | ((i & 1) ? V_FLAG : 0);
                            uint16_t old_flags = cpu_state.flags;
                            uint32_t old_result = cpu_state.flags_res;
                            EBX = 0x12345678; EDX = 0xabcdef12;
                            uint32_t expected = form == 0 ? (EBX & 0xffffff00) | condition
                                : form == 1 ? (EBX & 0xffff00ff) | (condition << 8)
                                : form == 2 ? (condition ? (EBX & 0xffff0000) | (EDX & 0xffff) : EBX)
                                            : (condition ? EDX : EBX);
                            entry();
                            if (form == 4) CHECK(cpu_state.pc == (condition ? 0x122 : 0x102));
                            else CHECK(EBX == expected && EDX == 0xabcdef12);
                            CHECK(cpu_state.flags == old_flags && cpu_state.flags_op == op);
                            CHECK(cpu_state.flags_op1 == a && cpu_state.flags_op2 == b && cpu_state.flags_res == old_result);
                            checks++;
                        }
                    }
}

#ifdef CODEGEN_BACKEND_HAS_OVERFLOW
static void test_overflow_aliases(void)
{
    for (unsigned width = 0; width < 3; width++)
        for (int subtract = 0; subtract < 2; subtract++)
            for (int d = 0; d < CODEGEN_HOST_REGS; d++)
                for (int a = 0; a < CODEGEN_HOST_REGS; a++)
                    for (int b = 0; b < CODEGEN_HOST_REGS; b++)
                        for (int invert = 0; invert < 2; invert++) {
                            begin_test("overflow register aliases");
                            unsigned bits = 8u << width;
                            uint32_t sign = 1u << (bits - 1);
                            uint32_t lhs_value = subtract ? sign : sign - 1;
                            int dest = codegen_host_reg_list[d].reg;
                            int lhs = codegen_host_reg_list[a].reg, rhs = codegen_host_reg_list[b].reg;
                            codegen_backend_prologue(&bench_block);
                            host_x86_MOV32_REG_IMM(&bench_block, lhs, lhs_value);
                            host_x86_MOV32_REG_IMM(&bench_block, rhs, 1);
                            uop_t op = {.dest_reg_a_real = dest | IREG_SIZE_L,
                                        .src_reg_a_real = lhs | IREG_SIZE_L,
                                        .src_reg_b_real = rhs | IREG_SIZE_L,
                                        .imm_data = invert | (subtract << 1) | (width == 0 ? 4 : width == 1 ? 8 : 0)};
                            codegen_OVERFLOW(&bench_block, &op);
                            host_x86_MOV32_ABS_REG(&bench_block, &EAX, dest);
                            host_x86_MOV32_ABS_REG(&bench_block, &EBX, lhs);
                            host_x86_MOV32_ABS_REG(&bench_block, &EDX, rhs);
                            codegen_backend_epilogue(&bench_block);
                            flush_code();
                            ((jit_fn) bench_block.data)();
                            CHECK(EAX == (unsigned) ((a != b) ^ invert));
                            CHECK(EBX == (d == a ? EAX : a == b ? 1 : lhs_value));
                            CHECK(EDX == (d == b ? EAX : 1));
                            checks++;
                        }
}
#endif

static void test_zero_conditions(void)
{
    static const struct { int op; unsigned bits; int materialized; } producers[] = {
        {FLAGS_ZN8, 8}, {FLAGS_ZN16, 16}, {FLAGS_ZN32, 32},
        {FLAGS_ADD8, 8}, {FLAGS_ADD16, 16}, {FLAGS_ADD32, 32},
        {FLAGS_SUB8, 8}, {FLAGS_SUB16, 16}, {FLAGS_SUB32, 32},
        {FLAGS_INC8, 8}, {FLAGS_INC16, 16}, {FLAGS_INC32, 32},
        {FLAGS_DEC8, 8}, {FLAGS_DEC16, 16}, {FLAGS_DEC32, 32},
        {FLAGS_SHL8, 8}, {FLAGS_SHL16, 16}, {FLAGS_SHL32, 32},
        {FLAGS_SHR8, 8}, {FLAGS_SHR16, 16}, {FLAGS_SHR32, 32},
        {FLAGS_SAR8, 8}, {FLAGS_SAR16, 16}, {FLAGS_SAR32, 32},
        {FLAGS_ADC8, 8}, {FLAGS_ADC16, 16}, {FLAGS_ADC32, 32},
        {FLAGS_SBC8, 8}, {FLAGS_SBC16, 16}, {FLAGS_SBC32, 32},
        {FLAGS_SHRD16, 16}, {FLAGS_SHRD32, 32},
        {FLAGS_UNKNOWN, 32, 1}, {FLAGS_ROL32, 32, 1}, {FLAGS_ROR32, 32, 1},
        {FLAGS_MUL32, 32, 1}, {FLAGS_IMUL32, 32, 1}
    };
    for (unsigned p = 0; p < sizeof(producers) / sizeof(producers[0]); p++)
        for (int known = 0; known < 2; known++)
            for (int invert = 0; invert < 2; invert++)
                for (int form = 0; form < 4; form++) {
                    ir_data_t *ir = begin_test("zero conditions");
                    codegen_flags_changed = known;
                    cpu_state.flags_op = producers[p].op;
                    if (form < 2)
                        bench_setcc[4 + invert](&bench_block, ir, 0, form ? 0xc7 : 0xc3, 0x300, 0x101);
                    else
                        bench_cmov[form - 2][4 + invert](&bench_block, ir, 0, 0xda, 0x300, 0x101);
                    jit_fn entry = finish_test(ir);
                    for (unsigned i = 0; i < 256; i++) {
                        uint32_t result = i % 4 == 0 ? 0 : i % 4 == 1 ? 1u << (producers[p].bits - 1)
                                                                    : random_u32();
                        result &= UINT32_MAX >> (32 - producers[p].bits);
                        cpu_state.flags_res = result;
                        cpu_state.flags_op1 = 0xdeadbeef;
                        cpu_state.flags_op2 = 0x12345678;
                        /* Deliberately disagree with the lazy result on half
                           the inputs, so stale materialized ZF cannot pass. */
                        cpu_state.flags = 0x203 | ((i & 1) ? Z_FLAG : 0);
                        uint16_t old_flags = cpu_state.flags;
                        unsigned condition = (producers[p].materialized ? (i & 1) : result == 0) ^ invert;
                        EBX = 0x87654321;
                        EDX = 0xabcdef12;
                        uint32_t expected = form == 0 ? (EBX & 0xffffff00) | condition
                            : form == 1 ? (EBX & 0xffff00ff) | (condition << 8)
                            : form == 2 ? (condition ? (EBX & 0xffff0000) | (EDX & 0xffff) : EBX)
                                        : (condition ? EDX : EBX);
                        entry();
                        CHECK(EBX == expected && EDX == 0xabcdef12);
                        CHECK(cpu_state.flags == old_flags && cpu_state.flags_op == producers[p].op);
                        CHECK(cpu_state.flags_res == result && cpu_state.flags_op1 == 0xdeadbeef
                              && cpu_state.flags_op2 == 0x12345678);
                        checks++;
                    }
                }
#ifdef CODEGEN_BACKEND_HAS_CMP_Z
    /* Check all host byte encodings and dest/source overlap independently
       of the register allocator's choices in the instruction cases above. */
    const uint32_t values[] = {0, 1, 0x80000000, UINT32_MAX};
    for (int parity = 0; parity < 2; parity++)
    for (int d = 0; d < CODEGEN_HOST_REGS; d++)
        for (int s = 0; s < CODEGEN_HOST_REGS; s++)
            for (int invert = 0; invert < 2; invert++)
                for (unsigned v = 0; v < sizeof(values) / sizeof(values[0]); v++) {
                    begin_test("zero compare aliases");
                    int dest = codegen_host_reg_list[d].reg, src = codegen_host_reg_list[s].reg;
                    codegen_backend_prologue(&bench_block);
                    host_x86_MOV32_REG_IMM(&bench_block, dest, UINT32_MAX);
                    host_x86_MOV32_REG_IMM(&bench_block, src, values[v]);
                    uop_t op = {.dest_reg_a_real = dest | IREG_SIZE_L, .src_reg_a_real = src | IREG_SIZE_L,
                                .imm_data = invert};
                    if (parity) codegen_PARITY(&bench_block, &op);
                    else codegen_CMP_Z(&bench_block, &op);
                    host_x86_MOV32_ABS_REG(&bench_block, &EAX, dest);
                    host_x86_MOV32_ABS_REG(&bench_block, &EBX, src);
                    codegen_backend_epilogue(&bench_block);
                    flush_code();
                    ((jit_fn) bench_block.data)();
                    CHECK(EAX == (unsigned) ((parity ? v != 1 : values[v] == 0) ^ invert));
                    CHECK(EBX == (src == dest ? EAX : values[v]));
                    checks++;
                }
#endif
}

/* Compare low-byte parity independently of the emulator's lookup table,
   including stale upper bits and branches that leave through the shared exit. */
static void test_parity_conditions(void)
{
    const int ops[] = {FLAGS_ZN8, FLAGS_ADD16, FLAGS_SUB32, FLAGS_SHL32, FLAGS_ADC32,
                       FLAGS_SBC32, FLAGS_SHRD16, FLAGS_UNKNOWN, FLAGS_ROL8,
                       FLAGS_MUL32, FLAGS_IMUL8};
    for (unsigned op = 0; op < sizeof(ops) / sizeof(ops[0]); op++)
        for (int known = 0; known < 2; known++)
            for (int invert = 0; invert < 2; invert++)
                for (int form = 0; form < 4; form++) {
                    ir_data_t *ir = begin_test("parity conditions");
                    codegen_flags_changed = known;
                    cpu_state.flags_op = ops[op];
                    if (form == 0)
                        bench_setcc[10 + invert](&bench_block, ir, 0, 0xc7, 0x300, 0x101);
                    else if (form < 3)
                        bench_cmov[form - 1][10 + invert](&bench_block, ir, 0, 0xda, 0x300, 0x101);
                    else {
                        instruction_bytes[0x101] = 0x20;
                        if (invert) ropJNP_8(&bench_block, ir, 0, 0, 0x300, 0x101);
                        else ropJP_8(&bench_block, ir, 0, 0, 0x300, 0x101);
                        uop_MOV_IMM(ir, IREG_pc, 0x102);
                    }
                    jit_fn entry = finish_test(ir);
                    for (unsigned i = 0; i < 512; i++) {
                        uint32_t result = (random_u32() & 0xffffff00) | (i & 255);
                        unsigned ones = 0;
                        for (unsigned b = 0; b < 8; b++) ones += (result >> b) & 1;
                        unsigned parity = op < 7 ? !(ones & 1) : (i >> 8);
                        unsigned condition = parity ^ invert;
                        cpu_state.flags_res = result;
                        cpu_state.flags = 0x203 | ((i >> 8) ? P_FLAG : 0);
                        uint16_t old_flags = cpu_state.flags;
                        EBX = 0x12345678; EDX = 0xabcdef12;
                        uint32_t expected = form == 0 ? (EBX & 0xffff00ff) | (condition << 8)
                            : form == 1 ? (condition ? (EBX & 0xffff0000) | (EDX & 0xffff) : EBX)
                            : (condition ? EDX : EBX);
                        entry();
                        if (form == 3) CHECK(cpu_state.pc == (condition ? 0x122 : 0x102));
                        else CHECK(EBX == expected && EDX == 0xabcdef12);
                        CHECK(cpu_state.flags_res == result && cpu_state.flags_op == ops[op]);
                        CHECK(cpu_state.flags == old_flags);
                        checks++;
                    }
                }
}

#ifdef CODEGEN_BACKEND_HAS_CMOV_Z
static void test_zero_cmov_aliases(void)
{
    /* Exercise all physical overlaps, including a destination that already
       holds the selected source or the lazy result. Word CMOV keeps its high half. */
    for (int word = 0; word < 2; word++)
        for (int d = 0; d < CODEGEN_HOST_REGS; d++)
            for (int a = 0; a < CODEGEN_HOST_REGS; a++)
                for (int b = 0; b < CODEGEN_HOST_REGS; b++)
                    for (int c = 0; c < CODEGEN_HOST_REGS; c++)
                        for (int invert = 0; invert < 2; invert++)
                            for (int zero = 0; zero < 2; zero++) {
                                begin_test("zero CMOV aliases");
                                codegen_backend_prologue(&bench_block);
                                uint32_t values[CODEGEN_HOST_REGS];
                                for (int r = 0; r < CODEGEN_HOST_REGS; r++) {
                                    values[r] = r == c ? (zero ? 0 : 0x80000000u) : 0x12348000u + r;
                                    host_x86_MOV32_REG_IMM(&bench_block, codegen_host_reg_list[r].reg, values[r]);
                                }
                                int size = word ? IREG_SIZE_W : IREG_SIZE_L;
                                uop_t op = {.dest_reg_a_real = codegen_host_reg_list[d].reg | size,
                                    .src_reg_a_real = codegen_host_reg_list[a].reg | size,
                                    .src_reg_b_real = codegen_host_reg_list[b].reg | size,
                                    .src_reg_c_real = codegen_host_reg_list[c].reg | IREG_SIZE_L,
                                    .imm_data = invert};
                                codegen_CMOV_Z(&bench_block, &op);
                                host_x86_MOV32_ABS_REG(&bench_block, &EAX, codegen_host_reg_list[d].reg);
                                codegen_backend_epilogue(&bench_block);
                                flush_code();
                                ((jit_fn) bench_block.data)();
                                uint32_t expected = values[(zero ^ invert) ? b : a];
                                if (word) expected = (values[d] & 0xffff0000) | (expected & 0xffff);
                                CHECK(EAX == expected);
                                checks++;
                            }
}
#endif

static void test_carry_faults(void)
{
    for (int subtract = 0; subtract <= 1; subtract++) {
        ir_data_t *ir = begin_test("carry/memory fault ordering");
        cpu_state.flags_op = FLAGS_SUB32;
        cpu_state.oldpc = 0x100;
        codegen_flags_changed = 1;
        uint32_t imm = 7;
        memcpy(instruction_bytes + 0x102, &imm, 4);
        rop81_l(&bench_block, ir, 0x81, subtract ? 0x18 : 0x10, 0x300, 0x101);
        jit_fn entry = finish_test(ir);
        for (int fault = 1; fault <= 2; fault++) {
            uint32_t operand = 0x12345678;
            memcpy(memory + 0x4000, &operand, 4);
            prepare_flags(FLAGS_SUB32, 32, 0, UINT32_MAX, 0);
            cpu_state.abrt = 0;
            EAX = 0x4000;
            readlookup2[4] = writelookup2[4] = (uintptr_t) -1;
            test_fault_access = fault;
            entry();
            CHECK(cpu_state.abrt == 14 && cpu_state.oldpc == 0x100);
            CHECK(cpu_state.flags_op == FLAGS_SUB32 && cpu_state.flags_op1 == 0);
            CHECK(cpu_state.flags_op2 == UINT32_MAX && cpu_state.flags_res == 1);
            CHECK(!memcmp(memory + 0x4000, &operand, 4));
            readlookup2[4] = writelookup2[4] = (uintptr_t) memory;
            checks++;
        }
    }
}

static void test_condition_version_limit(void)
{
    for (int add = 0; add <= 1; add++)
        for (int below_equal = 0; below_equal <= 1; below_equal++) {
            ir_data_t *ir = begin_test("carry/register version boundary");
            /* The decoder can enter an instruction with version 250. It
               only honors CPU_BLOCK_END after that instruction is emitted. */
            for (int i = 0; i < REG_VERSION_MAX; i++) {
                uop_MOV_IMM(ir, IREG_temp0, i);
                uop_MOV_IMM(ir, IREG_temp1, i);
            }
            cpu_block_end = 0;
            cpu_state.flags_op = add ? FLAGS_ADD32 : FLAGS_SUB32;
            codegen_flags_changed = 1;
            if (below_equal) setcc_gen_BE(ir, 0);
            else setcc_gen_B(ir, 1);
            CHECK(cpu_block_end && reg_last_version[IREG_temp0] >= REG_VERSION_MAX);
            CHECK(reg_last_version[IREG_temp1] >= REG_VERSION_MAX);
            uop_MOV(ir, IREG_EAX, IREG_temp0);
            jit_fn entry = finish_test(ir);
            prepare_flags(add ? FLAGS_ADD32 : FLAGS_SUB32, 32, add ? UINT32_MAX : 0, 1, 0);
            entry();
            CHECK(EAX == (unsigned) below_equal);
            checks++;
        }
}

#ifdef CODEGEN_BACKEND_HAS_CMP_ULT
static void test_native_compare_aliases(void)
{
    /* Exercise every allocator register, including REX byte encodings and
       destinations aliased with either comparison operand. */
    for (int d = 0; d < CODEGEN_HOST_REGS; d++)
        for (int a = 0; a < CODEGEN_HOST_REGS; a++)
            for (int b = 0; b < CODEGEN_HOST_REGS; b++)
                for (int invert = 0; invert <= 1; invert++) {
                    begin_test("carry/native compare aliases");
                    int dest = codegen_host_reg_list[d].reg;
                    int lhs = codegen_host_reg_list[a].reg, rhs = codegen_host_reg_list[b].reg;
                    codegen_backend_prologue(&bench_block);
                    host_x86_MOV32_REG_IMM(&bench_block, lhs, 0x80000000u);
                    host_x86_MOV32_REG_IMM(&bench_block, rhs, UINT32_MAX);
                    uop_t uop = { .dest_reg_a_real = dest | IREG_SIZE_L,
                                  .src_reg_a_real = lhs | IREG_SIZE_L,
                                  .src_reg_b_real = rhs | IREG_SIZE_L, .imm_data = invert };
                    codegen_CMP_ULT(&bench_block, &uop);
                    host_x86_MOV32_ABS_REG(&bench_block, &EAX, dest);
                    codegen_backend_epilogue(&bench_block);
                    flush_code();
                    ((jit_fn) bench_block.data)();
                    CHECK(EAX == (unsigned) ((lhs != rhs) ^ invert));
                    checks++;
                }
}
#endif

#ifdef _WIN64
static uint8_t sentinel[160], observed[160];

/* A generated caller avoids compiler/inline-asm assumptions about Win64
   shadow space and XMM clobbers. Raw emitters must still save the full ABI. */
static jit_fn abi_caller(jit_fn child)
{
    uint8_t *entry = start_code();
    codegen_backend_prologue(&bench_block);
    for (int r = 6; r <= 15; r++)
        host_x86_MOVDQU_XREG_ABS(&bench_block, r, sentinel + 16 * (r - 6));
    host_x86_CALL(&bench_block, child);
    for (int r = 6; r <= 15; r++)
        host_x86_MOVDQU_ABS_XREG(&bench_block, observed + 16 * (r - 6), r);
    codegen_backend_epilogue(&bench_block);
    flush_code();
    return (jit_fn) entry;
}

static void test_xmm_abi(void)
{
    for (unsigned i = 0; i < sizeof(sentinel); i++) sentinel[i] = i * 17 + 3;
    /* Include no XMM, read-only allocation, all 15 allocatable XMM registers,
       spilling, joins, C helpers, memory faults and the shared branch exit. */
    for (unsigned live = 0; live <= 16; live++)
        for (int path = 0; path < 6; path++) {
            ir_data_t *ir = begin_test("Win64 XMM preservation");
            for (unsigned r = 0; r < live; r++) {
                int reg = r < 8 ? IREG_XMM(r) : IREG_MM(r - 8);
                uop_PADDD(ir, reg, reg, reg);
            }
            if (path == 1 || path == 2 || path == 5) {
                uop_MOV_IMM(ir, IREG_eaaddr, 0x4000);
                /* An abort must also write back the old SIMD destination,
                   including one assigned beyond bit 15 of the host mask. */
                int dest = path == 5 ? (live > 8 ? IREG_MM(0) : IREG_XMM(0)) : IREG_EAX;
                uop_MEM_LOAD_REG(ir, dest, IREG_DS_base, IREG_eaaddr);
            }
            if (path == 3) uop_JMP(ir, codegen_exit_rout);
            if (path == 4) {
                int jump = uop_CMP_IMM_JNZ_DEST(ir, IREG_EAX, 0);
                uop_MOV_IMM(ir, IREG_ECX, 123);
                uop_set_jump_dest(ir, jump);
            }
            for (unsigned r = 0; r < live; r++) {
                int reg = r < 8 ? IREG_XMM(r) : IREG_MM(r - 8);
                uop_PADDD(ir, reg, reg, reg);
            }
            jit_fn child = finish_test(ir);
#ifdef CODEGEN_BACKEND_HAS_SELECTIVE_XMM
            uint16_t used = codegen_win64_xmm_used;
            CHECK(live || path == 5 || !used);
            if (live == 16) CHECK((used & 0xffc0) == 0xffc0);
#endif
            jit_fn entry = abi_caller(child);
            cpu_state_t before = cpu_state;
            int fault = path == 2 || path == 5;
            readlookup2[4] = path == 1 || fault ? (uintptr_t) -1 : (uintptr_t) memory;
            test_fault_access = fault ? 1 : 0;
            entry();
            CHECK(!memcmp(sentinel, observed, sizeof(sentinel)));
            CHECK(cpu_state.abrt == (fault ? 14 : 0));
            unsigned factor = fault || path == 3 ? 2 : 4;
            for (unsigned r = 0; r < live; r++)
                for (unsigned lane = 0; lane < (r < 8 ? 4u : 2u); lane++) {
                    uint32_t actual = r < 8 ? cpu_state.XMM[r].l[lane] : cpu_state.MM[r - 8].l[lane];
                    uint32_t expected = (r < 8 ? before.XMM[r].l[lane] : before.MM[r - 8].l[lane]) * factor;
                    if (actual != expected)
                        fatal("live=%u path=%d reg=%u lane=%u: %08x != %08x\n", live, path, r, lane, actual, expected);
                }
            readlookup2[4] = (uintptr_t) memory;
            checks++;
        }
    /* Read-only SIMD sources also clobber a host XMM when loaded. */
    ir_data_t *ir = begin_test("Win64 read-only XMM allocation");
    uop_MOV_IMM(ir, IREG_eaaddr, 0x4000);
    uop_MEM_STORE_REG(ir, IREG_DS_base, IREG_eaaddr, IREG_XMM(0));
    jit_fn child = finish_test(ir);
#ifdef CODEGEN_BACKEND_HAS_SELECTIVE_XMM
    CHECK(codegen_win64_xmm_used);
#endif
    abi_caller(child)();
    CHECK(!memcmp(sentinel, observed, sizeof(sentinel)));
    CHECK(!memcmp(memory + 0x4000, &cpu_state.XMM[0], 16));
    checks++;
    /* Force the body and restore sequence into later allocator chunks. */
    ir = begin_test("Win64 multi-chunk epilogue");
    for (unsigned i = 0; i < 192; i++) {
        uop_PADDD(ir, IREG_XMM(i % 8), IREG_XMM(i % 8), IREG_XMM(i % 8));
        uop_MEM_STORE_REG(ir, IREG_DS_base, IREG_EAX, IREG_XMM(i % 8));
    }
    child = finish_test(ir);
    CHECK(next_chunk > first_codegen_chunk + 1);
    EAX = 0x4000;
    abi_caller(child)();
    CHECK(!memcmp(sentinel, observed, sizeof(sentinel)));
    checks++;
}
#endif

static void test_ret_imm(void)
{
    const uint32_t stacks[] = {0x4000, 0xfffc, 0x7654fffc, 0x4fff};
    const uint16_t immediates[] = {0, 4, 0x8000, 0xffff};
    for (int wide = 0; wide <= 1; wide++)
        for (int dynamic = 0; dynamic <= 1; dynamic++)
            for (int split = 0; split <= 1; split++)
                for (unsigned s = 0; s < 4; s++)
                    for (unsigned n = 0; n < 4; n++) {
                        if (wide && s == 2) continue;
                        ir_data_t *ir = begin_test("RET imm16 / operand32");
                        uint32_t pc = split ? 0xfff : 0x101;
                        memcpy(instruction_bytes + pc, &immediates[n], 2);
                        if (dynamic) bench_block.flags |= CODEBLOCK_NO_IMMEDIATES;
                        stack32 = wide;
                        cpu_state.oldpc = pc - 1;
                        CHECK(ropRET_imm_32(&bench_block, ir, 0xc2, 0, 0x300, pc) == UINT32_MAX);
                        jit_fn entry = finish_test(ir);
                        for (int fault = 0; fault < 3; fault++) {
                            uint16_t imm = dynamic ? immediates[(n + 1) % 4] : immediates[n];
                            if (dynamic) memcpy(instruction_bytes + pc, &imm, 2);
                            ESP = stacks[s];
                            uint32_t address = wide ? ESP : SP;
                            uint32_t target = 0xdeadbeef;
                            memcpy(memory + address, &target, 4);
                            cpu_state.pc = 0x1234;
                            cpu_state.abrt = 0;
                            cpu_state.seg_ss.limit_high = fault == 1 ? address + 2 : UINT32_MAX;
                            readlookup2[address >> 12] = fault == 2 ? (uintptr_t) -1 : (uintptr_t) memory;
                            test_fault_access = fault == 2 ? 1 : 0;
                            entry();
                            uint32_t expected_sp = wide ? stacks[s] + 4 + imm
                                : (stacks[s] & 0xffff0000) | (uint16_t) (stacks[s] + 4 + imm);
                            CHECK(cpu_state.abrt == (fault == 1 ? 12 : fault == 2 ? 14 : 0));
                            CHECK(ESP == (fault ? stacks[s] : expected_sp));
                            CHECK(cpu_state.pc == (fault ? 0x1234 : target));
                            CHECK(cpu_state.oldpc == pc - 1);
                            readlookup2[address >> 12] = (uintptr_t) memory;
                            checks++;
                        }
                    }
}

static void test_incdec_carry(void)
{
    const int producers[] = {FLAGS_ADD8, FLAGS_ADD16, FLAGS_ADD32, FLAGS_SUB8, FLAGS_SUB16, FLAGS_SUB32,
                            FLAGS_ZN8, FLAGS_ZN16, FLAGS_ZN32, FLAGS_INC32, FLAGS_DEC32, FLAGS_ADC32, FLAGS_SBC32, FLAGS_UNKNOWN};
    for (unsigned p = 0; p < sizeof(producers) / sizeof(producers[0]); p++)
        for (int known = 0; known < 2; known++)
            for (int width = 0; width < 3; width++)
                for (int form = 0; form < 3; form++)
                    for (int decrement = 0; decrement < 2; decrement++) {
                        ir_data_t *ir = begin_test("INC/DEC carry preservation");
                        cpu_state.flags_op = producers[p];
                        codegen_flags_changed = known;
                        int mem = form == 2, high = !width && form == 1;
                        uint32_t modrm = (decrement ? 8 : 0) | (mem ? 0 : high ? 0xc7 : 0xc3);
                        if (!width) ropINCDEC(&bench_block, ir, 0xfe, modrm, 0x300, 0x101);
                        else if (form) (width == 1 ? ropFF_16 : ropFF_32)(&bench_block, ir, 0xff, modrm, 0x300, 0x101);
                        else {
                            translator op = decrement ? (width == 1 ? ropDEC_r16 : ropDEC_r32) : (width == 1 ? ropINC_r16 : ropINC_r32);
                            op(&bench_block, ir, decrement ? 0x4b : 0x43, 0, 0x300, 0x101);
                        }
                        jit_fn entry = finish_test(ir);
                        unsigned bits = 8u << width, producer_bits = p < 9 ? 8u << (p % 3) : 32;
                        uint32_t mask = UINT32_MAX >> (32 - bits);
                        for (unsigned i = 0; i < 512; i++) {
                            uint32_t a = random_u32(), b = random_u32();
                            unsigned carry = prepare_flags(producers[p], producer_bits, a, b, i & 1);
                            uint16_t flags_before = cpu_state.flags;
                            uint32_t value = (i < 8 ? (i / 2 & 1 ? (1u << (bits - 1)) : 0) - (i & 1) : random_u32()) & mask;
                            EBX = high ? 0x12340078 | (value << 8) : (0x12345678 & ~mask) | value;
                            uint32_t saved_ebx = EBX;
                            EAX = 0x4000;
                            memcpy(memory + EAX, &value, bits / 8);
                            int fault = mem && i % 8 == 0 ? (i / 8 % 2) + 1 : 0;
                            readlookup2[EAX >> 12] = writelookup2[EAX >> 12] = fault ? (uintptr_t) -1 : (uintptr_t) memory;
                            test_fault_access = fault;
                            cpu_state.abrt = 0;
                            entry();
                            uint32_t expected = fault ? value : (value + (decrement ? -1 : 1)) & mask;
                            uint32_t actual = 0;
                            if (mem) memcpy(&actual, memory + EAX, bits / 8);
                            else actual = high ? BH : EBX & mask;
                            CHECK(actual == expected && cpu_state.abrt == (fault ? 14 : 0));
                            CHECK(!!CF_SET() == carry);
                            CHECK((cpu_state.flags & ~C_FLAG) == (flags_before & ~C_FLAG));
                            if (mem) CHECK(EBX == saved_ebx);
                            else CHECK((EBX & ~(high ? 0xff00 : mask)) == (saved_ebx & ~(high ? 0xff00 : mask)));
                            if (fault) CHECK(cpu_state.flags_op == producers[p]);
                            else CHECK(cpu_state.flags_res == expected);
                            checks++;
                        }
                        readlookup2[4] = writelookup2[4] = (uintptr_t) memory;
                    }
}

static void test_cmov_memory(void)
{
    const uint32_t edge[] = {0, 1, 0x7fffffff, 0x80000000, UINT32_MAX};
    for (int wide = 0; wide < 2; wide++)
        for (int cond = 0; cond < 16; cond++)
            for (int known = 0; known < 2; known++)
                for (int alias = 0; alias < 2; alias++) {
                    ir_data_t *ir = begin_test("CMOV memory source");
                    cpu_state.oldpc = 0x100;
                    cpu_state.flags_op = FLAGS_SUB32;
                    codegen_flags_changed = known;
                    if (!alias) uop_MOV_IMM(ir, IREG_EBX, 0xa5a55a5a);
                    for (int r = 0; r < 8; r++) uop_PADDD(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(r));
                    CHECK(bench_cmov[wide][cond](&bench_block, ir, 0, alias ? 0 : 0x18, 0x300, 0x101) == 0x102);
                    for (int r = 0; r < 8; r++) uop_PADDD(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(r));
                    jit_fn entry = finish_test(ir);
                    for (unsigned i = 0; i < 256; i++)
                        for (int location = 0; location < 3; location++)
                            for (int fault = 0; fault < 3; fault++) {
                                uint32_t a = i < 25 ? edge[i / 5] : random_u32();
                                uint32_t b = i < 25 ? edge[i % 5] : random_u32();
                                prepare_flags(FLAGS_SUB32, 32, a, b, 0);
                                uint32_t address = location == 1 ? 0x4fff : 0x4000;
                                uint32_t value = random_u32(), mask = wide ? UINT32_MAX : 0xffff;
                                EAX = address;
                                EBX = 0;
                                memcpy(memory + address, &value, wide ? 4 : 2);
                                cpu_state.abrt = 0;
                                cpu_state.oldpc = 0;
                                cpu_state.seg_ds.limit_high = fault == 1 ? address : UINT32_MAX;
                                readlookup2[address >> 12] = location == 2 || fault ? (uintptr_t) -1 : (uintptr_t) memory;
                                test_fault_access = fault ? 1 : 0;
                                helper_calls = 0;
                                for (int r = 0; r < 8; r++)
                                    for (int lane = 0; lane < 4; lane++) cpu_state.XMM[r].l[lane] = 1 + r * 4 + lane;
                                entry();
                                uint32_t expected = alias ? address : 0xa5a55a5a;
                                if (!fault && condition_oracle(cond, a, b, 32, 0)) expected = (expected & ~mask) | (value & mask);
                                CHECK((alias ? EAX : EBX) == expected);
                                CHECK(cpu_state.abrt == (fault == 1 ? 13 : fault == 2 ? 14 : 0));
                                CHECK(cpu_state.oldpc == 0x100);
                                CHECK(cpu_state.flags_op == FLAGS_SUB32 && cpu_state.flags_op1 == a && cpu_state.flags_op2 == b);
                                CHECK(cpu_state.flags_res == a - b && cpu_state.flags == 0x202);
                                /* Segment faults precede the read, including false conditions. */
                                if (fault == 1) CHECK(helper_calls == 0);
                                else if (location || fault) CHECK(helper_calls != 0);
                                for (int r = 0; r < 8; r++)
                                    for (int lane = 0; lane < 4; lane++) CHECK(cpu_state.XMM[r].l[lane] == (unsigned) (1 + r * 4 + lane) * (fault ? 2 : 4));
                                readlookup2[address >> 12] = (uintptr_t) memory;
                                checks++;
                            }
                }
}

static void test_umul_aliases(void)
{
    static uint32_t input[CODEGEN_HOST_REGS], output[CODEGEN_HOST_REGS];
    const uint32_t edge[] = {0, 1, 0xffff, 0x8000, 0x80000000, UINT32_MAX};
    for (int wide = 0; wide < 2; wide++)
        for (int d = 0; d < CODEGEN_HOST_REGS; d++)
            for (int a = 0; a < CODEGEN_HOST_REGS; a++)
                for (int b = 0; b < CODEGEN_HOST_REGS; b++) {
                    begin_test("low-half UMUL aliases");
                    codegen_backend_prologue(&bench_block);
                    for (int r = 0; r < CODEGEN_HOST_REGS; r++)
                        host_x86_MOV32_REG_ABS(&bench_block, codegen_host_reg_list[r].reg, &input[r]);
                    int size = wide ? IREG_SIZE_L : IREG_SIZE_W;
                    uop_t uop = { .dest_reg_a_real = codegen_host_reg_list[d].reg | size,
                                  .src_reg_a_real = codegen_host_reg_list[a].reg | size,
                                  .src_reg_b_real = codegen_host_reg_list[b].reg | size };
                    codegen_UMUL(&bench_block, &uop);
                    for (int r = 0; r < CODEGEN_HOST_REGS; r++)
                        host_x86_MOV32_ABS_REG(&bench_block, &output[r], codegen_host_reg_list[r].reg);
                    codegen_backend_epilogue(&bench_block);
                    flush_code();
                    for (unsigned i = 0; i < 256; i++) {
                        for (int r = 0; r < CODEGEN_HOST_REGS; r++) input[r] = random_u32();
                        input[a] = i < 36 ? edge[i / 6] : random_u32();
                        input[b] = i < 36 ? edge[i % 6] : random_u32();
                        uint32_t mask = wide ? UINT32_MAX : 0xffff;
                        uint32_t product = (uint64_t) (input[a] & mask) * (input[b] & mask);
                        ((jit_fn) bench_block.data)();
                        for (int r = 0; r < CODEGEN_HOST_REGS; r++)
                            CHECK(output[r] == (r == d ? (input[d] & ~mask) | (product & mask) : input[r]));
                        checks++;
                    }
                }
}

/* Work in unsigned magnitudes, including INT64_MIN / -1, so the oracle
   cannot itself take the host exception being checked. */
static int divide_oracle(unsigned bits, int is_signed, uint64_t dividend, uint32_t divisor,
                         uint32_t *quotient, uint32_t *remainder)
{
    uint32_t mask = UINT32_MAX >> (32 - bits);
    uint64_t dividend_mask = bits == 32 ? UINT64_MAX : (UINT64_C(1) << (2 * bits)) - 1;
    dividend &= dividend_mask;
    divisor &= mask;
    int negative_n = is_signed && (dividend >> (2 * bits - 1));
    int negative_d = is_signed && (divisor >> (bits - 1));
    uint64_t numerator = negative_n ? (-dividend & dividend_mask) : dividend;
    uint32_t denominator = negative_d ? (-divisor & mask) : divisor;
    if (!denominator) return 1;
    uint64_t q = numerator / denominator, r = numerator % denominator;
    uint64_t limit = is_signed ? (UINT64_C(1) << (bits - 1)) - !(negative_n ^ negative_d) : mask;
    if (q > limit) return 1;
    *quotient = (uint32_t) ((negative_n ^ negative_d) ? -q : q) & mask;
    *remainder = (uint32_t) (negative_n ? -r : r) & mask;
    return 0;
}

static void test_division(void)
{
    const uint64_t edge[] = {0, 1, 0x7f, 0x80, 0xff, 0x100, 0x7fff, 0x8000, 0xffff,
                             0x10000, 0x7fffffff, 0x80000000, 0xffffffff, 0x100000000,
                             UINT64_C(0x8000000000000000), UINT64_MAX};
    const int live_regs[] = {IREG_ECX, IREG_ESP, IREG_EBP, IREG_ESI, IREG_EDI};
    for (int width = 0; width < 3; width++)
        for (int sign = 0; sign < 2; sign++)
            for (int form = 0; form < 5; form++)
                for (int model = 0; model < 3; model++) {
                    ir_data_t *ir = begin_test("DIV/IDIV results and faults");
                    cpu_iscyrix = model == 1;
                    is6117 = model == 2;
                    cpu_state.oldpc = 0x100;
                    for (int r = 0; r < 5; r++) uop_ADD_IMM(ir, live_regs[r], live_regs[r], 1);
                    for (int r = 0; r < 8; r++) uop_PADDD(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(r));
                    unsigned operand = form == 0 ? 3 : form == 1 ? 0 : form == 2 ? 2 : 4;
                    uint32_t modrm = (sign ? 0x38 : 0x30) | (form == 4 ? 0 : 0xc0 | operand);
                    translator op = width == 0 ? ropF6 : width == 1 ? ropF7_16 : ropF7_32;
                    CHECK(op(&bench_block, ir, 0, modrm, 0x300, 0x101) == 0x102);
                    for (int r = 0; r < 5; r++) uop_ADD_IMM(ir, live_regs[r], live_regs[r], 2);
                    for (int r = 0; r < 8; r++) uop_PADDD(ir, IREG_XMM(r), IREG_XMM(r), IREG_XMM(r));
                    jit_fn entry = finish_test(ir);
                    unsigned bits = 8u << width;
                    uint32_t mask = UINT32_MAX >> (32 - bits);
                    for (unsigned i = 0; i < 1024; i++) {
                        uint64_t dividend = i < 256 ? edge[i / 16] : ((uint64_t) random_u32() << 32) | random_u32();
                        uint32_t divisor = i < 256 ? edge[i % 16] : random_u32();
                        for (int r = 0; r < 8; r++) cpu_state.regs[r].l = random_u32();
                        EAX = width == 0 ? (EAX & 0xffff0000) | (uint16_t) dividend : (EAX & ~mask) | (dividend & mask);
                        if (width) EDX = (EDX & ~mask) | ((dividend >> bits) & mask);
                        EBX = divisor;
                        if (form == 4) EAX = i & 1 ? 0x4fff : 0x4000;
                        memcpy(memory + 0x4000, &divisor, bits / 8);
                        memcpy(memory + 0x4fff, &divisor, bits / 8);
                        uint32_t before[8];
                        for (int r = 0; r < 8; r++) before[r] = cpu_state.regs[r].l;
                        if (form != 4) divisor = !width && operand == 4 ? AH : cpu_state.regs[operand].l;
                        /* ESP is deliberately live and dirty before DIV. */
                        if (width && form == 3) divisor++;
                        dividend = !width ? AX : ((uint64_t) (EDX & mask) << bits) | (EAX & mask);
                        uint32_t q = 0, rem = 0;
                        int fault = divide_oracle(bits, sign, dividend, divisor, &q, &rem);
                        int read_fault = form == 4 && i % 4 == 0;
                        uint32_t address = EAX;
                        if (form == 4) readlookup2[address >> 12] = (uintptr_t) -1;
                        test_fault_access = read_fault ? 1 : 0;
                        cpu_state.abrt = 0;
                        prepare_flags(FLAGS_SUB32, 32, 1, 2, 0);
                        cpu_state.oldpc = 0;
                        for (int r = 0; r < 8; r++)
                            for (int lane = 0; lane < 4; lane++) cpu_state.XMM[r].l[lane] = r * 4 + lane + 1;
                        entry();
                        CHECK(cpu_state.abrt == (read_fault ? 14 : fault ? 1 : 0));
                        if (fault || read_fault) CHECK(cpu_state.oldpc == 0x100);
                        int failed = fault || read_fault;
                        if (!failed) {
                            before[0] = !width ? (before[0] & 0xffff0000) | q | (rem << 8) : (before[0] & ~mask) | q;
                            if (width) before[2] = (before[2] & ~mask) | rem;
                        }
                        for (int r = 0; r < 5; r++) before[IREG_GET_REG(live_regs[r])] += failed ? 1 : 3;
                        for (int r = 0; r < 8; r++) CHECK(cpu_state.regs[r].l == before[r]);
                        if (failed || model) CHECK(cpu_state.flags_op == FLAGS_SUB32 && cpu_state.flags_res == UINT32_MAX && cpu_state.flags == 0x202);
                        else if (!width) CHECK(cpu_state.flags_op == FLAGS_UNKNOWN && cpu_state.flags == 0xad6);
                        else CHECK(cpu_state.flags_op == (width == 1 ? FLAGS_ZN16 : FLAGS_ZN32) && cpu_state.flags_res == q && cpu_state.flags == 0x202);
                        for (int r = 0; r < 8; r++)
                            for (int lane = 0; lane < 4; lane++) CHECK(cpu_state.XMM[r].l[lane] == (unsigned) (r * 4 + lane + 1) * (failed ? 2 : 4));
                        if (form == 4) readlookup2[address >> 12] = (uintptr_t) memory;
                        checks++;
                    }
                }
    cpu_iscyrix = is6117 = 0;
}

int main(void)
{
#ifdef _WIN32
    code_memory = VirtualAlloc(NULL, CODE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
#else
    code_memory = mmap(NULL, CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(code_memory != MAP_FAILED);
#endif
    CHECK(code_memory);
    ram = memory;
    memset(readlookup2, 0xff, sizeof(readlookup2));
    memset(writelookup2, 0xff, sizeof(writelookup2));
    for (unsigned i = 0; i < RAM_SIZE / 4096; i++) readlookup2[i] = writelookup2[i] = (uintptr_t) memory;
    prepare_codegen();
    /* Exception dispatch is outside this fixture; retain the real checks and
       shared exit while recording the exception vector for assertions. */
    codegen_ss_rout = start_code();
    host_x86_MOV8_ABS_IMM(&bench_block, &cpu_state.abrt, 12);
    host_x86_JMP(&bench_block, codegen_exit_rout);
    codegen_gpf_rout = start_code();
    host_x86_MOV8_ABS_IMM(&bench_block, &cpu_state.abrt, 13);
    host_x86_JMP(&bench_block, codegen_exit_rout);
    first_codegen_chunk = next_chunk;
    test_conditions();
    test_adc_sbb();
    test_signed_conditions();
    test_overflow_conditions();
#ifdef CODEGEN_BACKEND_HAS_OVERFLOW
    test_overflow_aliases();
#endif
    test_zero_conditions();
    test_parity_conditions();
#ifdef CODEGEN_BACKEND_HAS_CMOV_Z
    test_zero_cmov_aliases();
#endif
    test_incdec_carry();
    test_cmov_memory();
    test_umul_aliases();
    test_division();
    test_carry_faults();
    test_condition_version_limit();
#ifdef CODEGEN_BACKEND_HAS_CMP_ULT
    test_native_compare_aliases();
#endif
#ifdef _WIN64
    test_xmm_abi();
#endif
    test_ret_imm();
    printf("cpu_fastpath_test: %u executions passed\n", checks);
    return 0;
}
