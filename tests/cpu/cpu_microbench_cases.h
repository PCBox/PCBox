/* Larger matrices share the same arena, timing loop and validation as the
   original cases. Keep decoder inputs explicit: these are block benchmarks. */
typedef uint32_t (*bench_translator)(codeblock_t *, ir_data_t *, uint8_t, uint32_t, uint32_t, uint32_t);

static const bench_translator bench_setcc[] = {
    ropSETO, ropSETNO, ropSETB, ropSETNB, ropSETE, ropSETNE, ropSETBE, ropSETNBE,
    ropSETS, ropSETNS, ropSETP, ropSETNP, ropSETL, ropSETNL, ropSETLE, ropSETNLE
};
static const bench_translator bench_cmov[2][16] = {
    {ropCMOVO_w, ropCMOVNO_w, ropCMOVB_w, ropCMOVNB_w, ropCMOVE_w, ropCMOVNE_w, ropCMOVBE_w, ropCMOVNBE_w,
     ropCMOVS_w, ropCMOVNS_w, ropCMOVP_w, ropCMOVNP_w, ropCMOVL_w, ropCMOVNL_w, ropCMOVLE_w, ropCMOVNLE_w},
    {ropCMOVO_l, ropCMOVNO_l, ropCMOVB_l, ropCMOVNB_l, ropCMOVE_l, ropCMOVNE_l, ropCMOVBE_l, ropCMOVNBE_l,
     ropCMOVS_l, ropCMOVNS_l, ropCMOVP_l, ropCMOVNP_l, ropCMOVL_l, ropCMOVNL_l, ropCMOVLE_l, ropCMOVNLE_l}
};
static const char *condition_names[] = {"o", "no", "b", "nb", "e", "ne", "be", "nbe",
                                       "s", "ns", "p", "np", "l", "nl", "le", "nle"};

/* A real C-call barrier, with no device work mixed into its cost. */
static JIT_WRAPPER void bench_barrier(void) { __asm__ volatile("" ::: "memory"); }

/* Baseline model for rejected CMOV memory translations. The decoder and
   instruction timing are deliberately excluded, as on the translated path. */
#define BENCH_CMOV_FALLBACK(name, expression) \
    static JIT_WRAPPER void bench_cmov_##name(void) \
    { \
        uint32_t value = cpu_state.op32 ? readmemll(EAX) : readmemwl(EAX); \
        if (!cpu_state.abrt && (expression)) { \
            if (cpu_state.op32) EBX = value; else BX = value; \
        } \
    }
BENCH_CMOV_FALLBACK(o, VF_SET())
BENCH_CMOV_FALLBACK(no, !VF_SET())
BENCH_CMOV_FALLBACK(b, CF_SET())
BENCH_CMOV_FALLBACK(nb, !CF_SET())
BENCH_CMOV_FALLBACK(e, ZF_SET())
BENCH_CMOV_FALLBACK(ne, !ZF_SET())
BENCH_CMOV_FALLBACK(be, CF_SET() || ZF_SET())
BENCH_CMOV_FALLBACK(nbe, !CF_SET() && !ZF_SET())
BENCH_CMOV_FALLBACK(s, NF_SET())
BENCH_CMOV_FALLBACK(ns, !NF_SET())
BENCH_CMOV_FALLBACK(p, PF_SET())
BENCH_CMOV_FALLBACK(np, !PF_SET())
BENCH_CMOV_FALLBACK(l, !!NF_SET() != !!VF_SET())
BENCH_CMOV_FALLBACK(nl, !!NF_SET() == !!VF_SET())
BENCH_CMOV_FALLBACK(le, ZF_SET() || (!!NF_SET() != !!VF_SET()))
BENCH_CMOV_FALLBACK(nle, !ZF_SET() && (!!NF_SET() == !!VF_SET()))
#undef BENCH_CMOV_FALLBACK
static void (*const cmov_fallback[])(void) = {
    bench_cmov_o, bench_cmov_no, bench_cmov_b, bench_cmov_nb, bench_cmov_e, bench_cmov_ne, bench_cmov_be, bench_cmov_nbe,
    bench_cmov_s, bench_cmov_ns, bench_cmov_p, bench_cmov_np, bench_cmov_l, bench_cmov_nl, bench_cmov_le, bench_cmov_nle
};

int cpu_iscyrix, is6117;
void x86de(char *message, uint16_t error) { (void) message; (void) error; cpu_state.abrt = 1; }

static uint32_t chase_order[RAM_SIZE / 64];
static void prepare_chase(const bench_case_t *c)
{
    unsigned count = c->working_set / 64;
    uint32_t rng = 0x12345678;
    for (unsigned i = 0; i < count; i++) chase_order[i] = i * 64;
    if (c->form)
        for (unsigned i = count - 1; i; i--) {
            rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
            unsigned other = rng % (i + 1);
            uint32_t tmp = chase_order[i]; chase_order[i] = chase_order[other]; chase_order[other] = tmp;
        }
    for (unsigned i = 0; i < count; i++)
        memcpy(memory + chase_order[i], &chase_order[(i + 1) % count], 4);
}

static void make_extended_cases(void)
{
    char name[128];
    const char *locations[] = {"aligned", "unaligned", "line-split", "page-end", "page-split", "miss"};
    const char *boundaries[] = {"straight", "join", "call"};
    for (int store = 0; store < 2; store++)
        for (int size = 1; size <= 16; size *= 2)
            for (int loc = 0; loc < 6; loc++) {
                if (size == 1 && loc > 0 && loc < 5) continue;
                for (int gpr = 0; gpr <= 6; gpr += 2)
                    for (int simd = 0; simd <= 7; simd += (simd == 4 ? 3 : 2))
                        for (int boundary = 0; boundary < 3; boundary++) {
                            snprintf(name, sizeof(name), "matrix/%s%u/%s/gpr-%d/simd-%d/%s",
                                     store ? "store" : "load", size * 8, locations[loc], gpr, simd, boundaries[boundary]);
                            bench_case_t *c = add_case(name, MEMORY);
                            c->size = size; c->store = store;
                            c->address = loc == 1 ? 129 : loc == 2 ? 127 : loc == 3 ? 4096 - size : loc == 4 ? 4095 : 128;
                            c->lookup_miss = loc == 5;
                            c->gpr_pressure = gpr; c->simd_pressure = simd;
                            c->boundary = boundary; c->cached_cycles = 1;
                        }
            }
    for (int store = 0; store < 2; store++)
        for (int size = 1; size <= 16; size *= 2)
            for (unsigned working = 4096; working <= RAM_SIZE; working *= 4)
                for (int stride = 0; stride < 4; stride++)
                    for (int pressure = 0; pressure < 2; pressure++) {
                        unsigned step = stride == 0 ? size : stride == 1 ? 64 : stride == 2 ? 4096 : 4160;
                        if (step > working) continue;
                        snprintf(name, sizeof(name), "working-set/%s%u/%uKiB/stride-%u/%s", store ? "store" : "load",
                                 size * 8, working / 1024, step, pressure ? "pressure" : "plain");
                        bench_case_t *c = add_case(name, MEMORY);
                        c->size = size; c->store = store; c->working_set = working; c->stride = step;
                        c->gpr_pressure = pressure * 6; c->simd_pressure = pressure * 7;
                    }
    for (int width = 0; width < 3; width++)
        for (int producer = 0; producer < 2; producer++)
            for (int unknown = 0; unknown < 2; unknown++)
                for (int cond = 0; cond < 16; cond++)
                    for (int move = 0; move < 2; move++)
                        for (int input = 0; input < 2; input++) {
                            if (move && width == 0) continue;
                            snprintf(name, sizeof(name), "conditions/%s%u/%s%s/%s/input-%d", producer ? "add" : "cmp",
                                     8 << width, move ? "cmov" : "set", condition_names[cond], unknown ? "unknown" : "known", input);
                            bench_case_t *c = add_case(name, CONDITIONS);
                            c->size = 1 << width; c->producer = producer; c->condition = cond;
                            c->unknown_flags = unknown; c->form = move; c->backward = input;
                        }
    for (int width = 1; width < 3; width++)
        for (int producer = 0; producer < 2; producer++)
            for (int unknown = 0; unknown < 2; unknown++)
                for (int decrement = 0; decrement < 2; decrement++)
                    for (int input = 0; input < 2; input++) {
                        snprintf(name, sizeof(name), "carry/%s%u/%s/%s/input-%d", producer ? "add" : "cmp", 8 << width,
                                 decrement ? "dec" : "inc", unknown ? "unknown" : "known", input);
                        bench_case_t *c = add_case(name, CARRY_INCDEC);
                        c->size = 1 << width; c->producer = producer; c->unknown_flags = unknown;
                        c->form = decrement; c->backward = input;
                    }
    for (int size = 2; size <= 4; size *= 2)
        for (int cond = 0; cond < 16; cond++)
            for (int loc = 0; loc < 3; loc++)
                for (int input = 0; input < 2; input++) {
                    snprintf(name, sizeof(name), "cmov-memory/%u/%s/%s/input-%d", size * 8, condition_names[cond],
                             loc == 0 ? "ram" : loc == 1 ? "page-split" : "miss", input);
                    bench_case_t *c = add_case(name, CMOV_MEMORY);
                    c->size = size; c->condition = cond; c->backward = input;
                    c->address = loc == 1 ? 4095 : 128; c->lookup_miss = loc == 2;
                }
    for (int size = 2; size <= 4; size *= 2)
        for (int sign = 0; sign < 2; sign++)
            for (int live = 1; live <= 4; live *= 2) {
                snprintf(name, sizeof(name), "multiply/%s%u/live-%d", sign ? "signed" : "unsigned", size * 8, live);
                bench_case_t *c = add_case(name, MULTIPLY);
                c->size = size; c->producer = sign; c->live_regs = live;
            }
    for (int sign = 0; sign < 2; sign++)
        for (int input = 0; input < 3; input++)
            for (int simd = 0; simd <= 7; simd += (simd == 4 ? 3 : 4)) {
                snprintf(name, sizeof(name), "divide/%s32/input-%d/simd-%d", sign ? "signed" : "unsigned", input, simd);
                bench_case_t *c = add_case(name, DIVIDE);
                c->producer = sign; c->form = input; c->simd_pressure = simd;
            }
    for (unsigned working = 4096; working <= RAM_SIZE; working *= 4)
        for (int random = 0; random < 2; random++)
            for (int chains = 1; chains <= 4; chains *= 2) {
                snprintf(name, sizeof(name), "pointer-chase/%uKiB/%s/chains-%d", working / 1024,
                         random ? "shuffled" : "sequential", chains);
                bench_case_t *c = add_case(name, POINTER_CHASE);
                c->working_set = working; c->form = random; c->live_regs = chains; c->size = 4;
            }
}

static void emit_producer(ir_data_t *ir, const bench_case_t *c)
{
    static const bench_translator producers[2][3] = {
        {ropCMP_b_rm, ropCMP_w_rm, ropCMP_l_rm}, {ropADD_b_rm, ropADD_w_rm, ropADD_l_rm}
    };
    int width = c->size == 1 ? 0 : c->size == 2 ? 1 : 2;
    producers[c->producer][width](&bench_block, ir, c->producer ? 3 : 0x3b, 0xc1, 0x300, 0x101);
    cpu_state.flags_op = (c->producer ? FLAGS_ADD8 : FLAGS_SUB8) + width;
    codegen_flags_changed = !c->unknown_flags;
}

static void emit_extended(ir_data_t *ir, const bench_case_t *c, unsigned i)
{
    if (c->kind == CONDITIONS || c->kind == CARRY_INCDEC) {
        emit_producer(ir, c);
        if (c->kind == CONDITIONS) {
            if (c->form) bench_cmov[c->size == 4][c->condition](&bench_block, ir, 0, 0xda, 0x300, 0x103);
            else bench_setcc[c->condition](&bench_block, ir, 0, 0xc3, 0x300, 0x103);
        } else {
            bench_translator op = c->form ? (c->size == 4 ? ropDEC_r32 : ropDEC_r16)
                                         : (c->size == 4 ? ropINC_r32 : ropINC_r16);
            op(&bench_block, ir, c->form ? 0x4b : 0x43, 0, 0x300, 0x103);
        }
    } else if (c->kind == CMOV_MEMORY) {
        /* Compare ECX,EDX so EAX remains the supplied memory address. */
        ropCMP_l_rm(&bench_block, ir, 0x3b, 0xca, 0x300, 0x101);
        cpu_state.flags_op = FLAGS_SUB32;
        if (!bench_cmov[c->size == 4][c->condition](&bench_block, ir, 0, 0x18, 0x300, 0x103)) {
            uop_MOV_IMM(ir, IREG_op32, c->size == 4 ? 0x100 : 0);
            uop_CALL_FUNC(ir, cmov_fallback[c->condition]);
            frontend_fallbacks++;
        }
    } else if (c->kind == MULTIPLY) {
        int reg = c->size == 4 ? IREG_32(i % c->live_regs) : IREG_16(i % c->live_regs);
        int src = c->size == 4 ? IREG_EDI : IREG_DI;
        if (c->producer) uop_IMUL(ir, reg, reg, src);
        else uop_UMUL(ir, reg, reg, src);
    } else if (c->kind == POINTER_CHASE) {
        int reg = IREG_32(i % c->live_regs);
        uop_MEM_LOAD_REG(ir, reg, IREG_DS_base, reg);
    } else if (c->kind == DIVIDE) {
        uint32_t low = c->form == 0 ? 123456 : c->form == 1 ? 0x87654321 : 0xffff0001;
        uint32_t high = c->producer ? (c->form == 2 ? UINT32_MAX : 0) : c->form == 1;
        uop_MOV_IMM(ir, IREG_EAX, low);
        uop_MOV_IMM(ir, IREG_EDX, high);
        ropF7_32(&bench_block, ir, 0xf7, c->producer ? 0xfb : 0xf3, 0x300, 0x101);
        uop_ADD(ir, IREG_ESI, IREG_ESI, IREG_EAX);
        uop_ADD(ir, IREG_EDI, IREG_EDI, IREG_EDX);
        cpu_state.flags_op = FLAGS_ZN32;
    }
}

static void reset_extended(const bench_case_t *c)
{
    if (c->kind == CONDITIONS || c->kind == CARRY_INCDEC) {
        EAX = c->backward ? 0x80008080 : 0x7fff7f7f;
        ECX = c->backward ? 0x7fffffff : 1;
    } else if (c->kind == CMOV_MEMORY) {
        EAX = c->address; ECX = c->backward ? 0x80000000 : 1; EDX = 1;
    } else if (c->kind == POINTER_CHASE) {
        for (int r = 0; r < c->live_regs; r++)
            cpu_state.regs[r].l = chase_order[r * (c->working_set / 64) / c->live_regs];
    } else if (c->kind == DIVIDE)
        EBX = c->producer ? (uint32_t) -257 : 257;
}

static unsigned condition_oracle(unsigned condition, uint32_t a, uint32_t b, unsigned bits, int add)
{
    uint32_t mask = UINT32_MAX >> (32 - bits), sign = 1u << (bits - 1);
    a &= mask; b &= mask;
    uint32_t result = (add ? a + b : a - b) & mask;
    unsigned carry = add ? (uint64_t) a + b > mask : a < b;
    unsigned overflow = !!((add ? ~(a ^ b) : (a ^ b)) & (a ^ result) & sign);
    unsigned negative = !!(result & sign), zero = result == 0, ones = 0;
    for (unsigned v = result & 255; v; v >>= 1) ones += v & 1;
    unsigned value;
    switch (condition >> 1) {
        case 0: value = overflow; break;
        case 1: value = carry; break;
        case 2: value = zero; break;
        case 3: value = carry || zero; break;
        case 4: value = negative; break;
        case 5: value = !(ones & 1); break;
        case 6: value = negative != overflow; break;
        default: value = zero || (negative != overflow); break;
    }
    return value ^ (condition & 1);
}

static void validate_extended(const bench_case_t *c, cpu_state_t *expected, unsigned ops)
{
    unsigned bits = c->size * 8;
    uint32_t mask = bits ? UINT32_MAX >> (32 - bits) : UINT32_MAX;
    for (unsigned i = 0; i < ops; i++) {
        if (c->kind == CONDITIONS || c->kind == CARRY_INCDEC) {
            uint32_t a = expected->regs[0].l, b = expected->regs[1].l;
            unsigned value = condition_oracle(c->condition, a, b, bits, c->producer);
            if (c->producer) expected->regs[0].l = (a & ~mask) | ((a + b) & mask);
            if (c->kind == CARRY_INCDEC) {
                expected->regs[3].l = (expected->regs[3].l & ~mask) | ((expected->regs[3].l + (c->form ? -1 : 1)) & mask);
                CHECK(!!(cpu_state.flags & C_FLAG) == condition_oracle(2, a, b, bits, c->producer) || i + 1 != ops);
            } else if (!c->form) expected->regs[3].b.l = value;
            else if (value) expected->regs[3].l = (expected->regs[3].l & ~mask) | (expected->regs[2].l & mask);
        } else if (c->kind == CMOV_MEMORY) {
            if (condition_oracle(c->condition, expected->regs[1].l, expected->regs[2].l, 32, 0))
                expected->regs[3].l = (expected->regs[3].l & ~mask) | (0xa5a5a5a5 & mask);
        } else if (c->kind == MULTIPLY) {
            unsigned r = i % c->live_regs;
            expected->regs[r].l = (expected->regs[r].l & ~mask) | ((expected->regs[r].l * expected->regs[7].l) & mask);
        } else if (c->kind == POINTER_CHASE) {
            unsigned r = i % c->live_regs;
            memcpy(&expected->regs[r].l, memory + expected->regs[r].l, 4);
        } else if (c->kind == DIVIDE) {
            uint32_t low = c->form == 0 ? 123456 : c->form == 1 ? 0x87654321 : 0xffff0001;
            uint32_t high = c->producer ? (c->form == 2 ? UINT32_MAX : 0) : c->form == 1;
            uint64_t n = ((uint64_t) high << 32) | low;
            uint32_t q = c->producer ? (int64_t) n / -257 : n / 257;
            uint32_t r = c->producer ? (int64_t) n % -257 : n % 257;
            expected->regs[0].l = q; expected->regs[2].l = r;
            expected->regs[6].l += q; expected->regs[7].l += r;
        }
    }
    CHECK(!memcmp(cpu_state.regs, expected->regs, sizeof(expected->regs)));
}
