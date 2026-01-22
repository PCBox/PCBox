/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Second CPU header.
 *
 *
 *
 * Authors: Sarah Walker, <https://pcem-emulator.co.uk/>
 *          leilei,
 *          Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2008-2020 Sarah Walker.
 *          Copyright 2016-2018 leilei.
 *          Copyright 2016-2020 Miran Grca.
 */
#ifndef EMU_X86_H
#define EMU_X86_H

#define ABRT_MASK 0x3f
/*An 'expected' exception is one that would be expected to occur on every execution
  of this code path; eg a GPF due to being in v86 mode. An 'unexpected' exception is
  one that would be unlikely to occur on the next exception, eg a page fault may be
  fixed up by the exception handler and the next execution would not hit it.

  This distinction is used by the dynarec; a block that hits an 'expected' exception
  would be compiled, a block that hits an 'unexpected' exception would be rejected so
  that we don't end up with an unnecessarily short block*/
#define ABRT_EXPECTED 0x80

extern uint8_t opcode;

extern uint8_t flags_p;
extern uint8_t znptable8[256];

extern uint16_t  zero;
extern uint16_t  oldcs;
extern uint16_t  lastcs;
extern uint16_t  lastpc;
extern uint16_t *mod1add[2][8];
extern uint16_t  znptable16[65536];

extern int x86_was_reset;
extern int trap;
extern int codegen_flat_ss;
extern int codegen_flat_ds;
extern int timetolive;
extern int keyboardtimer;
extern int trap;
extern int optype;
extern int stack32;
extern int oldcpl;
extern int cpl_override;
extern int nmi_enable;
extern int oddeven;
extern int inttype;

extern int cpu_init;

extern uint32_t  use32;
extern uint32_t  rmdat;
extern uint32_t  easeg;
extern uint32_t  oxpc;
extern uint32_t  flags_zn;
extern uint32_t  abrt_error;
extern uint32_t  backupregs[16];
extern uint32_t *mod1seg[8];
extern uint32_t *eal_r;
extern uint32_t *eal_w;

extern int fpu_cycles;

#define fetchdat  rmdat

#define setznp168 setznp16

#define getr8(r)  (((r & 4) && !(r & 0x10)) ? cpu_state.regs[r & 3].b.h : (r & 8) ? cpu_state.regs64[r&7].b.l : cpu_state.regs[r&7].b.l)
#define getr16(r) (r & 8) ? cpu_state.regs64[r&7].w : cpu_state.regs[r&0x7].w
#define getr32(r) (r & 8) ? cpu_state.regs64[r&7].l : cpu_state.regs[r&0x7].l
#define getr64(r) (r & 8) ? ((uint64_t)cpu_state.regs_high[r&0xf] << 32) | (uint64_t)cpu_state.regs64[r&7].l : \
                  ((uint64_t)cpu_state.regs_high[r&0xf] << 32) | (uint64_t)cpu_state.regs[r&0x7].l

#define setr8(r, v)                    \
    if ((r & 4) && !(r & 0x10))                         \
        cpu_state.regs[r & 3].b.h = v; \
    else if(!(r & 8))                               \
        cpu_state.regs[r & 7].b.l = v; \
    else \
        cpu_state.regs64[r & 7].b.l = v
#define setr16(r, v) \
    if(r & 8) \
    { \
        cpu_state.regs64[r&0x7].w = v; \
    } \
    else \
    { \
        cpu_state.regs[r&0x7].w = v; \
    }
#define setr32(r, v) \
    if(r & 8) \
    { \
        cpu_state.regs64[r&0x7].l = v; cpu_state.regs_high[r&0xf] = 0; \
    } \
    else \
    { \
        cpu_state.regs[r&0x7].l = v; cpu_state.regs_high[r&0xf] = 0; \
    }

#define setr64(r, v) \
    if(r & 8) \
    { \
        cpu_state.regs64[r&0x7].l = v & 0xffffffffu; cpu_state.regs_high[r&0xf] = v >> 32; \
    } \
    else \
    { \
        cpu_state.regs[r&0x7].l = v & 0xffffffffu; cpu_state.regs_high[r&0xf] = v >> 32; \
    }

#define fetchea()                  \
    {                              \
        rmdat = readmemb(cs + pc); \
        pc++;                      \
        reg = ((rmdat >> 3) & 7) | ((cpu_state.rex_byte & 4) << 1) | (cpu_state.rex_present << 4);  \
        mod = (rmdat >> 6) & 3;    \
        rm  = (rmdat & 7) | ((cpu_state.rex_byte & 1) << 3) | (cpu_state.rex_present << 4);         \
        if (mod != 3)              \
            fetcheal();            \
    }

extern void x86illegal(void);

extern uint8_t rep_op;
extern uint8_t is_smint;

extern uint16_t io_port;
extern uint32_t io_val;

#endif /*EMU_X86_H*/
