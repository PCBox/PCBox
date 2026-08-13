#if defined __amd64__ || defined _M_X64

#    include <stdlib.h>
#    include <stdint.h>
#    include <86box/86box.h>
#    include "cpu.h"
#    include <86box/mem.h>
#    include <86box/plat.h>

#    include "codegen.h"
#    include "codegen_allocator.h"
#    include "codegen_backend.h"
#    include "codegen_backend_x86-64_defs.h"
#    include "codegen_backend_x86-64_ops.h"
#    include "codegen_backend_x86-64_ops_sse.h"
#    include "codegen_reg.h"
#    include "x86.h"
#    include "x86seg_common.h"
#    include "x86seg.h"

#    if defined(__linux__) || defined(__APPLE__)
#        include <sys/mman.h>
#        include <unistd.h>
#    endif
#    if defined WIN32 || defined _WIN32 || defined _WIN32
#        include <windows.h>
#    endif
#    if defined _MSC_VER
#        include <intrin.h>
#    endif
#    include <string.h>

void *codegen_mem_load_byte;
void *codegen_mem_load_word;
void *codegen_mem_load_long;
void *codegen_mem_load_quad;
void *codegen_mem_load_single;
void *codegen_mem_load_double;

void *codegen_mem_store_byte;
void *codegen_mem_store_word;
void *codegen_mem_store_long;
void *codegen_mem_store_quad;
void *codegen_mem_store_single;
void *codegen_mem_store_double;

void *codegen_gpf_rout;
void *codegen_exit_rout;

uint64_t codegen_host_cpu_features;

host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS] = {
  /*Note: while EAX and EDX are normally volatile registers under x86
  calling conventions, the recompiler will explicitly save and restore
  them across funcion calls*/
    {REG_EAX,  0},
    { REG_EBX, 0},
    { REG_EDX, 0},
    { REG_R14, 0},
    { REG_R15, 0}
};

host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS] = {
#    if _WIN64
  /*Windows x86-64 calling convention preserves XMM6-XMM15*/
    {REG_XMM6,  0                     },
    { REG_XMM7, 0                     },
#    else
    /*System V AMD64 calling convention does not preserve any XMM registers*/
    { REG_XMM6, HOST_REG_FLAG_VOLATILE },
    { REG_XMM7, HOST_REG_FLAG_VOLATILE },
#    endif
    { REG_XMM1, HOST_REG_FLAG_VOLATILE},
    { REG_XMM2, HOST_REG_FLAG_VOLATILE},
    { REG_XMM3, HOST_REG_FLAG_VOLATILE},
    { REG_XMM4, HOST_REG_FLAG_VOLATILE},
    { REG_XMM5, HOST_REG_FLAG_VOLATILE}
};

static void
host_cpuid(uint32_t leaf, uint32_t subleaf, uint32_t *eax, uint32_t *ebx, uint32_t *ecx, uint32_t *edx)
{
#    if defined(__GNUC__) || defined(__clang__)
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(subleaf));
#    else
    *eax = *ebx = *ecx = *edx = 0;
#    endif
}

static uint64_t
host_xgetbv(uint32_t xcr)
{
#    if defined(__GNUC__) || defined(__clang__)
    uint32_t eax, edx;

    __asm__ volatile(
        "xgetbv"
        : "=a"(eax), "=d"(edx)
        : "c"(xcr));
    return ((uint64_t) edx << 32) | eax;
#    else
    return 0;
#    endif
}

static uint64_t
detect_host_cpu_features(void)
{
    uint64_t features = 0;
    uint32_t max_leaf;
    uint32_t eax, ebx, ecx, edx;
    int      os_avx, os_avx512;

    host_cpuid(0, 0, &max_leaf, &ebx, &ecx, &edx);

    if (max_leaf < 1)
        return 0;

    host_cpuid(1, 0, &eax, &ebx, &ecx, &edx);

    if (ecx & (1U << 0))
        features |= CODEGEN_HOST_CPU_FEATURE_SSE3;
    if (ecx & (1U << 9))
        features |= CODEGEN_HOST_CPU_FEATURE_SSSE3;
    if (ecx & (1U << 19))
        features |= CODEGEN_HOST_CPU_FEATURE_SSE4_1;
    if (ecx & (1U << 20))
        features |= CODEGEN_HOST_CPU_FEATURE_SSE4_2;

    os_avx = ((ecx & ((1U << 27) | (1U << 28))) == ((1U << 27) | (1U << 28))) &&
             ((host_xgetbv(0) & 0x6) == 0x6);
    if (os_avx)
        features |= CODEGEN_HOST_CPU_FEATURE_AVX;

    os_avx512 = os_avx && ((host_xgetbv(0) & 0xe0) == 0xe0);

    if (max_leaf >= 7) {
        host_cpuid(7, 0, &eax, &ebx, &ecx, &edx);
        if (ebx & (1U << 3))
            features |= CODEGEN_HOST_CPU_FEATURE_BMI1;
        if (ebx & (1U << 8))
            features |= CODEGEN_HOST_CPU_FEATURE_BMI2;
        if (os_avx && (ebx & (1U << 5)))
            features |= CODEGEN_HOST_CPU_FEATURE_AVX2;
        if (os_avx512 && (ebx & (0xd003U << 16)))
            features |= CODEGEN_HOST_CPU_FEATURE_AVX512;
    }

    codegen_host_cpu_features = features;

    return features;
}

static void
build_load_routine(codeblock_t *block, int size, int is_float)
{
    uint8_t *branch_offset;
    uint8_t *misaligned_offset = NULL;

    /*In - ESI = address
      Out - ECX = data, ESI = abrt*/
    /*MOV ECX, ESI
      SHR ESI, 12
      MOV RSI, [readlookup2+ESI*4]
      CMP ESI, -1
      JNZ +
      MOVZX ECX, B[RSI+RCX]
      XOR ESI,ESI
      RET
    * PUSH EAX
      PUSH EDX
      PUSH ECX
      CALL readmembl
      POP ECX
      POP EDX
      POP EAX
      MOVZX ECX, AL
      RET
    */
    host_x86_MOV32_REG_REG(block, REG_ECX, REG_ESI);
    host_x86_SHR32_IMM(block, REG_ESI, 12);
    host_x86_MOV64_REG_IMM(block, REG_RDI, (uint64_t) (uintptr_t) readlookup2);
    host_x86_MOV64_REG_BASE_INDEX_SHIFT(block, REG_RSI, REG_RDI, REG_RSI, 3);
    if (size != 1) {
        host_x86_TEST32_REG_IMM(block, REG_ECX, size - 1);
        misaligned_offset = host_x86_JNZ_short(block);
    }
    host_x86_CMP64_REG_IMM(block, REG_RSI, (uint32_t) -1);
    branch_offset = host_x86_JZ_short(block);
    if (size == 1 && !is_float)
        host_x86_MOVZX_BASE_INDEX_32_8(block, REG_ECX, REG_RSI, REG_RCX);
    else if (size == 2 && !is_float)
        host_x86_MOVZX_BASE_INDEX_32_16(block, REG_ECX, REG_RSI, REG_RCX);
    else if (size == 4 && !is_float)
        host_x86_MOV32_REG_BASE_INDEX(block, REG_ECX, REG_RSI, REG_RCX);
    else if (size == 4 && is_float)
        host_x86_CVTSS2SD_XREG_BASE_INDEX(block, REG_XMM_TEMP, REG_RSI, REG_RCX);
    else if (size == 8)
        host_x86_MOVQ_XREG_BASE_INDEX(block, REG_XMM_TEMP, REG_RSI, REG_RCX);
    else
        fatal("build_load_routine: size=%i\n", size);
    host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
    host_x86_RET(block);

    *branch_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 1;
    if (size != 1)
        *misaligned_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) misaligned_offset) - 1;
    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x28);
    // host_x86_MOV32_REG_REG(block, REG_ECX, uop->imm_data);
#    else
    /* Align RSP to 16: entry RSP%16=8 (after CALL from JIT block), two PUSHes
       leave it at 8; subtract 8 more to satisfy the SysV ABI before calling C. */
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x8);
    host_x86_MOV32_REG_REG(block, REG_EDI, REG_ECX);
#    endif
    if (size == 1 && !is_float) {
        host_x86_CALL(block, (void *) readmembl);
        host_x86_MOVZX_REG_32_8(block, REG_ECX, REG_EAX);
    } else if (size == 2 && !is_float) {
        host_x86_CALL(block, (void *) readmemwl);
        host_x86_MOVZX_REG_32_16(block, REG_ECX, REG_EAX);
    } else if (size == 4 && !is_float) {
        host_x86_CALL(block, (void *) readmemll);
        host_x86_MOV32_REG_REG(block, REG_ECX, REG_EAX);
    } else if (size == 4 && is_float) {
        host_x86_CALL(block, (void *) readmemll);
        host_x86_MOVD_XREG_REG(block, REG_XMM_TEMP, REG_EAX);
        host_x86_CVTSS2SD_XREG_XREG(block, REG_XMM_TEMP, REG_XMM_TEMP);
    } else if (size == 8) {
        host_x86_CALL(block, (void *) readmemql);
        host_x86_MOVQ_XREG_REG(block, REG_XMM_TEMP, REG_RAX);
    }
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x28);
#    else
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x8);
#    endif
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);
    host_x86_MOVZX_REG_ABS_32_8(block, REG_ESI, &cpu_state.abrt);
    host_x86_RET(block);
}

static void
build_store_routine(codeblock_t *block, int size, int is_float)
{
    uint8_t *branch_offset;
    uint8_t *misaligned_offset = NULL;

    /*In - ECX = data, ESI = address
      Out - ESI = abrt
      Corrupts EDI*/
    /*MOV EDI, ESI
      SHR ESI, 12
      MOV ESI, [writelookup2+ESI*4]
      CMP ESI, -1
      JNZ +
      MOV [RSI+RDI], ECX
      XOR ESI,ESI
      RET
    * PUSH EAX
      PUSH EDX
      PUSH ECX
      CALL writemembl
      POP ECX
      POP EDX
      POP EAX
      MOVZX ECX, AL
      RET
    */
    host_x86_MOV32_REG_REG(block, REG_EDI, REG_ESI);
    host_x86_SHR32_IMM(block, REG_ESI, 12);
    host_x86_MOV64_REG_IMM(block, REG_R8, (uint64_t) (uintptr_t) writelookup2);
    host_x86_MOV64_REG_BASE_INDEX_SHIFT(block, REG_RSI, REG_R8, REG_RSI, 3);
    if (size != 1) {
        host_x86_TEST32_REG_IMM(block, REG_EDI, size - 1);
        misaligned_offset = host_x86_JNZ_short(block);
    }
    host_x86_CMP64_REG_IMM(block, REG_RSI, (uint32_t) -1);
    branch_offset = host_x86_JZ_short(block);
    if (size == 1 && !is_float)
        host_x86_MOV8_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
    else if (size == 2 && !is_float)
        host_x86_MOV16_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
    else if (size == 4 && !is_float)
        host_x86_MOV32_BASE_INDEX_REG(block, REG_RSI, REG_RDI, REG_ECX);
    else if (size == 4 && is_float)
        host_x86_MOVD_BASE_INDEX_XREG(block, REG_RSI, REG_RDI, REG_XMM_TEMP);
    else if (size == 8)
        host_x86_MOVQ_BASE_INDEX_XREG(block, REG_RSI, REG_RDI, REG_XMM_TEMP);
    else
        fatal("build_store_routine: size=%i\n", size);
    host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
    host_x86_RET(block);

    *branch_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) branch_offset) - 1;
    if (size != 1)
        *misaligned_offset = (uint8_t) ((uintptr_t) &block_write_data[block_pos] - (uintptr_t) misaligned_offset) - 1;
    host_x86_PUSH(block, REG_RAX);
    host_x86_PUSH(block, REG_RDX);
#    if _WIN64
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x28);
    if (size == 4 && is_float)
        host_x86_MOVD_REG_XREG(block, REG_EDX, REG_XMM_TEMP); // data
    else if (size == 8)
        host_x86_MOVQ_REG_XREG(block, REG_RDX, REG_XMM_TEMP); // data
    else
        host_x86_MOV32_REG_REG(block, REG_EDX, REG_ECX); // data
    host_x86_MOV32_REG_REG(block, REG_ECX, REG_EDI);     // address
#    else
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x8);
    // host_x86_MOV32_REG_REG(block, REG_EDI, REG_ECX);  //address
    if (size == 4 && is_float)
        host_x86_MOVD_REG_XREG(block, REG_ESI, REG_XMM_TEMP); // data
    else if (size == 8)
        host_x86_MOVQ_REG_XREG(block, REG_RSI, REG_XMM_TEMP); // data
    else
        host_x86_MOV32_REG_REG(block, REG_ESI, REG_ECX); // data
#    endif
    if (size == 1)
        host_x86_CALL(block, (void *) writemembl);
    else if (size == 2)
        host_x86_CALL(block, (void *) writememwl);
    else if (size == 4)
        host_x86_CALL(block, (void *) writememll);
    else if (size == 8)
        host_x86_CALL(block, (void *) writememql);
#    if _WIN64
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x28);
#    else
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x8);
#    endif
    host_x86_POP(block, REG_RDX);
    host_x86_POP(block, REG_RAX);
    host_x86_MOVZX_REG_ABS_32_8(block, REG_ESI, &cpu_state.abrt);
    host_x86_RET(block);
}

static void
build_loadstore_routines(codeblock_t *block)
{
    /* Helper emission may advance to another allocator block, so always use
       block_write_data rather than the first block's data pointer. */
    codegen_mem_load_byte = &block_write_data[block_pos];
    build_load_routine(block, 1, 0);
    codegen_mem_load_word = &block_write_data[block_pos];
    build_load_routine(block, 2, 0);
    codegen_mem_load_long = &block_write_data[block_pos];
    build_load_routine(block, 4, 0);
    codegen_mem_load_quad = &block_write_data[block_pos];
    build_load_routine(block, 8, 0);
    codegen_mem_load_single = &block_write_data[block_pos];
    build_load_routine(block, 4, 1);
    codegen_mem_load_double = &block_write_data[block_pos];
    build_load_routine(block, 8, 1);

    codegen_mem_store_byte = &block_write_data[block_pos];
    build_store_routine(block, 1, 0);
    codegen_mem_store_word = &block_write_data[block_pos];
    build_store_routine(block, 2, 0);
    codegen_mem_store_long = &block_write_data[block_pos];
    build_store_routine(block, 4, 0);
    codegen_mem_store_quad = &block_write_data[block_pos];
    build_store_routine(block, 8, 0);
    codegen_mem_store_single = &block_write_data[block_pos];
    build_store_routine(block, 4, 1);
    codegen_mem_store_double = &block_write_data[block_pos];
    build_store_routine(block, 8, 1);
}

void
codegen_backend_init(void)
{
    codeblock_t *block;
    int          c;
    uint8_t      large_block = 0;
    uint8_t      large_hash = 0;

    codegen_host_cpu_features = detect_host_cpu_features();
    
    codeblock      = plat_mmap(BLOCK_SIZE * sizeof(codeblock_t), 0, &large_block);
    codeblock_hash = plat_mmap(HASH_SIZE * CODEBLOCK_HASH_WAYS * sizeof(uint16_t), 0, &large_hash);

    if (large_block)
        pclog("Allocated %llu bytes of large pages for codeblock pointers\n", BLOCK_SIZE * sizeof(codeblock_t));
    if (large_hash)
        pclog("Allocated %llu bytes of large pages for codeblock hashes\n", HASH_SIZE * CODEBLOCK_HASH_WAYS * sizeof(uint16_t));

    for (c = 0; c < BLOCK_SIZE; c++)
        codeblock[c].valid = 0;

    block_current                           = 0;
    block_pos                               = 0;
    block                                   = &codeblock[block_current];
    codeblock[block_current].head_mem_block = codegen_allocator_allocate(NULL, block_current);
    codeblock[block_current].data           = codeblock_allocator_get_ptr(codeblock[block_current].head_mem_block);
    block_write_data                        = codeblock[block_current].data;
    build_loadstore_routines(&codeblock[block_current]);

    codegen_gpf_rout = &block_write_data[block_pos];
#    if _WIN64
    host_x86_XOR32_REG_REG(block, REG_ECX, REG_ECX);
    host_x86_XOR32_REG_REG(block, REG_EDX, REG_EDX);
#    else
    host_x86_XOR32_REG_REG(block, REG_EDI, REG_EDI);
    host_x86_XOR32_REG_REG(block, REG_ESI, REG_ESI);
#    endif
    host_x86_CALL(block, (void *) x86gpf);
    codegen_exit_rout = &block_write_data[block_pos];
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x58);
    host_x86_POP(block, REG_R15);
    host_x86_POP(block, REG_R14);
    host_x86_POP(block, REG_R13);
    host_x86_POP(block, REG_R12);
#ifdef _WIN64
    host_x86_POP(block, REG_RDI);
    host_x86_POP(block, REG_RSI);
#endif
    host_x86_POP(block, REG_RBP);
    host_x86_POP(block, REG_RBX);
    host_x86_RET(block);

    block_write_data = NULL;

    asm(
        "stmxcsr %0\n"
        : "=m"(cpu_state.old_fp_control));
    cpu_state.trunc_fp_control = cpu_state.old_fp_control | 0x6000;
}

void
codegen_set_rounding_mode(int mode)
{
    cpu_state.new_fp_control = (cpu_state.old_fp_control & ~0x6000) | (mode << 13);
}

void
codegen_backend_prologue(codeblock_t *block)
{
    block_pos = BLOCK_START; /*Entry code*/
    host_x86_PUSH(block, REG_RBX);
    host_x86_PUSH(block, REG_RBP);
#ifdef _WIN64
    host_x86_PUSH(block, REG_RSI);
    host_x86_PUSH(block, REG_RDI);
#endif
    host_x86_PUSH(block, REG_R12);
    host_x86_PUSH(block, REG_R13);
    host_x86_PUSH(block, REG_R14);
    host_x86_PUSH(block, REG_R15);
    /*Stack offsets 16-31 = integer temps, 32 = FPU TOP diff,
      40-55 = FP temps, 64-79 = 128-bit temp*/
    host_x86_SUB64_REG_IMM(block, REG_RSP, 0x58);
    host_x86_MOV64_REG_IMM(block, REG_RBP, ((uintptr_t) &cpu_state) + 128);
    if (block->flags & CODEBLOCK_HAS_FPU) {
        host_x86_MOV32_REG_ABS(block, REG_EAX, &cpu_state.TOP);
        host_x86_SUB32_REG_IMM(block, REG_EAX, block->TOP);
        host_x86_MOV32_BASE_OFFSET_REG(block, REG_RSP, IREG_TOP_diff_stack_offset, REG_EAX);
    }
    if (block->flags & CODEBLOCK_NO_IMMEDIATES)
        host_x86_MOV64_REG_IMM(block, REG_R12, ((uintptr_t) ram) + 2147483648ULL);
}

void
codegen_backend_epilogue(codeblock_t *block)
{
    host_x86_ADD64_REG_IMM(block, REG_RSP, 0x58);
    host_x86_POP(block, REG_R15);
    host_x86_POP(block, REG_R14);
    host_x86_POP(block, REG_R13);
    host_x86_POP(block, REG_R12);
#ifdef _WIN64
    host_x86_POP(block, REG_RDI);
    host_x86_POP(block, REG_RSI);
#endif
    host_x86_POP(block, REG_RBP);
    host_x86_POP(block, REG_RBX);
    host_x86_RET(block);
}
#endif
