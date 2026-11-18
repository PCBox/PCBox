/* Decode effective addresses independently of the production emitters, then
   execute representative loads/stores. Synthetic addresses are never accessed.
   Negative displacements are explicit, regardless of the linker's layout. */
#include <inttypes.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <xmmintrin.h>
#ifdef _WIN32
#    include <windows.h>
#else
#    include <sys/mman.h>
#endif
#include <86box/86box.h>
#include "cpu.h"
#include "codegen.h"
#include "codegen_allocator.h"
#include "codegen_backend.h"
#include "codegen_backend_x86-64_defs.h"
#include "codegen_backend_x86-64_ops.h"
#include "codegen_backend_x86-64_ops_sse.h"

cpu_state_t cpu_state;
uint8_t *ram, *block_write_data;
int block_pos, cpu_block_end;
static codeblock_t storage;
codeblock_t *codeblock = &storage;
static uint8_t *code_memory;
static jmp_buf fatal_jump;
static int expect_fatal;
static unsigned checks;
static const char *current_case;
static uint64_t target[4];

#define CHECK(c) do { if (!(c)) { \
    fprintf(stderr, "%s, line %d: %s\n", current_case, __LINE__, #c); \
    exit(1); \
} } while (0)

void fatal(const char *fmt, ...)
{
    if (expect_fatal) longjmp(fatal_jump, 1);
    va_list args;
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(1);
}

struct mem_block_t { uint8_t *data; };
static struct mem_block_t chunks[4];
static unsigned next_chunk;

struct mem_block_t *codegen_allocator_allocate(struct mem_block_t *parent, int nr)
{
    (void) parent; (void) nr;
    CHECK(next_chunk < 4);
    chunks[next_chunk].data = code_memory + next_chunk * 1024;
    return &chunks[next_chunk++];
}
uint8_t *codeblock_allocator_get_ptr(struct mem_block_t *block)
{
    return block->data;
}

enum { IMM8, IMM16, IMM32, STORE8, STORE16, STORE32, STORE64,
       LOAD8, LOAD16, LOAD32, LOAD64, MXCSR, XMM_LOAD, XMM_STORE, FORMS };
static const char *names[FORMS] = {
    "MOV8_ABS_IMM", "MOV16_ABS_IMM", "MOV32_ABS_IMM",
    "MOV8_ABS_REG", "MOV16_ABS_REG", "MOV32_ABS_REG", "MOV64_ABS_REG",
    "MOV8_REG_ABS", "MOV16_REG_ABS", "MOV32_REG_ABS", "MOV64_REG_ABS",
    "LDMXCSR", "MOVDQU_XREG_ABS", "MOVDQU_ABS_XREG"
};

static void emit(int form, void *p, int reg)
{
    switch (form) {
        case IMM8: host_x86_MOV8_ABS_IMM(&storage, p, 0x98); break;
        case IMM16: host_x86_MOV16_ABS_IMM(&storage, p, 0x7698); break;
        case IMM32: host_x86_MOV32_ABS_IMM(&storage, p, 0x32547698); break;
        case STORE8: host_x86_MOV8_ABS_REG(&storage, p, reg); break;
        case STORE16: host_x86_MOV16_ABS_REG(&storage, p, reg); break;
        case STORE32: host_x86_MOV32_ABS_REG(&storage, p, reg); break;
        case STORE64: host_x86_MOV64_ABS_REG(&storage, p, reg); break;
        case LOAD8: host_x86_MOV8_REG_ABS(&storage, reg, p); break;
        case LOAD16: host_x86_MOV16_REG_ABS(&storage, reg, p); break;
        case LOAD32: host_x86_MOV32_REG_ABS(&storage, reg, p); break;
        case LOAD64: host_x86_MOV64_REG_ABS(&storage, reg, p); break;
        case MXCSR: host_x86_LDMXCSR(&storage, p); break;
        case XMM_LOAD: host_x86_MOVDQU_XREG_ABS(&storage, reg, p); break;
        case XMM_STORE: host_x86_MOVDQU_ABS_XREG(&storage, p, reg); break;
        default: CHECK(0);
    }
}

static uint64_t read_integer(const uint8_t *p, unsigned size)
{
    uint64_t result = 0;
    memcpy(&result, p, size);
    return result;
}

/* Only the MOV/MOVDQU/LDMXCSR addressing forms emitted here are decoded.
   Resolve RIP, SIB, RBP/R12 displacement and MOVABS-R9 independently. */
static uintptr_t address(const uint8_t *p, unsigned size)
{
    unsigned at = 0, rex = 0, addr32 = 0;
    uintptr_t r9 = 0;
    if (size >= 10 && p[0] == 0x49 && p[1] == 0xb9) {
        r9 = read_integer(p + 2, 8);
        at = 10;
    }
    while (at < size) {
        unsigned byte = p[at];
        if (byte == 0x67) addr32 = 1;
        else if (byte >= 0x40 && byte <= 0x4f) rex = byte;
        else if (byte != 0x66 && byte != 0xf2 && byte != 0xf3) break;
        at++;
    }
    CHECK(at + 2 <= size);
    if (p[at++] == 0x0f) at++;
    unsigned modrm = p[at++], mod = modrm >> 6, rm = modrm & 7;
    uintptr_t base = 0;
    if (rm == 4) {
        CHECK(at < size);
        unsigned sib = p[at++];
        CHECK((sib & 0x38) == 0x20); /* no index */
        rm = sib & 7;
        if (mod == 0 && rm == 5) rm = 16; /* no base */
    } else if (mod == 0 && rm == 5) {
        base = (uintptr_t) p + size; /* RIP-relative, not absolute */
        rm = 16;
    }
    if (rm != 16) {
        rm |= (rex & 1) ? 8 : 0;
        if (rm == REG_RBP) base = (uintptr_t) &cpu_state + 128;
        else if (rm == REG_R12) base = (uintptr_t) ram + UINT64_C(2147483648);
        else if (rm == REG_R9) base = r9;
        else CHECK(0);
    }
    int64_t displacement = 0;
    if (mod == 1) { CHECK(at < size); displacement = (int8_t) p[at]; }
    else if (mod == 2 || rm == 16) {
        CHECK(at + 4 <= size);
        displacement = (int32_t) read_integer(p + at, 4);
    }
    uintptr_t result = base + displacement;
    return addr32 ? (uint32_t) result : result;
}

static void check_address(int form, uintptr_t p, int reg)
{
    current_case = names[form];
    int64_t delta = p - ((uintptr_t) &cpu_state + 128);
    int nearby = delta >= INT32_MIN && delta <= INT32_MAX;
    int fallback = form == LOAD8 || form == LOAD32;
    int absolute = (form <= STORE64 || form == XMM_LOAD || form == XMM_STORE) && p <= UINT32_MAX;
    block_pos = 0;
    block_write_data = code_memory;
    memset(code_memory, 0xcc, 256);
    expect_fatal = !nearby && !fallback && !absolute;
    if (setjmp(fatal_jump)) {
        CHECK(expect_fatal);
        expect_fatal = 0;
        checks++;
        return;
    }
    emit(form, (void *) p, reg);
    CHECK(!expect_fatal);
    CHECK(address(code_memory, block_pos) == p);
    if (nearby && form == IMM32) {
        /* The common path stays one instruction, including negative offsets. */
        CHECK(block_pos == (delta >= -128 && delta <= 127 ? 7 : 10));
    }
    checks++;
}

static void check_tail(int form, uintptr_t p, int reg)
{
    current_case = names[form];
    for (int remaining = 5; remaining <= 20; remaining++) {
        next_chunk = 0;
        storage.head_mem_block = codegen_allocator_allocate(NULL, 0);
        block_write_data = code_memory;
        int start = block_pos = BLOCK_MAX - remaining;
        emit(form, (void *) p, reg);
        if (block_write_data != code_memory) start = 0;
        CHECK(block_pos <= BLOCK_MAX - 5); /* reserve the allocator's jump */
        CHECK(address(block_write_data + start, block_pos - start) == p);
        checks++;
    }
    block_write_data = code_memory;
}

static void execute(int form, uint64_t *data)
{
    current_case = names[form];
    const uint64_t value = UINT64_C(0xfedcba9832547698);
    const uint64_t guard = UINT64_C(0x1122334455667788);
    static const unsigned widths[] = { 1, 2, 4, 1, 2, 4, 8, 1, 2, 4, 8, 4 };
    unsigned width = widths[form];
    uint64_t mask = width == 8 ? UINT64_MAX : (UINT64_C(1) << (width * 8)) - 1;
    data[0] = data[2] = data[3] = guard;
    data[1] = form >= LOAD8 && form <= LOAD64 ? value : guard;
    if (form == MXCSR) data[1] = 0x3f80;
    uint32_t saved_mxcsr = _mm_getcsr();
    block_pos = 0;
    block_write_data = code_memory;
    host_x86_PUSH(&storage, REG_RBP);
    host_x86_MOV64_REG_IMM(&storage, REG_RBP, (uintptr_t) &cpu_state + 128);
    host_x86_MOV64_REG_IMM(&storage, REG_RAX, 0);
    host_x86_MOV64_REG_IMM(&storage, REG_RCX, value);
    emit(form, &data[1], form >= LOAD8 && form <= LOAD64 ? REG_RAX : REG_RCX);
    host_x86_POP(&storage, REG_RBP);
    host_x86_RET(&storage);
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, block_pos));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + block_pos);
#endif
    uint64_t result = ((uint64_t (*)(void)) code_memory)();
    uint32_t mxcsr = _mm_getcsr();
    _mm_setcsr(saved_mxcsr);
    if (form >= LOAD8 && form <= LOAD64) CHECK(result == (value & mask));
    else if (form == MXCSR) CHECK(mxcsr == 0x3f80);
    else CHECK(data[1] == ((guard & ~mask) | (value & mask)));
    CHECK(data[0] == guard && data[2] == guard && data[3] == guard);
    checks++;
}

static void check_lea_base(void)
{
    const int bases[] = { REG_RSP, REG_R12 };
    const int offsets[] = { 0, 0x123456 };
    current_case = "LEA stack base";
    for (unsigned b = 0; b < 2; b++) {
        for (unsigned d = 0; d < 2; d++) {
            block_pos = 0;
            host_x86_LEA_REG_IMM(&storage, REG_EAX, bases[b], offsets[d]);
            const uint8_t *p = code_memory;
            if (bases[b] == REG_R12)
                CHECK(*p++ == 0x41);
            CHECK(*p++ == 0x8d);
            CHECK(*p++ == (offsets[d] ? 0x84 : 0x04));
            CHECK(*p++ == 0x24);
            if (offsets[d]) {
                CHECK(read_integer(p, 4) == (unsigned) offsets[d]);
                p += 4;
            }
            CHECK(p == code_memory + block_pos);
            checks++;
        }
    }
}

static void execute_high_byte(int ram_relative)
{
    uint8_t value = 0xab;
    current_case = "absolute load into DH";
    block_pos = 0;
    block_write_data = code_memory;
    storage.flags = ram_relative ? CODEBLOCK_NO_IMMEDIATES : 0;
    ram = &value;
    host_x86_PUSH(&storage, REG_RBP);
    host_x86_PUSH(&storage, REG_R12);
    host_x86_PUSH(&storage, REG_RSI);
    host_x86_MOV64_REG_IMM(&storage, REG_RBP, (uintptr_t) &cpu_state + 128);
    host_x86_MOV64_REG_IMM(&storage, REG_R12, (uintptr_t) ram + UINT64_C(0x80000000));
    host_x86_MOV32_REG_IMM(&storage, REG_EDX, 0x12345678);
    emit(LOAD8, &value, 6); /* DH */
    host_x86_MOV32_REG_REG(&storage, REG_EAX, REG_EDX);
    host_x86_POP(&storage, REG_RSI);
    host_x86_POP(&storage, REG_R12);
    host_x86_POP(&storage, REG_RBP);
    host_x86_RET(&storage);
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, block_pos));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + block_pos);
#endif
    CHECK(((uint32_t (*)(void)) code_memory)() == 0x1234ab78);
    storage.flags = 0;
    ram = NULL;
    checks++;
}

static void execute_xmm(uint64_t *data)
{
    const uint64_t expected[2] = { UINT64_C(0xfedcba9832547698), UINT64_C(0x0123456789abcdef) };
    uint64_t result[2] = { 0, 0 };
    current_case = "SSE absolute round trip";
    data[0] = data[3] = UINT64_MAX;
    data[1] = data[2] = 0;
    block_pos = 0;
    block_write_data = code_memory;
    host_x86_PUSH(&storage, REG_RBP);
    host_x86_MOV64_REG_IMM(&storage, REG_RBP, (uintptr_t) &cpu_state + 128);
    host_x86_MOV64_REG_IMM(&storage, REG_RCX, (uintptr_t) expected);
    host_x86_MOVDQU_XREG_BASE_OFFSET(&storage, REG_XMM1, REG_RCX, 0);
    emit(XMM_STORE, data + 1, REG_XMM1);
    host_x86_PXOR_XREG_XREG(&storage, REG_XMM1, REG_XMM1);
    emit(XMM_LOAD, data + 1, REG_XMM1);
    host_x86_MOV64_REG_IMM(&storage, REG_RCX, (uintptr_t) result);
    host_x86_MOVDQU_BASE_OFFSET_XREG(&storage, REG_RCX, 0, REG_XMM1);
    host_x86_POP(&storage, REG_RBP);
    host_x86_RET(&storage);
#ifdef _WIN32
    CHECK(FlushInstructionCache(GetCurrentProcess(), code_memory, block_pos));
#else
    __builtin___clear_cache((char *) code_memory, (char *) code_memory + block_pos);
#endif
    ((void (*)(void)) code_memory)();
    CHECK(!memcmp(data + 1, expected, sizeof(expected)));
    CHECK(!memcmp(result, expected, sizeof(expected)));
    CHECK(data[0] == UINT64_MAX && data[3] == UINT64_MAX);
    checks++;
}

int main(void)
{
    current_case = "allocation";
#ifdef _WIN32
    code_memory = VirtualAlloc(NULL, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE);
    CHECK(code_memory != NULL);
#else
    code_memory = mmap(NULL, 4096, PROT_READ | PROT_WRITE | PROT_EXEC, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(code_memory != MAP_FAILED);
#endif
    block_write_data = code_memory;
    static const int64_t offsets[] = {
        -INT64_C(0x100c34), -129, -128, -1, 0, 1, 126, 127, 128,
        INT32_MIN, INT32_MAX, (int64_t) INT32_MIN - 1, (int64_t) INT32_MAX + 1,
        -INT64_C(0x100000000), INT64_C(0x100000000), INT64_C(0x10000007f)
    };
    for (int form = 0; form < FORMS; form++) {
        for (unsigned n = 0; n < sizeof(offsets) / sizeof(offsets[0]); n++) {
            check_address(form, (uintptr_t) &cpu_state + 128 + offsets[n], REG_ECX);
            if (form != STORE8 && form != LOAD8)
                check_address(form, (uintptr_t) &cpu_state + 128 + offsets[n], REG_R10);
        }
        check_address(form, UINT32_C(0x12345678), REG_ECX);
        check_address(form, UINT32_C(0x81234567), REG_ECX);
        if (form != STORE8 && form != LOAD8)
            check_address(form, UINT32_C(0x81234567), REG_R10);
        int reg = form == STORE8 || form == LOAD8 ? REG_ECX : REG_R10;
        check_tail(form, (uintptr_t) &cpu_state - 1024, reg);
        if (form <= STORE64 || form == XMM_LOAD || form == XMM_STORE)
            check_tail(form, UINT32_C(0x81234567), reg);
        if (form <= MXCSR) execute(form, target);
    }
    execute_xmm(target);
    execute_high_byte(0);
    execute_high_byte(1);
    check_lea_base();
    /* Exercise real absolute disp32 instructions below and above 2 GB when
       these address ranges are available. Encoding coverage above is mandatory. */
    for (unsigned n = 0; n < 2; n++) {
        void *hint = (void *) (uintptr_t) (n ? UINT32_C(0x92340000) : UINT32_C(0x12340000));
#ifdef _WIN32
        void *mapped = VirtualAlloc(hint, 4096, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
        if (!mapped) { puts("Low-address execution unavailable; encoding checked"); continue; }
#else
        void *mapped = mmap(hint, 4096, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapped == MAP_FAILED) { puts("Low-address execution unavailable; encoding checked"); continue; }
#endif
        if (mapped == hint) {
            for (int form = 0; form <= LOAD32; form++)
                if (form != LOAD16) execute(form, mapped);
            execute_xmm(mapped);
        } else puts("Low-address execution unavailable; encoding checked");
#ifdef _WIN32
        CHECK(VirtualFree(mapped, 0, MEM_RELEASE));
#else
        CHECK(munmap(mapped, 4096) == 0);
#endif
    }
    printf("Absolute addressing: %u checks passed (global offset %" PRIdPTR ")\n",
           checks, (intptr_t) ((uintptr_t) target - ((uintptr_t) &cpu_state + 128)));
#ifdef _WIN32
    CHECK(VirtualFree(code_memory, 0, MEM_RELEASE));
#else
    CHECK(munmap(code_memory, 4096) == 0);
#endif
    return 0;
}
