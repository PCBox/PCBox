#include "codegen_backend_x86-64_defs.h"

#define BLOCK_SIZE  0x8000
#define BLOCK_MASK  0x7fff
#define BLOCK_START 0

#define HASH_SIZE   0x20000
#define HASH_MASK   0x1ffff

#define HASH(l)     ((l>>12) &0x1ffff)

#define BLOCK_MAX   0x3c0

#define CODEGEN_BACKEND_HAS_MOV_IMM
