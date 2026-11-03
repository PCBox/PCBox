#ifndef CODEGEN_OPS_X87H_H
#define CODEGEN_OPS_X87H_H

/* The host-x87 FPU's compiled instructions, one table per escape (D8-DF). */
extern RecompOpFn recomp_opcodes_x87h[8][512];

/* The handler codegen_generate_call() would call for the instruction
   (codegen.c). */
extern OpFn codegen_fpu_handler;

void codegen_x87h_init(void);

/* codegen.c: the block may or may not have run a handler's setup, so the
   next handler call sets up everything again. */
void codegen_x87h_forget(void);

#endif