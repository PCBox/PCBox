#ifndef _CODEGEN_BACKEND_H_
#define _CODEGEN_BACKEND_H_

#include "codegen.h"

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
#ifdef CODEGEN_CHAIN
void codegen_backend_link_stub(codeblock_t *block);
void codegen_backend_indirect_stub(codeblock_t *block);
#endif

struct ir_data_t;
struct uop_t;

#ifdef CODEGEN_BACKEND_HAS_MEM_STUBS
void codegen_backend_mem_begin(void);
void codegen_backend_mem_finish(codeblock_t *block);
void codegen_backend_mem_call(codeblock_t *block, int size, int is_float, int store, void *callback);
void codegen_backend_mem_call_128(codeblock_t *block, int store, int data_offset);
#endif

#ifdef CODEGEN_BACKEND_HAS_SSE_RECHECK
void codegen_backend_sse_recheck(codeblock_t *block, struct uop_t *uop);
#endif

#ifdef CODEGEN_BACKEND_HAS_SSE_MXCSR
void codegen_backend_sse_mxcsr_begin(codeblock_t *block);
void codegen_backend_sse_mxcsr_end(codeblock_t *block);
#endif

struct ir_data_t *codegen_get_ir_data(void);

typedef int (*uOpFn)(codeblock_t *codeblock, struct uop_t *uop);

extern const uOpFn uop_handlers[];

/*MMX_ENTER's fault: #UD with CR0.EM set, #NM for CR0.TS*/
void codegen_mmx_unavailable(void);
/*SSE_ENTER's fault: #UD with CR0.EM set or CR4.OSFXSR clear, else #NM for
  CR0.TS*/
void codegen_sse_unavailable(void);

/*Register will not be preserved across function calls*/
#define HOST_REG_FLAG_VOLATILE (1 << 0)
/*Register has no byte forms usable here (x86-64 R8-R15: AH-BH can't be
  encoded next to them), so it never holds a register accessed by byte*/
#define HOST_REG_FLAG_NO_BYTE (1 << 1)

typedef struct host_reg_def_t {
    int reg;
    int flags;
} host_reg_def_t;

extern host_reg_def_t codegen_host_reg_list[CODEGEN_HOST_REGS];
extern host_reg_def_t codegen_host_fp_reg_list[CODEGEN_HOST_FP_REGS];

#endif
