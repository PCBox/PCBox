/* Compare cached page lookups with fresh translations at every access.
   Both paths execute the real allocator and memory helpers. */
#define RAM_UOP_HANDLERS ram_uop_handlers
#define main ram_register_test_main
#include "ram_register_test.c"
#undef main

static int fresh_lookups;

static int
emit_uop(codeblock_t *block, uop_t *uop)
{
    /* Change only the emitted lookup. Inserting IR barriers would also change
       register allocation and writeback, obscuring which feature we compare. */
    if (fresh_lookups)
        uop->type &= ~UOP_TYPE_MEM_REUSE;
    if ((uop->type & UOP_MASK) == UOP_TEST_PADDING && uop->imm_data == UINT32_MAX) {
        /* An unlisted emitter is free to destroy the lookup scratch state. */
        host_x86_MOV32_REG_IMM(block, REG_R8, 0);
        host_x86_MOV64_REG_IMM(block, REG_RDI, UINT64_MAX);
        return 0;
    }
    CHECK(ram_uop_handlers[uop->type & UOP_MASK] != NULL);
    return ram_uop_handlers[uop->type & UOP_MASK](block, uop);
}

const uOpFn uop_handlers[UOP_MAX] = { [0 ... UOP_MAX - 1] = emit_uop };

enum { ACCESSES = 12, TRACE_SIZE = ACCESSES * 2 };
static uint8_t other_memory[sizeof(memory)];
static unsigned callback_mode, callback_count, reuse_sites, same_address_sites;
static int change_registers, access_segment;
struct callback_trace {
    cpu_state_t cpu;
    uint32_t address;
    unsigned size, store;
};
static struct callback_trace trace[TRACE_SIZE];
struct result {
    cpu_state_t cpu;
    uint8_t ram[sizeof(memory)], other[sizeof(memory)];
    struct callback_trace trace[TRACE_SIZE];
    unsigned calls, aborted;
};

static void
change_mapping(void)
{
    /* Change page zero after an access to an unmapped page. Returning to the
       original page must not reuse either its old tag or its old host bias. */
    if (callback_mode == 1)
        readlookup2[0] = writelookup2[0] = (uintptr_t) other_memory;
    else if (callback_mode == 2)
        readlookup2[0] = writelookup2[0] = (uintptr_t) -1;
    else if (callback_mode == 3)
        readlookup2[0] = writelookup2[0] = (uintptr_t) memory;
    else if (callback_mode == 4) {
        readlookup2[0] = (uintptr_t) other_memory;
        writelookup2[0] = (uintptr_t) -1;
    }
}

static void
record_callback(uint32_t addr, unsigned size, int store)
{
    CHECK(callback_count < TRACE_SIZE);
    struct callback_trace *t = &trace[callback_count++];
    t->cpu = cpu_state;
    t->address = addr;
    t->size = size;
    t->store = store;
    expected_oldpc = cpu_state.oldpc;
    change_mapping();
    if (change_registers) {
        EBP += 3;
        cpu_state.ST[cpu_state.TOP] += 0.25;
        cpu_state.XMM[7].l[0] += 5;
    }
}

static void
emit_access(ir_data_t *ir, int size, int store, unsigned i, int form)
{
    static const int bytes[] = { IREG_AL, IREG_CH, IREG_DL, IREG_BH };
    int reg = size == 16 ? IREG_XMM(i & 7) : size == 8 ? IREG_MM(i & 7)
              : size == 4 ? IREG_EAX + (i & 7) : size == 2 ? IREG_AX + (i & 7) : bytes[i & 3];
    if (form == FORM_ABS && size <= 4) {
        if (store) uop_MEM_STORE_ABS(ir, IREG_eaaddr, 0, reg);
        else uop_MEM_LOAD_ABS(ir, reg, IREG_eaaddr, 0);
    } else if (form == FORM_IMM && size <= 4 && store) {
        if (size == 1) uop_MEM_STORE_IMM_8(ir, access_segment, IREG_eaaddr, 0x12345678 + i);
        else if (size == 2) uop_MEM_STORE_IMM_16(ir, access_segment, IREG_eaaddr, 0x12345678 + i);
        else uop_MEM_STORE_IMM_32(ir, access_segment, IREG_eaaddr, 0x12345678 + i);
    } else if (form == FORM_SINGLE && size == 4) {
        if (store) uop_MEM_STORE_SINGLE(ir, access_segment, IREG_eaaddr, IREG_ST(i & 7));
        else uop_MEM_LOAD_SINGLE(ir, IREG_ST(i & 7), access_segment, IREG_eaaddr);
    } else if (form == FORM_DOUBLE && size == 8) {
        if (store) uop_MEM_STORE_DOUBLE(ir, access_segment, IREG_eaaddr, IREG_ST(i & 7));
        else uop_MEM_LOAD_DOUBLE(ir, IREG_ST(i & 7), access_segment, IREG_eaaddr);
    } else if (store) uop_MEM_STORE_REG(ir, access_segment, IREG_eaaddr, reg);
    else uop_MEM_LOAD_REG(ir, reg, access_segment, IREG_eaaddr);
}

static void
run_sequence(struct result *result, int fresh, int size, int direction, int layout,
             unsigned offset, int mapping, unsigned fault, unsigned pad, int top, int form, int boundary)
{
    memset(&cpu_state, 0, sizeof(cpu_state));
    memset(&test_block, 0, sizeof(test_block));
    memset(trace, 0, sizeof(trace));
    next_chunk = helper_calls = aborted = callback_count = 0;
    fault_on_call = fault;
    cycles = 10000;
    cpu_state.seg_ss.base = layout == 6 ? 4096 : 0;
    cpu_state.seg_ds.base = layout == 7 ? UINT32_C(0xfffff000) : 0;
    for (unsigned i = 0; i < sizeof(memory); i++) {
        memory[i] = (i * 37 + (i >> 8)) & 0xff;
        other_memory[i] = memory[i] ^ 0x5a;
    }
    for (int i = 0; i < 8; i++) {
        cpu_state.regs[i].l = 0x12345670 + i;
        cpu_state.ST[i] = 1.25 + i;
        cpu_state.MM[i].q = UINT64_C(0x8877665544332211) + i;
        for (int lane = 0; lane < 4; lane++) cpu_state.XMM[i].l[lane] = 0x11223344 + i * 4 + lane;
    }
    for (int page = 0; page < 2; page++) {
        readlookup2[page] = mapping & (1 << page) ? (uintptr_t) memory : (uintptr_t) -1;
        writelookup2[page] = mapping & (4 << page) ? (uintptr_t) memory : (uintptr_t) -1;
    }
    /* Different read/write backing catches reusing a read translation for a
       write even when both permissions are present. */
    if (mapping == 16) {
        readlookup2[0] = readlookup2[1] = (uintptr_t) memory;
        writelookup2[0] = writelookup2[1] = (uintptr_t) other_memory;
    }
    readlookup2[0xfffff] = writelookup2[0xfffff] = (uintptr_t) memory - UINT32_C(0xfffff000);
    start_code();
    build_loadstore_routines(&test_block);
    codegen_exit_rout = start_code();
    host_x86_MOV64_REG_IMM(&test_block, REG_RDI, (uintptr_t) &aborted);
    host_x86_MOV32_BASE_OFFSET_IMM(&test_block, REG_RDI, 0, 1);
    codegen_backend_epilogue(&test_block);
    codegen_reg_reset();
    ir_data_t *ir = codegen_ir_init();
    test_block.flags = CODEBLOCK_HAS_FPU | (top ? 0 : CODEBLOCK_STATIC_TOP);
    for (int i = 0; i < 8; i++) {
        uop_ADD_IMM(ir, IREG_EAX + i, IREG_EAX + i, 1);
        uop_PADDD(ir, IREG_XMM(i), IREG_XMM(i), IREG_XMM(i));
    }
    uop_FADD(ir, IREG_ST(0), IREG_ST(0), IREG_ST(0));
    uop_gen_imm(UOP_TEST_PADDING, ir, pad);
    if (layout >= 8)
        uop_MOV_IMM(ir, IREG_eaaddr, layout == 8 ? 64 + offset : UINT32_C(0xfffffff8));
    int branch = -1;
    for (unsigned i = 0; i < ACCESSES; i++) {
        if (i == 6 && boundary == 1) uop_CALL_FUNC(ir, change_mapping);
        if (i == 6 && (boundary == 2 || boundary == 3)) branch = uop_CMP_IMM_JZ_DEST(ir, IREG_DS_base, boundary == 2 ? 0 : 1);
        if (i == 6 && boundary == 4) uop_gen_imm(UOP_TEST_PADDING, ir, UINT32_MAX);
        if (i == 8 && branch >= 0) uop_set_jump_dest(ir, branch);
        unsigned addr = 64 + i * size + offset;
        if (layout == 1) addr += (i & 1) * 4096;
        if (layout == 2) addr = i % 3 == 1 ? 4160 + offset : 64 + offset;
        if (layout == 3) addr = 4080 + i + offset;
        if (layout == 4) addr = (i < 6 ? UINT32_C(0xfffff000) : 0) + 128 + i * size;
        if (layout == 7) addr += 4096;
        uop_MOV_IMM(ir, IREG_oldpc, 0x1000 + i);
        uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, -1);
        if (layout < 8) uop_MOV_IMM(ir, IREG_eaaddr, addr);
        int store = direction == 1 || (direction == 2 && (i & 1)) || (direction == 3 && (i & 2));
        access_segment = layout == 6 && (i & 1) ? IREG_SS_base : IREG_DS_base;
        emit_access(ir, layout == 5 ? 1 << (i % 5) : size, store, i, form);
        if (layout == 9) {
            ir->uops[ir->wr_pos - 1].imm_data = 16;
            ir->uops[ir->wr_pos - 1].is_a16 = 1;
        }
    }
    /* Keep these architectural values live in both translations. */
    for (int i = 0; i < 8; i++) uop_ADD_IMM(ir, IREG_EAX + i, IREG_EAX + i, 0);
    uop_ADD_IMM(ir, IREG_cycles, IREG_cycles, 0);
    uint8_t *entry = start_code();
    fresh_lookups = fresh;
    codegen_ir_compile(ir, &test_block);
    for (int i = 0; i < mem_slow_count; i++) {
        if (fresh) CHECK(!mem_slow_sites[i].lookup_resume);
        else {
            reuse_sites += !!mem_slow_sites[i].lookup_resume;
            same_address_sites += mem_slow_sites[i].lookup_resume && mem_slow_sites[i].same_address;
        }
    }
    if (top) cpu_state.TOP = 3;
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, CODE_SIZE));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + CODE_SIZE);
#endif
    ((void (*)(void)) entry)();
    result->cpu = cpu_state;
    memcpy(result->ram, memory, sizeof(memory));
    memcpy(result->other, other_memory, sizeof(other_memory));
    memcpy(result->trace, trace, sizeof(trace));
    result->calls = helper_calls;
    result->aborted = aborted;
}

static void
compare_sequence(int size, int direction, int layout, unsigned offset, int mapping,
                 unsigned fault, unsigned pad, int top, int form, int boundary)
{
    static struct result reference, cached;
    cases++;
    run_sequence(&reference, 1, size, direction, layout, offset, mapping, fault, pad, top, form, boundary);
    run_sequence(&cached, 0, size, direction, layout, offset, mapping, fault, pad, top, form, boundary);
    if (memcmp(&reference, &cached, sizeof(reference))) {
        fprintf(stderr, "size=%d direction=%d layout=%d offset=%u mapping=%d callback=%u fault=%u pad=%u top=%d form=%d boundary=%d\n",
                size, direction, layout, offset, mapping, callback_mode, fault, pad, top, form, boundary);
        CHECK(reference.calls == cached.calls);
        CHECK(reference.aborted == cached.aborted);
        CHECK(!memcmp(reference.ram, cached.ram, sizeof(memory)));
        CHECK(!memcmp(reference.other, cached.other, sizeof(memory)));
        CHECK(!memcmp(reference.trace, cached.trace, sizeof(trace)));
        for (unsigned i = 0; i < sizeof(cpu_state); i++)
            if (((uint8_t *) &reference.cpu)[i] != ((uint8_t *) &cached.cpu)[i])
                fprintf(stderr, "CPU byte %u: %02x != %02x\n", i, ((uint8_t *) &reference.cpu)[i], ((uint8_t *) &cached.cpu)[i]);
        CHECK(!memcmp(&reference.cpu, &cached.cpu, sizeof(cpu_state)));
    }
}

int
main(void)
{
#ifdef _WIN32
    code_memory = VirtualAlloc(NULL, CODE_SIZE, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
    CHECK(code_memory != NULL);
#else
    code_memory = mmap(NULL, CODE_SIZE, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(code_memory != MAP_FAILED);
#endif
    memset(readlookup2, 0xff, sizeof(readlookup2));
    memset(writelookup2, 0xff, sizeof(writelookup2));
    memory_callback = record_callback;
    for (int size = 1; size <= 16; size *= 2)
        for (int direction = 0; direction < 4; direction++)
            for (int layout = 0; layout < 4; layout++)
                for (unsigned offset = 0; offset < 16; offset++)
                    for (int mapping = 0; mapping <= 16; mapping++) {
                        timing_misaligned = offset & 1 ? 3 : 0;
                        cpu_cyrix_alignment = !!(offset & 2);
                        change_registers = !!(offset & 4);
                        compare_sequence(size, direction, layout, offset, mapping, 0, offset * 59, offset & 1, FORM_REG, 0);
                    }
    for (callback_mode = 0; callback_mode <= 4; callback_mode++)
        for (int size = 1; size <= 16; size *= 2)
            for (int direction = 0; direction < 4; direction++)
                for (unsigned fault = 0; fault <= TRACE_SIZE; fault++)
                    for (int top = 0; top < 2; top++) {
                        compare_sequence(size, direction, 2, 1, callback_mode == 3 ? 0 : 5, fault, fault * 37, top, FORM_REG, 0);
                        compare_sequence(size, direction, 3, 8, 5, fault, fault * 37, top, FORM_REG, 0);
                    }
    timing_misaligned = 3;
    for (int size = 1; size <= 16; size *= 2)
        for (int direction = 0; direction < 4; direction++)
            for (int form = FORM_REG; form <= FORM_DOUBLE; form++)
                for (int boundary = 0; boundary < 5; boundary++) {
                    callback_mode = 1;
                    compare_sequence(size, direction, 0, 1, 15, 0, 953, 1, form, boundary);
                    callback_mode = 0;
                    compare_sequence(size, direction, 2, 3, 5, 0, 959, 1, form, boundary);
                }
    callback_mode = 0;
    for (unsigned pad = 0; pad < BLOCK_MAX; pad++)
        for (int direction = 0; direction < 2; direction++) {
            compare_sequence(4, direction, 1, 1, 15, 0, pad, 0, FORM_REG, 0);
            compare_sequence(16, direction, 3, 8, 5, 2, pad, 1, FORM_REG, 0);
        }
    for (int size = 1; size <= 16; size *= 2)
        for (int direction = 0; direction < 4; direction++)
            compare_sequence(size, direction, 4, 0, 15, 0, 0, 0, FORM_REG, 0);
    for (int layout = 5; layout <= 7; layout++)
        for (int direction = 0; direction < 4; direction++)
            for (int mapping = 0; mapping <= 16; mapping++)
                for (unsigned fault = 0; fault <= TRACE_SIZE; fault++)
                    compare_sequence(4, direction, layout, fault & 15, mapping, fault, fault * 37, fault & 1, FORM_REG, 0);
    for (int layout = 8; layout <= 9; layout++)
        for (int size = 1; size <= 16; size *= 2)
            for (callback_mode = 0; callback_mode <= 4; callback_mode++)
                for (int direction = 0; direction < 4; direction++)
                    for (unsigned fault = 0; fault <= TRACE_SIZE; fault++)
                        compare_sequence(size, direction, layout, fault & 15, fault & 15, fault, fault * 37, fault & 1, FORM_REG, 0);
    CHECK(reuse_sites > cases);
    CHECK(same_address_sites > 1000);
    printf("%u lookup comparisons passed (%u cached access sites, %u unchanged addresses)\n", cases, reuse_sites, same_address_sites);
    return 0;
}
