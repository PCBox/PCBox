#ifndef _CODEGEN_BACKEND_H_
#define _CODEGEN_BACKEND_H_

#if defined __amd64__ || defined _M_X64
#    include "codegen_backend_x86-64.h"
#elif defined __aarch64__ || defined _M_ARM64
#    include "codegen_backend_arm64.h"
#elif defined __loongarch_lp64
#    include "codegen_backend_loongarch64.h"
#else
#    error New dynamic recompiler not implemented on your platform
#endif

void codegen_backend_init(void);
void codegen_backend_prologue(codeblock_t *block);
void codegen_backend_epilogue(codeblock_t *block);
#ifdef CODEGEN_BACKEND_HAS_SELECTIVE_XMM
void codegen_backend_ir_prologue(codeblock_t *block);
#endif

struct ir_data_t;
struct uop_t;

#ifdef CODEGEN_BACKEND_HAS_MEM_STUBS
void codegen_backend_mem_begin(void);
void codegen_backend_mem_finish(codeblock_t *block);
void codegen_backend_mem_call(codeblock_t *block, int size, int is_float, int store, void *callback);
#endif

#ifdef CODEGEN_BACKEND_HAS_SSE_RECHECK
void codegen_backend_sse_recheck(codeblock_t *block, struct uop_t *uop);
#endif

struct ir_data_t *codegen_get_ir_data(void);

typedef int (*uOpFn)(codeblock_t *codeblock, struct uop_t *uop);

extern const uOpFn uop_handlers[];

/*Register will not be preserved across function calls*/
#define HOST_REG_FLAG_VOLATILE (1 << 0)

typedef struct host_reg_def_t {
    int reg;
    int flags;
} host_reg_def_t;

extern host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS];
extern host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS];

#endif
