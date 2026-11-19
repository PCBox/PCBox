/* SPDX-License-Identifier: GPL-2.0-or-later */
/* Executes the production decoder, MMU and both execution engines. */
#define main previous_regression_main
#define outl previous_outl
#define timer_process previous_timer_process
#include "simd_exception_test.c"
#undef main
#undef outl
#undef timer_process
#include "x87_sf.h"
#include "x87.h"

typedef struct {
    const char *name;
    uint8_t bytes[32];
    unsigned length, cr0_bits, no_osfxsr, trap_p3, trap_p4, setup, only_p4, shift_width;
    uint8_t lanes[8]; /* Expected word indices for XMM_LANES. */
} opcode_case;
enum { BIT_WRITE = 28, POP_SP, PUSH_ALL_LIMIT, PUSH_ALL_WRAP, POP_ALL_LIMIT, POP_ALL_WRAP, UNPACK_SS, SSE_CALLBACK, SSE2_MOVD, SSE2_MOVQ, SSE2_MOVDQ, SSE2_MOVDQA_FAULT,
       PSHUFD_REG, PSHUFD_SELF, PSHUFD_MEM, SHUFPD_REG, SHUFPD_SELF, SHUFPD_MEM, MOVMSKPD, MOVQ2DQ, MOVDQ2Q, SHIFT_LEFT, SHIFT_RIGHT, SHIFT_ARITH, PACKED_ARITH, MMX_PMULUDQ,
       XMM_LANES, EXTRACT_WORD, BYTE_MASK, STREAM_DQ, STREAM_DWORD, MASK_PAGE };
/* Each lane shift takes an imm8, XMM register or aligned memory count. */
#define SHIFT_CASES(name, kind, width, group, ext, opcode) \
    {#name " packed shift imm", {0x66,0x0f,group,0xc0 | ((ext) << 3),5},5,0,0,6,0,kind,1,width}, \
    {#name " packed shift reg", {0x66,0x0f,opcode,0xc1},4,0,0,6,0,kind,1,width}, \
    {#name " packed shift mem a16", {0x67,0x66,0x0f,opcode,0x47,16},6,0,0,6,0,kind,1,width}
#define ARITH_CASES(name, opcode) \
    {#name " packed arithmetic reg", {0x66,0x0f,opcode,0xc1},4,0,0,6,0,PACKED_ARITH,1}, \
    {#name " packed arithmetic mem", {0x67,0x66,0x0f,opcode,0x47,16},6,0,0,6,0,PACKED_ARITH,1}
static const opcode_case opcases[] = {
    ARITH_CASES(PMINUB,  0xda),
    ARITH_CASES(PMAXUB,  0xde),
    ARITH_CASES(PAVGB,   0xe0),
    ARITH_CASES(PAVGW,   0xe3),
    ARITH_CASES(PMULHUW, 0xe4),
    ARITH_CASES(PMINSW,  0xea),
    ARITH_CASES(PMAXSW,  0xee),
    ARITH_CASES(PMULUDQ, 0xf4),
    ARITH_CASES(PSADBW,  0xf6),
    {"PMULUDQ packed arithmetic self", {0x66,0x0f,0xf4,0xc0},4,0,0,6,0,PACKED_ARITH,1},
    {"PMULUDQ packed arithmetic MMX", {0x0f,0xf4,0xc1},3,0,0,6,0,MMX_PMULUDQ,1},
    {"PMULUDQ packed arithmetic MMX unaligned", {0x0f,0xf4,0x47,1},4,0,0,6,0,MMX_PMULUDQ,1},
    {"PMULUDQ packed arithmetic SS limit", {0x36,0x66,0x0f,0xf4,0x07},5,0,0,6,12,UNPACK_SS,1},
    {"PAVGW packed arithmetic SS limit", {0x36,0x66,0x0f,0xe3,0x07},5,0,0,6,13,UNPACK_SS,1},
    SHIFT_CASES(PSLLW, SHIFT_LEFT, 16, 0x71, 6, 0xf1),
    SHIFT_CASES(PSLLD, SHIFT_LEFT, 32, 0x72, 6, 0xf2),
    SHIFT_CASES(PSLLQ, SHIFT_LEFT, 64, 0x73, 6, 0xf3),
    SHIFT_CASES(PSRLW, SHIFT_RIGHT, 16, 0x71, 2, 0xd1),
    SHIFT_CASES(PSRLD, SHIFT_RIGHT, 32, 0x72, 2, 0xd2),
    SHIFT_CASES(PSRLQ, SHIFT_RIGHT, 64, 0x73, 2, 0xd3),
    SHIFT_CASES(PSRAW, SHIFT_ARITH, 16, 0x71, 4, 0xe1),
    SHIFT_CASES(PSRAD, SHIFT_ARITH, 32, 0x72, 4, 0xe2),
    {"PSLLDQ packed shift bytes", {0x66,0x0f,0x73,0xf8,5},5,0,0,6,0,SHIFT_LEFT,1,128},
    {"PSRLDQ packed shift bytes", {0x66,0x0f,0x73,0xd8,9},5,0,0,6,0,SHIFT_RIGHT,1,128},
    {"PSLLDQ packed shift by zero", {0x66,0x0f,0x73,0xf8,0},5,0,0,6,0,SHIFT_LEFT,1,128},
    {"PSRLDQ packed shift by 15", {0x66,0x0f,0x73,0xd8,15},5,0,0,6,0,SHIFT_RIGHT,1,128},
    {"PSLLDQ packed shift by 16", {0x66,0x0f,0x73,0xf8,16},5,0,0,6,0,SHIFT_LEFT,1,128},
    {"PSRLDQ packed shift by 255", {0x66,0x0f,0x73,0xd8,255},5,0,0,6,0,SHIFT_RIGHT,1,128},
    {"PSLLD packed shift self", {0x66,0x0f,0xf2,0xc0},4,0,0,6,0,SHIFT_LEFT,1,32},
    {"PSRAD packed shift by 255", {0x66,0x0f,0x72,0xe0,255},5,0,0,6,0,SHIFT_ARITH,1,32},
    {"PSRLW packed shift SS limit", {0x36,0x66,0x0f,0xd1,0x07},5,0,0,6,12,UNPACK_SS,1},
    {"PSLLDQ invalid memory destination", {0x66,0x0f,0x73,0x3f,1},5,0,0,6,6,0,1},
    {"MOVMSKPD edx,xmm5", {0x66,0x0f,0x50,0xd5},4,0,0,6,0,MOVMSKPD,1},
    {"MOVMSKPD memory", {0x66,0x0f,0x50,0x17},4,0,0,6,6,MOVMSKPD,1},
    {"MOVMSKPD TS", {0x66,0x0f,0x50,0xd5},4,8,0,6,7,MOVMSKPD,1},
    /* Words 0..7 come from xmm0, 8..15 from xmm1, 16..23 from memory, and 24 from CX. */
    {"PSHUFLW reverse", {0xf2,0x0f,0x70,0xc1,0x1b},5,0,0,6,0,XMM_LANES,1,0,{11,10,9,8,12,13,14,15}},
    {"PSHUFLW a16 memory", {0x67,0xf2,0x0f,0x70,0x47,16,0xaa},7,0,0,6,0,XMM_LANES,1,0,{18,18,18,18,20,21,22,23}},
    {"PSHUFHW reverse self", {0xf3,0x0f,0x70,0xc0,0x1b},5,0,0,6,0,XMM_LANES,1,0,{0,1,2,3,7,6,5,4}},
    {"PSHUFHW memory", {0xf3,0x0f,0x70,0x43,16,0x8d},6,0,0,6,0,XMM_LANES,1,0,{16,17,18,19,21,23,20,22}},
    {"PSHUFHW SS limit", {0x36,0xf3,0x0f,0x70,0x07,0xe4},6,0,0,6,12,UNPACK_SS,1},
    {"PUNPCKLQDQ register", {0x66,0x0f,0x6c,0xc1},4,0,0,6,0,XMM_LANES,1,0,{0,1,2,3,8,9,10,11}},
    {"PUNPCKLQDQ self", {0x66,0x0f,0x6c,0xc0},4,0,0,6,0,XMM_LANES,1,0,{0,1,2,3,0,1,2,3}},
    {"PUNPCKHQDQ a16 memory", {0x67,0x66,0x0f,0x6d,0x47,16},6,0,0,6,0,XMM_LANES,1,0,{4,5,6,7,20,21,22,23}},
    {"PUNPCKHQDQ SS limit", {0x36,0x66,0x0f,0x6d,0x07},5,0,0,6,12,UNPACK_SS,1},
    {"MOVLPD load", {0x66,0x0f,0x12,0x43,18},5,0,0,6,0,XMM_LANES,1,0,{17,18,19,20,4,5,6,7}},
    {"MOVHPD load a16", {0x67,0x66,0x0f,0x16,0x47,18},6,0,0,6,0,XMM_LANES,1,0,{0,1,2,3,17,18,19,20}},
    {"MOVLPD load register", {0x66,0x0f,0x12,0xc1},4,0,0,6,6,0,1},
    {"MOVHPD load register", {0x66,0x0f,0x16,0xc1},4,0,0,6,6,0,1},
    {"PINSRW XMM register", {0x66,0x0f,0xc4,0xc1,0xfe},5,0,0,6,0,XMM_LANES,1,0,{0,1,2,3,4,5,24,7}},
    {"PINSRW XMM memory", {0x66,0x0f,0xc4,0x43,18,0xff},6,0,0,6,0,XMM_LANES,1,0,{0,1,2,3,4,5,6,17}},
    {"PEXTRW edx,xmm5", {0x66,0x0f,0xc5,0xd5,0xfe},5,0,0,6,0,EXTRACT_WORD,1},
    {"PEXTRW invalid memory", {0x66,0x0f,0xc5,0x17,0},5,0,0,6,6,0,1},
    {"PMOVMSKB invalid memory", {0x66,0x0f,0xd7,0x17},4,0,0,6,6,0,1},
    {"PMOVMSKB edx,xmm5", {0x66,0x0f,0xd7,0xd5},4,0,0,6,0,BYTE_MASK,1},
    {"MOVNTDQ aligned", {0x66,0x0f,0xe7,0x0f},4,0,0,6,0,STREAM_DQ,1},
    {"MOVNTDQ misaligned", {0x66,0x0f,0xe7,0x4f,1},5,0,0,6,13,SSE2_MOVDQA_FAULT,1},
    {"MOVNTDQ invalid register", {0x66,0x0f,0xe7,0xc1},4,0,0,6,6,0,1},
    {"MOVNTI unaligned with TS and OSFXSR clear", {0x0f,0xc3,0x4f,1},4,8,1,6,0,STREAM_DWORD},
    {"MOVNTI invalid register", {0x0f,0xc3,0xc1},3,0,0,6,6},
    /* Both masked-store inputs are produced in the block, then DI + 4 falls on an unmapped page. */
    {"MASKMOVDQU a16 FS page crossing", {0x0f,0x28,0xc2, 0x0f,0x28,0xcb,
                                        0x64,0x67,0x66,0x0f,0xf7,0xc1},12,0x80010000,0,6,14,MASK_PAGE,1},
    {"PSHUFD reverse", {0x66,0x0f,0x70,0xc1,0x1b},5,0,0,6,0,PSHUFD_REG,1},
    {"PSHUFD self broadcast", {0x66,0x0f,0x70,0xc0,0xaa},5,0,0,6,0,PSHUFD_SELF,1},
    {"PSHUFD a16 [bx+16]", {0x67,0x66,0x0f,0x70,0x47,16,0x8d},7,0,0,6,0,PSHUFD_MEM,1},
    {"SHUFPD register", {0x66,0x0f,0xc6,0xc1,0xfd},5,0,0,6,0,SHUFPD_REG,1},
    {"SHUFPD self swap", {0x66,0x0f,0xc6,0xc0,1},5,0,0,6,0,SHUFPD_SELF,1},
    {"SHUFPD [ebx+16]", {0x66,0x0f,0xc6,0x43,16,0xfe},6,0,0,6,0,SHUFPD_MEM,1},
    {"PSHUFD SS limit", {0x36,0x66,0x0f,0x70,0x07,0},6,0,0,6,12,UNPACK_SS,1},
    {"SHUFPD SS limit", {0x36,0x66,0x0f,0xc6,0x07,0},6,0,0,6,12,UNPACK_SS,1},
    /* MOVD xmm0,ecx / [edi],xmm0 / xmm1,[edi] / edx,xmm1 */
    {"SSE2 MOVD round trip", {0x66,0x0f,0x6e,0xc1, 0x66,0x0f,0x7e,0x07,
                              0x66,0x0f,0x6e,0x0f, 0x66,0x0f,0x7e,0xca},16,0,0,6,0,SSE2_MOVD,1},
    /* MOVQ xmm0,xmm0 / xmm1,xmm0 / [edi],xmm1 */
    {"SSE2 MOVQ self copy", {0xf3,0x0f,0x7e,0xc0, 0x66,0x0f,0xd6,0xc1,
                             0x66,0x0f,0xd6,0x0f},12,0,0,6,0,SSE2_MOVQ,1},
    /* Copy via an aligned load, both register directions, an unaligned
       store/load pair, and an aligned store. */
    {"SSE2 MOVDQA MOVDQU", {0x66,0x0f,0x6f,0x07, 0x66,0x0f,0x7f,0xc1,
                            0xf3,0x0f,0x7f,0x4f,17, 0xf3,0x0f,0x6f,0x57,17,
                            0xf3,0x0f,0x6f,0xda, 0x66,0x0f,0x7f,0x5f,48},27,0,0,6,0,SSE2_MOVDQ,1},
    {"SSE2 MOVDQA misaligned load", {0x66,0x0f,0x6f,0x47,1},5,0,0,6,13,SSE2_MOVDQA_FAULT,1},
    {"SSE2 MOVDQA misaligned store", {0x66,0x0f,0x7f,0x47,1},5,0,0,6,13,SSE2_MOVDQA_FAULT,1},
    /* MOVAPS xmm2,xmm0; MOV edx,[edi] / MOV [edi],edx; MOVAPS xmm2,xmm1 */
    {"SSE after scalar read callback", {0x0f,0x28,0xd0,0x8b,0x17,0x0f,0x28,0xd1},8,0,0,0,0,SSE_CALLBACK},
    {"SSE after scalar write callback", {0x0f,0x28,0xd0,0x89,0x17,0x0f,0x28,0xd1},8,0,0,0,0,SSE_CALLBACK},
    {"UNPCKLPS SS limit", {0x36,0x0f,0x14,0x07},4,0,0,12,12,UNPACK_SS},
    {"UNPCKHPS SS limit", {0x36,0x0f,0x15,0x07},4,0,0,12,12,UNPACK_SS},
    {"UNPCKLPD SS limit", {0x36,0x66,0x0f,0x14,0x07},5,0,0,12,12,UNPACK_SS,1},
    {"UNPCKHPD SS limit", {0x36,0x66,0x0f,0x15,0x07},5,0,0,12,12,UNPACK_SS,1},
    {"POP SP short encoding", {0x66,0x5c},2,0,0,0,0,POP_SP},
    {"POP SP ModRM encoding", {0x66,0x8f,0xc4},3,0,0,0,0,POP_SP},
    {"PUSHAD stack limit", {0x60},1,0,0,12,12,PUSH_ALL_LIMIT},
    {"PUSHAD wrapped stack limit", {0x60},1,0,0,12,12,PUSH_ALL_WRAP},
    {"POPA stack limit", {0x66,0x61},2,0,0,12,12,POP_ALL_LIMIT},
    {"POPAD stack limit", {0x61},1,0,0,12,12,POP_ALL_LIMIT},
    {"POPAD wrapped stack limit", {0x61},1,0,0,12,12,POP_ALL_WRAP},
    {"MOVNTPS register", {0x0f,0x2b,0xc1},3,0,0,6,6,18},
    {"MOVNTPD register", {0x66,0x0f,0x2b,0xc1},4,0,0,6,6,18,1},
    {"BTS word register offset", {0x66,0x0f,0xab,0x0f},4,0x80010000,0,14,14,BIT_WRITE},
    {"BTR dword register offset", {0x0f,0xb3,0x0f},3,0x80010000,0,14,14,BIT_WRITE},
    {"BTC dword immediate offset", {0x0f,0xba,0x3f,0x00},4,0x80010000,0,14,14,BIT_WRITE},
    {"CVTSD2SS denormal reg a32", {0xf2,0x0f,0x5a,0xc1},4,0,0,6,0,26,1},
    {"CVTSD2SS denormal reg a16", {0x67,0xf2,0x0f,0x5a,0xc1},5,0,0,6,0,26,1},
    {"CVTSD2SS denormal mem a32", {0xf2,0x0f,0x5a,0x00},4,0,0,6,0,26,1},
    {"CVTSD2SS denormal mem a16", {0x67,0xf2,0x0f,0x5a,0x07},5,0,0,6,0,26,1},
    {"CVTPD2PS denormal reg a32", {0x66,0x0f,0x5a,0xc1},4,0,0,6,0,26,1},
    {"CVTPD2PS denormal reg a16", {0x67,0x66,0x0f,0x5a,0xc1},5,0,0,6,0,26,1},
    {"CVTPD2PS denormal mem a32", {0x66,0x0f,0x5a,0x00},4,0,0,6,0,26,1},
    {"CVTPD2PS denormal mem a16", {0x67,0x66,0x0f,0x5a,0x07},5,0,0,6,0,26,1},
    {"RDPMC selector 00000000", {0xb9,0x0,0x0,0x0,0x0,0xf,0x33},7,0,0,0,0,25},
    {"RDPMC selector 00000001", {0xb9,0x1,0x0,0x0,0x0,0xf,0x33},7,0,0,0,0,25},
    {"RDPMC selector 00000002", {0xb9,0x2,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 00000003", {0xb9,0x3,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 00000004", {0xb9,0x4,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 00000005", {0xb9,0x5,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 00000006", {0xb9,0x6,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 00000007", {0xb9,0x7,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 00000008", {0xb9,0x8,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 00000009", {0xb9,0x9,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 0000000a", {0xb9,0xa,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 0000000b", {0xb9,0xb,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 0000000c", {0xb9,0xc,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 0000000d", {0xb9,0xd,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 0000000e", {0xb9,0xe,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 0000000f", {0xb9,0xf,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 00000010", {0xb9,0x10,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 00000011", {0xb9,0x11,0x0,0x0,0x0,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 00000012", {0xb9,0x12,0x0,0x0,0x0,0xf,0x33},7,0,0,13,13,25},
    {"RDPMC selector 40000000", {0xb9,0x0,0x0,0x0,0x40,0xf,0x33},7,0,0,13,13,25},
    {"RDPMC selector 7fffffff", {0xb9,0xff,0xff,0xff,0x7f,0xf,0x33},7,0,0,13,13,25},
    {"RDPMC selector 80000000", {0xb9,0x0,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000001", {0xb9,0x1,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000002", {0xb9,0x2,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000003", {0xb9,0x3,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000004", {0xb9,0x4,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000005", {0xb9,0x5,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000006", {0xb9,0x6,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000007", {0xb9,0x7,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000008", {0xb9,0x8,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000009", {0xb9,0x9,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 8000000a", {0xb9,0xa,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 8000000b", {0xb9,0xb,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 8000000c", {0xb9,0xc,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 8000000d", {0xb9,0xd,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 8000000e", {0xb9,0xe,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 8000000f", {0xb9,0xf,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000010", {0xb9,0x10,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000011", {0xb9,0x11,0x0,0x0,0x80,0xf,0x33},7,0,0,13,0,25},
    {"RDPMC selector 80000012", {0xb9,0x12,0x0,0x0,0x80,0xf,0x33},7,0,0,13,13,25},
    {"RDPMC selector ffffffff", {0xb9,0xff,0xff,0xff,0xff,0xf,0x33},7,0,0,13,13,25},
    {"Length 15 NOP", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x90},15,0,0,0,0,23},
    {"Length 16 NOP", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x90},16,0,0,13,13,23},
    {"Length 15 store32", {0x3e,0x3e,0x3e,0x3e,0xc7,0x4,0x25,0x0,0x0,0x2,0x0,0x78,0x56,0x34,0x12},15,0,0,0,0,23},
    {"Length 16 store32", {0x3e,0x3e,0x3e,0x3e,0x3e,0xc7,0x4,0x25,0x0,0x0,0x2,0x0,0x78,0x56,0x34,0x12},16,0,0,13,13,23},
    {"Length 15 store16", {0x3e,0x3e,0x3e,0x3e,0x3e,0x66,0xc7,0x4,0x25,0x0,0x0,0x2,0x0,0x78,0x56},15,0,0,0,0,23},
    {"Length 16 store16", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x66,0xc7,0x4,0x25,0x0,0x0,0x2,0x0,0x78,0x56},16,0,0,13,13,23},
    {"Length 15 address16", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x67,0xc7,0x6,0x0,0x40,0x78,0x56,0x34,0x12},15,0,0,0,0,23},
    {"Length 16 address16", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x67,0xc7,0x6,0x0,0x40,0x78,0x56,0x34,0x12},16,0,0,13,13,23},
    {"Length 15 immediate", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0xb8,0x78,0x56,0x34,0x12},15,0,0,0,0,23},
    {"Length 16 immediate", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0xb8,0x78,0x56,0x34,0x12},16,0,0,13,13,23},
    {"Length 15 moffs", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0xa1,0x0,0x0,0x2,0x0},15,0,0,0,0,23},
    {"Length 16 moffs", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0xa1,0x0,0x0,0x2,0x0},16,0,0,13,13,23},
    {"Length 15 SHUFPS", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0xf,0xc6,0x4,0x25,0x0,0x0,0x2,0x0,0x0},15,0,0,0,0,23},
    {"Length 16 SHUFPS", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0xf,0xc6,0x4,0x25,0x0,0x0,0x2,0x0,0x0},16,0,0,13,13,23},
    {"Length 15 PSHUFW", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0xf,0x70,0x4,0x25,0x0,0x0,0x2,0x0,0x0},15,0,0,0,0,23},
    {"Length 16 PSHUFW", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0xf,0x70,0x4,0x25,0x0,0x0,0x2,0x0,0x0},16,0,0,13,13,23},
    {"Length 15 REP MOVSS", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0xf3,0xf,0x10,0xc1},15,0,0,0,0,23},
    {"Length 16 REP MOVSS", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0xf3,0xf,0x10,0xc1},16,0,0,13,13,23},
    {"Length 16 prefix 26", {0x26,0x26,0x26,0x26,0x26,0x26,0x26,0x26,0x26,0x26,0x26,0x26,0x26,0x26,0x26,0x90},16,0,0,13,13,23},
    {"Length 16 prefix 2e", {0x2e,0x2e,0x2e,0x2e,0x2e,0x2e,0x2e,0x2e,0x2e,0x2e,0x2e,0x2e,0x2e,0x2e,0x2e,0x90},16,0,0,13,13,23},
    {"Length 16 prefix 36", {0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x36,0x90},16,0,0,13,13,23},
    {"Length 16 prefix 3e", {0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x3e,0x90},16,0,0,13,13,23},
    {"Length 16 prefix 64", {0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x64,0x90},16,0,0,13,13,23},
    {"Length 16 prefix 65", {0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x65,0x90},16,0,0,13,13,23},
    {"Length 16 prefix 66", {0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x90},16,0,0,13,13,23},
    {"Length 16 prefix 67", {0x67,0x67,0x67,0x67,0x67,0x67,0x67,0x67,0x67,0x67,0x67,0x67,0x67,0x67,0x67,0x90},16,0,0,13,13,23},
    {"Length 16 prefix f2", {0xf2,0xf2,0xf2,0xf2,0xf2,0xf2,0xf2,0xf2,0xf2,0xf2,0xf2,0xf2,0xf2,0xf2,0xf2,0x90},16,0,0,13,13,23},
    {"Length 16 prefix f3", {0xf3,0xf3,0xf3,0xf3,0xf3,0xf3,0xf3,0xf3,0xf3,0xf3,0xf3,0xf3,0xf3,0xf3,0xf3,0x90},16,0,0,13,13,23},
    {"Length 32 prefix chain", {0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x90},32,0,0,13,13,23},
    {"Length 16 unmapped next byte", {0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x66,0x90},16,0x80010000,0,13,13,24},
    {"LOCK 00 register", {0xf0,0x0,0xc8},3,0,0,6,6,22},
    {"LOCK 00 memory", {0xf0,0x0,0x8},3,0,0,0,0,22},
    {"LOCK 01 register", {0xf0,0x1,0xc8},3,0,0,6,6,22},
    {"LOCK 01 memory", {0xf0,0x1,0x8},3,0,0,0,0,22},
    {"LOCK 08 register", {0xf0,0x8,0xc8},3,0,0,6,6,22},
    {"LOCK 08 memory", {0xf0,0x8,0x8},3,0,0,0,0,22},
    {"LOCK 09 register", {0xf0,0x9,0xc8},3,0,0,6,6,22},
    {"LOCK 09 memory", {0xf0,0x9,0x8},3,0,0,0,0,22},
    {"LOCK 10 register", {0xf0,0x10,0xc8},3,0,0,6,6,22},
    {"LOCK 10 memory", {0xf0,0x10,0x8},3,0,0,0,0,22},
    {"LOCK 11 register", {0xf0,0x11,0xc8},3,0,0,6,6,22},
    {"LOCK 11 memory", {0xf0,0x11,0x8},3,0,0,0,0,22},
    {"LOCK 18 register", {0xf0,0x18,0xc8},3,0,0,6,6,22},
    {"LOCK 18 memory", {0xf0,0x18,0x8},3,0,0,0,0,22},
    {"LOCK 19 register", {0xf0,0x19,0xc8},3,0,0,6,6,22},
    {"LOCK 19 memory", {0xf0,0x19,0x8},3,0,0,0,0,22},
    {"LOCK 20 register", {0xf0,0x20,0xc8},3,0,0,6,6,22},
    {"LOCK 20 memory", {0xf0,0x20,0x8},3,0,0,0,0,22},
    {"LOCK 21 register", {0xf0,0x21,0xc8},3,0,0,6,6,22},
    {"LOCK 21 memory", {0xf0,0x21,0x8},3,0,0,0,0,22},
    {"LOCK 28 register", {0xf0,0x28,0xc8},3,0,0,6,6,22},
    {"LOCK 28 memory", {0xf0,0x28,0x8},3,0,0,0,0,22},
    {"LOCK 29 register", {0xf0,0x29,0xc8},3,0,0,6,6,22},
    {"LOCK 29 memory", {0xf0,0x29,0x8},3,0,0,0,0,22},
    {"LOCK 30 register", {0xf0,0x30,0xc8},3,0,0,6,6,22},
    {"LOCK 30 memory", {0xf0,0x30,0x8},3,0,0,0,0,22},
    {"LOCK 31 register", {0xf0,0x31,0xc8},3,0,0,6,6,22},
    {"LOCK 31 memory", {0xf0,0x31,0x8},3,0,0,0,0,22},
    {"LOCK 86 register", {0xf0,0x86,0xc8},3,0,0,6,6,22},
    {"LOCK 86 memory", {0xf0,0x86,0x8},3,0,0,0,0,22},
    {"LOCK 87 register", {0xf0,0x87,0xc8},3,0,0,6,6,22},
    {"LOCK 87 memory", {0xf0,0x87,0x8},3,0,0,0,0,22},
    {"LOCK 02 invalid direction", {0xf0,0x2,0x0},3,0,0,6,6,22},
    {"LOCK 03 invalid direction", {0xf0,0x3,0x0},3,0,0,6,6,22},
    {"LOCK 04 invalid direction", {0xf0,0x4,0x0},3,0,0,6,6,22},
    {"LOCK 05 invalid direction", {0xf0,0x5,0x0,0x0,0x0,0x0},6,0,0,6,6,22},
    {"LOCK 0a invalid direction", {0xf0,0xa,0x0},3,0,0,6,6,22},
    {"LOCK 0b invalid direction", {0xf0,0xb,0x0},3,0,0,6,6,22},
    {"LOCK 0c invalid direction", {0xf0,0xc,0x0},3,0,0,6,6,22},
    {"LOCK 0d invalid direction", {0xf0,0xd,0x0,0x0,0x0,0x0},6,0,0,6,6,22},
    {"LOCK 12 invalid direction", {0xf0,0x12,0x0},3,0,0,6,6,22},
    {"LOCK 13 invalid direction", {0xf0,0x13,0x0},3,0,0,6,6,22},
    {"LOCK 22 invalid direction", {0xf0,0x22,0x0},3,0,0,6,6,22},
    {"LOCK 23 invalid direction", {0xf0,0x23,0x0},3,0,0,6,6,22},
    {"LOCK 32 invalid direction", {0xf0,0x32,0x0},3,0,0,6,6,22},
    {"LOCK 33 invalid direction", {0xf0,0x33,0x0},3,0,0,6,6,22},
    {"LOCK size prefix order0 mem0", {0xf0,0x66,0x1,0xc8},4,0,0,6,6,22},
    {"LOCK size prefix order0 mem1", {0xf0,0x66,0x1,0x8},4,0,0,0,0,22},
    {"LOCK size prefix order1 mem0", {0x66,0xf0,0x1,0xc8},4,0,0,6,6,22},
    {"LOCK size prefix order1 mem1", {0x66,0xf0,0x1,0x8},4,0,0,0,0,22},
    {"CMPXCHG w8 a16 lock0 cross0", {0x64,0x67,0xf,0xb0,0xe,0x0,0x0,0x9c,0x5e},9,0x80010000,0,14,14,20},
    {"CMPXCHG w8 a16 lock1 cross0", {0xf0,0x64,0x67,0xf,0xb0,0xe,0x0,0x0,0x9c,0x5e},10,0x80010000,0,14,14,20},
    {"CMPXCHG w8 a32 lock0 cross0", {0x64,0xf,0xb0,0xf,0x9c,0x5e},6,0x80010000,0,14,14,20},
    {"CMPXCHG w8 a32 lock1 cross0", {0xf0,0x64,0xf,0xb0,0xf,0x9c,0x5e},7,0x80010000,0,14,14,20},
    {"CMPXCHG w16 a16 lock0 cross0", {0x64,0x67,0x66,0xf,0xb1,0xe,0x0,0x0,0x9c,0x5e},10,0x80010000,0,14,14,20},
    {"CMPXCHG w16 a16 lock1 cross0", {0xf0,0x64,0x67,0x66,0xf,0xb1,0xe,0x0,0x0,0x9c,0x5e},11,0x80010000,0,14,14,20},
    {"CMPXCHG w16 a32 lock0 cross0", {0x64,0x66,0xf,0xb1,0xf,0x9c,0x5e},7,0x80010000,0,14,14,20},
    {"CMPXCHG w16 a32 lock1 cross0", {0xf0,0x64,0x66,0xf,0xb1,0xf,0x9c,0x5e},8,0x80010000,0,14,14,20},
    {"CMPXCHG w32 a16 lock0 cross0", {0x64,0x67,0xf,0xb1,0xe,0x0,0x0,0x9c,0x5e},9,0x80010000,0,14,14,20},
    {"CMPXCHG w32 a16 lock1 cross0", {0xf0,0x64,0x67,0xf,0xb1,0xe,0x0,0x0,0x9c,0x5e},10,0x80010000,0,14,14,20},
    {"CMPXCHG w32 a32 lock0 cross0", {0x64,0xf,0xb1,0xf,0x9c,0x5e},6,0x80010000,0,14,14,20},
    {"CMPXCHG w32 a32 lock1 cross0", {0xf0,0x64,0xf,0xb1,0xf,0x9c,0x5e},7,0x80010000,0,14,14,20},
    {"CMPXCHG w64 a16 lock0 cross0", {0x64,0x67,0xf,0xc7,0xe,0x0,0x0,0x9c,0x5e},9,0x80010000,0,14,14,20},
    {"CMPXCHG w64 a16 lock0 cross1", {0x64,0x67,0xf,0xc7,0xe,0xfc,0xf,0x9c,0x5e},9,0x80010000,0,14,14,21},
    {"CMPXCHG w64 a16 lock1 cross0", {0xf0,0x64,0x67,0xf,0xc7,0xe,0x0,0x0,0x9c,0x5e},10,0x80010000,0,14,14,20},
    {"CMPXCHG w64 a16 lock1 cross1", {0xf0,0x64,0x67,0xf,0xc7,0xe,0xfc,0xf,0x9c,0x5e},10,0x80010000,0,14,14,21},
    {"CMPXCHG w64 a32 lock0 cross0", {0x64,0xf,0xc7,0xf,0x9c,0x5e},6,0x80010000,0,14,14,20},
    {"CMPXCHG w64 a32 lock0 cross1", {0x64,0xf,0xc7,0xf,0x9c,0x5e},6,0x80010000,0,14,14,21},
    {"CMPXCHG w64 a32 lock1 cross0", {0xf0,0x64,0xf,0xc7,0xf,0x9c,0x5e},7,0x80010000,0,14,14,20},
    {"CMPXCHG w64 a32 lock1 cross1", {0xf0,0x64,0xf,0xc7,0xf,0x9c,0x5e},7,0x80010000,0,14,14,21},
    {"CMOV 0 a16 w16", {0x64,0x67,0x66,0xf,0x40,0x7},6,0x80010000,0,14,14,19},
    {"CMOV 0 a16 w32", {0x64,0x67,0xf,0x40,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 0 a32 w16", {0x64,0x66,0xf,0x40,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 0 a32 w32", {0x64,0xf,0x40,0x7},4,0x80010000,0,14,14,19},
    {"CMOV 0 segment limit", {0x64,0x0f,0x40,0},4,0,0,13,13,1},
    {"CMOV 1 a16 w16", {0x64,0x67,0x66,0xf,0x41,0x7},6,0x80010000,0,14,14,19},
    {"CMOV 1 a16 w32", {0x64,0x67,0xf,0x41,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 1 a32 w16", {0x64,0x66,0xf,0x41,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 1 a32 w32", {0x64,0xf,0x41,0x7},4,0x80010000,0,14,14,19},
    {"CMOV 1 segment limit", {0x64,0x0f,0x41,0},4,0,0,13,13,1},
    {"CMOV 2 a16 w16", {0x64,0x67,0x66,0xf,0x42,0x7},6,0x80010000,0,14,14,19},
    {"CMOV 2 a16 w32", {0x64,0x67,0xf,0x42,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 2 a32 w16", {0x64,0x66,0xf,0x42,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 2 a32 w32", {0x64,0xf,0x42,0x7},4,0x80010000,0,14,14,19},
    {"CMOV 2 segment limit", {0x64,0x0f,0x42,0},4,0,0,13,13,1},
    {"CMOV 3 a16 w16", {0x64,0x67,0x66,0xf,0x43,0x7},6,0x80010000,0,14,14,19},
    {"CMOV 3 a16 w32", {0x64,0x67,0xf,0x43,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 3 a32 w16", {0x64,0x66,0xf,0x43,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 3 a32 w32", {0x64,0xf,0x43,0x7},4,0x80010000,0,14,14,19},
    {"CMOV 3 segment limit", {0x64,0x0f,0x43,0},4,0,0,13,13,1},
    {"CMOV 4 a16 w16", {0x64,0x67,0x66,0xf,0x44,0x7},6,0x80010000,0,14,14,19},
    {"CMOV 4 a16 w32", {0x64,0x67,0xf,0x44,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 4 a32 w16", {0x64,0x66,0xf,0x44,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 4 a32 w32", {0x64,0xf,0x44,0x7},4,0x80010000,0,14,14,19},
    {"CMOV 4 segment limit", {0x64,0x0f,0x44,0},4,0,0,13,13,1},
    {"CMOV 5 a16 w16", {0x64,0x67,0x66,0xf,0x45,0x7},6,0x80010000,0,14,14,19},
    {"CMOV 5 a16 w32", {0x64,0x67,0xf,0x45,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 5 a32 w16", {0x64,0x66,0xf,0x45,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 5 a32 w32", {0x64,0xf,0x45,0x7},4,0x80010000,0,14,14,19},
    {"CMOV 5 segment limit", {0x64,0x0f,0x45,0},4,0,0,13,13,1},
    {"CMOV 6 a16 w16", {0x64,0x67,0x66,0xf,0x46,0x7},6,0x80010000,0,14,14,19},
    {"CMOV 6 a16 w32", {0x64,0x67,0xf,0x46,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 6 a32 w16", {0x64,0x66,0xf,0x46,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 6 a32 w32", {0x64,0xf,0x46,0x7},4,0x80010000,0,14,14,19},
    {"CMOV 6 segment limit", {0x64,0x0f,0x46,0},4,0,0,13,13,1},
    {"CMOV 7 a16 w16", {0x64,0x67,0x66,0xf,0x47,0x7},6,0x80010000,0,14,14,19},
    {"CMOV 7 a16 w32", {0x64,0x67,0xf,0x47,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 7 a32 w16", {0x64,0x66,0xf,0x47,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 7 a32 w32", {0x64,0xf,0x47,0x7},4,0x80010000,0,14,14,19},
    {"CMOV 7 segment limit", {0x64,0x0f,0x47,0},4,0,0,13,13,1},
    {"CMOV 8 a16 w16", {0x64,0x67,0x66,0xf,0x48,0x7},6,0x80010000,0,14,14,19},
    {"CMOV 8 a16 w32", {0x64,0x67,0xf,0x48,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 8 a32 w16", {0x64,0x66,0xf,0x48,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 8 a32 w32", {0x64,0xf,0x48,0x7},4,0x80010000,0,14,14,19},
    {"CMOV 8 segment limit", {0x64,0x0f,0x48,0},4,0,0,13,13,1},
    {"CMOV 9 a16 w16", {0x64,0x67,0x66,0xf,0x49,0x7},6,0x80010000,0,14,14,19},
    {"CMOV 9 a16 w32", {0x64,0x67,0xf,0x49,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 9 a32 w16", {0x64,0x66,0xf,0x49,0x7},5,0x80010000,0,14,14,19},
    {"CMOV 9 a32 w32", {0x64,0xf,0x49,0x7},4,0x80010000,0,14,14,19},
    {"CMOV 9 segment limit", {0x64,0x0f,0x49,0},4,0,0,13,13,1},
    {"CMOV a a16 w16", {0x64,0x67,0x66,0xf,0x4a,0x7},6,0x80010000,0,14,14,19},
    {"CMOV a a16 w32", {0x64,0x67,0xf,0x4a,0x7},5,0x80010000,0,14,14,19},
    {"CMOV a a32 w16", {0x64,0x66,0xf,0x4a,0x7},5,0x80010000,0,14,14,19},
    {"CMOV a a32 w32", {0x64,0xf,0x4a,0x7},4,0x80010000,0,14,14,19},
    {"CMOV a segment limit", {0x64,0x0f,0x4a,0},4,0,0,13,13,1},
    {"CMOV b a16 w16", {0x64,0x67,0x66,0xf,0x4b,0x7},6,0x80010000,0,14,14,19},
    {"CMOV b a16 w32", {0x64,0x67,0xf,0x4b,0x7},5,0x80010000,0,14,14,19},
    {"CMOV b a32 w16", {0x64,0x66,0xf,0x4b,0x7},5,0x80010000,0,14,14,19},
    {"CMOV b a32 w32", {0x64,0xf,0x4b,0x7},4,0x80010000,0,14,14,19},
    {"CMOV b segment limit", {0x64,0x0f,0x4b,0},4,0,0,13,13,1},
    {"CMOV c a16 w16", {0x64,0x67,0x66,0xf,0x4c,0x7},6,0x80010000,0,14,14,19},
    {"CMOV c a16 w32", {0x64,0x67,0xf,0x4c,0x7},5,0x80010000,0,14,14,19},
    {"CMOV c a32 w16", {0x64,0x66,0xf,0x4c,0x7},5,0x80010000,0,14,14,19},
    {"CMOV c a32 w32", {0x64,0xf,0x4c,0x7},4,0x80010000,0,14,14,19},
    {"CMOV c segment limit", {0x64,0x0f,0x4c,0},4,0,0,13,13,1},
    {"CMOV d a16 w16", {0x64,0x67,0x66,0xf,0x4d,0x7},6,0x80010000,0,14,14,19},
    {"CMOV d a16 w32", {0x64,0x67,0xf,0x4d,0x7},5,0x80010000,0,14,14,19},
    {"CMOV d a32 w16", {0x64,0x66,0xf,0x4d,0x7},5,0x80010000,0,14,14,19},
    {"CMOV d a32 w32", {0x64,0xf,0x4d,0x7},4,0x80010000,0,14,14,19},
    {"CMOV d segment limit", {0x64,0x0f,0x4d,0},4,0,0,13,13,1},
    {"CMOV e a16 w16", {0x64,0x67,0x66,0xf,0x4e,0x7},6,0x80010000,0,14,14,19},
    {"CMOV e a16 w32", {0x64,0x67,0xf,0x4e,0x7},5,0x80010000,0,14,14,19},
    {"CMOV e a32 w16", {0x64,0x66,0xf,0x4e,0x7},5,0x80010000,0,14,14,19},
    {"CMOV e a32 w32", {0x64,0xf,0x4e,0x7},4,0x80010000,0,14,14,19},
    {"CMOV e segment limit", {0x64,0x0f,0x4e,0},4,0,0,13,13,1},
    {"CMOV f a16 w16", {0x64,0x67,0x66,0xf,0x4f,0x7},6,0x80010000,0,14,14,19},
    {"CMOV f a16 w32", {0x64,0x67,0xf,0x4f,0x7},5,0x80010000,0,14,14,19},
    {"CMOV f a32 w16", {0x64,0x66,0xf,0x4f,0x7},5,0x80010000,0,14,14,19},
    {"CMOV f a32 w32", {0x64,0xf,0x4f,0x7},4,0x80010000,0,14,14,19},
    {"CMOV f segment limit", {0x64,0x0f,0x4f,0},4,0,0,13,13,1},
    {"MOVLPD store a16 register", {0x67,0x66,0xf,0x13,0xc1},5,0,0,6,6,18,1},
    {"MOVLPD store a16 valid", {0x67,0x66,0xf,0x13,0x7},5,0,0,6,0,17,1},
    {"MOVLPD store a32 register", {0x66,0xf,0x13,0xc1},4,0,0,6,6,18,1},
    {"MOVLPD store a32 valid", {0x66,0xf,0x13,0x0},4,0,0,6,0,17,1},
    {"MOVHPD store a16 register", {0x67,0x66,0xf,0x17,0xc1},5,0,0,6,6,18,1},
    {"MOVHPD store a16 valid", {0x67,0x66,0xf,0x17,0x7},5,0,0,6,0,17,1},
    {"MOVHPD store a32 register", {0x66,0xf,0x17,0xc1},4,0,0,6,6,18,1},
    {"MOVHPD store a32 valid", {0x66,0xf,0x17,0x0},4,0,0,6,0,17,1},
    {"LFENCE r/m0", {0x0f,0xae,0xe8},3,0,0,6,0},
    {"LFENCE r/m3", {0x0f,0xae,0xeb},3,0,0,6,0},
    {"LFENCE r/m7", {0x0f,0xae,0xef},3,0,0,6,0},
    {"LFENCE memory invalid", {0x0f,0xae,0x28},3,0,0,6,6},
    {"MFENCE r/m0", {0x0f,0xae,0xf0},3,0,0,6,0},
    {"MFENCE r/m3", {0x0f,0xae,0xf3},3,0,0,6,0},
    {"MFENCE r/m7", {0x0f,0xae,0xf7},3,0,0,6,0},
    {"MFENCE memory invalid", {0x0f,0xae,0x30},3,0,0,6,6},
    {"SFENCE r/m0", {0x0f,0xae,0xf8},3,0,0,0,0},
    {"SFENCE r/m3", {0x0f,0xae,0xfb},3,0,0,0,0},
    {"SFENCE r/m7", {0x0f,0xae,0xff},3,0,0,0,0},
    {"CLFLUSH feature", {0x0f,0xae,0x38},3,0,0,6,0},
    {"FXSAVE register a32 TS0", {0x8b,0x15,0x1,0x2,0x0,0x0,0xf,0xae,0xc0},9,0,0,6,6,16},
    {"FXSAVE register a32 TS1", {0x8b,0x15,0x1,0x2,0x0,0x0,0xf,0xae,0xc0},9,8,0,6,6,16},
    {"FXSAVE register a16 TS0", {0x8b,0x15,0x1,0x2,0x0,0x0,0x67,0xf,0xae,0xc0},10,0,0,6,6,16},
    {"FXSAVE register a16 TS1", {0x8b,0x15,0x1,0x2,0x0,0x0,0x67,0xf,0xae,0xc0},10,8,0,6,6,16},
    {"FXRSTOR register a32 TS0", {0x8b,0x15,0x1,0x2,0x0,0x0,0xf,0xae,0xc8},9,0,0,6,6,16},
    {"FXRSTOR register a32 TS1", {0x8b,0x15,0x1,0x2,0x0,0x0,0xf,0xae,0xc8},9,8,0,6,6,16},
    {"FXRSTOR register a16 TS0", {0x8b,0x15,0x1,0x2,0x0,0x0,0x67,0xf,0xae,0xc8},10,0,0,6,6,16},
    {"FXRSTOR register a16 TS1", {0x8b,0x15,0x1,0x2,0x0,0x0,0x67,0xf,0xae,0xc8},10,8,0,6,6,16},
    {"FXRSTOR FXSAVE environment", {0x0f,0xae,0x08,0x0f,0xae,0x00},6,0,0,0,0,15},
    {"FXRSTOR pointer width a32 o32", {0xf,0xae,0x8},3,0,0,0,0,4},
    {"FXSAVE pointer width a32 o32", {0xf,0xae,0x0},3,0,0,0,0,14},
    {"FXRSTOR pointer width a32 o16", {0x66,0xf,0xae,0x8},4,0,0,0,0,4},
    {"FXSAVE pointer width a32 o16", {0x66,0xf,0xae,0x0},4,0,0,0,0,14},
    {"FXRSTOR pointer width a16 o32", {0x67,0xf,0xae,0xf},4,0,0,0,0,4},
    {"FXSAVE pointer width a16 o32", {0x67,0xf,0xae,0x7},4,0,0,0,0,14},
    {"FXRSTOR pointer width a16 o16", {0x67,0x66,0xf,0xae,0xf},5,0,0,0,0,4},
    {"FXSAVE pointer width a16 o16", {0x67,0x66,0xf,0xae,0x7},5,0,0,0,0,14},
    {"FXSAVE edit FTW FXRSTOR", {0x0f,0x6f,0xc1,0x0f,0xae,0x00,0xc6,0x40,0x04,0x00,0x0f,0xae,0x08},13,0,0,0,0},
    {"FXRSTOR reserved tail", {0x0f,0xae,0x08},3,0,0,0,0,5},
    {"FXRSTOR mixed physical tags", {0x0f,0xae,0x08,0x0f,0xae,0x00},6,0,0,0,0,13},
    {"FXRSTOR MMX payload", {0x0f,0x6f,0xc1,0x0f,0xae,0x00,0x0f,0x77,0x0f,0xae,0x08},11,0,0,0,0},
    {"MMX pending x87 exception", {0x0f,0x6f,0xc1},3,0x20,0,16,16,9},
    {"MMX pending x87 exception in counted loop", {0x0f,0xfe,0xc1,0x49,0x75,0xfa},6,0x20,0,16,16,27},
    {"EMMS pending x87 exception", {0x0f,0x77},2,0x20,0,16,16,9},
    {"FWAIT pending control", {0x9b},1,0x20,0,16,16,9},
    {"ADDPS pending x87 control", {0x0f,0x58,0xc1},3,0x20,0,0,0,9},
    {"MMX fault MOVQ", {0xf,0x6f,0x7},3,0x80010000,0,14,14,12},
    {"MMX fault MOVD", {0xf,0x6e,0x7},3,0x80010000,0,14,14,12},
    {"MMX fault PADDD", {0xf,0xfe,0x7},3,0x80010000,0,14,14,12},
    {"MMX fault PCMPEQD", {0xf,0x76,0x7},3,0x80010000,0,14,14,12},
    {"MMX fault PAND", {0xf,0xdb,0x7},3,0x80010000,0,14,14,12},
    {"MMX fault PACKSSWB", {0xf,0x63,0x7},3,0x80010000,0,14,14,12},
    {"MMX fault PSLLQ", {0xf,0xf3,0x7},3,0x80010000,0,14,14,12},
    {"MMX fault PSHUFW", {0xf,0x70,0x7,0x0},4,0x80010000,0,14,14,12},
    {"MMX fault PINSRW", {0xf,0xc4,0x7,0x0},4,0x80010000,0,14,14,12},
    {"MMX fault MOVQ store", {0xf,0x7f,0x7},3,0x80010000,0,14,14,12},
    {"MMX fault MOVD store", {0xf,0x7e,0x7},3,0x80010000,0,14,14,12},
    {"MOVQ2DQ memory", {0xf3,0xf,0xd6,0x07},4,0,0,6,6,MOVQ2DQ,1},
    {"MOVQ2DQ pending x87", {0xf3,0xf,0xd6,0xc1},4,0x20,0,6,16,MOVQ2DQ,1},
    {"MOVQ2DQ normal a32", {0xf3,0xf,0xd6,0xc1},4,0,0,6,0,MOVQ2DQ,1},
    {"MOVQ2DQ TS a32", {0xf3,0xf,0xd6,0xc1},4,8,0,6,7,MOVQ2DQ,1},
    {"MOVQ2DQ EM a32", {0xf3,0xf,0xd6,0xc1},4,4,0,6,6,MOVQ2DQ,1},
    {"MOVQ2DQ OSFXSR off a32", {0xf3,0xf,0xd6,0xc1},4,0,1,6,6,MOVQ2DQ,1},
    {"MOVQ2DQ normal a16", {0x67,0xf3,0xf,0xd6,0xc1},5,0,0,6,0,MOVQ2DQ,1},
    {"MOVQ2DQ TS a16", {0x67,0xf3,0xf,0xd6,0xc1},5,8,0,6,7,MOVQ2DQ,1},
    {"MOVQ2DQ EM a16", {0x67,0xf3,0xf,0xd6,0xc1},5,4,0,6,6,MOVQ2DQ,1},
    {"MOVQ2DQ OSFXSR off a16", {0x67,0xf3,0xf,0xd6,0xc1},5,0,1,6,6,MOVQ2DQ,1},
    {"MOVDQ2Q memory", {0xf2,0xf,0xd6,0x07},4,0,0,6,6,MOVDQ2Q,1},
    {"MOVDQ2Q pending x87", {0xf2,0xf,0xd6,0xc1},4,0x20,0,6,16,MOVDQ2Q,1},
    {"MOVDQ2Q normal a32", {0xf2,0xf,0xd6,0xc1},4,0,0,6,0,MOVDQ2Q,1},
    {"MOVDQ2Q TS a32", {0xf2,0xf,0xd6,0xc1},4,8,0,6,7,MOVDQ2Q,1},
    {"MOVDQ2Q EM a32", {0xf2,0xf,0xd6,0xc1},4,4,0,6,6,MOVDQ2Q,1},
    {"MOVDQ2Q OSFXSR off a32", {0xf2,0xf,0xd6,0xc1},4,0,1,6,6,MOVDQ2Q,1},
    {"MOVDQ2Q normal a16", {0x67,0xf2,0xf,0xd6,0xc1},5,0,0,6,0,MOVDQ2Q,1},
    {"MOVDQ2Q TS a16", {0x67,0xf2,0xf,0xd6,0xc1},5,8,0,6,7,MOVDQ2Q,1},
    {"MOVDQ2Q EM a16", {0x67,0xf2,0xf,0xd6,0xc1},5,4,0,6,6,MOVDQ2Q,1},
    {"MOVDQ2Q OSFXSR off a16", {0x67,0xf2,0xf,0xd6,0xc1},5,0,1,6,6,MOVDQ2Q,1},
    {"CVTPI2PD normal a32", {0x66,0xf,0x2a,0xc1},4,0,0,6,0,0,1},
    {"CVTPI2PD TS a32", {0x66,0xf,0x2a,0xc1},4,8,0,6,7,0,1},
    {"CVTPI2PD EM a32", {0x66,0xf,0x2a,0xc1},4,4,0,6,6,0,1},
    {"CVTPI2PD OSFXSR off a32", {0x66,0xf,0x2a,0xc1},4,0,1,6,6,0,1},
    {"CVTPI2PD normal a16", {0x67,0x66,0xf,0x2a,0xc1},5,0,0,6,0,0,1},
    {"CVTPI2PD TS a16", {0x67,0x66,0xf,0x2a,0xc1},5,8,0,6,7,0,1},
    {"CVTPI2PD EM a16", {0x67,0x66,0xf,0x2a,0xc1},5,4,0,6,6,0,1},
    {"CVTPI2PD OSFXSR off a16", {0x67,0x66,0xf,0x2a,0xc1},5,0,1,6,6,0,1},
    {"LDMXCSR normal", {0x0f,0xae,0x10},3,0,0,0,0},
    {"LDMXCSR TS", {0x0f,0xae,0x10},3,8,0,7,7},
    {"LDMXCSR EM", {0x0f,0xae,0x10},3,4,0,6,6},
    {"LDMXCSR OSFXSR off", {0x0f,0xae,0x10},3,0,1,6,6},
    {"STMXCSR TS", {0x0f,0xae,0x18},3,8,0,7,7},
    {"STMXCSR EM", {0x0f,0xae,0x18},3,4,0,6,6},
    {"STMXCSR OSFXSR off", {0x0f,0xae,0x18},3,0,1,6,6},
    {"MOVQ xmm absent a32", {0xf3,0x0f,0x7e,0x07},4,0x80010000,0,6,14,3,1},
    {"MOVQ xmm absent a16", {0x64,0x67,0xf3,0x0f,0x7e,0x07},6,0x80010000,0,6,14,3,1},
    {"MOVQ xmm cross-page", {0xf3,0x0f,0x7e,0x07},4,0x80010000,0,6,14,11,1},
    {"MOVQ xmm valid", {0xf3,0x0f,0x7e,0x00},4,0,0,6,0,0,1},
    {"MASKMOVQ FS limit a32", {0x8b,0x15,0,2,0,0,0x64,0x0f,0xf7,0xc1},10,0,0,13,13,1},
    {"MASKMOVQ FS limit a16", {0x8b,0x15,0,2,0,0,0x64,0x67,0x0f,0xf7,0xc1},11,0,0,13,13,1},
    {"MASKMOVDQU FS limit a32", {0x8b,0x15,0,2,0,0,0x64,0x66,0x0f,0xf7,0xc1},11,0,0,6,13,1,1},
    {"MASKMOVDQU FS limit a16", {0x8b,0x15,0,2,0,0,0x64,0x67,0x66,0x0f,0xf7,0xc1},12,0,0,6,13,1,1},
    {"MASKMOVQ valid", {0x0f,0xf7,0xc1},3,0,0,0,0},
    {"MASKMOVDQU valid", {0x66,0x0f,0xf7,0xc1},4,0,0,6,0,0,1},
};
static const opcode_case *probe;
static uint32_t guest_body;
static unsigned finished_compiled;
static mem_mapping_t sse_mapping;
static unsigned callback_trap, callback_count;

static uint32_t sse_read(uint32_t addr, void *priv)
{
    (void)addr;
    (void)priv;
    callback_count++;
    if (callback_trap == 7)
        cr0 |= 8;
    else if (callback_trap == 6)
        cr4 &= ~CR4_OSFXSR;
    return 0x12345678;
}

static void sse_write(uint32_t addr, uint32_t value, void *priv)
{
    (void)value;
    sse_read(addr, priv);
}
void outl(uint16_t port, uint32_t value) {
    (void)value;
    if (port != 0xec) fatal("Unexpected audit port %x", port);
    finished_compiled += !!inrecomp;
    done = 1;
    cycles = cycles_main = 0;
    cpu_block_end = 1;
}
void timer_process(void) {
    timer_target = tsc + 1000;
    if (tsc > 100000000 || x86_was_reset || cpu_state.abrt)
        fatal("Audit watchdog: %s", probe ? probe->name : "setup");
}
static void initialize(void) {
    resetx86();
    cr0 = 1; cr4 = CR4_OSFXSR | CR4_OSXMMEXCPT;
    flat_segment(&cpu_state.seg_cs,8,0x9b);
    flat_segment(&cpu_state.seg_ds,16,0x93); flat_segment(&cpu_state.seg_es,16,0x93);
    flat_segment(&cpu_state.seg_ss,16,0x93); flat_segment(&cpu_state.seg_fs,16,0x93);
    flat_segment(&cpu_state.seg_gs,16,0x93);
    cpu_cur_status = CPU_STATUS_PMODE | CPU_STATUS_USE32 | CPU_STATUS_STACK32;
    use32=0x300; stack32=1; cpu_state.flags=2; cpu_state.eflags=0;
    cycles=cycles_main=0; tsc=0; timer_target=1000; x86_was_reset=0;
    mem_a20_alt=1; mem_a20_recalc(); flushmmucache();
    memset(ram+DATA,0,0x2000);
    guest_body=probe->setup==24 ? 0x11ff1 : BODY;
    emit_pos=DRIVER; b(0xe8); d(guest_body-(DRIVER+5)); b(0xe7); b(0xec); b(0xeb); b(0xfe);
    emit_pos=guest_body;
    for (unsigned i=0;i<probe->length;i++) b(probe->bytes[i]);
    if(probe->setup==6 || probe->setup==7 || probe->setup==8) {
        b(0x0f);b(0x31);b(0xa3);d(RESULT+32);
        for(int i=0;i<(probe->setup==6?64:probe->setup==8?32:0);i++){b(0x83);b(0xc3);b(1);}
        b(0x0f);b(0x31);b(0xa3);d(RESULT+36);
        b(0x0f);b(0x31);b(0xa3);d(RESULT+40);
    }
    if (probe->setup==24) emit_pos=BODY+0x400;
    resume_pc=emit_pos;
    if (probe->setup >= POP_SP && probe->setup <= POP_ALL_WRAP) {
        /* Report directly so CALL/RET do not use the stack under test. */
        b(0xe7); b(0xec); b(0xeb); b(0xfe);
    } else
        b(0xc3);
    exception_tables();
    if (probe->setup==1) cpu_state.seg_fs.limit=cpu_state.seg_fs.limit_high=0xfff;
    if (probe->setup==MASK_PAGE || probe->setup==BIT_WRITE || probe->setup==2 || probe->setup==3 || probe->setup==11 || probe->setup==12 || probe->setup==19 || probe->setup==20 || probe->setup==21 || probe->setup==24) {
        memset(ram+0x30000,0,0x5000);
        for (unsigned n=0;n<4;n++) {
            put32(0x30000+4*n,(0x31000+0x1000*n)|3);
            for (unsigned p=0;p<1024;p++) put32(0x31000+0x1000*n+4*p,((n*1024+p)*4096)|3);
        }
        put32(0x31000+4*(probe->setup==11 ? 0x41 : 0x40),probe->setup==2 ? 0x40001 : 0);
        if (probe->setup==24) put32(0x31000+4*0x12,0);
        cr3=0x30000;
    }
}
#ifndef AUDIT_OPCODE_NO_MAIN
#include "codegen_allocator.h"
#include "codegen_backend.h"
int test_register_allocation(codeblock_t *block);
int test_ir(codeblock_t *block);

/* Select the source bit independently for every result bit, including sign
   extension. This also covers shifts across the middle of a 128-bit value. */
static SSE_REG
expected_shift(SSE_REG data, int width, unsigned kind, uint64_t count)
{
    SSE_REG result = {0};
    if (width == 128)
        count *= 8; /* The whole-register forms have an immediate byte count. */
    int distance = count < (uint64_t)width ? (int)count : width;
    for (int bit = 0; bit < 128; bit++) {
        int lane = bit / width * width;
        int source = bit + (kind == SHIFT_LEFT ? -distance : distance);
        if (kind == SHIFT_ARITH && source >= lane + width)
            source = lane + width - 1;
        if (source >= lane && source < lane + width)
            result.b[bit / 8] |= ((data.b[source / 8] >> (source % 8)) & 1) << (bit % 8);
    }
    return result;
}

static SSE_REG
expected_packed_arith(uint8_t opcode, SSE_REG a, SSE_REG b)
{
    SSE_REG result = {0};
    for (int i = 0; i < 16; i++) {
        switch (opcode) {
            case 0xda: result.b[i] = a.b[i] < b.b[i] ? a.b[i] : b.b[i]; break;
            case 0xde: result.b[i] = a.b[i] > b.b[i] ? a.b[i] : b.b[i]; break;
            case 0xe0: result.b[i] = (a.b[i] + b.b[i] + 1) >> 1; break;
            case 0xf6: result.q[i / 8] += abs(a.b[i] - b.b[i]); break;
        }
    }
    for (int i = 0; i < 8; i++) {
        switch (opcode) {
            case 0xe3: result.w[i] = (a.w[i] + b.w[i] + 1) >> 1; break;
            case 0xe4: result.w[i] = ((uint32_t)a.w[i] * b.w[i]) >> 16; break;
            case 0xea: result.sw[i] = a.sw[i] < b.sw[i] ? a.sw[i] : b.sw[i]; break;
            case 0xee: result.sw[i] = a.sw[i] > b.sw[i] ? a.sw[i] : b.sw[i]; break;
        }
    }
    if (opcode == 0xf4) {
        result.q[0] = (uint64_t)a.l[0] * b.l[0];
        result.q[1] = (uint64_t)a.l[2] * b.l[2];
    }
    return result;
}

static int run_codegen_test(int (*test)(codeblock_t *))
{
    block_current = 1;
    codeblock_t *block = &codeblock[block_current];
#ifdef CODEGEN_BACKEND_HAS_BLOCK_LINK
    codegen_block_link = 0;
#endif
    block->head_mem_block = codegen_allocator_allocate(NULL, block_current);
    block->data = codeblock_allocator_get_ptr(block->head_mem_block);
    int failed = test(block);
    codegen_allocator_free(block->head_mem_block);
    return failed;
}

int main(int argc,char **argv) {
    int p3=argc>1&&!strcmp(argv[1],"p3");
    engine=argc>2?atoi(argv[2]):0; fpu_softfloat=argc>3?atoi(argv[3]):0;
    cpu_dyn_accurate_fpu_env=argc>5?atoi(argv[5]):0;
    cpu_f=cpu_get_family(p3?"pentium3_katmai":"pentium4w"); cpu=p3?0:4;
    fpu_type=FPU_INTERNAL; cpu_use_dynarec=engine;
    cpu_set(); codegen_init(); mem_size=16*1024; mem_init(); mem_reset();
    if (argc == 2 && !strcmp(argv[1], "allocator"))
        return run_codegen_test(test_register_allocation);
    if (argc == 2 && !strcmp(argv[1], "ir"))
        return run_codegen_test(test_ir);
    mem_mapping_add(&sse_mapping, 0xd0000, 0x1000, NULL, NULL, sse_read,
                    NULL, NULL, sse_write, NULL, MEM_MAPPING_EXTERNAL, NULL);
    unsigned checked = 0;
    for(unsigned i=0;i<sizeof(opcases)/sizeof(opcases[0]);i++) {
        probe=&opcases[i]; if(argc>4&&!strstr(probe->name,argv[4]))continue;
        if (p3 && probe->only_p4) continue;
        initialize(); finished_compiled=0;
        for(unsigned call=0;call<(probe->setup==26 ? 256u : 64u);call++) {
            cr0=1|probe->cr0_bits; cr4=CR4_OSXMMEXCPT|(probe->no_osfxsr?0:CR4_OSFXSR);
            cpu_state.mxcsr=0x1f80; cpu_state.flags=2; cpu_state.eflags=0;
            cpu_state.npxc=fpu_state.cwd=0x37f; cpu_state.npxs=fpu_state.swd=0;
            cpu_state.ismmx=0;
            cpu_state.TOP=3; x87_settag(0xffff); fpu_state.tos=3; fpu_state.tag=0xffff;
            cpu_state.ssegs=0;
            cpu_state.eaaddr=DATA; easeg=0; EAX=DATA; EDI=DATA+0x800; ESP=0x700000;
            ECX=0x12345678; EDX=0;
            memset(&cpu_state.XMM[0],0x11,16); memset(&cpu_state.XMM[1],0x80,16);
            MMP[0]->q=UINT64_C(0x1111111111111111); MMP[1]->q=UINT64_C(0x8080808080808080);
            cpu_state.MM[0].q=UINT64_C(0x1111111111111111); cpu_state.MM[1].q=UINT64_C(0x8080808080808080);
            int mmx_xmm_move = probe->setup == MOVQ2DQ || probe->setup == MOVDQ2Q;
            uint64_t mm_src = UINT64_C(0xfedcba9876543210) + call;
            uint64_t xmm_src = UINT64_C(0x0123456789abcdef) - call;
            if (mmx_xmm_move) {
                MMP[1]->q = mm_src;
                cpu_state.XMM[1].q[0] = xmm_src;
                *MMEP[0] = 0x1234;
            }
            unsigned mask = (call / 8) % 3;
            if (probe->setup != MASK_PAGE && strstr(probe->name, "MASKMOV")) {
                for (unsigned n = 0; n < 16; n++) cpu_state.XMM[1].b[n] = mask == 0 ? 0 : mask == 1 ? 0x80 : (n & 1) * 0x80;
                for (unsigned n = 0; n < 8; n++) MMP[1]->b[n] = cpu_state.XMM[1].b[n];
                memset(MMP[0], 0x11, 8);
                memset(ram + DATA + 0x800, 0x5a, 16);
                memset(ram + DATA + 0x1800, 0x5a, 16);
            }
            put32(DATA,0x3f80); put32(TRAP,0); put32(FAULT_PC,0);
            if (probe->setup==2 || probe->setup==3 || probe->setup==11 || probe->setup==12 || probe->setup==19 || probe->setup==20 || probe->setup==21 || probe->setup==24) {
                cpu_state.seg_fs.base=0x40000; EBX=0;
                EDI=probe->setup==11 ? 0x40ffc : 0x40000; EAX=0x1234; put32(0x40000,0x89abcdef); put32(0x40004,0x76543210);
                flushmmucache();
            }
            if (probe->setup==4 || probe->setup==5 || probe->setup==13 || probe->setup==14 || probe->setup==15) {
                EAX=EBX=0x4000;
                memset(ram+0x4000,0,512);
                put32(0x4000,0x037f); put32(0x4008,0x12345678); put32(0x4010,0x87654321); put32(0x4018,0x1f80);
                if (probe->setup==15) {
                    ram[0x4006]=0x23;ram[0x4007]=1;ram[0x400c]=0x28;ram[0x4014]=0x30;
                    cpu_state.fpu_pc=0;cpu_state.fpu_ea=0;cpu_state.fpu_op=0;cpu_state.fpu_CS=cpu_state.fpu_DS=0;
                }
                if (probe->setup==14) {
                    cpu_state.fpu_pc=0x12345678;cpu_state.fpu_ea=0x87654321;
                }
                if (probe->setup==13) {
                    ram[0x4003]=0x18; ram[0x4004]=0xa5;
                    for(unsigned n=0;n<8;n++){put32(0x4020+16*n+4,0x80000000);ram[0x4020+16*n+8]=0xff;ram[0x4020+16*n+9]=0x3f;}
                    memset(ram+0x4000+400,0x5a,112);
                }
                if(probe->setup==5){put32(0x4000+400,0x54474553);memset(ram+0x4000+404,1,9);}
            }
            if (probe->setup==9 || probe->setup==27 || (mmx_xmm_move && probe->trap_p4 == 16)) {
                cpu_state.npxc=fpu_state.cwd=0x37e;cpu_state.npxs=fpu_state.swd=((call / 8) & 1) ? 0x8081 : 0;
            }
            if (probe->setup==27) {
                ECX=32; cpu_state.flags=2|(call&1); cpu_state.flags_op=0;
                MMP[0]->q=0; MMP[1]->q=UINT64_C(0x0000000200000001);
            }
            if (probe->setup==17) {
                EAX=EBX=0x4001;memset(ram+0x4000,0x5a,16);cpu_state.XMM[0].q[1]=UINT64_C(0x2222222222222222);
            }
            if (probe->setup == 12 || probe->setup == 19) {
                put32(0x31000+4*0x40, ((call / 8) & 1) ? 0 : 0x40003);
                flushmmucache();
            }
            if (probe->setup == 1) EDI = DATA + 0x1800;
            if (probe->setup==19) {
                EDI=EBX=0;EAX=0x12345678;cpu_state.flags_op=0;
                cpu_state.flags=2 | (call&1) | ((call&2)<<1) | ((call&4)<<4) | ((call&8)<<4) | ((call&16)<<7);
            }
            if (probe->setup == BIT_WRITE) {
                EDI = 0x40000;
                ECX = 0;
                put32(EDI, call & 1);
                put32(0x31000 + 4 * 0x40, ((call / 8) & 1) ? 0x40001 : 0x40003);
                flushmmucache();
                cpu_state.flags = 2 | ((call & 1) ^ 1);
                cpu_state.flags_op = 0;
            }
            uint32_t cmp_address = probe->setup==21 ? 0x40ffc : 0x40000;
            if (probe->setup==20 || probe->setup==21) {
                if (probe->setup==21) put32(0x31000+4*0x40,0x40003);
                put32(0x31000+4*(probe->setup==21 ? 0x41 : 0x40), (probe->setup==21 ? 0x41000 : 0x40000) | (((call/8)&1) ? 1 : 3));
                flushmmucache();
                EDI=probe->setup==21 ? 0xffc : 0;
                EAX=(call&1) ? 0x89abcdef : 0x12345678;EDX=(call&1) ? 0x76543210 : 0;EBX=0x87654321;ESI=0;
                put32(cmp_address,0x89abcdef);put32(cmp_address+4,0x76543210);
                cpu_state.flags=2;cpu_state.flags_op=0;
            }
            if (probe->setup==23 || probe->setup==24) { EAX=DATA;put32(0x4000,0x3f80); }
            uint32_t conversion_mxcsr = 0x1f80;
            if (probe->setup==26) {
                static const uint64_t fractions[] = {
                    1, 2, 3, UINT64_C(1)<<22, (UINT64_C(1)<<23)-1,
                    (UINT64_C(1)<<24)+1, UINT64_C(1)<<51, (UINT64_C(1)<<52)-1
                };
                uint64_t input = fractions[call & 7] | ((uint64_t)((call >> 3) & 1) << 63);
                EAX=EBX=0x4000;
                cpu_state.XMM[1].q[0]=input; cpu_state.XMM[1].q[1]=0;
                memcpy(ram+0x4000,&cpu_state.XMM[1],16);
                conversion_mxcsr |= ((call >> 4) & 3) << 13;
                if (call & 64) conversion_mxcsr &= ~0x800u;
                if (call & 128) conversion_mxcsr |= 0x40;
                cpu_state.mxcsr = conversion_mxcsr;
                cpu_cur_status &= ~CPU_STATUS_SSE_UNMASKED;
                if (call & 64) cpu_cur_status |= CPU_STATUS_SSE_UNMASKED;
            }
            unsigned stack_fault = (call / 8) & 1;
            SSE_REG arith_result;
            if (probe->setup == PACKED_ARITH || probe->setup == MMX_PMULUDQ) {
                SSE_REG a, b;
                for (int i = 0; i < 16; i++) {
                    a.b[i] = (uint8_t)(0xff - 39 * i + call);
                    b.b[i] = (uint8_t)(0x80 + 17 * i + call / 2);
                }
                if (probe->setup == PACKED_ARITH) {
                    const uint8_t *op = probe->bytes + (probe->bytes[0] == 0x67 ? 3 : 2);
                    cpu_state.XMM[0] = a;
                    cpu_state.XMM[1] = b;
                    EBX = 0x4000;
                    memcpy(ram + EBX + 16, &b, 16);
                    arith_result = expected_packed_arith(op[0], a, op[1] == 0xc0 ? a : b);
                } else {
                    MMP[0]->q = a.q[0];
                    MMP[1]->q = b.q[0];
                    *MMEP[0] = 0x1234;
                    memcpy(ram + EDI + 1, &b, 8);
                    arith_result = expected_packed_arith(0xf4, a, b);
                }
            }
            SSE_REG shift_result;
            if (probe->shift_width) {
                uint64_t counts[] = {0, 1, probe->shift_width - 1, probe->shift_width,
                                     probe->shift_width + 1, 255, UINT64_C(1) << 32, UINT64_MAX};
                uint64_t count = counts[call % 8];
                for (unsigned i = 0; i < 16; i++)
                    cpu_state.XMM[0].b[i] = (uint8_t)(call + 37 * i + 0x42);
                cpu_state.XMM[1].q[0] = count;
                cpu_state.XMM[1].q[1] = UINT64_MAX;
                EBX = 0x4000;
                memcpy(ram + EBX + 16, &cpu_state.XMM[1], 16);
                const uint8_t *op = probe->bytes + (probe->bytes[0] == 0x67 ? 3 : 2);
                if (op[0] >= 0x71 && op[0] <= 0x73)
                    count = op[2]; /* imm8 */
                else if (op[1] == 0xc0)
                    cpu_state.XMM[0].q[0] = count; /* self shift */
                shift_result = expected_shift(cpu_state.XMM[0], probe->shift_width, probe->setup, count);
            }
            if (probe->setup == MOVMSKPD) {
                EDX = UINT32_MAX;
                /* Set bit 31 in each qword to distinguish this from MOVMSKPS. */
                cpu_state.XMM[5].q[0] = UINT64_C(0x7fffffffffffffff) | ((uint64_t)(call & 1) << 63);
                cpu_state.XMM[5].q[1] = UINT64_C(0x80000000) | ((uint64_t)((call >> 1) & 1) << 63);
            }
            uint16_t lane_base = 0x8100 + call;
            uint16_t byte_mask = 0xa391 ^ (call * 0x421);
            if (probe->setup == XMM_LANES) {
                EBX = 0x4000;
                ECX = 0xabcd0000u | (lane_base + 24);
                for (unsigned n = 0; n < 8; n++) {
                    cpu_state.XMM[0].w[n] = lane_base + n;
                    cpu_state.XMM[1].w[n] = lane_base + 8 + n;
                    uint16_t word = lane_base + 16 + n;
                    memcpy(ram + EBX + 16 + 2 * n, &word, sizeof(word));
                }
            }
            if (probe->setup == EXTRACT_WORD || probe->setup == BYTE_MASK) {
                EDX = UINT32_MAX;
                for (unsigned n = 0; n < 16; n++)
                    cpu_state.XMM[5].b[n] = n | (((byte_mask >> n) & 1) << 7);
                if (probe->setup == EXTRACT_WORD)
                    cpu_state.XMM[5].w[6] = lane_base;
            }
            if (probe->setup == STREAM_DQ || probe->setup == STREAM_DWORD) {
                memset(ram + EDI - 1, 0x5a, 18);
                for (unsigned n = 0; n < 16; n++)
                    cpu_state.XMM[1].b[n] = call + n;
            }
            if (probe->setup == MASK_PAGE) {
                cpu_state.seg_fs.base = 0x30000;
                EDI = 0xdeadfffd;
                memset(ram + 0x3fffd, 0x5a, 16);
                memset(&cpu_state.XMM[3], 0x7f, 16);
                cpu_state.XMM[3].b[0] = cpu_state.XMM[3].b[2] = 0x80;
                if ((call / 8) & 1)
                    cpu_state.XMM[3].b[4] = 0xff;
                for (unsigned n = 0; n < 16; n++)
                    cpu_state.XMM[2].b[n] = call + n;
            }
            if (probe->setup >= PSHUFD_REG && probe->setup <= SHUFPD_MEM) {
                EBX = 0x4000;
                for (unsigned lane = 0; lane < 4; lane++) {
                    cpu_state.XMM[0].l[lane] = call + lane;
                    cpu_state.XMM[1].l[lane] = call + lane + 4;
                    put32(EBX + 16 + lane * 4, call + lane + 8);
                }
            }
            if (probe->setup >= SSE2_MOVD && probe->setup <= SSE2_MOVDQA_FAULT) {
                for (unsigned n = 0; n < 64; ++n)
                    ram[EDI + n] = (uint8_t)(n + call);
            }
            if (probe->setup == SSE_CALLBACK) {
                EDI = 0xd0000;
                callback_count = 0;
                callback_trap = (call / 8) % 3 == 0 ? 0 : (call / 8) % 3 == 1 ? 7 : 6;
                memset(&cpu_state.XMM[2], 0x33, 16);
            }
            int wrapped_stack = probe->setup == POP_ALL_WRAP || probe->setup == PUSH_ALL_WRAP;
            int push_all = probe->setup == PUSH_ALL_LIMIT || probe->setup == PUSH_ALL_WRAP;
            if (probe->setup == POP_SP) {
                ESP = (call & 1) ? 0x2fffe : 0x2fff0;
                put32(ESP, 0x4321);
            } else if (wrapped_stack) {
                stack32 = 0;
                cpu_cur_status &= ~CPU_STATUS_STACK32;
                cpu_state.seg_ss.ar_high &= ~0x40;
                cpu_state.seg_ss.base = 0x40000;
                cpu_state.seg_ss.limit = cpu_state.seg_ss.limit_high = stack_fault ? 0xfff7 : 0xffff;
                cpu_cur_status |= CPU_STATUS_NOTFLATSS | CPU_STATUS_NOTWIDESS;
                ESP = push_all ? 0x14 : 0xfff0;
            } else if (probe->setup == PUSH_ALL_LIMIT) {
                cpu_state.seg_ss.limit_low = 0x40000;
                cpu_cur_status |= CPU_STATUS_NOTFLATSS;
                ESP = stack_fault ? 0x40010 : 0x40040;
            } else if (probe->setup == POP_ALL_LIMIT) {
                cpu_state.seg_ss.limit = cpu_state.seg_ss.limit_high = 0x4001e;
                cpu_cur_status |= CPU_STATUS_NOTFLATSS | CPU_STATUS_NOTWIDESS;
                ESP = stack_fault ? 0x40010 : 0x3ffe0;
            } else if (probe->setup == UNPACK_SS) {
                cpu_state.seg_ss.limit = cpu_state.seg_ss.limit_high = 0x6fffff;
                cpu_cur_status |= CPU_STATUS_NOTFLATSS | CPU_STATUS_NOTWIDESS;
                unsigned phase = (call / 8) % 3;
                EDI = phase == 0 ? DATA : phase == 1 ? 0x6ffffa : DATA + 1;
            }
            uint32_t initial_sp = ESP;
            uint32_t final_sp = push_all ? ESP - 32 : ESP + (probe->length == 2 ? 16 : 32);
            if (wrapped_stack)
                final_sp = (uint16_t) final_sp;
            if (push_all)
                put32(cpu_state.seg_ss.base + final_sp, 0xfeed1234);
            if (probe->setup == POP_ALL_LIMIT || probe->setup == POP_ALL_WRAP)
                put32(cpu_state.seg_ss.base + ESP, 0x12345678);
            cpu_state.pc = (probe->setup >= POP_SP && probe->setup <= POP_ALL_WRAP) ? BODY : DRIVER; done=0; cycles=cycles_main=0;
            for(unsigned n=0;!done;n++) {
                if(n>10000)fatal("Audit did not stop");
                if(engine) exec386_dynarec(10000); else exec386(10000);
            }
            unsigned expected = p3 ? probe->trap_p3 : probe->trap_p4;
            if (probe->setup == SSE_CALLBACK) {
                expected = callback_trap;
                if (callback_count != 1 || (expected && get32(FAULT_PC) != BODY + 5) ||
                    memcmp(&cpu_state.XMM[2], &cpu_state.XMM[expected ? 0 : 1], 16))
                    fatal("%s call %u: callbacks %u, trap %u, EIP %x, xmm2 %x",
                          probe->name, call, callback_count, get32(TRAP), get32(FAULT_PC), cpu_state.XMM[2].l[0]);
            }
            if (mmx_xmm_move && probe->trap_p4 == 16 && !((call / 8) & 1)) expected = 0;
            if (probe->setup==26) expected = (call & 64) && !(call & 128) ? 19 : 0;
            if ((probe->setup == BIT_WRITE || probe->setup == 12 || probe->setup == 19 || probe->setup == 20 || probe->setup == 21 || probe->setup == 9 || probe->setup == 27) && !((call / 8) & 1)) expected = 0;
            if (probe->setup >= PUSH_ALL_LIMIT && probe->setup <= POP_ALL_WRAP && !stack_fault)
                expected = 0;
            if (probe->setup == UNPACK_SS) {
                unsigned phase = (call / 8) % 3;
                expected = phase == 0 ? 0 : phase == 1 ? probe->trap_p4 : 13;
                if (expected && (get32(FAULT_PC) != BODY ||
                                 cpu_state.XMM[0].q[0] != UINT64_C(0x1111111111111111) ||
                                 cpu_state.XMM[0].q[1] != UINT64_C(0x1111111111111111)))
                    fatal("%s: fault changed XMM0 or EIP", probe->name);
            }
            if (probe->setup == MASK_PAGE && !((call / 8) & 1))
                expected = 0;
            if (get32(TRAP) != expected) {
                fprintf(stderr, "%s call %u: trap %u, expected %u\n", probe->name, call, get32(TRAP), expected);
                return 1;
            }
            if (probe->setup == XMM_LANES) {
                for (unsigned n = 0; n < 8; n++) {
                    if (cpu_state.XMM[0].w[n] != lane_base + probe->lanes[n] ||
                        cpu_state.XMM[1].w[n] != lane_base + 8 + n || cpu_state.ismmx)
                        fatal("%s: incorrect word %u", probe->name, n);
                }
            }
            if ((probe->setup == EXTRACT_WORD && EDX != lane_base) ||
                (probe->setup == BYTE_MASK && EDX != byte_mask))
                fatal("%s: incorrect extracted value %x", probe->name, EDX);
            if (probe->setup == STREAM_DQ || (probe->setup == STREAM_DWORD && !expected)) {
                unsigned start = probe->setup == STREAM_DQ ? 0 : 1;
                unsigned count = probe->setup == STREAM_DQ ? 16 : 4;
                const uint8_t *bytes = probe->setup == STREAM_DQ ? cpu_state.XMM[1].b : (uint8_t *)&ECX;
                if (memcmp(ram + EDI + start, bytes, count) ||
                    ram[EDI + start - 1] != 0x5a || ram[EDI + start + count] != 0x5a)
                    fatal("%s: wrong store width or contents", probe->name);
            }
            if (probe->setup == MASK_PAGE) {
                for (unsigned n = 0; n < 16; n++)
                    if (ram[0x3fffd + n] != (n == 0 || n == 2 ? call + n : 0x5a))
                        fatal("%s call %u: wrong byte %u (%x)", probe->name, call, n, ram[0x3fffd + n]);
                if (expected && (cr2 != 0x40001 || get32(FAULT_PC) != BODY + 6))
                    fatal("%s: wrong page fault address or EIP", probe->name);
            }
            if (probe->setup >= PSHUFD_REG && probe->setup <= SHUFPD_MEM) {
                static const uint8_t lanes[][4] = {
                    {7,6,5,4}, {2,2,2,2}, {9,11,8,10},
                    {2,3,4,5}, {2,3,0,1}, {0,1,10,11},
                };
                for (unsigned lane = 0; lane < 4; lane++) {
                    if (cpu_state.XMM[0].l[lane] != call + lanes[probe->setup - PSHUFD_REG][lane] ||
                        cpu_state.XMM[1].l[lane] != call + lane + 4)
                        fatal("%s: incorrect shuffle at lane %u", probe->name, lane);
                }
            }
            if (mmx_xmm_move) {
                unsigned top = fpu_softfloat ? fpu_state.tos : cpu_state.TOP;
                unsigned tag = fpu_softfloat ? fpu_state.tag : x87_gettag();
                int wrote_xmm = !expected && probe->setup == MOVQ2DQ;
                int wrote_mm = !expected && probe->setup == MOVDQ2Q;
                uint64_t initial = UINT64_C(0x1111111111111111);
                if (cpu_state.XMM[0].q[0] != (wrote_xmm ? mm_src : initial) ||
                    cpu_state.XMM[0].q[1] != (wrote_xmm ? 0 : initial) ||
                    MMP[0]->q != (wrote_mm ? xmm_src : initial) ||
                    *MMEP[0] != (wrote_mm ? 0xffff : 0x1234) ||
                    top != (expected ? 3 : 0) || tag != (expected ? 0xffff : 0) ||
                    !!cpu_state.ismmx != !expected || (expected && get32(FAULT_PC) != BODY))
                    fatal("%s: incorrect transfer, MMX state or fault EIP", probe->name);
            }
            if (probe->setup == PACKED_ARITH && (memcmp(&cpu_state.XMM[0], &arith_result, 16) || cpu_state.ismmx))
                fatal("%s call %u: incorrect XMM result or MMX state", probe->name, call);
            if (probe->setup == MMX_PMULUDQ &&
                (MMP[0]->q != arith_result.q[0] || *MMEP[0] != 0xffff || !cpu_state.ismmx))
                fatal("%s call %u: incorrect MMX result or x87 state", probe->name, call);
            if (probe->shift_width && (memcmp(&cpu_state.XMM[0], &shift_result, 16) || cpu_state.ismmx))
                fatal("%s call %u: wrong shift result or MMX state", probe->name, call);
            if (probe->setup == MOVMSKPD &&
                (EDX != (expected ? UINT32_MAX : (call & 3)) || (expected && get32(FAULT_PC) != BODY)))
                fatal("%s: wrong destination or fault EIP", probe->name);
            if (probe->setup == SSE2_MOVD || probe->setup == SSE2_MOVQ) {
                int dword = probe->setup == SSE2_MOVD;
                unsigned width = dword ? 4 : 8;
                uint64_t value = dword ? UINT32_C(0x12345678) : UINT64_C(0x1111111111111111);
                if (cpu_state.XMM[0].q[0] != value || cpu_state.XMM[0].q[1] ||
                    cpu_state.XMM[1].q[0] != value || cpu_state.XMM[1].q[1] ||
                    (dword && EDX != value) || memcmp(ram + EDI, &value, width) ||
                    ram[EDI + width] != width + call || cpu_state.ismmx)
                    fatal("%s: incorrect scalar transfer", probe->name);
            }
            if (probe->setup == SSE2_MOVDQ || probe->setup == SSE2_MOVDQA_FAULT) {
                for (unsigned n = 0; n < 64; ++n) {
                    unsigned src = n;
                    if (probe->setup == SSE2_MOVDQ) {
                        if (n >= 17 && n < 33) src = n - 17;
                        if (n >= 48) src = n - 48;
                    }
                    if (ram[EDI + n] != src + call)
                        fatal("%s: wrong byte at offset %u", probe->name, n);
                }
                if (expected && (get32(FAULT_PC) != BODY ||
                                 cpu_state.XMM[0].q[0] != UINT64_C(0x1111111111111111) ||
                                 cpu_state.XMM[0].q[1] != UINT64_C(0x1111111111111111)))
                    fatal("%s: wrong fault EIP or destination", probe->name);
            }
            if (probe->setup == POP_SP && ESP != (((initial_sp + 2) & 0xffff0000) | 0x4321))
                fatal("%s: wrong ESP %x", probe->name, ESP);
            if (probe->setup >= PUSH_ALL_LIMIT && probe->setup <= POP_ALL_WRAP) {
                unsigned bytes = probe->length == 2 ? 16 : 32;
                if (expected) {
                    if (get32(FAULT_PC) != BODY || ESP != initial_sp || EDI != DATA + 0x800)
                        fatal("%s: stack fault changed registers or EIP", probe->name);
                    if (push_all && get32(cpu_state.seg_ss.base + final_sp) != 0xfeed1234)
                        fatal("PUSHAD wrote beyond SS");
                } else if (push_all) {
                    if (ESP != final_sp || get32(cpu_state.seg_ss.base + ESP) != EDI)
                        fatal("PUSHAD stack contents");
                } else if (ESP != final_sp || EDI != (bytes == 16 ? 0x25678 : 0x12345678))
                    fatal("%s: successful pop changed ESP or EDI incorrectly", probe->name);
            }
            if (probe->setup != MASK_PAGE && strstr(probe->name, "MASKMOV")) {
                if (expected && get32(FAULT_PC) != BODY + 6) fatal("Masked store fault EIP");
                unsigned width = strstr(probe->name, "DQU") ? 16 : 8;
                unsigned address = DATA + (expected ? 0x1800 : 0x800);
                for (unsigned n = 0; n < width; n++) {
                    unsigned written = !expected && (mask == 1 || (mask == 2 && (n & 1)));
                    if (ram[address + n] != (written ? 0x11 : 0x5a)) fatal("Masked store byte %u", n);
                }
            }
            if (strstr(probe->name, "MOVQ xmm")) {
                if (expected) {
                    for (unsigned n=0;n<4;n++) if(cpu_state.XMM[0].l[n]!=0x11111111) fatal("MOVQ changed destination on fault");
                    if (get32(FAULT_PC)!=BODY) fatal("MOVQ fault EIP");
                } else if (cpu_state.XMM[0].q[0]!=0x3f80 || cpu_state.XMM[0].q[1]) fatal("MOVQ result");
            }
            if (probe->setup == 12) {
                unsigned top = fpu_softfloat ? fpu_state.tos : cpu_state.TOP;
                unsigned tag = fpu_softfloat ? fpu_state.tag : x87_gettag();
                if (expected && (top != 3 || tag != 0xffff || cpu_state.ismmx)) fatal("Faulting MMX changed x87 state: %s", probe->name);
                if (!expected && (top || !cpu_state.ismmx)) fatal("Successful MMX did not enter MMX mode");
                if (expected && get32(FAULT_PC)!=BODY) fatal("MMX fault EIP");
            }
            if ((probe->setup == 9 || probe->setup == 27) && expected) {
                if (get32(FAULT_PC)!=BODY || cpu_state.ismmx || (fpu_softfloat ? fpu_state.tos : cpu_state.TOP)!=3)
                    fatal("Pending x87 exception changed MMX state or EIP");
            }
            if (probe->setup == 27) {
                if (MMP[0]->q != (expected ? 0 : UINT64_C(0x0000004000000020)))
                    fatal("Counted MMX loop changed its result or committed a faulting instruction");
                if (!expected && (ECX || (cpu_state.flags & 1) != (call & 1)))
                    fatal("Counted MMX loop changed carry or failed to finish");
                if (expected && (fpu_softfloat ? fpu_state.tag : x87_gettag()) != 0xffff)
                    fatal("Counted MMX loop changed tags before #MF");
            }
            if (strstr(probe->name, "edit FTW") || probe->setup==5) {
                if ((fpu_softfloat ? fpu_state.tag : x87_gettag()) != 0xffff) fatal("FXRSTOR ignored empty FTW");
            }
            if (probe->setup==13) {
                if(ram[0x4004]!=0xa5 || (get32(0x4000)&0x38000000)!=0x18000000) fatal("FXRSTOR physical tags/TOP");
                for(unsigned n=400;n<512;n++) if(ram[0x4000+n]!=0x5a) fatal("FXSAVE wrote private state");
            }
            if (strstr(probe->name, "MMX payload") && MMP[0]->q!=UINT64_C(0x8080808080808080)) fatal("FXRSTOR lost MMX payload");
            if (probe->setup==4) {
                if ((fpu_softfloat ? fpu_state.fip : x87_pc_off)!=0x12345678 || (fpu_softfloat ? fpu_state.fdp : x87_op_off)!=0x87654321) fatal("FXRSTOR truncated pointers");
            }
            if (probe->setup==14 && (get32(0x4008)!=0x12345678 || get32(0x4010)!=0x87654321)) fatal("FXSAVE truncated pointers");
            if (probe->setup==15) {
                if(get32(0x4008)!=0x12345678 || get32(0x4010)!=0x87654321 || ram[0x4006]!=0x23 || ram[0x4007]!=1 || ram[0x400c]!=0x28 || ram[0x4014]!=0x30) fatal("FXRSTOR/FXSAVE lost environment");
            }
            if (probe->setup==16 && get32(FAULT_PC)!=BODY+6) fatal("FX register form fault EIP");
            if (probe->setup==17) {
                unsigned byte = strstr(probe->name,"MOVH") ? 0x22 : 0x11;
                for(unsigned n=1;n<=8;n++) if(ram[0x4000+n]!=byte) fatal("MOVLPD/MOVHPD stored wrong lane");
                if(ram[0x4000]!=0x5a || ram[0x4009]!=0x5a) fatal("MOVLPD/MOVHPD store width");
            }
            if (probe->setup==18) {
                for(unsigned n=0;n<16;n++) if(cpu_state.XMM[1].b[n]!=0x80) fatal("Invalid store changed register");
                if(get32(FAULT_PC)!=BODY) fatal("Invalid store fault EIP");
            }
            if (probe->setup==19) {
                unsigned condition=probe->bytes[probe->length-2]&15;
                unsigned c=!!(call&1),p=!!(call&2),z=!!(call&4),sign=!!(call&8),o=!!(call&16);
                unsigned conditions[8]={o,c,z,c|z,sign,p,sign^o,z|(sign^o)};
                unsigned selected=conditions[condition>>1] ^ (condition&1);
                uint32_t value = !expected && selected ? (strstr(probe->name,"w16") ? 0x1234cdef : 0x89abcdef) : 0x12345678;
                if(EAX!=value || (expected && get32(FAULT_PC)!=BODY)) fatal("CMOV result or fault EIP: %s",probe->name);
            }
            if (probe->setup == BIT_WRITE) {
                unsigned original = call & 1;
                unsigned updated = probe->bytes[0] == 0x66 ? 1 :
                                   probe->bytes[1] == 0xb3 ? 0 : original ^ 1;
                if ((cpu_state.flags & 1) != (expected ? original ^ 1 : original) ||
                    get32(EDI) != (expected ? original : updated) ||
                    (expected && get32(FAULT_PC) != BODY))
                    fatal("%s changed carry, memory, or fault EIP", probe->name);
            }
            if (probe->setup==20 || probe->setup==21) {
                unsigned width = strstr(probe->name,"w64") ? 64 : strstr(probe->name,"w32") ? 32 : strstr(probe->name,"w16") ? 16 : 8;
                uint32_t original_eax=(call&1) ? 0x89abcdef : 0x12345678;
                if(expected) {
                    if(EAX!=original_eax || EDX!=((call&1)?0x76543210:0) || get32(FAULT_PC)!=BODY || (cpu_state.flags&0x8d5)) fatal("CMPXCHG fault changed registers/flags");
                    if(get32(cmp_address)!=0x89abcdef || get32(cmp_address+4)!=0x76543210) fatal("CMPXCHG fault changed memory");
                } else {
                    uint32_t mask=width>=32 ? UINT32_MAX : (1u<<width)-1;
                    uint32_t value=width==64 ? 0x87654321 : ((0x89abcdef & ~mask) | (0x12345678 & mask));
                    if(get32(cmp_address)!=((call&1)?value:0x89abcdef) || !!(ESI&0x40)!=!!(call&1)) fatal("CMPXCHG write/flags result");
                    uint32_t eax_expected=(call&1) ? original_eax : ((original_eax&~mask)|(0x89abcdef&mask));
                    if(EAX!=eax_expected) fatal("CMPXCHG accumulator result");
                }
            }
            if (probe->setup==22 && expected) {
                if(EAX!=DATA || ECX!=0x12345678 || get32(DATA)!=0x3f80 || get32(FAULT_PC)!=BODY) fatal("Illegal LOCK changed architectural state");
            }
            if (probe->setup==23 || probe->setup==24) {
                if (expected) {
                    if(get32(FAULT_PC)!=guest_body || EAX!=DATA || get32(DATA)!=0x3f80 || cpu_state.ismmx) fatal("Overlong instruction changed state or fault EIP");
                    for(unsigned n=0;n<4;n++) if(cpu_state.XMM[0].l[n]!=0x11111111) fatal("Overlong instruction changed XMM");
                } else {
                    if(strstr(probe->name,"store32") && get32(DATA)!=0x12345678) fatal("Valid 15-byte store");
                    if(strstr(probe->name,"store16") && get32(DATA)!=0x5678) fatal("Valid 15-byte word store");
                    if(strstr(probe->name,"address16") && get32(0x4000)!=0x12345678) fatal("Valid 15-byte address-size override");
                    if(strstr(probe->name,"immediate") && EAX!=0x12345678) fatal("Valid 15-byte immediate");
                    if(strstr(probe->name,"moffs") && EAX!=0x3f80) fatal("Valid 15-byte moffs");
                }
            }
            if (probe->setup==25) {
                if (expected && (get32(FAULT_PC)!=BODY+5 || EAX!=DATA || EDX)) fatal("RDPMC invalid selector state");
                if (!expected && (EAX || EDX)) fatal("RDPMC reset counter value");
            }
            if (probe->setup==26) {
                unsigned fraction=call&7, sign=(call>>3)&1, rounding=(call>>4)&3;
                unsigned flags = (call&128) ? 0 : expected ? (0x12 | ((fraction==5 || fraction==7) ? 0x20 : 0)) : 0x32;
                if (cpu_state.mxcsr != (conversion_mxcsr | flags))
                    fatal("%s call %u: MXCSR %x, expected %x",probe->name,call,cpu_state.mxcsr,conversion_mxcsr|flags);
                if (expected) {
                    if (get32(FAULT_PC)!=BODY) fatal("Conversion underflow fault EIP");
                    for(unsigned n=0;n<4;n++) if(cpu_state.XMM[0].l[n]!=0x11111111) fatal("Conversion fault changed destination");
                } else {
                    uint32_t result = sign << 31;
                    if (!(call&128) && ((sign && rounding==1) || (!sign && rounding==2))) result |= 1;
                    if(cpu_state.XMM[0].l[0]!=result) fatal("Denormal conversion result");
                    uint32_t upper = strstr(probe->name,"CVTPD") ? 0 : 0x11111111;
                    for(unsigned n=1;n<4;n++) if(cpu_state.XMM[0].l[n]!=upper) fatal("Conversion upper lanes");
                }
            }
            ++checked;
        }

    }
    printf("%u opcode executions passed\n", checked);
    return 0;
}
#endif
