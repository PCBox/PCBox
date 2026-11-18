/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdarg.h>
#include <stdlib.h>
#include "../../../src/cpu/codegen_timing_p6.c"
cpu_state_t cpu_state;
int cpu_has_feature(int feature) { return feature == CPU_FEATURE_MMX; }
static CPU model = {.cpu_type=CPU_PENTIUM3};
CPU *cpu_s = &model;
int codegen_block_cycles;
void fatal(const char *fmt, ...) {
    va_list ap; va_start(ap,fmt); vfprintf(stderr,fmt,ap); va_end(ap); exit(1);
}
#define CHECK(c) do { if (!(c)) fatal("line %d: %s\n",__LINE__,#c); } while (0)
static void reset(void) { codegen_timing_p6_start(); codegen_timing_p6_block_start(); codegen_block_cycles=0; }
int main(void)
{
    reset(); reg_available_timestamp[0]=10;
    decode_instruction(&alu_op, SRCDEP_RM|DSTDEP_REG|MODRM, 0xd8, 0x300, 0);
    CHECK(decode_buffer.earliest_start[0]==10); /* MOV EBX,EAX */
    reset(); reg_available_timestamp[3]=10;
    decode_instruction(&alu_op, SRCDEP_RM|DSTDEP_REG|MODRM, 0xd8, 0x300, 0);
    CHECK(decode_buffer.earliest_start[0]==0); /* Old EBX is not an input. */
    reset(); reg_available_timestamp[0]=20; reg_available_timestamp[3]=10;
    decode_instruction(&alu_op,SRCDEP_RM|SRCDEP_REG|DSTDEP_REG|MODRM,0xd8,0x300,0);
    CHECK(decode_buffer.earliest_start[0]==20); /* ADD EBX,EAX: maximum. */
    reset(); fpu_st_timestamp[0]=20; fpu_st_timestamp[1]=10;
    decode_instruction(&alu_op,FPU_READ_ST0|FPU_READ_ST1,0,0x300,0);
    CHECK(decode_buffer.earliest_start[0]==20);
    reset(); fpu_st_timestamp[0]=20;
    decode_instruction(&alu_op,FPU_WRITE_ST0,0,0x300,0);
    CHECK(decode_buffer.earliest_start[0]==0);
    reset(); reg_available_timestamp[0]=10;
    decode_instruction(&alu_op,SRCDEP_RM|DSTDEP_REG|MODRM,0xd8,0x300,0);
    decode_instruction(&alu_op,SRCDEP_RM|DSTDEP_REG|MODRM,0xcb,0x300,0); /* MOV ECX,EBX */
    CHECK(decode_buffer.earliest_start[0]==11);
    codegen_timing_p6_block_end();
    CHECK(reg_available_timestamp[1]==12);
    reset(); CHECK(reg_available_timestamp[1]==0);
    puts("P6 input dependencies, queued producers and block reset passed");
    return 0;
}
