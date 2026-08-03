#include "codegen_backend_x86-64_defs.h"

#define BLOCK_SIZE  0x4000
#define BLOCK_MASK  0x3fff
#define BLOCK_START 0

#define HASH_SIZE   0x20000
#define HASH_MASK   0x1ffff

#define HASH(l)     (((l) ^ ((l)>>12)) &0x1ffff)

#define BLOCK_MAX   0x3c0

#define CODEGEN_BACKEND_HAS_MOV_IMM

#define CODEGEN_HAS_SSE

#define CODEGEN_HOST_CPU_FEATURE_SSE3   (1ULL << 0)
#define CODEGEN_HOST_CPU_FEATURE_SSSE3  (1ULL << 1)
#define CODEGEN_HOST_CPU_FEATURE_SSE4_1 (1ULL << 2)
#define CODEGEN_HOST_CPU_FEATURE_SSE4_2 (1ULL << 3)
#define CODEGEN_HOST_CPU_FEATURE_AVX    (1ULL << 4)
#define CODEGEN_HOST_CPU_FEATURE_AVX2   (1ULL << 5)
#define CODEGEN_HOST_CPU_FEATURE_BMI1   (1ULL << 6)
#define CODEGEN_HOST_CPU_FEATURE_BMI2   (1ULL << 7)
#define CODEGEN_HOST_CPU_FEATURE_AVX512 (1ULL << 8)

extern uint64_t codegen_host_cpu_features;

static inline int
codegen_host_cpu_has_feature(uint64_t feature)
{
    return (codegen_host_cpu_features & feature) == feature;
}
