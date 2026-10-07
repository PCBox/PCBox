/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of nVidia's RIVA TNT graphics card.
 *		Special thanks to Marcelina Kościelnicka, without whom this
 *		would not have been possible.
 *
 * Version:	@(#)vid_rivatnt.c	1.0.0	2019/09/13
 *
 * Authors:	Miran Grca, <mgrca8@gmail.com>
 *		Melody Goad
 *
 *		Copyright 2020 Miran Grca.
 *		Copyright 2020 Melody Goad.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include "../cpu/cpu.h"
#include <86box/dma.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/device.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/vid_svga.h>
#include <86box/vid_svga_render.h>
#include <86box/plat_unused.h>

#define BIOS_RIVATNT_PATH		"roms/video/nvidia/NV4_diamond_revB.rom"

#define RIVATNT_VENDOR_ID 0x10de
#define RIVATNT_DEVICE_ID 0x0020

#define RIVATNT_NUM_CHANNELS 16
/* NV4 CACHE1 holds 32 entries; the RM sizes every channel for 124 bytes
   (31 usable entries), so PUT == GET means empty. */
#define RIVATNT_CACHE1_SIZE  32
#define RIVATNT_CACHE1_MASK  ((RIVATNT_CACHE1_SIZE - 1) << 2)

/* PFIFO_INTR_0 bits */
#define PFIFO_INTR_CACHE_ERROR (1 << 0)
#define PFIFO_INTR_RUNOUT      (1 << 4)
#define PFIFO_INTR_RUNOUT_OVF  (1 << 8)
#define PFIFO_INTR_DMA_PUSHER  (1 << 12)
#define PFIFO_INTR_DMA_PT      (1 << 16)

/* CACHE1_PULL0 bits */
#define PULL0_ACCESS        (1 << 0)
#define PULL0_HASH_FAILED   (1 << 4)
#define PULL0_DEVICE_SW     (1 << 8)

/* Engines in RAMHT, CACHE1_ENGINE and CACHE1_PULL1 */
#define ENGINE_SW       0
#define ENGINE_GRAPHICS 1

/* PGRAPH_INTR bits */
#define PGRAPH_INTR_NOTIFY         (1 << 0)
#define PGRAPH_INTR_MISSING_HW     (1 << 4)
#define PGRAPH_INTR_CONTEXT_SWITCH (1 << 12)

/* PGRAPH registers, as offsets into the register file */
#define PGRAPH_REG(a) (((a) - 0x400000) >> 2)
#define NV_PGRAPH_DEBUG_1      0x400084
#define NV_PGRAPH_INTR         0x400100
#define NV_PGRAPH_NSTATUS      0x400104
#define NV_PGRAPH_NSOURCE      0x400108
#define NV_PGRAPH_INTR_EN      0x400140
#define NV_PGRAPH_CTX_SWITCH1  0x400160
#define NV_PGRAPH_CTX_SWITCH2  0x400164
#define NV_PGRAPH_CTX_SWITCH3  0x400168
#define NV_PGRAPH_CTX_SWITCH4  0x40016c
#define NV_PGRAPH_CTX_CONTROL  0x400170
#define NV_PGRAPH_CTX_USER     0x400174
#define NV_PGRAPH_CTX_CACHE1   0x400180
#define NV_PGRAPH_CTX_CACHE2   0x4001a0
#define NV_PGRAPH_CTX_CACHE3   0x4001c0
#define NV_PGRAPH_CTX_CACHE4   0x4001e0
#define NV_PGRAPH_STATUS       0x400700
#define NV_PGRAPH_TRAPPED_ADDR 0x400704
#define NV_PGRAPH_TRAPPED_DATA 0x400708
#define NV_PGRAPH_NOTIFY       0x400714
#define NV_PGRAPH_FIFO         0x400720

#define PGRAPH_NOTIFY_REQ   (1 << 16)
#define PGRAPH_NOTIFY_STYLE (1 << 20)
#define PGRAPH_NOTIFY_BUFFER_REQ   (1 << 0)
#define PGRAPH_NOTIFY_BUFFER_STYLE (1 << 8)
#define PGRAPH_INTR_BUFFER_NOTIFY  (1 << 16)

/* 2D state registers */
#define NV_PGRAPH_ABS_UCLIP_XMIN  0x40053c
#define NV_PGRAPH_ABS_UCLIP_YMIN  0x400540
#define NV_PGRAPH_ABS_UCLIP_XMAX  0x400544
#define NV_PGRAPH_ABS_UCLIP_YMAX  0x400548
#define NV_PGRAPH_ABS_UCLIPA_XMIN 0x400560
#define NV_PGRAPH_ABS_UCLIPA_YMIN 0x400564
#define NV_PGRAPH_ABS_UCLIPA_XMAX 0x400568
#define NV_PGRAPH_ABS_UCLIPA_YMAX 0x40056c
#define NV_PGRAPH_MONO_COLOR0     0x400600
#define NV_PGRAPH_ROP3            0x400604
#define NV_PGRAPH_BETA_AND        0x400608
#define NV_PGRAPH_BETA_PREMULT    0x40060c
#define NV_PGRAPH_BOFFSET(i)      (0x400640 + (i) * 4)
#define NV_PGRAPH_BBASE(i)        (0x400658 + (i) * 4)
#define NV_PGRAPH_BPITCH(i)       (0x400670 + (i) * 4)
#define NV_PGRAPH_BLIMIT(i)       (0x400684 + (i) * 4)
#define NV_PGRAPH_BPIXEL          0x400724
#define NV_PGRAPH_PATT_COLOR(i)   (0x400800 + (i) * 4)
#define NV_PGRAPH_PATTERN(i)      (0x400808 + (i) * 4)
#define NV_PGRAPH_PATTERN_SHAPE   0x400810
#define NV_PGRAPH_CHROMA          0x400814
#define NV_PGRAPH_STORED_FMT      0x400830
#define NV_PGRAPH_PATT_COLORRAM(i) (0x400900 + (i) * 4)

/* Surfaces: BOFFSET/BPITCH/BPIXEL index 0 is the destination, 1 the source. */
#define SURF_DST 0
#define SURF_SRC 1

/* CTX_SWITCH1 PATCH_CONFIG (the object's OPERATION) */
#define OP_SRCCOPY_AND 0
#define OP_ROP_AND     1
#define OP_BLEND_AND   2
#define OP_SRCCOPY     3
#define OP_SRCCOPY_PRE 4
#define OP_BLEND_PRE   5

typedef struct rivatnt_t
{
    mem_mapping_t	mmio_mapping;
    mem_mapping_t 	linear_mapping;

    svga_t		svga;

    rom_t		bios_rom;

    uint32_t		vram_size, vram_mask,
            mmio_base, lfb_base, ramin_flip;

    uint8_t		read_bank, write_bank;

    uint8_t		pci_regs[256];
    uint8_t     pci_slot;
    uint8_t     irq_state;
    uint8_t		int_line;

    uint32_t cursor_offset;
	int cursor_vram;
	int cursor_enabled;

    int			card;

    struct
    {
        uint8_t rma_access_reg[4];
        uint8_t rma_mode;
        uint32_t rma_dst_addr;
        uint32_t rma_data;
    } rma;

    struct 
    {
        uint32_t intr;
        uint32_t intr_en;
        uint32_t intr_line;
        uint32_t enable;
    } pmc;

    struct
    {
        uint32_t intr;
        uint32_t intr_en;
        uint32_t debug_0;

        uint32_t delay_0, dma_timeslice, pio_timeslice, timeslice, next_channel;
        uint32_t ramht, ramfc, ramro;
        uint32_t runout_put, runout_get;
        uint32_t caches, mode, dma, size;

        /* CACHE0 is a single entry the RM uses to inject methods. */
        uint32_t cache0_push0, cache0_push1, cache0_put, cache0_get;
        uint32_t cache0_pull0, cache0_pull1, cache0_hash, cache0_engine;
        uint32_t cache0_method, cache0_data;

        uint32_t cache1_push0, cache1_push1, cache1_put, cache1_get;
        uint32_t cache1_pull0, cache1_pull1, cache1_hash, cache1_engine;
        uint32_t cache1_status1;
        uint32_t cache1_method[RIVATNT_CACHE1_SIZE];
        uint32_t cache1_data[RIVATNT_CACHE1_SIZE];

        /* DMA pusher state for the channel loaded in CACHE1 */
        uint32_t dma_push, dma_fetch, dma_state, dma_instance;
        uint32_t dma_ctl, dma_limit, dma_tlb_tag, dma_tlb_pte;
        uint32_t dma_put, dma_get, dma_dcount;
        uint32_t dma_get_jmp_shadow, dma_rsvd_shadow, dma_data_shadow;
    } pfifo;

    struct
    {
        uint32_t intr, intr_en;

        uint64_t time;
        double   time_frac;
        uint32_t alarm;

        uint16_t clock_mul, clock_div; /* DENOMINATOR, NUMERATOR */
    } ptimer;

    struct
    {
        uint16_t width;
        int bpp;

        uint32_t config_0;
        uint32_t regs[0x1000 >> 2];
    } pfb;
    
    struct
    {
        uint32_t intr, intr_en;
    } pcrtc;

    struct
    {
        /* 0x400000-0x401fff. PGRAPH keeps its whole channel state in
           registers that the RM saves and restores on a context switch. */
        uint32_t regs[0x2000 >> 2];
    } pgraph;

    int gpu_busy;

    /* Progress of the multi-method primitive in flight (image data streams,
       vertex pairs). The real chip keeps this in volatile PGRAPH state. */
    struct
    {
        int32_t  x, y;         /* destination of the image or bitmap */
        int32_t  w_in, h_in;   /* image size in pixels */
        int32_t  w_out, h_out; /* destination clip size */
        uint32_t pos;          /* pixels of the image received so far */
        uint32_t color0, color1;
        int32_t  clip_x0, clip_y0, clip_x1, clip_y1;
        int32_t  vtx_x[3], vtx_y[3];
        int      vtx_n;
        uint32_t color;
        uint32_t font, palette_offset, index_format;
        uint32_t m2mf_offset_in, m2mf_offset_out;
        int32_t  m2mf_pitch_in, m2mf_pitch_out;
        uint32_t m2mf_line_length, m2mf_line_count, m2mf_format;
    } d2;
    

    struct
    {
        uint32_t nvpll, mpll, vpll;
        uint32_t cursor_pos;
    } pramdac;

    /* PTIMER is computed from the TSC when accessed; this only fires the alarm. */
    pc_timer_t ptimer_alarm_timer;
    uint64_t   ptimer_tsc_base;
    double     nvclk; /* Hz */

    void *i2c, *ddc;
} rivatnt_t;

static video_timings_t timing_rivatnt		= {VIDEO_PCI, 2,  2,  1,  20, 20, 21};

static uint8_t rivatnt_in(uint16_t addr, void *p);
static void rivatnt_out(uint16_t addr, uint8_t val, void *p);

uint8_t
rivatnt_ramin_read(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	svga_t *svga = &rivatnt->svga;

	addr &= rivatnt->vram_mask;

	return svga->vram[addr ^ rivatnt->ramin_flip];
}


uint16_t
rivatnt_ramin_read_w(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	svga_t *svga = &rivatnt->svga;
	uint16_t *vram_w = (uint16_t *)svga->vram;

	addr &= rivatnt->vram_mask;

	return vram_w[(addr ^ rivatnt->ramin_flip) >> 1];
}


uint32_t
rivatnt_ramin_read_l(uint32_t addr, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	svga_t *svga = &rivatnt->svga;
	uint32_t *vram_l = (uint32_t *)svga->vram;

	addr &= rivatnt->vram_mask;

	return vram_l[(addr ^ rivatnt->ramin_flip) >> 2];
}


void
rivatnt_ramin_write(uint32_t addr, uint8_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	svga_t *svga = &rivatnt->svga;

	addr &= rivatnt->vram_mask;

	//pclog("[RIVA 128] RAMIN write %08x %02x\n", addr, val);

	svga->vram[addr ^ rivatnt->ramin_flip] = val;
}


void
rivatnt_ramin_write_w(uint32_t addr, uint16_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	svga_t *svga = &rivatnt->svga;
	uint16_t *vram_w = (uint16_t *)svga->vram;

	addr &= rivatnt->vram_mask;

	//pclog("[RIVA 128] RAMIN write %08x %04x\n", addr, val);

	vram_w[(addr ^ rivatnt->ramin_flip) >> 1] = val;
}


void
rivatnt_ramin_write_l(uint32_t addr, uint32_t val, void *p)
{
	rivatnt_t *rivatnt = (rivatnt_t *)p;
	svga_t *svga = &rivatnt->svga;
	uint32_t *vram_l = (uint32_t *)svga->vram;

	addr &= rivatnt->vram_mask;

	//pclog("[RIVA 128] RAMIN write %08x %08x\n", addr, val);

	vram_l[(addr ^ rivatnt->ramin_flip) >> 2] = val;
}

static uint8_t 
rivatnt_pci_read(int func, int addr, UNUSED(int len), void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    // svga_t *svga = &rivatnt->svga;

    //pclog("RIVA TNT PCI read %02x\n", addr);

    switch (addr) {
    case 0x00: return 0xde; /*nVidia*/
    case 0x01: return 0x10;

    case 0x02: return 0x20;
    case 0x03: return 0x00;
    
    case 0x04: return rivatnt->pci_regs[0x04] & 0x37; /*Respond to IO and memory accesses*/
    case 0x05: return rivatnt->pci_regs[0x05] & 0x03;

    case 0x06: return 0x18;
    case 0x07: return 0x02;

    case 0x08: return 0x00; /*Revision ID*/
    case 0x09: return 0x00; /*Programming interface*/

    case 0x0a: return 0x00; /*Supports VGA interface*/
    case 0x0b: return 0x03;

    case 0x13: return rivatnt->mmio_base >> 24;

    case 0x17: return rivatnt->lfb_base >> 24;

    case 0x2c: case 0x2d: case 0x2e: case 0x2f:
        return rivatnt->pci_regs[addr];

    case 0x30: return (rivatnt->pci_regs[0x30] & 0x01); /*BIOS ROM address*/
    case 0x31: return 0x00;
    case 0x32: return rivatnt->pci_regs[0x32];
    case 0x33: return rivatnt->pci_regs[0x33];

    case 0x3c: return rivatnt->int_line;
    case 0x3d: return PCI_INTA;

    case 0x3e: return 0x03;
    case 0x3f: return 0x01;
    }

    return 0x00;
}


static void 
rivatnt_recalc_mapping(rivatnt_t *rivatnt)
{
    svga_t *svga = &rivatnt->svga;
        
    if (!(rivatnt->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
    //pclog("PCI mem off\n");
        mem_mapping_disable(&svga->mapping);
        mem_mapping_disable(&rivatnt->mmio_mapping);
        mem_mapping_disable(&rivatnt->linear_mapping);
    return;
    }

    //pclog("PCI mem on\n");
    //pclog("rivatnt->mmio_base = %08X\n", rivatnt->mmio_base);
    if (rivatnt->mmio_base)
        mem_mapping_set_addr(&rivatnt->mmio_mapping, rivatnt->mmio_base, 0x1000000);
    else
        mem_mapping_disable(&rivatnt->mmio_mapping);

    //pclog("rivatnt->lfb_base = %08X\n", rivatnt->lfb_base);
    if (rivatnt->lfb_base) {
    mem_mapping_set_addr(&rivatnt->linear_mapping, rivatnt->lfb_base, 0x1000000);
    } else {
        mem_mapping_disable(&rivatnt->linear_mapping);
    }

    switch (svga->gdcreg[6] & 0x0c) {
    case 0x0: /*128k at A0000*/
        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
        svga->banked_mask = 0x1ffff;
        break;
    case 0x4: /*64k at A0000*/
        mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x10000);
        svga->banked_mask = 0xffff;
        break;
    case 0x8: /*32k at B0000*/
        mem_mapping_set_addr(&svga->mapping, 0xb0000, 0x08000);
        svga->banked_mask = 0x7fff;
        break;
    case 0xC: /*32k at B8000*/
        mem_mapping_set_addr(&svga->mapping, 0xb8000, 0x08000);
        svga->banked_mask = 0x7fff;
        break;
    }
}


static void 
rivatnt_pci_write(int func, int addr, UNUSED(int len), uint8_t val, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    //pclog("RIVA TNT PCI write %02x %02x\n", addr, val);

    switch (addr) {
    case PCI_REG_COMMAND:
        rivatnt->pci_regs[PCI_REG_COMMAND] = val & 0x37;
        io_removehandler(0x03a0, 0x0040, rivatnt_in, NULL, NULL, rivatnt_out, NULL, NULL, rivatnt);
        if (val & PCI_COMMAND_IO)
            io_sethandler(0x03a0, 0x0040, rivatnt_in, NULL, NULL, rivatnt_out, NULL, NULL, rivatnt);
        rivatnt_recalc_mapping(rivatnt);
        break;

    case 0x05:
        rivatnt->pci_regs[0x05] = val & 0x01;
        break;

    case 0x13:
        rivatnt->mmio_base = val << 24;
        rivatnt_recalc_mapping(rivatnt);
        break;

    case 0x17: 
        rivatnt->lfb_base = val << 24;
        rivatnt_recalc_mapping(rivatnt);
        break;

    case 0x30: case 0x32: case 0x33:
        rivatnt->pci_regs[addr] = val;
        if (rivatnt->pci_regs[0x30] & 0x01) {
            uint32_t addr = (rivatnt->pci_regs[0x32] << 16) | (rivatnt->pci_regs[0x33] << 24);
            mem_mapping_set_addr(&rivatnt->bios_rom.mapping, addr, 0x10000);
        } else
            mem_mapping_disable(&rivatnt->bios_rom.mapping);
        break;

    case 0x3c:
        rivatnt->int_line = val;
        break;

    case 0x40: case 0x41: case 0x42: case 0x43:
        /* 0x40-0x43 are ways to write to 0x2c-0x2f */
        rivatnt->pci_regs[0x2c + (addr & 0x03)] = val;
        break;
    }
}

#define PGR(r) (rivatnt->pgraph.regs[PGRAPH_REG(r)])

static void rivatnt_do_gpu_work(rivatnt_t *rivatnt);

uint32_t
rivatnt_pmc_recompute_intr(void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    uint32_t intr = 0;
    if(rivatnt->pfifo.intr & rivatnt->pfifo.intr_en) intr |= (1 << 8);
    if(PGR(NV_PGRAPH_INTR) & PGR(NV_PGRAPH_INTR_EN)) intr |= (1 << 12);
    if(rivatnt->ptimer.intr & rivatnt->ptimer.intr_en) intr |= (1 << 20);
    if(rivatnt->pcrtc.intr & rivatnt->pcrtc.intr_en) intr |= (1 << 24);
    if(rivatnt->pmc.intr & (1u << 31)) intr |= (1u << 31);

    /* The interrupt line is level-triggered: it follows the pending sources. */
    if (((intr & 0x7fffffff) && (rivatnt->pmc.intr_en & 1)) || ((intr & (1u << 31)) && (rivatnt->pmc.intr_en & 2)))
        pci_set_irq(rivatnt->pci_slot, PCI_INTA, &rivatnt->irq_state);
    else
        pci_clear_irq(rivatnt->pci_slot, PCI_INTA, &rivatnt->irq_state);
    return intr;
}

uint32_t
rivatnt_pmc_read(uint32_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    switch(addr)
    {
    case 0x000000:
        return 0x20104010; //ID register.
    case 0x000100:
        return rivatnt_pmc_recompute_intr(rivatnt);
    case 0x000140:
        return rivatnt->pmc.intr_en;
    case 0x000200:
        return rivatnt->pmc.enable;
    }
    return 0;
}

void
rivatnt_pmc_write(uint32_t addr, uint32_t val, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    switch(addr)
    {
    case 0x000100:
        rivatnt->pmc.intr = val & (1u << 31);
        rivatnt_pmc_recompute_intr(rivatnt);
        break;
    case 0x000140:
        rivatnt->pmc.intr_en = val & 3;
        rivatnt_pmc_recompute_intr(rivatnt);
        break;
    case 0x000200:
        rivatnt->pmc.enable = val;
        break;
    }
}

static int
rivatnt_cache1_empty(rivatnt_t *rivatnt)
{
    return rivatnt->pfifo.cache1_put == rivatnt->pfifo.cache1_get;
}

static int
rivatnt_cache1_full(rivatnt_t *rivatnt)
{
    return ((rivatnt->pfifo.cache1_put + 4) & RIVATNT_CACHE1_MASK) == rivatnt->pfifo.cache1_get;
}

static int
rivatnt_cache1_chid(rivatnt_t *rivatnt)
{
    return rivatnt->pfifo.cache1_push1 & 0xf;
}

static int
rivatnt_cache1_is_dma(rivatnt_t *rivatnt)
{
    return !!(rivatnt->pfifo.cache1_push1 & 0x100);
}

/* The DMA pusher is idle when it is switched off or has caught up with PUT. */
static int
rivatnt_dma_pusher_idle(rivatnt_t *rivatnt)
{
    return !rivatnt_cache1_is_dma(rivatnt) || !(rivatnt->pfifo.dma_push & 1) ||
           (rivatnt->pfifo.dma_put == rivatnt->pfifo.dma_get);
}

static uint32_t
rivatnt_ramfc_addr(rivatnt_t *rivatnt, int chid)
{
    return (((rivatnt->pfifo.ramfc >> 1) & 0xff) << 9) + chid * 32;
}

uint32_t
rivatnt_pfifo_read(uint32_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    if ((addr >= 0x003800) && (addr < 0x004000)) {
        int idx = ((addr >> 3) & 0x7f) & (RIVATNT_CACHE1_SIZE - 1);
        return (addr & 4) ? rivatnt->pfifo.cache1_data[idx] : rivatnt->pfifo.cache1_method[idx];
    }

    switch(addr)
    {
    case 0x002040: return rivatnt->pfifo.delay_0;
    case 0x002044: return rivatnt->pfifo.dma_timeslice;
    case 0x002048: return rivatnt->pfifo.pio_timeslice;
    case 0x00204c: return rivatnt->pfifo.timeslice;
    case 0x002050: return rivatnt->pfifo.next_channel;
    case 0x002080: return rivatnt->pfifo.debug_0;
    case 0x002100: return rivatnt->pfifo.intr;
    case 0x002140: return rivatnt->pfifo.intr_en;
    case 0x002210: return rivatnt->pfifo.ramht;
    case 0x002214: return rivatnt->pfifo.ramfc;
    case 0x002218: return rivatnt->pfifo.ramro;
    case 0x002400:
        if (rivatnt->pfifo.runout_put == rivatnt->pfifo.runout_get)
            return 0x10; /* LOW_MARK empty */
        return 0x01; /* RANOUT */
    case 0x002410: return rivatnt->pfifo.runout_put;
    case 0x002420: return rivatnt->pfifo.runout_get;
    case 0x002500: return rivatnt->pfifo.caches;
    case 0x002504: return rivatnt->pfifo.mode;
    case 0x002508: return rivatnt->pfifo.dma;
    case 0x00250c: return rivatnt->pfifo.size;

    case 0x003000: return rivatnt->pfifo.cache0_push0;
    case 0x003004: return rivatnt->pfifo.cache0_push1;
    case 0x003010: return rivatnt->pfifo.cache0_put;
    case 0x003014: return (rivatnt->pfifo.cache0_put == rivatnt->pfifo.cache0_get) ? 0x10 : 0x100;
    case 0x003050: return rivatnt->pfifo.cache0_pull0;
    case 0x003054: return rivatnt->pfifo.cache0_pull1;
    case 0x003058: return rivatnt->pfifo.cache0_hash;
    case 0x003070: return rivatnt->pfifo.cache0_get;
    case 0x003080: return rivatnt->pfifo.cache0_engine;
    case 0x003100: return rivatnt->pfifo.cache0_method;
    case 0x003104: return rivatnt->pfifo.cache0_data;

    case 0x003200: return rivatnt->pfifo.cache1_push0;
    case 0x003204: return rivatnt->pfifo.cache1_push1;
    case 0x003210: return rivatnt->pfifo.cache1_put;
    case 0x003214:
        return (rivatnt_cache1_empty(rivatnt) ? 0x10 : 0) | (rivatnt_cache1_full(rivatnt) ? 0x100 : 0);
    case 0x003218: return rivatnt->pfifo.cache1_status1;
    case 0x003220:
        /* STATE (bit 4) busy while there is work; BUFFER (bit 8) is always
           empty because fetched words go straight into CACHE1. */
        return (rivatnt->pfifo.dma_push & 0x1001) | 0x100 | (rivatnt_dma_pusher_idle(rivatnt) ? 0 : 0x10);
    case 0x003224: return rivatnt->pfifo.dma_fetch;
    case 0x003228: return rivatnt->pfifo.dma_state;
    case 0x00322c: return rivatnt->pfifo.dma_instance;
    case 0x003230: return rivatnt->pfifo.dma_ctl;
    case 0x003234: return rivatnt->pfifo.dma_limit;
    case 0x003238: return rivatnt->pfifo.dma_tlb_tag;
    case 0x00323c: return rivatnt->pfifo.dma_tlb_pte;
    case 0x003240: return rivatnt->pfifo.dma_put;
    case 0x003244: return rivatnt->pfifo.dma_get;
    case 0x003250: return rivatnt->pfifo.cache1_pull0;
    case 0x003254: return rivatnt->pfifo.cache1_pull1;
    case 0x003258: return rivatnt->pfifo.cache1_hash;
    case 0x003270: return rivatnt->pfifo.cache1_get;
    case 0x003280: return rivatnt->pfifo.cache1_engine;
    case 0x0032a0: return rivatnt->pfifo.dma_dcount;
    case 0x0032a4: return rivatnt->pfifo.dma_get_jmp_shadow;
    case 0x0032a8: return rivatnt->pfifo.dma_rsvd_shadow;
    case 0x0032ac: return rivatnt->pfifo.dma_data_shadow;
    }
    return 0;
}

void
rivatnt_pfifo_write(uint32_t addr, uint32_t val, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    if ((addr >= 0x003800) && (addr < 0x004000)) {
        int idx = ((addr >> 3) & 0x7f) & (RIVATNT_CACHE1_SIZE - 1);
        if (addr & 4)
            rivatnt->pfifo.cache1_data[idx] = val;
        else
            rivatnt->pfifo.cache1_method[idx] = val & 0xfffc;
        return;
    }

    switch(addr)
    {
    case 0x002040: rivatnt->pfifo.delay_0 = val; break;
    case 0x002044: rivatnt->pfifo.dma_timeslice = val; break;
    case 0x002048: rivatnt->pfifo.pio_timeslice = val; break;
    case 0x00204c: rivatnt->pfifo.timeslice = val; break;
    case 0x002050: rivatnt->pfifo.next_channel = val; break;
    case 0x002080: rivatnt->pfifo.debug_0 = val; break;
    case 0x002100:
        rivatnt->pfifo.intr &= ~val;
        if (!(rivatnt->pfifo.intr & PFIFO_INTR_CACHE_ERROR))
            rivatnt->pfifo.debug_0 &= ~0x11;
        rivatnt_pmc_recompute_intr(rivatnt);
        break;
    case 0x002140:
        rivatnt->pfifo.intr_en = val & 0x11111;
        rivatnt_pmc_recompute_intr(rivatnt);
        break;
    case 0x002210: rivatnt->pfifo.ramht = val; break;
    case 0x002214: rivatnt->pfifo.ramfc = val; break;
    case 0x002218: rivatnt->pfifo.ramro = val; break;
    case 0x002410: rivatnt->pfifo.runout_put = val & 0x1ff8; break;
    case 0x002420: rivatnt->pfifo.runout_get = val & 0x1ff8; break;
    case 0x002500: rivatnt->pfifo.caches = val & 0x11; break;
    case 0x002504: rivatnt->pfifo.mode = val & 0xffff; break;
    case 0x002508: rivatnt->pfifo.dma = val & 0xffff; break;
    case 0x00250c: rivatnt->pfifo.size = val & 0xffff; break;

    case 0x003000: rivatnt->pfifo.cache0_push0 = val & 1; break;
    case 0x003004: rivatnt->pfifo.cache0_push1 = val & 0x10f; break;
    case 0x003010: rivatnt->pfifo.cache0_put = val & 4; break;
    case 0x003050: rivatnt->pfifo.cache0_pull0 = val; break;
    case 0x003054: rivatnt->pfifo.cache0_pull1 = val; break;
    case 0x003058: rivatnt->pfifo.cache0_hash = val; break;
    case 0x003070: rivatnt->pfifo.cache0_get = val & 4; break;
    case 0x003080: rivatnt->pfifo.cache0_engine = val; break;
    case 0x003100: rivatnt->pfifo.cache0_method = val & 0xfffc; break;
    case 0x003104: rivatnt->pfifo.cache0_data = val; break;

    case 0x003200: rivatnt->pfifo.cache1_push0 = val & 1; break;
    case 0x003204: rivatnt->pfifo.cache1_push1 = val & 0x10f; break;
    case 0x003210: rivatnt->pfifo.cache1_put = val & RIVATNT_CACHE1_MASK; break;
    case 0x003218: rivatnt->pfifo.cache1_status1 = val & 1; break;
    case 0x003220: rivatnt->pfifo.dma_push = val & 0x1001; break;
    case 0x003224: rivatnt->pfifo.dma_fetch = val; break;
    case 0x003228: rivatnt->pfifo.dma_state = val; break;
    case 0x00322c: rivatnt->pfifo.dma_instance = val & 0xffff; break;
    case 0x003230: rivatnt->pfifo.dma_ctl = val; break;
    case 0x003234: rivatnt->pfifo.dma_limit = val; break;
    case 0x003238: rivatnt->pfifo.dma_tlb_tag = val; break;
    case 0x00323c: rivatnt->pfifo.dma_tlb_pte = val; break;
    case 0x003240: rivatnt->pfifo.dma_put = val & 0x1ffffffc; break;
    case 0x003244: rivatnt->pfifo.dma_get = val & 0x1ffffffc; break;
    case 0x003250: rivatnt->pfifo.cache1_pull0 = val & 1; break;
    case 0x003254: rivatnt->pfifo.cache1_pull1 = val & 3; break;
    case 0x003258: rivatnt->pfifo.cache1_hash = val; break;
    case 0x003270: rivatnt->pfifo.cache1_get = val & RIVATNT_CACHE1_MASK; break;
    case 0x003280: rivatnt->pfifo.cache1_engine = val & 0x33333333; break;
    case 0x0032a0: rivatnt->pfifo.dma_dcount = val; break;
    case 0x0032a4: rivatnt->pfifo.dma_get_jmp_shadow = val; break;
    case 0x0032a8: rivatnt->pfifo.dma_rsvd_shadow = val; break;
    case 0x0032ac: rivatnt->pfifo.dma_data_shadow = val; break;
    default:
        return;
    }

    /* Most of these can unblock the pusher or the puller. */
    rivatnt_do_gpu_work(rivatnt);
}

/* Look a handle up in RAMHT the way the NV4 puller does: fold the handle into
   the table index, mix in the channel, then search linearly without wrapping
   (the RM inserts entries the same way). Returns the context word, or 0. */
static uint32_t
rivatnt_ramht_lookup(rivatnt_t *rivatnt, uint32_t handle, int chid)
{
    int      bits    = 9 + ((rivatnt->pfifo.ramht >> 16) & 3);
    uint32_t entries = 1 << bits;
    uint32_t search  = 16 << ((rivatnt->pfifo.ramht >> 24) & 3);
    uint32_t base    = ((rivatnt->pfifo.ramht >> 4) & 0x1f) << 12;
    uint32_t hash    = 0;

    for (uint32_t h = handle; h; h >>= bits)
        hash ^= h & (entries - 1);
    hash ^= (chid << (bits - 4)) & (entries - 1);

    for (uint32_t i = hash; (i < entries) && (i < hash + search); i++) {
        uint32_t ctx = rivatnt_ramin_read_l(base + i * 8 + 4, rivatnt);

        if ((rivatnt_ramin_read_l(base + i * 8, rivatnt) == handle) && (ctx & 0x80000000) &&
            (((ctx >> 24) & 0x1f) == (uint32_t) chid))
            return ctx;
    }
    return 0;
}

/* The puller stops on a cache error and leaves the reason in CACHE1_PULL0.
   The RM handles the method at GET in software, advances GET and restarts it. */
static void
rivatnt_pfifo_cache_error(rivatnt_t *rivatnt, uint32_t reason)
{
    rivatnt->pfifo.cache1_pull0 = (rivatnt->pfifo.cache1_pull0 & ~PULL0_ACCESS) | reason;
    rivatnt->pfifo.debug_0 |= 0x10; /* CACHE_ERROR1 */
    rivatnt->pfifo.intr |= PFIFO_INTR_CACHE_ERROR;
    rivatnt_pmc_recompute_intr(rivatnt);
}

static int rivatnt_pgraph_submit(rivatnt_t *rivatnt, int chid, int subc, uint32_t mthd, uint32_t data);

/* Take one method out of CACHE1 and deliver it. Returns 1 if it was consumed. */
static int
rivatnt_pfifo_pull(rivatnt_t *rivatnt)
{
    uint32_t get = rivatnt->pfifo.cache1_get;
    int      chid = rivatnt_cache1_chid(rivatnt);
    uint32_t mthd, data, ctx;
    int      subc, engine;

    if (!(rivatnt->pfifo.cache1_pull0 & PULL0_ACCESS) || rivatnt_cache1_empty(rivatnt))
        return 0;

    mthd = rivatnt->pfifo.cache1_method[get >> 2] & 0x1ffc;
    subc = (rivatnt->pfifo.cache1_method[get >> 2] >> 13) & 7;
    data = rivatnt->pfifo.cache1_data[get >> 2];

    if (mthd == 0) {
        /* Bind an object: remember its engine for this subchannel and hand
           the object's instance to that engine. */
        if (!(ctx = rivatnt_ramht_lookup(rivatnt, data, chid))) {
            rivatnt_pfifo_cache_error(rivatnt, PULL0_HASH_FAILED);
            return 0;
        }
        engine = (ctx >> 16) & 3;
        rivatnt->pfifo.cache1_engine = (rivatnt->pfifo.cache1_engine & ~(3 << (subc * 4))) | (engine << (subc * 4));
        rivatnt->pfifo.cache1_pull1  = engine;
        data                         = ctx & 0xffff;
    } else {
        engine                      = (rivatnt->pfifo.cache1_engine >> (subc * 4)) & 3;
        rivatnt->pfifo.cache1_pull1 = engine;
        /* Methods below 0x100 are puller methods; NV4 implements none of them
           besides OBJECT, so the RM emulates them. */
        if ((mthd < 0x100) || (engine != ENGINE_GRAPHICS)) {
            rivatnt_pfifo_cache_error(rivatnt, PULL0_DEVICE_SW);
            return 0;
        }
        /* 0x180-0x1fc take object handles, which the puller translates. */
        if ((mthd >= 0x180) && (mthd < 0x200)) {
            if (!(ctx = rivatnt_ramht_lookup(rivatnt, data, chid))) {
                rivatnt_pfifo_cache_error(rivatnt, PULL0_HASH_FAILED);
                return 0;
            }
            data = ctx & 0xffff;
        }
    }

    if (engine != ENGINE_GRAPHICS) {
        rivatnt_pfifo_cache_error(rivatnt, PULL0_DEVICE_SW);
        return 0;
    }

    if (!rivatnt_pgraph_submit(rivatnt, chid, subc, mthd, data))
        return 0;

    rivatnt->pfifo.cache1_get = (get + 4) & RIVATNT_CACHE1_MASK;
    return 1;
}

static void
rivatnt_pfifo_dma_error(rivatnt_t *rivatnt, uint32_t error)
{
    pclog("[RIVA TNT] DMA pusher error %u, state %08x get %08x put %08x inst %04x\n", error,
          rivatnt->pfifo.dma_state, rivatnt->pfifo.dma_get, rivatnt->pfifo.dma_put, rivatnt->pfifo.dma_instance);
    rivatnt->pfifo.dma_state = (rivatnt->pfifo.dma_state & 0x3fffffff) | (error << 30);
    rivatnt->pfifo.intr |= PFIFO_INTR_DMA_PUSHER;
    rivatnt_pmc_recompute_intr(rivatnt);
}

/* Read a word of the push buffer through its DMA object. NV4 DMA objects:
   word 0 = class, PAGE_TABLE (12), PAGE_ENTRY linear (13), TARGET (17:16),
   ADJUST (31:20); word 1 = limit; then one PTE per 4K page. */
static int
rivatnt_pfifo_dma_read(rivatnt_t *rivatnt, uint32_t offset, uint32_t *val)
{
    uint32_t inst  = (rivatnt->pfifo.dma_instance & 0xffff) << 4;
    uint32_t obj   = rivatnt_ramin_read_l(inst, rivatnt);
    uint32_t addr  = offset + (obj >> 20);
    uint32_t pte, phys;

    rivatnt->pfifo.dma_ctl   = ((obj >> 20) & 0xffc) | (obj & 0x33000) | 0x80000000;
    rivatnt->pfifo.dma_limit = rivatnt_ramin_read_l(inst + 4, rivatnt);

    if (offset > rivatnt->pfifo.dma_limit) {
        rivatnt_pfifo_dma_error(rivatnt, 3);
        return 0;
    }

    if (obj & (1 << 13))
        pte = rivatnt_ramin_read_l(inst + 8, rivatnt) + (addr & ~0xfff);
    else
        pte = rivatnt_ramin_read_l(inst + 8 + ((addr >> 12) << 2), rivatnt);
    rivatnt->pfifo.dma_tlb_tag = (addr & ~0xfff) | 1;
    rivatnt->pfifo.dma_tlb_pte = pte;

    if (!(pte & 1)) {
        rivatnt_pfifo_dma_error(rivatnt, 3);
        return 0;
    }
    phys = (pte & ~0xfff) | (addr & 0xfff);

    if (((obj >> 16) & 3) < 2) {
        uint8_t *vram = rivatnt->svga.vram;
        *val = vram[phys & rivatnt->vram_mask] | (vram[(phys + 1) & rivatnt->vram_mask] << 8) |
               (vram[(phys + 2) & rivatnt->vram_mask] << 16) | ((uint32_t) vram[(phys + 3) & rivatnt->vram_mask] << 24);
    } else {
        uint8_t bytes[4];
        dma_bm_read(phys, bytes, 4, 4);
        *val = bytes[0] | (bytes[1] << 8) | (bytes[2] << 16) | ((uint32_t) bytes[3] << 24);
    }
    return 1;
}

/* Turn push buffer words into CACHE1 entries. NV4 commands:
     000CCCCCCCCCCC00SSSMMMMMMMMMMM00  increasing methods
     010CCCCCCCCCCC00SSSMMMMMMMMMMM00  non-increasing methods
     001JJJJJJJJJJJJJJJJJJJJJJJJJJJ00  jump
   Returns 1 if any word was consumed. */
static int
rivatnt_pfifo_dma_pusher(rivatnt_t *rivatnt)
{
    int progress = 0;
    int budget   = 1024;

    if (!rivatnt_cache1_is_dma(rivatnt) || !(rivatnt->pfifo.cache1_push0 & 1) || !(rivatnt->pfifo.dma_push & 1) ||
        (rivatnt->pfifo.dma_state >> 30) || (rivatnt->pfifo.intr & PFIFO_INTR_DMA_PUSHER))
        return 0;

    while ((rivatnt->pfifo.dma_get != rivatnt->pfifo.dma_put) && budget--) {
        uint32_t state = rivatnt->pfifo.dma_state;
        uint32_t count = (state >> 18) & 0x7ff;
        uint32_t word;

        if (count && rivatnt_cache1_full(rivatnt))
            break;
        if (!rivatnt_pfifo_dma_read(rivatnt, rivatnt->pfifo.dma_get, &word))
            break;

        progress = 1;
        if (count) {
            uint32_t mthd = state & 0x1ffc;
            uint32_t put  = rivatnt->pfifo.cache1_put;

            rivatnt->pfifo.cache1_method[put >> 2] = mthd | (state & 0xe000);
            rivatnt->pfifo.cache1_data[put >> 2]   = word;
            rivatnt->pfifo.cache1_put              = (put + 4) & RIVATNT_CACHE1_MASK;
            rivatnt->pfifo.dma_data_shadow         = word;
            rivatnt->pfifo.dma_dcount++;

            if (!(state & 1))
                mthd = (mthd + 4) & 0x1ffc;
            rivatnt->pfifo.dma_state = (state & ~(0x1ffc | (0x7ff << 18))) | mthd | ((count - 1) << 18);
            rivatnt->pfifo.dma_get += 4;
        } else if ((word & 0xe0000003) == 0x20000000) {
            rivatnt->pfifo.dma_get_jmp_shadow = rivatnt->pfifo.dma_get;
            rivatnt->pfifo.dma_get            = word & 0x1ffffffc;
        } else if (((word & 0xe0030003) == 0x00000000) || ((word & 0xe0030003) == 0x40000000)) {
            rivatnt->pfifo.dma_rsvd_shadow = word;
            rivatnt->pfifo.dma_dcount      = 0;
            rivatnt->pfifo.dma_state       = (word & 0x1ffcfffc) | (word >> 30);
            rivatnt->pfifo.dma_get += 4;
        } else {
            /* GET stays on the bad word; the RM skips it. */
            rivatnt_pfifo_dma_error(rivatnt, 2);
            break;
        }
    }
    return progress;
}

static void
rivatnt_pfifo_save_channel(rivatnt_t *rivatnt)
{
    int      chid = rivatnt_cache1_chid(rivatnt);
    uint32_t fc   = rivatnt_ramfc_addr(rivatnt, chid);

    rivatnt_ramin_write_l(fc + 0x00, rivatnt->pfifo.dma_put, rivatnt);
    rivatnt_ramin_write_l(fc + 0x04, rivatnt->pfifo.dma_get, rivatnt);
    rivatnt_ramin_write_l(fc + 0x08, rivatnt->pfifo.dma_instance, rivatnt);
    rivatnt_ramin_write_l(fc + 0x0c, rivatnt->pfifo.dma_state, rivatnt);
    rivatnt_ramin_write_l(fc + 0x10, rivatnt->pfifo.dma_fetch, rivatnt);
    rivatnt_ramin_write_l(fc + 0x14, rivatnt->pfifo.cache1_engine, rivatnt);
    rivatnt_ramin_write_l(fc + 0x18, rivatnt->pfifo.cache1_pull1, rivatnt);

    rivatnt->pfifo.dma &= ~(1 << chid);
    if (rivatnt_cache1_is_dma(rivatnt) && (rivatnt->pfifo.dma_put != rivatnt->pfifo.dma_get))
        rivatnt->pfifo.dma |= 1 << chid;
}

static void
rivatnt_pfifo_load_channel(rivatnt_t *rivatnt, int chid)
{
    uint32_t fc = rivatnt_ramfc_addr(rivatnt, chid);
    int      dma = !!(rivatnt->pfifo.mode & (1 << chid));

    rivatnt->pfifo.cache1_push1  = chid | (dma ? 0x100 : 0);
    rivatnt->pfifo.dma_put       = rivatnt_ramin_read_l(fc + 0x00, rivatnt);
    rivatnt->pfifo.dma_get       = rivatnt_ramin_read_l(fc + 0x04, rivatnt);
    rivatnt->pfifo.dma_instance  = rivatnt_ramin_read_l(fc + 0x08, rivatnt) & 0xffff;
    rivatnt->pfifo.dma_state     = rivatnt_ramin_read_l(fc + 0x0c, rivatnt);
    rivatnt->pfifo.dma_fetch     = rivatnt_ramin_read_l(fc + 0x10, rivatnt);
    rivatnt->pfifo.cache1_engine = rivatnt_ramin_read_l(fc + 0x14, rivatnt) & 0x33333333;
    rivatnt->pfifo.cache1_pull1  = rivatnt_ramin_read_l(fc + 0x18, rivatnt) & 3;
    rivatnt->pfifo.dma_tlb_tag   = 0;
    rivatnt->pfifo.dma &= ~(1 << chid);
    if (dma)
        rivatnt->pfifo.dma_push |= 1;
}

/* CACHE1 can only move to another channel while reassignment is enabled and
   the current channel has nothing in flight. */
static int
rivatnt_pfifo_can_switch(rivatnt_t *rivatnt)
{
    return (rivatnt->pfifo.caches & 1) && rivatnt_cache1_empty(rivatnt) && rivatnt_dma_pusher_idle(rivatnt);
}

static void
rivatnt_pfifo_switch_channel(rivatnt_t *rivatnt, int chid)
{
    rivatnt_pfifo_save_channel(rivatnt);
    rivatnt_pfifo_load_channel(rivatnt, chid);
}

/* Serve DMA channels whose PUT was written while they were not loaded. */
static int
rivatnt_pfifo_switch_pending(rivatnt_t *rivatnt)
{
    int cur = rivatnt_cache1_chid(rivatnt);

    if (!rivatnt->pfifo.dma || !rivatnt_pfifo_can_switch(rivatnt))
        return 0;
    for (int i = 1; i <= RIVATNT_NUM_CHANNELS; i++) {
        int chid = (cur + i) % RIVATNT_NUM_CHANNELS;

        if (rivatnt->pfifo.dma & (1 << chid)) {
            if (chid == cur) {
                rivatnt->pfifo.dma &= ~(1 << chid);
                return 0;
            }
            rivatnt_pfifo_switch_channel(rivatnt, chid);
            return 1;
        }
    }
    return 0;
}

static void
rivatnt_pfifo_runout(rivatnt_t *rivatnt, uint32_t addr, uint32_t val, int reason)
{
    uint32_t base = ((rivatnt->pfifo.ramro >> 1) & 0xff) << 9;
    uint32_t mask = (rivatnt->pfifo.ramro & 0x10000) ? 0x1ff8 : 0x1f8;
    uint32_t put  = rivatnt->pfifo.runout_put;

    rivatnt_ramin_write_l(base + put, (addr & 0xfffc) | (((addr >> 16) & 0x7f) << 16) | (0xf << 24) | (reason << 28),
                          rivatnt);
    rivatnt_ramin_write_l(base + put + 4, val, rivatnt);
    rivatnt->pfifo.runout_put = (put + 8) & mask;
    rivatnt->pfifo.intr |= PFIFO_INTR_RUNOUT;
    if (rivatnt->pfifo.runout_put == rivatnt->pfifo.runout_get)
        rivatnt->pfifo.intr |= PFIFO_INTR_RUNOUT_OVF;
    rivatnt_pmc_recompute_intr(rivatnt);
}

/* NV_USER: 0x10000 bytes per channel, 0x2000 per subchannel. */
static uint32_t
rivatnt_user_read(rivatnt_t *rivatnt, uint32_t addr)
{
    int      chid   = (addr >> 16) & 0xf;
    uint32_t offset = addr & 0x1ffc;

    if (rivatnt->pfifo.mode & (1 << chid)) {
        int      cur = (chid == rivatnt_cache1_chid(rivatnt));
        uint32_t fc  = rivatnt_ramfc_addr(rivatnt, chid);

        switch (offset) {
        case 0x40:
            return cur ? rivatnt->pfifo.dma_put : rivatnt_ramin_read_l(fc, rivatnt);
        case 0x44:
            return cur ? rivatnt->pfifo.dma_get : rivatnt_ramin_read_l(fc + 4, rivatnt);
        }
        return 0;
    }

    if (offset == 0x10) {
        /* Free space in CACHE1, in bytes. */
        if ((chid != rivatnt_cache1_chid(rivatnt)) && rivatnt_pfifo_can_switch(rivatnt))
            rivatnt_pfifo_switch_channel(rivatnt, chid);
        if (chid != rivatnt_cache1_chid(rivatnt))
            return 0;
        rivatnt_do_gpu_work(rivatnt);
        return (rivatnt->pfifo.cache1_get - rivatnt->pfifo.cache1_put - 4) & RIVATNT_CACHE1_MASK;
    }
    return 0;
}

static void
rivatnt_user_write(rivatnt_t *rivatnt, uint32_t addr, uint32_t val)
{
    int      chid   = (addr >> 16) & 0xf;
    int      subc   = (addr >> 13) & 7;
    uint32_t offset = addr & 0x1ffc;
    uint32_t put;

    if (rivatnt->pfifo.mode & (1 << chid)) {
        /* DMA channel: the only writable register is DMA_PUT. */
        if (offset == 0x40) {
            if (chid == rivatnt_cache1_chid(rivatnt))
                rivatnt->pfifo.dma_put = val & 0x1ffffffc;
            else {
                rivatnt_ramin_write_l(rivatnt_ramfc_addr(rivatnt, chid), val & 0x1ffffffc, rivatnt);
                rivatnt->pfifo.dma |= 1 << chid;
            }
            rivatnt_do_gpu_work(rivatnt);
        }
        return;
    }

    if ((offset != 0) && (offset < 0x100)) {
        rivatnt_pfifo_runout(rivatnt, addr, val, 5); /* RESERVED_ACCESS */
        return;
    }
    if (chid != rivatnt_cache1_chid(rivatnt)) {
        if (!rivatnt_pfifo_can_switch(rivatnt)) {
            rivatnt_pfifo_runout(rivatnt, addr, val, 1); /* NO_CACHE_AVAILABLE */
            return;
        }
        rivatnt_pfifo_switch_channel(rivatnt, chid);
    }
    if (!(rivatnt->pfifo.cache1_push0 & 1)) {
        rivatnt_pfifo_runout(rivatnt, addr, val, 1);
        return;
    }
    if (rivatnt->pfifo.runout_put != rivatnt->pfifo.runout_get) {
        rivatnt_pfifo_runout(rivatnt, addr, val, 2); /* CACHE_RAN_OUT */
        return;
    }
    if (rivatnt_cache1_full(rivatnt)) {
        rivatnt_pfifo_runout(rivatnt, addr, val, 3); /* FREE_COUNT_OVERRUN */
        return;
    }

    put = rivatnt->pfifo.cache1_put;
    rivatnt->pfifo.cache1_method[put >> 2] = offset | (subc << 13);
    rivatnt->pfifo.cache1_data[put >> 2]   = val;
    rivatnt->pfifo.cache1_put              = (put + 4) & RIVATNT_CACHE1_MASK;
    rivatnt_do_gpu_work(rivatnt);
}

/* PGRAPH */

static void
rivatnt_pgraph_intr(rivatnt_t *rivatnt, uint32_t intr, uint32_t nsource)
{
    PGR(NV_PGRAPH_INTR) |= intr;
    PGR(NV_PGRAPH_NSOURCE) |= nsource;
    rivatnt_pmc_recompute_intr(rivatnt);
}

/* Object state. The context methods update the subchannel's cached context,
   the active context, and the object's grobj in RAMIN, so the state survives
   the object being bound again. */

static uint32_t
rivatnt_grobj_addr(rivatnt_t *rivatnt)
{
    return (PGR(NV_PGRAPH_CTX_SWITCH4) & 0xffff) << 4;
}

static void
rivatnt_grobj_set_a(rivatnt_t *rivatnt, int subc, uint32_t mask, uint32_t val)
{
    uint32_t inst = rivatnt_grobj_addr(rivatnt);
    uint32_t a    = (PGR(NV_PGRAPH_CTX_SWITCH1) & ~mask) | (val & mask);
    uint32_t g    = rivatnt_ramin_read_l(inst, rivatnt);

    rivatnt_ramin_write_l(inst, (g & ~mask & 0xff) | (PGR(NV_PGRAPH_CTX_SWITCH1) & ~mask & ~0xff) | (val & mask), rivatnt);
    PGR(NV_PGRAPH_CTX_CACHE1 + subc * 4) = a & 0x0303f0ff;
    PGR(NV_PGRAPH_CTX_SWITCH1)           = a & 0x0303f0ff;
}

static void
rivatnt_grobj_set_word(rivatnt_t *rivatnt, int subc, int word, uint32_t mask, uint32_t val)
{
    static const uint32_t sw[3] = { 0, NV_PGRAPH_CTX_SWITCH2, NV_PGRAPH_CTX_SWITCH3 };
    static const uint32_t ca[3] = { 0, NV_PGRAPH_CTX_CACHE2, NV_PGRAPH_CTX_CACHE3 };
    uint32_t              inst  = rivatnt_grobj_addr(rivatnt) + word * 4;

    rivatnt_ramin_write_l(inst, (rivatnt_ramin_read_l(inst, rivatnt) & ~mask) | (val & mask), rivatnt);
    PGR(sw[word]) = (PGR(sw[word]) & ~mask) | (val & mask);
    PGR(ca[word] + subc * 4) = PGR(sw[word]);
}

static uint32_t
rivatnt_object_class(rivatnt_t *rivatnt, uint32_t inst)
{
    return rivatnt_ramin_read_l(inst << 4, rivatnt) & 0xfff;
}

/* SET_DMA_NOTIFY and the other DMA object methods. The NULL object (class
   0x30) unbinds. which: 0 = notify, 1 = DMA A, 2 = DMA B. */
static void
rivatnt_grobj_set_dma(rivatnt_t *rivatnt, int subc, int which, uint32_t inst, int clear_b)
{
    if (rivatnt_object_class(rivatnt, inst) == 0x30)
        inst = 0;
    switch (which) {
    case 0:
        rivatnt_grobj_set_word(rivatnt, subc, 1, 0xffff0000, inst << 16);
        break;
    case 1:
        rivatnt_grobj_set_word(rivatnt, subc, 2, clear_b ? 0xffffffff : 0xffff, inst);
        break;
    case 2:
        rivatnt_grobj_set_word(rivatnt, subc, 2, 0xffff0000, inst << 16);
        break;
    }
}

/* SET_CONTEXT_* for a patch object: records whether one is attached. */
static void
rivatnt_grobj_set_ctx(rivatnt_t *rivatnt, int subc, int bit, uint32_t inst)
{
    rivatnt_grobj_set_a(rivatnt, subc, 1u << bit, (rivatnt_object_class(rivatnt, inst) != 0x30) << bit);
}

/* Where a DMA object puts offset 0, and how many bytes it covers. */
static uint32_t
rivatnt_dma_base(rivatnt_t *rivatnt, uint32_t inst, uint32_t *limit, int *target)
{
    uint32_t obj = rivatnt_ramin_read_l(inst << 4, rivatnt);

    if (limit)
        *limit = rivatnt_ramin_read_l((inst << 4) + 4, rivatnt);
    if (target)
        *target = (obj >> 16) & 3;
    return (rivatnt_ramin_read_l((inst << 4) + 8, rivatnt) & ~0xfff) + (obj >> 20);
}

/* Translate an offset into a DMA object to a physical address. Returns 0 if
   it is out of range or not present. */
static int
rivatnt_dma_translate(rivatnt_t *rivatnt, uint32_t inst, uint32_t offset, uint32_t *phys, int *target)
{
    uint32_t obj   = rivatnt_ramin_read_l(inst << 4, rivatnt);
    uint32_t limit = rivatnt_ramin_read_l((inst << 4) + 4, rivatnt);
    uint32_t addr  = offset + (obj >> 20);
    uint32_t pte;

    if (!inst || (offset > limit))
        return 0;
    if (obj & (1 << 13))
        pte = rivatnt_ramin_read_l((inst << 4) + 8, rivatnt) + (addr & ~0xfff);
    else
        pte = rivatnt_ramin_read_l((inst << 4) + 8 + ((addr >> 12) << 2), rivatnt);
    if (!(pte & 1))
        return 0;
    *phys   = (pte & ~0xfff) | (addr & 0xfff);
    *target = (obj >> 16) & 3;
    return 1;
}

static uint8_t
rivatnt_dma_read8(rivatnt_t *rivatnt, uint32_t inst, uint32_t offset)
{
    uint32_t phys;
    int      target;
    uint8_t  val = 0;

    if (!rivatnt_dma_translate(rivatnt, inst, offset, &phys, &target))
        return 0;
    if (target < 2)
        return rivatnt->svga.vram[phys & rivatnt->vram_mask];
    dma_bm_read(phys, &val, 1, 1);
    return val;
}

static void
rivatnt_dma_write8(rivatnt_t *rivatnt, uint32_t inst, uint32_t offset, uint8_t val)
{
    uint32_t phys;
    int      target;

    if (!rivatnt_dma_translate(rivatnt, inst, offset, &phys, &target))
        return;
    if (target < 2) {
        rivatnt->svga.vram[phys & rivatnt->vram_mask]                     = val;
        rivatnt->svga.changedvram[(phys & rivatnt->vram_mask) >> 12] = changeframecount;
    } else
        dma_bm_write(phys, &val, 1, 1);
}

/* Colours. Object colours come in the object's format (CTX_SWITCH2 13:8),
   surfaces have a BPIXEL depth. Everything is converted to the destination's
   native pixel bits by zero-filling to A8R8G8B8 and truncating back, which
   is exact when the formats agree, so ROPs stay bitwise on the surface. */

static uint32_t
rivatnt_color_expand(int cfmt, uint32_t c)
{
    uint32_t a = 0xff, r, g, b;

    switch (cfmt) {
    case 0x01: case 0x02: case 0x03: /* Y8, X16A8Y8, X24Y8 */
        r = g = b = c & 0xff;
        if (cfmt == 0x02)
            a = (c >> 8) & 0xff;
        break;
    case 0x06: case 0x07: case 0x08: case 0x09: /* A1R5G5B5 and X variants */
        r = ((c >> 10) & 0x1f) << 3;
        g = ((c >> 5) & 0x1f) << 3;
        b = (c & 0x1f) << 3;
        if ((cfmt == 0x06) || (cfmt == 0x08))
            a = (c & 0x8000) ? 0xff : 0;
        break;
    case 0x0a: case 0x0b: case 0x0c: /* R5G6B5, A16R5G6B5, X16R5G6B5 */
        r = ((c >> 11) & 0x1f) << 3;
        g = ((c >> 5) & 0x3f) << 2;
        b = (c & 0x1f) << 3;
        if (cfmt == 0x0b)
            a = (c >> 24) & 0xff;
        break;
    case 0x0d: /* A8R8G8B8 */
        return c;
    case 0x0f: case 0x10: case 0x11: /* Y16 */
        r = g = b = (c >> 8) & 0xff;
        break;
    default: /* X8R8G8B8, Y32 */
        return c | 0xff000000;
    }
    return (a << 24) | (r << 16) | (g << 8) | b;
}

static int
rivatnt_surf_bpp(int dfmt)
{
    switch (dfmt) {
    case 0x01:
        return 1;
    case 0x02: case 0x03: case 0x04: case 0x05: case 0x06: case 0x0e: case 0x0f:
        return 2;
    default:
        return 4;
    }
}

static uint32_t
rivatnt_surf_pack(int dfmt, uint32_t argb, uint32_t raw)
{
    uint32_t r = (argb >> 16) & 0xff, g = (argb >> 8) & 0xff, b = argb & 0xff;

    switch (dfmt) {
    case 0x01: /* Y8: colours are palette indices */
        return raw & 0xff;
    case 0x02:
        return ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
    case 0x03:
        return 0x8000 | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
    case 0x04:
        return ((argb >> 31) << 15) | ((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3);
    case 0x05:
        return ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    case 0x06:
        return raw & 0xffff;
    case 0x07: case 0x09:
        return argb & 0xffffff;
    case 0x08:
        return 0x01000000 | (argb & 0xffffff);
    case 0x0a:
        return (argb & 0x80000000) | (argb & 0xffffff);
    case 0x0b:
        return 0xff000000 | (argb & 0xffffff);
    case 0x0c:
        return argb;
    default: /* Y32, YUV */
        return raw;
    }
}

static uint32_t
rivatnt_surf_expand(int dfmt, uint32_t p)
{
    switch (dfmt) {
    case 0x01:
        return 0xff000000 | (p & 0xff) * 0x010101;
    case 0x02: case 0x03: case 0x04:
        return rivatnt_color_expand((dfmt == 0x04) ? 0x06 : 0x07, p);
    case 0x05:
        return rivatnt_color_expand(0x0a, p);
    case 0x06:
        return rivatnt_color_expand(0x0f, p);
    case 0x0c:
        return p;
    default:
        return 0xff000000 | p;
    }
}

/* A colour in object format cfmt as a pixel on a dfmt surface. */
static uint32_t
rivatnt_color_to_surf(int cfmt, uint32_t c, int dfmt)
{
    return rivatnt_surf_pack(dfmt, rivatnt_color_expand(cfmt, c), c);
}

static uint32_t
rivatnt_rop3(uint8_t rop, uint32_t p, uint32_t s, uint32_t d)
{
    uint32_t r = 0;

    if (rop & 0x01) r |= ~p & ~s & ~d;
    if (rop & 0x02) r |= ~p & ~s & d;
    if (rop & 0x04) r |= ~p & s & ~d;
    if (rop & 0x08) r |= ~p & s & d;
    if (rop & 0x10) r |= p & ~s & ~d;
    if (rop & 0x20) r |= p & ~s & d;
    if (rop & 0x40) r |= p & s & ~d;
    if (rop & 0x80) r |= p & s & d;
    return r;
}

typedef struct {
    uint32_t base, pitch;
    int      fmt, bpp;
    int      op;
    uint8_t  rop;
    int      x0, y0, x1, y1; /* clip, exclusive at the far edge */
    int      chroma;
    uint32_t chroma_key;     /* native, only valid if chroma */
} rivatnt_draw_t;

/* Gather what drawing on the destination surface needs. Primitives that
   carry their own clip pass it in. */
static void
rivatnt_draw_setup(rivatnt_t *rivatnt, rivatnt_draw_t *d, int clip_x0, int clip_y0, int clip_x1, int clip_y1)
{
    uint32_t ctx1  = PGR(NV_PGRAPH_CTX_SWITCH1);
    uint32_t limit = PGR(NV_PGRAPH_BLIMIT(SURF_DST)) & 0xffffff;

    d->fmt   = PGR(NV_PGRAPH_BPIXEL) & 0xf;
    d->bpp   = rivatnt_surf_bpp(d->fmt);
    d->base  = PGR(NV_PGRAPH_BBASE(SURF_DST)) + PGR(NV_PGRAPH_BOFFSET(SURF_DST));
    d->pitch = PGR(NV_PGRAPH_BPITCH(SURF_DST)) & 0xffff;
    d->op    = (ctx1 >> 15) & 7;
    d->rop   = PGR(NV_PGRAPH_ROP3) & 0xff;
    d->x0    = clip_x0;
    d->y0    = clip_y0;
    d->x1    = clip_x1;
    d->y1    = clip_y1;

    /* Never write outside the surface's pitch. */
    if (d->pitch && (d->x1 > (int) (d->pitch / d->bpp)))
        d->x1 = d->pitch / d->bpp;
    if (d->x0 < 0)
        d->x0 = 0;
    if (d->y0 < 0)
        d->y0 = 0;
    if (limit && d->pitch) {
        uint32_t offset = PGR(NV_PGRAPH_BOFFSET(SURF_DST));
        int      rows   = (offset > limit) ? 0 : (int) ((limit + 1 - offset) / d->pitch);

        if (d->y1 > rows)
            d->y1 = rows;
    }

    /* Objects with a clip rectangle attached are clipped to it as well. */
    if (ctx1 & (1 << 13)) {
        int ux0 = (int16_t) PGR(NV_PGRAPH_ABS_UCLIP_XMIN), uy0 = (int16_t) PGR(NV_PGRAPH_ABS_UCLIP_YMIN);
        int ux1 = (int16_t) PGR(NV_PGRAPH_ABS_UCLIP_XMAX), uy1 = (int16_t) PGR(NV_PGRAPH_ABS_UCLIP_YMAX);

        if (ux0 > d->x0) d->x0 = ux0;
        if (uy0 > d->y0) d->y0 = uy0;
        if (ux1 < d->x1) d->x1 = ux1;
        if (uy1 < d->y1) d->y1 = uy1;
    }

    d->chroma = 0;
    if (ctx1 & (1 << 12)) {
        int      kfmt = (PGR(NV_PGRAPH_STORED_FMT) >> 24) & 0x3f;
        uint32_t key  = PGR(NV_PGRAPH_CHROMA);

        if (rivatnt_color_expand(kfmt, key) >> 24) {
            d->chroma     = 1;
            d->chroma_key = rivatnt_color_to_surf(kfmt, key, d->fmt);
        }
    }
}

static uint32_t
rivatnt_vram_read_px(rivatnt_t *rivatnt, uint32_t addr, int bpp)
{
    uint8_t *vram = rivatnt->svga.vram;
    uint32_t mask = rivatnt->vram_mask;

    switch (bpp) {
    case 1:
        return vram[addr & mask];
    case 2:
        return vram[addr & mask] | (vram[(addr + 1) & mask] << 8);
    default:
        return vram[addr & mask] | (vram[(addr + 1) & mask] << 8) | (vram[(addr + 2) & mask] << 16) |
               ((uint32_t) vram[(addr + 3) & mask] << 24);
    }
}

static void
rivatnt_vram_write_px(rivatnt_t *rivatnt, uint32_t addr, int bpp, uint32_t val)
{
    uint8_t *vram = rivatnt->svga.vram;
    uint32_t mask = rivatnt->vram_mask;

    for (int i = 0; i < bpp; i++)
        vram[(addr + i) & mask] = val >> (i * 8);
    rivatnt->svga.changedvram[(addr & mask) >> 12] = changeframecount;
}

/* The pattern pixel at (x, y), on the destination surface. */
static uint32_t
rivatnt_pattern_px(rivatnt_t *rivatnt, rivatnt_draw_t *d, int x, int y)
{
    uint32_t shape = PGR(NV_PGRAPH_PATTERN_SHAPE);
    int      idx;

    switch (shape & 3) {
    case 1:
        idx = x & 63;
        break;
    case 2:
        idx = y & 63;
        break;
    default:
        idx = ((y & 7) << 3) | (x & 7);
        break;
    }

    if (shape & 0x10) {
        uint32_t c = PGR(NV_PGRAPH_PATT_COLORRAM(idx));
        return rivatnt_surf_pack(d->fmt, 0xff000000 | c, c);
    }
    {
        int bit  = (PGR(NV_PGRAPH_PATTERN(idx >> 5)) >> (idx & 31)) & 1;
        int pfmt = (PGR(NV_PGRAPH_STORED_FMT) >> 8) & 0x3f;

        return rivatnt_color_to_surf(pfmt, PGR(NV_PGRAPH_PATT_COLOR(bit)), d->fmt);
    }
}

static uint32_t
rivatnt_blend(uint32_t s, uint32_t dst, int beta)
{
    uint32_t out = 0;

    for (int i = 0; i < 32; i += 8) {
        int sc = (s >> i) & 0xff, dc = (dst >> i) & 0xff;
        out |= (uint32_t) ((sc * beta + dc * (255 - beta)) / 255) << i;
    }
    return out;
}

/* Write one pixel. src is already a native destination pixel. */
static void
rivatnt_draw_px(rivatnt_t *rivatnt, rivatnt_draw_t *d, int x, int y, uint32_t src)
{
    uint32_t addr, dst, out;

    if ((x < d->x0) || (x >= d->x1) || (y < d->y0) || (y >= d->y1))
        return;
    if (d->chroma && (src == d->chroma_key))
        return;

    addr = d->base + y * d->pitch + x * d->bpp;
    switch (d->op) {
    case OP_ROP_AND:
        /* Most ROPs used by GDI don't read the destination or the pattern. */
        switch (d->rop) {
        case 0xcc:
            out = src;
            break;
        case 0xf0:
            out = rivatnt_pattern_px(rivatnt, d, x, y);
            break;
        default:
            dst = rivatnt_vram_read_px(rivatnt, addr, d->bpp);
            out = rivatnt_rop3(d->rop, rivatnt_pattern_px(rivatnt, d, x, y), src, dst);
            break;
        }
        break;
    case OP_BLEND_AND:
    case OP_BLEND_PRE: {
        int beta = (d->op == OP_BLEND_AND) ? (PGR(NV_PGRAPH_BETA_AND) >> 23) & 0xff
                                           : (PGR(NV_PGRAPH_BETA_PREMULT) >> 24) & 0xff;
        dst = rivatnt_surf_expand(d->fmt, rivatnt_vram_read_px(rivatnt, addr, d->bpp));
        out = rivatnt_surf_pack(d->fmt, rivatnt_blend(rivatnt_surf_expand(d->fmt, src), dst, beta), src);
        break;
    }
    default:
        out = src;
        break;
    }
    rivatnt_vram_write_px(rivatnt, addr, d->bpp, out);
}

static void
rivatnt_draw_rect(rivatnt_t *rivatnt, rivatnt_draw_t *d, int x, int y, int w, int h, uint32_t src)
{
    int x0 = (x > d->x0) ? x : d->x0, y0 = (y > d->y0) ? y : d->y0;
    int x1 = (x + w < d->x1) ? x + w : d->x1, y1 = (y + h < d->y1) ? y + h : d->y1;

    for (int py = y0; py < y1; py++)
        for (int px = x0; px < x1; px++)
            rivatnt_draw_px(rivatnt, d, px, py, src);
}

/* Lines leave out their last pixel, like GDI's. */
static void
rivatnt_draw_line(rivatnt_t *rivatnt, rivatnt_draw_t *d, int x0, int y0, int x1, int y1, uint32_t src)
{
    int dx = abs(x1 - x0), sx = (x0 < x1) ? 1 : -1;
    int dy = -abs(y1 - y0), sy = (y0 < y1) ? 1 : -1;
    int err = dx + dy;

    while ((x0 != x1) || (y0 != y1)) {
        int e2 = 2 * err;

        rivatnt_draw_px(rivatnt, d, x0, y0, src);
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

static int64_t
rivatnt_edge(int ax, int ay, int bx, int by, int px, int py)
{
    return (int64_t) (bx - ax) * (py - ay) - (int64_t) (by - ay) * (px - ax);
}

static void
rivatnt_draw_triangle(rivatnt_t *rivatnt, rivatnt_draw_t *d, uint32_t src)
{
    int32_t *vx = rivatnt->d2.vtx_x, *vy = rivatnt->d2.vtx_y;
    int      minx = vx[0], maxx = vx[0], miny = vy[0], maxy = vy[0];

    for (int i = 1; i < 3; i++) {
        if (vx[i] < minx) minx = vx[i];
        if (vx[i] > maxx) maxx = vx[i];
        if (vy[i] < miny) miny = vy[i];
        if (vy[i] > maxy) maxy = vy[i];
    }
    if (minx < d->x0) minx = d->x0;
    if (miny < d->y0) miny = d->y0;
    if (maxx >= d->x1) maxx = d->x1 - 1;
    if (maxy >= d->y1) maxy = d->y1 - 1;

    for (int y = miny; y <= maxy; y++) {
        for (int x = minx; x <= maxx; x++) {
            int64_t e0 = rivatnt_edge(vx[0], vy[0], vx[1], vy[1], x, y);
            int64_t e1 = rivatnt_edge(vx[1], vy[1], vx[2], vy[2], x, y);
            int64_t e2 = rivatnt_edge(vx[2], vy[2], vx[0], vy[0], x, y);

            if (((e0 >= 0) && (e1 >= 0) && (e2 >= 0)) || ((e0 <= 0) && (e1 <= 0) && (e2 <= 0)))
                rivatnt_draw_px(rivatnt, d, x, y, src);
        }
    }
}

/* Image streams (IFC, indexed IFC, GDI bitmaps). The image is size_in
   pixels, sent row by row with no padding, placed at (x, y) and clipped to
   size_out. */
static void
rivatnt_image_begin(rivatnt_t *rivatnt)
{
    rivatnt->d2.pos = 0;
}

static void
rivatnt_image_px(rivatnt_t *rivatnt, rivatnt_draw_t *d, uint32_t src, int draw)
{
    int32_t w = rivatnt->d2.w_in, h = rivatnt->d2.h_in;
    int32_t col, row;

    if (!w || ((int32_t) rivatnt->d2.pos >= w * h))
        return;
    col = rivatnt->d2.pos % w;
    row = rivatnt->d2.pos / w;
    rivatnt->d2.pos++;
    if (draw && (col < rivatnt->d2.w_out) && (row < rivatnt->d2.h_out))
        rivatnt_draw_px(rivatnt, d, rivatnt->d2.x + col, rivatnt->d2.y + row, src);
}

static void
rivatnt_setup_full(rivatnt_t *rivatnt, rivatnt_draw_t *d)
{
    rivatnt_draw_setup(rivatnt, d, 0, 0, 0x7fff, 0x7fff);
}

static void
rivatnt_setup_gdi_clip(rivatnt_t *rivatnt, rivatnt_draw_t *d)
{
    rivatnt_draw_setup(rivatnt, d, rivatnt->d2.clip_x0, rivatnt->d2.clip_y0, rivatnt->d2.clip_x1, rivatnt->d2.clip_y1);
}

static int
rivatnt_obj_cfmt(rivatnt_t *rivatnt)
{
    return (PGR(NV_PGRAPH_CTX_SWITCH2) >> 8) & 0x3f;
}

/* Mono bitmaps are LSB-first after this; CGA6 order is MSB-first per byte. */
static uint32_t
rivatnt_mono_expand(rivatnt_t *rivatnt, uint32_t mono)
{
    uint32_t res = 0;

    if ((PGR(NV_PGRAPH_CTX_SWITCH2) & 3) != 1)
        return mono;
    for (int i = 0; i < 32; i++)
        res |= ((mono >> i) & 1) << (i ^ 7);
    return res;
}

static void
rivatnt_pgraph_set_point(int32_t *x, int32_t *y, uint32_t data)
{
    *x = (int16_t) (data & 0xffff);
    *y = (int16_t) (data >> 16);
}

/* Methods shared by the patch objects: notify DMA, attached contexts,
   operation. Returns 1 if handled. */
typedef struct {
    uint16_t mthd;
    uint8_t  type;
    uint8_t  arg;
} rivatnt_ctx_mthd_t;

enum {
    CTXM_END = 0,
    CTXM_DMA,    /* arg: which (0 notify, 1 A, 2 B); 0x80 = clear B */
    CTXM_CTX,    /* arg: CTX_SWITCH1 bit */
    CTXM_SURF2D,
    CTXM_OPERATION
};

static const rivatnt_ctx_mthd_t ctx_line[] = { /* 0x5c, 0x5d, 0x5e */
    { 0x180, CTXM_DMA, 0 }, { 0x184, CTXM_CTX, 13 }, { 0x188, CTXM_CTX, 27 }, { 0x18c, CTXM_CTX, 28 },
    { 0x190, CTXM_CTX, 29 }, { 0x194, CTXM_CTX, 30 }, { 0x198, CTXM_SURF2D, 0 }, { 0x2fc, CTXM_OPERATION, 0 },
    { 0 }
};
static const rivatnt_ctx_mthd_t ctx_gdi[] = { /* 0x4a */
    { 0x180, CTXM_DMA, 0 }, { 0x184, CTXM_DMA, 1 }, { 0x188, CTXM_CTX, 27 }, { 0x18c, CTXM_CTX, 28 },
    { 0x190, CTXM_CTX, 29 }, { 0x194, CTXM_CTX, 30 }, { 0x198, CTXM_SURF2D, 0 }, { 0x2fc, CTXM_OPERATION, 0 },
    { 0 }
};
static const rivatnt_ctx_mthd_t ctx_blit[] = { /* 0x5f, 0x61 */
    { 0x180, CTXM_DMA, 0 }, { 0x184, CTXM_CTX, 12 }, { 0x188, CTXM_CTX, 13 }, { 0x18c, CTXM_CTX, 27 },
    { 0x190, CTXM_CTX, 28 }, { 0x194, CTXM_CTX, 29 }, { 0x198, CTXM_CTX, 30 }, { 0x19c, CTXM_SURF2D, 0 },
    { 0x2fc, CTXM_OPERATION, 0 }, { 0 }
};
static const rivatnt_ctx_mthd_t ctx_iifc[] = { /* 0x60 */
    { 0x180, CTXM_DMA, 0 }, { 0x184, CTXM_DMA, 1 }, { 0x188, CTXM_CTX, 12 }, { 0x18c, CTXM_CTX, 13 },
    { 0x190, CTXM_CTX, 27 }, { 0x194, CTXM_CTX, 28 }, { 0x198, CTXM_CTX, 29 }, { 0x19c, CTXM_CTX, 30 },
    { 0x1a0, CTXM_SURF2D, 0 }, { 0x3e4, CTXM_OPERATION, 0 }, { 0 }
};
static const rivatnt_ctx_mthd_t ctx_sifc[] = { /* 0x76 */
    { 0x180, CTXM_DMA, 0 }, { 0x184, CTXM_CTX, 12 }, { 0x188, CTXM_CTX, 27 }, { 0x18c, CTXM_CTX, 28 },
    { 0x190, CTXM_CTX, 29 }, { 0x194, CTXM_CTX, 30 }, { 0x198, CTXM_SURF2D, 0 }, { 0x2fc, CTXM_OPERATION, 0 },
    { 0 }
};
static const rivatnt_ctx_mthd_t ctx_sifm[] = { /* 0x77 */
    { 0x180, CTXM_DMA, 0 }, { 0x184, CTXM_DMA, 1 }, { 0x188, CTXM_CTX, 12 }, { 0x18c, CTXM_CTX, 27 },
    { 0x190, CTXM_CTX, 28 }, { 0x194, CTXM_CTX, 29 }, { 0x198, CTXM_CTX, 30 }, { 0x19c, CTXM_SURF2D, 0 },
    { 0x2fc, CTXM_OPERATION, 0 }, { 0 }
};
static const rivatnt_ctx_mthd_t ctx_m2mf[] = { /* 0x39 */
    { 0x180, CTXM_DMA, 0 }, { 0x184, CTXM_DMA, 0x81 }, { 0x188, CTXM_DMA, 2 }, { 0 }
};
static const rivatnt_ctx_mthd_t ctx_notify_only[] = {
    { 0x180, CTXM_DMA, 0 }, { 0 }
};

static int
rivatnt_pgraph_ctx_method(rivatnt_t *rivatnt, const rivatnt_ctx_mthd_t *t, int subc, uint32_t mthd, uint32_t data)
{
    for (; t->type != CTXM_END; t++) {
        if (t->mthd != mthd)
            continue;
        switch (t->type) {
        case CTXM_DMA:
            rivatnt_grobj_set_dma(rivatnt, subc, t->arg & 3, data, t->arg & 0x80);
            break;
        case CTXM_CTX:
            rivatnt_grobj_set_ctx(rivatnt, subc, t->arg, data);
            break;
        case CTXM_SURF2D:
            /* bit 25: a surface is attached; bit 14: it is swizzled */
            rivatnt_grobj_set_a(rivatnt, subc, (1 << 25) | (1 << 14),
                                ((rivatnt_object_class(rivatnt, data) != 0x30) << 25) |
                                ((rivatnt_object_class(rivatnt, data) == 0x52) << 14));
            break;
        case CTXM_OPERATION:
            rivatnt_grobj_set_a(rivatnt, subc, 7 << 15, (data & 7) << 15);
            break;
        }
        return 1;
    }
    return 0;
}

static void
rivatnt_set_color_format(rivatnt_t *rivatnt, int subc, int fmt)
{
    rivatnt_grobj_set_word(rivatnt, subc, 1, 0x3f00, fmt << 8);
}

/* SET_COLOR_FORMAT of the solid classes (rect, line, tri, GDI) */
static int
rivatnt_solid_format(uint32_t val)
{
    static const int fmts[4] = { 0, 0x0c, 0x09, 0x0e };
    return fmts[val & 3];
}

/* SET_COLOR_FORMAT of the image classes (IFC, SIFC, SIFM, IIFC) */
static int
rivatnt_image_format(uint32_t val)
{
    static const int fmts[8] = { 0, 0x0a, 0x06, 0x07, 0x0d, 0x0e, 0, 0 };
    return fmts[val & 7];
}

/* SET_COLOR_FORMAT of pattern and colour key: 1 A16R5G6B5, 2 X16A1R5G5B5, 3 A8R8G8B8 */
static int
rivatnt_ctx_format(uint32_t val)
{
    static const int fmts[4] = { 0, 0x0b, 0x08, 0x0d };
    return fmts[val & 3];
}

/* Feed one word of image data to the current image stream. */
static void
rivatnt_image_data(rivatnt_t *rivatnt, rivatnt_draw_t *d, uint32_t data)
{
    int cfmt = rivatnt_obj_cfmt(rivatnt);

    switch (cfmt) {
    case 0x06: case 0x07: case 0x0a:
        rivatnt_image_px(rivatnt, d, rivatnt_color_to_surf(cfmt, data & 0xffff, d->fmt), 1);
        rivatnt_image_px(rivatnt, d, rivatnt_color_to_surf(cfmt, data >> 16, d->fmt), 1);
        break;
    case 0x01:
        for (int i = 0; i < 32; i += 8)
            rivatnt_image_px(rivatnt, d, rivatnt_color_to_surf(cfmt, (data >> i) & 0xff, d->fmt), 1);
        break;
    default:
        rivatnt_image_px(rivatnt, d, rivatnt_color_to_surf(cfmt, data, d->fmt), 1);
        break;
    }
}

/* A mono bitmap word: 1 bits get color1, 0 bits color0 (or are skipped). */
static void
rivatnt_mono_data(rivatnt_t *rivatnt, rivatnt_draw_t *d, uint32_t data, int opaque)
{
    int      cfmt = rivatnt_obj_cfmt(rivatnt);
    uint32_t c0   = rivatnt_color_to_surf(cfmt, rivatnt->d2.color0, d->fmt);
    uint32_t c1   = rivatnt_color_to_surf(cfmt, rivatnt->d2.color1, d->fmt);
    uint32_t bits = rivatnt_mono_expand(rivatnt, data);

    for (int i = 0; i < 32; i++) {
        int on = (bits >> i) & 1;
        rivatnt_image_px(rivatnt, d, on ? c1 : c0, on || opaque);
    }
}

/* SCREEN_TO_SCREEN: copy from the source surface. Overlapping copies are
   done in whichever order reads every pixel before it is overwritten. */
static void
rivatnt_blit(rivatnt_t *rivatnt, int sx, int sy, int dx, int dy, int w, int h)
{
    rivatnt_draw_t d;
    int            sfmt   = (PGR(NV_PGRAPH_BPIXEL) >> 4) & 0xf;
    int            sbpp   = rivatnt_surf_bpp(sfmt);
    uint32_t       sbase  = PGR(NV_PGRAPH_BBASE(SURF_SRC)) + PGR(NV_PGRAPH_BOFFSET(SURF_SRC));
    uint32_t       spitch = PGR(NV_PGRAPH_BPITCH(SURF_SRC)) & 0xffff;
    int            ystep = 1, xstep = 1, y0 = 0, x0 = 0;
    uint32_t      *line;

    rivatnt_setup_full(rivatnt, &d);
    if ((w <= 0) || (h <= 0))
        return;
    if (dy > sy) {
        ystep = -1;
        y0    = h - 1;
    }
    if ((dy == sy) && (dx > sx)) {
        xstep = -1;
        x0    = w - 1;
    }

    line = malloc(w * sizeof(uint32_t));
    for (int j = 0, y = y0; j < h; j++, y += ystep) {
        /* Read the whole source row first so same-row overlaps work. */
        for (int i = 0; i < w; i++) {
            uint32_t p = rivatnt_vram_read_px(rivatnt, sbase + (sy + y) * spitch + (sx + i) * sbpp, sbpp);
            line[i]    = (sfmt == d.fmt) ? p : rivatnt_surf_pack(d.fmt, rivatnt_surf_expand(sfmt, p), p);
        }
        for (int i = 0, x = x0; i < w; i++, x += xstep)
            rivatnt_draw_px(rivatnt, &d, dx + x, dy + y, line[x]);
    }
    free(line);
}

static void
rivatnt_m2mf(rivatnt_t *rivatnt)
{
    uint32_t dma_in  = PGR(NV_PGRAPH_CTX_SWITCH3) & 0xffff;
    uint32_t dma_out = PGR(NV_PGRAPH_CTX_SWITCH3) >> 16;
    int      in_inc  = rivatnt->d2.m2mf_format & 7;
    int      out_inc = (rivatnt->d2.m2mf_format >> 8) & 7;
    uint32_t len     = rivatnt->d2.m2mf_line_length;
    int32_t  pin     = rivatnt->d2.m2mf_pitch_in;
    int32_t  pout    = rivatnt->d2.m2mf_pitch_out;

    if (!in_inc)
        in_inc = 1;
    if (!out_inc)
        out_inc = 1;
    for (uint32_t line = 0; line < rivatnt->d2.m2mf_line_count; line++) {
        uint32_t in  = rivatnt->d2.m2mf_offset_in + line * pin;
        uint32_t out = rivatnt->d2.m2mf_offset_out + line * pout;

        for (uint32_t i = 0, o = 0; i < len; i += in_inc, o += out_inc)
            rivatnt_dma_write8(rivatnt, dma_out, out + o, rivatnt_dma_read8(rivatnt, dma_in, in + i));
    }
}

static void
rivatnt_pgraph_method(rivatnt_t *rivatnt, int subc, uint32_t mthd, uint32_t data)
{
    uint32_t       grclass = PGR(NV_PGRAPH_CTX_SWITCH1) & 0xff;
    rivatnt_draw_t d;

    if (mthd == 0x100) /* NOP */
        return;

    switch (grclass) {
    case 0x12: /* NV1_BETA_SOLID */
        if (mthd == 0x300)
            PGR(NV_PGRAPH_BETA_AND) = (data & 0x80000000) ? 0 : (data & 0x7f800000);
        else
            rivatnt_pgraph_ctx_method(rivatnt, ctx_notify_only, subc, mthd, data);
        return;

    case 0x72: /* NV4_CONTEXT_BETA */
        if (mthd == 0x300)
            PGR(NV_PGRAPH_BETA_PREMULT) = data;
        else
            rivatnt_pgraph_ctx_method(rivatnt, ctx_notify_only, subc, mthd, data);
        return;

    case 0x19: /* NV1_CONTEXT_CLIP_RECTANGLE: clips are stored as min, max (exclusive) */
        switch (mthd) {
        case 0x300:
            PGR(NV_PGRAPH_ABS_UCLIP_XMIN) = (int16_t) data;
            PGR(NV_PGRAPH_ABS_UCLIP_YMIN) = (int16_t) (data >> 16);
            break;
        case 0x304:
            PGR(NV_PGRAPH_ABS_UCLIP_XMAX) = PGR(NV_PGRAPH_ABS_UCLIP_XMIN) + (data & 0xffff);
            PGR(NV_PGRAPH_ABS_UCLIP_YMAX) = PGR(NV_PGRAPH_ABS_UCLIP_YMIN) + (data >> 16);
            break;
        default:
            rivatnt_pgraph_ctx_method(rivatnt, ctx_notify_only, subc, mthd, data);
            break;
        }
        return;

    case 0x43: /* NV3_CONTEXT_ROP */
        if (mthd == 0x300)
            PGR(NV_PGRAPH_ROP3) = data & 0xff;
        else
            rivatnt_pgraph_ctx_method(rivatnt, ctx_notify_only, subc, mthd, data);
        return;

    case 0x57: /* NV4_CONTEXT_COLOR_KEY */
        switch (mthd) {
        case 0x300:
            rivatnt_set_color_format(rivatnt, subc, rivatnt_ctx_format(data));
            PGR(NV_PGRAPH_STORED_FMT) = (PGR(NV_PGRAPH_STORED_FMT) & 0x00ffffff) | (rivatnt_ctx_format(data) << 24);
            break;
        case 0x304:
            PGR(NV_PGRAPH_CHROMA) = data;
            break;
        default:
            rivatnt_pgraph_ctx_method(rivatnt, ctx_notify_only, subc, mthd, data);
            break;
        }
        return;

    case 0x44: /* NV4_CONTEXT_PATTERN */
        switch (mthd) {
        case 0x300:
            rivatnt_set_color_format(rivatnt, subc, rivatnt_ctx_format(data));
            PGR(NV_PGRAPH_STORED_FMT) = (PGR(NV_PGRAPH_STORED_FMT) & 0xff0000ff) | (rivatnt_ctx_format(data) * 0x0101 << 8);
            break;
        case 0x304:
            rivatnt_grobj_set_word(rivatnt, subc, 1, 3, data & 3);
            break;
        case 0x308:
            PGR(NV_PGRAPH_PATTERN_SHAPE) = (PGR(NV_PGRAPH_PATTERN_SHAPE) & ~3) | (data & 3);
            break;
        case 0x30c:
            PGR(NV_PGRAPH_PATTERN_SHAPE) = (PGR(NV_PGRAPH_PATTERN_SHAPE) & ~0x10) | ((data & 2) << 3);
            break;
        case 0x310:
        case 0x314:
            PGR(NV_PGRAPH_PATT_COLOR((mthd >> 2) & 1)) = data;
            break;
        case 0x318:
        case 0x31c:
            PGR(NV_PGRAPH_PATTERN((mthd >> 2) & 1)) = rivatnt_mono_expand(rivatnt, data);
            break;
        default:
            if ((mthd >= 0x400) && (mthd < 0x440)) { /* Y8 */
                for (int i = 0; i < 4; i++)
                    PGR(NV_PGRAPH_PATT_COLORRAM(((mthd - 0x400) >> 2) * 4 + i)) = ((data >> (i * 8)) & 0xff) * 0x010101;
            } else if (((mthd >= 0x500) && (mthd < 0x580)) || ((mthd >= 0x600) && (mthd < 0x680))) {
                int fmt = (mthd < 0x600) ? 0x0a : 0x07;
                for (int i = 0; i < 2; i++)
                    PGR(NV_PGRAPH_PATT_COLORRAM(((mthd & 0x7f) >> 2) * 2 + i)) =
                        rivatnt_color_expand(fmt, (data >> (i * 16)) & 0xffff) & 0xffffff;
            } else if ((mthd >= 0x700) && (mthd < 0x800))
                PGR(NV_PGRAPH_PATT_COLORRAM((mthd - 0x700) >> 2)) = data & 0xffffff;
            else
                rivatnt_pgraph_ctx_method(rivatnt, ctx_notify_only, subc, mthd, data);
            break;
        }
        return;

    case 0x42: /* NV4_CONTEXT_SURFACES_2D */
        switch (mthd) {
        case 0x184: /* source DMA */
        case 0x188: { /* destination DMA */
            int      surf = (mthd == 0x184) ? SURF_SRC : SURF_DST;
            uint32_t limit;
            PGR(NV_PGRAPH_BBASE(surf))  = rivatnt_dma_base(rivatnt, data, &limit, NULL) & 0xffffff;
            PGR(NV_PGRAPH_BLIMIT(surf)) = limit & 0xffffff;
            break;
        }
        case 0x300: {
            static const int fmts[16] = { 0, 1, 2, 3, 5, 6, 7, 0xb, 9, 0xa, 0xc, 0xd, 0, 0, 0, 0 };
            int              f       = fmts[data & 0xf];
            PGR(NV_PGRAPH_BPIXEL) = (PGR(NV_PGRAPH_BPIXEL) & ~0xff) | f | (f << 4);
            break;
        }
        case 0x304:
            PGR(NV_PGRAPH_BPITCH(SURF_SRC)) = data & 0xffff;
            PGR(NV_PGRAPH_BPITCH(SURF_DST)) = data >> 16;
            break;
        case 0x308:
            PGR(NV_PGRAPH_BOFFSET(SURF_SRC)) = data & 0xffffff;
            break;
        case 0x30c:
            PGR(NV_PGRAPH_BOFFSET(SURF_DST)) = data & 0xffffff;
            break;
        default:
            rivatnt_pgraph_ctx_method(rivatnt, ctx_notify_only, subc, mthd, data);
            break;
        }
        return;

    case 0x39: /* NV3_MEMORY_TO_MEMORY_FORMAT */
        switch (mthd) {
        case 0x30c: rivatnt->d2.m2mf_offset_in = data; break;
        case 0x310: rivatnt->d2.m2mf_offset_out = data; break;
        case 0x314: rivatnt->d2.m2mf_pitch_in = (int32_t) data; break;
        case 0x318: rivatnt->d2.m2mf_pitch_out = (int32_t) data; break;
        case 0x31c: rivatnt->d2.m2mf_line_length = data; break;
        case 0x320: rivatnt->d2.m2mf_line_count = data; break;
        case 0x324: rivatnt->d2.m2mf_format = data; break;
        case 0x328:
            rivatnt_m2mf(rivatnt);
            /* BUFFER_NOTIFY: the RM writes the notifier when told about it. */
            PGR(NV_PGRAPH_NOTIFY) = (PGR(NV_PGRAPH_NOTIFY) & ~PGRAPH_NOTIFY_BUFFER_STYLE) | PGRAPH_NOTIFY_BUFFER_REQ |
                                    ((data & 1) ? PGRAPH_NOTIFY_BUFFER_STYLE : 0);
            rivatnt_pgraph_intr(rivatnt, PGRAPH_INTR_BUFFER_NOTIFY, 0);
            break;
        default:
            rivatnt_pgraph_ctx_method(rivatnt, ctx_m2mf, subc, mthd, data);
            break;
        }
        return;

    case 0x5c: /* NV4_RENDER_SOLID_LIN */
    case 0x5d: /* NV4_RENDER_SOLID_TRIANGLE */
    case 0x5e: /* NV4_RENDER_SOLID_RECTANGLE */
        if (rivatnt_pgraph_ctx_method(rivatnt, ctx_line, subc, mthd, data))
            return;
        if (mthd == 0x300) {
            rivatnt_set_color_format(rivatnt, subc, rivatnt_solid_format(data));
            return;
        }
        if (mthd == 0x304) {
            rivatnt->d2.color = data;
            return;
        }
        rivatnt_setup_full(rivatnt, &d);
        if (grclass == 0x5e) {
            if ((mthd >= 0x400) && (mthd < 0x480)) {
                if (!(mthd & 4))
                    rivatnt_pgraph_set_point(&rivatnt->d2.vtx_x[0], &rivatnt->d2.vtx_y[0], data);
                else
                    rivatnt_draw_rect(rivatnt, &d, rivatnt->d2.vtx_x[0], rivatnt->d2.vtx_y[0], data & 0xffff, data >> 16,
                                      rivatnt_color_to_surf(rivatnt_obj_cfmt(rivatnt), rivatnt->d2.color, d.fmt));
            }
        } else if (grclass == 0x5c) {
            uint32_t c = rivatnt_color_to_surf(rivatnt_obj_cfmt(rivatnt), rivatnt->d2.color, d.fmt);
            int32_t  x, y;

            if ((mthd >= 0x400) && (mthd < 0x480)) { /* LIN: start, end */
                rivatnt_pgraph_set_point(&x, &y, data);
                if (mthd & 4)
                    rivatnt_draw_line(rivatnt, &d, rivatnt->d2.vtx_x[0], rivatnt->d2.vtx_y[0], x, y, c);
                rivatnt->d2.vtx_x[0] = x;
                rivatnt->d2.vtx_y[0] = y;
            } else if ((mthd >= 0x480) && (mthd < 0x500)) { /* LIN32 */
                int i = (mthd >> 2) & 3;
                if (i & 1)
                    rivatnt->d2.vtx_y[i >> 1] = (int32_t) data;
                else
                    rivatnt->d2.vtx_x[i >> 1] = (int32_t) data;
                if (i == 3)
                    rivatnt_draw_line(rivatnt, &d, rivatnt->d2.vtx_x[0], rivatnt->d2.vtx_y[0], rivatnt->d2.vtx_x[1],
                                      rivatnt->d2.vtx_y[1], c);
            } else if ((mthd >= 0x500) && (mthd < 0x580)) { /* POLYLIN */
                rivatnt_pgraph_set_point(&x, &y, data);
                if (rivatnt->d2.vtx_n)
                    rivatnt_draw_line(rivatnt, &d, rivatnt->d2.vtx_x[0], rivatnt->d2.vtx_y[0], x, y, c);
                rivatnt->d2.vtx_x[0] = x;
                rivatnt->d2.vtx_y[0] = y;
                rivatnt->d2.vtx_n    = 1;
            } else if ((mthd >= 0x600) && (mthd < 0x680)) { /* CPOLYLIN: colour, point */
                if (!(mthd & 4))
                    rivatnt->d2.color = data;
                else {
                    rivatnt_pgraph_set_point(&x, &y, data);
                    c = rivatnt_color_to_surf(rivatnt_obj_cfmt(rivatnt), rivatnt->d2.color, d.fmt);
                    if (rivatnt->d2.vtx_n)
                        rivatnt_draw_line(rivatnt, &d, rivatnt->d2.vtx_x[0], rivatnt->d2.vtx_y[0], x, y, c);
                    rivatnt->d2.vtx_x[0] = x;
                    rivatnt->d2.vtx_y[0] = y;
                    rivatnt->d2.vtx_n    = 1;
                }
            }
        } else { /* triangle */
            uint32_t c = rivatnt_color_to_surf(rivatnt_obj_cfmt(rivatnt), rivatnt->d2.color, d.fmt);

            if ((mthd >= 0x310) && (mthd < 0x31c)) {
                int i = (mthd - 0x310) >> 2;
                rivatnt_pgraph_set_point(&rivatnt->d2.vtx_x[i], &rivatnt->d2.vtx_y[i], data);
                if (i == 2)
                    rivatnt_draw_triangle(rivatnt, &d, c);
            } else if ((mthd >= 0x320) && (mthd < 0x338)) {
                int i = (mthd - 0x320) >> 3;
                if (mthd & 4)
                    rivatnt->d2.vtx_y[i] = (int32_t) data;
                else
                    rivatnt->d2.vtx_x[i] = (int32_t) data;
                if (mthd == 0x334)
                    rivatnt_draw_triangle(rivatnt, &d, c);
            }
        }
        /* Polylines start again on any other method. */
        if (!(((mthd >= 0x500) && (mthd < 0x580)) || ((mthd >= 0x600) && (mthd < 0x680))))
            rivatnt->d2.vtx_n = 0;
        return;

    case 0x5f: /* NV4_IMAGE_BLIT */
        if (rivatnt_pgraph_ctx_method(rivatnt, ctx_blit, subc, mthd, data))
            return;
        switch (mthd) {
        case 0x300:
            rivatnt_pgraph_set_point(&rivatnt->d2.vtx_x[0], &rivatnt->d2.vtx_y[0], data);
            break;
        case 0x304:
            rivatnt_pgraph_set_point(&rivatnt->d2.vtx_x[1], &rivatnt->d2.vtx_y[1], data);
            break;
        case 0x308:
            rivatnt_blit(rivatnt, rivatnt->d2.vtx_x[0], rivatnt->d2.vtx_y[0], rivatnt->d2.vtx_x[1], rivatnt->d2.vtx_y[1],
                         data & 0xffff, data >> 16);
            break;
        }
        return;

    case 0x61: /* NV4_IMAGE_FROM_CPU */
        if (rivatnt_pgraph_ctx_method(rivatnt, ctx_blit, subc, mthd, data))
            return;
        switch (mthd) {
        case 0x300:
            rivatnt_set_color_format(rivatnt, subc, rivatnt_image_format(data));
            break;
        case 0x304:
            rivatnt_pgraph_set_point(&rivatnt->d2.x, &rivatnt->d2.y, data);
            break;
        case 0x308:
            rivatnt->d2.w_out = data & 0xffff;
            rivatnt->d2.h_out = data >> 16;
            break;
        case 0x30c:
            rivatnt->d2.w_in = data & 0xffff;
            rivatnt->d2.h_in = data >> 16;
            rivatnt_image_begin(rivatnt);
            break;
        default:
            if ((mthd >= 0x400) && (mthd < 0x2000)) {
                rivatnt_setup_full(rivatnt, &d);
                rivatnt_image_data(rivatnt, &d, data);
            }
            break;
        }
        return;

    case 0x60: /* NV4_INDEXED_IMAGE_FROM_CPU */
        if (rivatnt_pgraph_ctx_method(rivatnt, ctx_iifc, subc, mthd, data))
            return;
        switch (mthd) {
        case 0x3e0: /* SET_COLOR_CONVERSION: dithering, done by the RM on NV4 */
            break;
        case 0x3e8:
            rivatnt_set_color_format(rivatnt, subc, rivatnt_image_format(data));
            break;
        case 0x3ec:
            rivatnt->d2.index_format = data;
            break;
        case 0x3f0:
            rivatnt->d2.palette_offset = data;
            break;
        case 0x3f4:
            rivatnt_pgraph_set_point(&rivatnt->d2.x, &rivatnt->d2.y, data);
            break;
        case 0x3f8:
            rivatnt->d2.w_out = data & 0xffff;
            rivatnt->d2.h_out = data >> 16;
            break;
        case 0x3fc:
            rivatnt->d2.w_in = data & 0xffff;
            rivatnt->d2.h_in = data >> 16;
            rivatnt_image_begin(rivatnt);
            break;
        default:
            if ((mthd >= 0x400) && (mthd < 0x2000)) {
                /* Indices (8 or 4 bits) into a palette of colours in the
                   object's format, read through the palette DMA object. */
                uint32_t pal  = PGR(NV_PGRAPH_CTX_SWITCH3) & 0xffff;
                int      cfmt = rivatnt_obj_cfmt(rivatnt);
                int      bits = (rivatnt->d2.index_format == 0) ? 8 : 4;
                int      csz  = ((cfmt == 0x06) || (cfmt == 0x07) || (cfmt == 0x0a)) ? 2 : 4;

                rivatnt_setup_full(rivatnt, &d);
                for (int i = 0; i < 32; i += bits) {
                    uint32_t idx = (data >> i) & ((1 << bits) - 1);
                    uint32_t off = rivatnt->d2.palette_offset + idx * csz;
                    uint32_t c   = 0;

                    for (int b = 0; b < csz; b++)
                        c |= rivatnt_dma_read8(rivatnt, pal, off + b) << (b * 8);
                    rivatnt_image_px(rivatnt, &d, rivatnt_color_to_surf(cfmt, c, d.fmt), 1);
                }
            }
            break;
        }
        return;

    case 0x4a: /* NV4_GDI_RECTANGLE_TEXT */
        if (rivatnt_pgraph_ctx_method(rivatnt, ctx_gdi, subc, mthd, data))
            return;
        if (mthd == 0x300) {
            rivatnt_set_color_format(rivatnt, subc, rivatnt_solid_format(data));
            return;
        }
        if (mthd == 0x304) { /* MONOCHROME_FORMAT */
            rivatnt_grobj_set_word(rivatnt, subc, 1, 3, data & 3);
            return;
        }
        /* A: unclipped rectangles */
        if (mthd == 0x3fc) {
            rivatnt->d2.color = data;
            return;
        }
        if ((mthd >= 0x400) && (mthd < 0x500)) {
            /* Unlike every other point and size, these are x_y and
               width_height: x and width in the high half (nv432.h's
               structure, which nvdisp.drv follows; its #defines disagree). */
            if (!(mthd & 4))
                rivatnt_pgraph_set_point(&rivatnt->d2.vtx_y[0], &rivatnt->d2.vtx_x[0], data);
            else {
                rivatnt_setup_full(rivatnt, &d);
                rivatnt_draw_rect(rivatnt, &d, rivatnt->d2.vtx_x[0], rivatnt->d2.vtx_y[0], data >> 16, data & 0xffff,
                                  rivatnt_color_to_surf(rivatnt_obj_cfmt(rivatnt), rivatnt->d2.color, d.fmt));
            }
            return;
        }
        /* B, C, E, F, G: clip point 0 (top left) and 1 (bottom right, exclusive) */
        if ((mthd == 0x5f4) || (mthd == 0x7ec) || (mthd == 0xbe4) || (mthd == 0xff4) || (mthd == 0x17f4)) {
            rivatnt_pgraph_set_point(&rivatnt->d2.clip_x0, &rivatnt->d2.clip_y0, data);
            return;
        }
        if ((mthd == 0x5f8) || (mthd == 0x7f0) || (mthd == 0xbe8) || (mthd == 0xff8) || (mthd == 0x17f8)) {
            rivatnt_pgraph_set_point(&rivatnt->d2.clip_x1, &rivatnt->d2.clip_y1, data);
            return;
        }
        /* B: clipped rectangles given by two corners */
        if (mthd == 0x5fc) {
            rivatnt->d2.color = data;
            return;
        }
        if ((mthd >= 0x600) && (mthd < 0x700)) {
            if (!(mthd & 4))
                rivatnt_pgraph_set_point(&rivatnt->d2.vtx_x[0], &rivatnt->d2.vtx_y[0], data);
            else {
                int32_t x1, y1;
                rivatnt_pgraph_set_point(&x1, &y1, data);
                rivatnt_setup_gdi_clip(rivatnt, &d);
                rivatnt_draw_rect(rivatnt, &d, rivatnt->d2.vtx_x[0], rivatnt->d2.vtx_y[0], x1 - rivatnt->d2.vtx_x[0],
                                  y1 - rivatnt->d2.vtx_y[0],
                                  rivatnt_color_to_surf(rivatnt_obj_cfmt(rivatnt), rivatnt->d2.color, d.fmt));
            }
            return;
        }
        /* C: transparent mono bitmap */
        switch (mthd) {
        case 0x7f4:
            rivatnt->d2.color1 = data;
            return;
        case 0x7f8:
            rivatnt->d2.w_in = rivatnt->d2.w_out = data & 0xffff;
            rivatnt->d2.h_in = rivatnt->d2.h_out = data >> 16;
            return;
        case 0x7fc:
            rivatnt_pgraph_set_point(&rivatnt->d2.x, &rivatnt->d2.y, data);
            rivatnt_image_begin(rivatnt);
            return;
        /* E: two colour mono bitmap */
        case 0xbec:
            rivatnt->d2.color0 = data;
            return;
        case 0xbf0:
            rivatnt->d2.color1 = data;
            return;
        case 0xbf4:
            rivatnt->d2.w_in = data & 0xffff;
            rivatnt->d2.h_in = data >> 16;
            return;
        case 0xbf8:
            rivatnt->d2.w_out = data & 0xffff;
            rivatnt->d2.h_out = data >> 16;
            return;
        case 0xbfc:
            rivatnt_pgraph_set_point(&rivatnt->d2.x, &rivatnt->d2.y, data);
            rivatnt_image_begin(rivatnt);
            return;
        /* F, G: characters from a font in memory */
        case 0xff0:
        case 0x17f0:
            rivatnt->d2.font = data;
            return;
        case 0xffc:
        case 0x17fc:
            rivatnt->d2.color1 = data;
            return;
        }
        if ((mthd >= 0x800) && (mthd < 0xa00)) {
            rivatnt_setup_gdi_clip(rivatnt, &d);
            rivatnt_mono_data(rivatnt, &d, data, 0);
            return;
        }
        if ((mthd >= 0xc00) && (mthd < 0xe00)) {
            rivatnt_setup_gdi_clip(rivatnt, &d);
            rivatnt_mono_data(rivatnt, &d, data, 1);
            return;
        }
        pclog("[RIVA TNT] GDI method %04x %08x not implemented\n", mthd, data);
        return;

    default:
        if (!rivatnt_pgraph_ctx_method(rivatnt, ctx_notify_only, subc, mthd, data))
            pclog("[RIVA TNT] PGRAPH class %02x method %04x %08x not implemented\n", grclass, mthd, data);
        return;
    }
}

/* Deliver a method to PGRAPH. Returns 0 if PGRAPH cannot take it yet. */
static int
rivatnt_pgraph_submit(rivatnt_t *rivatnt, int chid, int subc, uint32_t mthd, uint32_t data)
{
    uint32_t ctx_user = PGR(NV_PGRAPH_CTX_USER);
    uint32_t trapped  = mthd | (subc << 13) | (chid << 24);
    int      old_subc = (ctx_user >> 13) & 7;

    if (!(PGR(NV_PGRAPH_FIFO) & 1) || PGR(NV_PGRAPH_INTR))
        return 0;

    /* A method from another channel makes the RM swap the PGRAPH context. */
    if (!(PGR(NV_PGRAPH_CTX_CONTROL) & 0x10000) || (((ctx_user >> 24) & 0xf) != (uint32_t) chid)) {
        PGR(NV_PGRAPH_TRAPPED_ADDR) = trapped;
        PGR(NV_PGRAPH_TRAPPED_DATA) = data;
        rivatnt_pgraph_intr(rivatnt, PGRAPH_INTR_CONTEXT_SWITCH, 0);
        return 0;
    }

    /* Each subchannel caches its object's context; binding an object loads
       it from the grobj in RAMIN, and changing subchannel swaps it in. */
    if ((subc != old_subc) || (mthd == 0)) {
        if (mthd == 0) {
            uint32_t inst = (data & 0xffff) << 4;

            PGR(NV_PGRAPH_CTX_CACHE4 + subc * 4) = data & 0xffff;
            PGR(NV_PGRAPH_CTX_CACHE1 + subc * 4) = rivatnt_ramin_read_l(inst, rivatnt) & 0x0303f0ff;
            PGR(NV_PGRAPH_CTX_CACHE2 + subc * 4) = rivatnt_ramin_read_l(inst + 4, rivatnt) & 0xffff3f03;
            PGR(NV_PGRAPH_CTX_CACHE3 + subc * 4) = rivatnt_ramin_read_l(inst + 8, rivatnt);
        }
        PGR(NV_PGRAPH_CTX_USER)    = (ctx_user & ~0xe000) | (subc << 13);
        PGR(NV_PGRAPH_CTX_SWITCH1) = PGR(NV_PGRAPH_CTX_CACHE1 + subc * 4);
        PGR(NV_PGRAPH_CTX_SWITCH2) = PGR(NV_PGRAPH_CTX_CACHE2 + subc * 4);
        PGR(NV_PGRAPH_CTX_SWITCH3) = PGR(NV_PGRAPH_CTX_CACHE3 + subc * 4);
        PGR(NV_PGRAPH_CTX_SWITCH4) = PGR(NV_PGRAPH_CTX_CACHE4 + subc * 4);
    }
    PGR(NV_PGRAPH_TRAPPED_ADDR) = trapped;
    PGR(NV_PGRAPH_TRAPPED_DATA) = data;

    if (mthd == 0)
        return 1;

    /* NOTIFY arms a notification; PGRAPH raises it once the next method has
       executed, and the RM writes the notifier. */
    if (mthd == 0x104) {
        PGR(NV_PGRAPH_NOTIFY) = (PGR(NV_PGRAPH_NOTIFY) & ~PGRAPH_NOTIFY_STYLE) | PGRAPH_NOTIFY_REQ |
                                ((data & 1) ? PGRAPH_NOTIFY_STYLE : 0);
        return 1;
    }

    rivatnt_pgraph_method(rivatnt, subc, mthd, data);

    if (PGR(NV_PGRAPH_NOTIFY) & PGRAPH_NOTIFY_REQ)
        rivatnt_pgraph_intr(rivatnt, PGRAPH_INTR_NOTIFY, 1 /* NSOURCE_NOTIFICATION */);
    return 1;
}

uint32_t
rivatnt_pgraph_read(uint32_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    switch (addr) {
    case NV_PGRAPH_STATUS:
        return 0; /* idle: everything executes synchronously */
    }
    return PGR(addr);
}

void
rivatnt_pgraph_write(uint32_t addr, uint32_t val, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    switch (addr) {
    case NV_PGRAPH_INTR:
        PGR(NV_PGRAPH_INTR) &= ~val;
        if (!(PGR(NV_PGRAPH_INTR) & PGRAPH_INTR_NOTIFY))
            PGR(NV_PGRAPH_NSOURCE) = 0;
        rivatnt_pmc_recompute_intr(rivatnt);
        break;
    case NV_PGRAPH_INTR_EN:
        PGR(addr) = val;
        rivatnt_pmc_recompute_intr(rivatnt);
        break;
    case NV_PGRAPH_STATUS:
        return;
    default:
        PGR(addr) = val;
        break;
    }

    if ((addr == NV_PGRAPH_INTR) || (addr == NV_PGRAPH_FIFO) || (addr == NV_PGRAPH_CTX_CONTROL))
        rivatnt_do_gpu_work(rivatnt);
}

static void
rivatnt_do_gpu_work(rivatnt_t *rivatnt)
{
    if (rivatnt->gpu_busy)
        return;
    rivatnt->gpu_busy = 1;

    for (int i = 0; i < 64; i++) {
        int progress = 0;

        while (rivatnt_pfifo_pull(rivatnt))
            progress = 1;
        progress |= rivatnt_pfifo_dma_pusher(rivatnt);
        if (!progress)
            progress = rivatnt_pfifo_switch_pending(rivatnt);
        if (!progress)
            break;
    }

    rivatnt->gpu_busy = 0;
}

void
rivatnt_ptimer_interrupt(int num, void *p)
{
    //nv_riva_log("RIVA TNT PTIMER interrupt #%d fired!\n", num);
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    rivatnt->ptimer.intr |= (1 << num);

    rivatnt_pmc_recompute_intr(rivatnt);
}

/* PTIMER adds 32ns every time NVCLK * DENOMINATOR / NUMERATOR ticks. The RM
   programs NUMERATOR / DENOMINATOR = NVCLK / 31.25MHz, so TIME counts
   nanoseconds. TIME is brought up to date from the TSC whenever it is
   accessed, instead of being ticked. */
static double
rivatnt_ptimer_ns_per_us(rivatnt_t *rivatnt)
{
    if (!rivatnt->ptimer.clock_div)
        return 0.0;
    return 32.0 * (rivatnt->nvclk / 1000000.0) * rivatnt->ptimer.clock_mul / rivatnt->ptimer.clock_div;
}

static void
rivatnt_ptimer_update(rivatnt_t *rivatnt)
{
    uint64_t now     = tsc;
    double   us      = (double) (now - rivatnt->ptimer_tsc_base) * 4294967296.0 / (double) TIMER_USEC;
    double   ns      = rivatnt->ptimer.time_frac + us * rivatnt_ptimer_ns_per_us(rivatnt);
    uint32_t old_low = (uint32_t) rivatnt->ptimer.time;
    uint32_t new_low;

    rivatnt->ptimer_tsc_base = now;
    rivatnt->ptimer.time += (uint64_t) ns;
    rivatnt->ptimer.time_frac = ns - (double) (uint64_t) ns;
    new_low = (uint32_t) rivatnt->ptimer.time;

    /* The alarm fires when TIME_0 passes ALARM_0. */
    if ((uint32_t) (rivatnt->ptimer.alarm - old_low - 1) < (uint32_t) (new_low - old_low))
        rivatnt_ptimer_interrupt(0, rivatnt);
}

/* Arrange to be called when TIME_0 next reaches ALARM_0. */
static void
rivatnt_ptimer_schedule(rivatnt_t *rivatnt)
{
    double   rate  = rivatnt_ptimer_ns_per_us(rivatnt);
    uint32_t delta = rivatnt->ptimer.alarm - (uint32_t) rivatnt->ptimer.time;

    if (rate <= 0.0) {
        timer_disable(&rivatnt->ptimer_alarm_timer);
        return;
    }
    timer_on_auto(&rivatnt->ptimer_alarm_timer, ((delta ? (double) delta : 4294967296.0) / rate) + 0.01);
}

static void
rivatnt_ptimer_alarm_poll(void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    rivatnt_ptimer_update(rivatnt);
    rivatnt_ptimer_schedule(rivatnt);
}

uint32_t
rivatnt_ptimer_read(uint32_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    rivatnt_ptimer_update(rivatnt);

    switch(addr)
    {
    case 0x009100:
        return rivatnt->ptimer.intr;
    case 0x009140:
        return rivatnt->ptimer.intr_en;
    case 0x009200:
        return rivatnt->ptimer.clock_div;
    case 0x009210:
        return rivatnt->ptimer.clock_mul;
    case 0x009400:
        return rivatnt->ptimer.time & 0xffffffffULL;
    case 0x009410:
        return rivatnt->ptimer.time >> 32;
    case 0x009420:
        return rivatnt->ptimer.alarm;
    }
    return 0;
}

void
rivatnt_ptimer_write(uint32_t addr, uint32_t val, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    switch(addr)
    {
    case 0x009100:
        rivatnt->ptimer.intr &= ~val;
        rivatnt_pmc_recompute_intr(rivatnt);
        break;
    case 0x009140:
        rivatnt->ptimer.intr_en = val & 1;
        rivatnt_pmc_recompute_intr(rivatnt);
        break;
    case 0x009200:
        rivatnt_ptimer_update(rivatnt);
        if(!(uint16_t)val) val = 1;
        rivatnt->ptimer.clock_div = (uint16_t)val;
        rivatnt_ptimer_schedule(rivatnt);
        break;
    case 0x009210:
        rivatnt_ptimer_update(rivatnt);
        rivatnt->ptimer.clock_mul = (uint16_t)val;
        rivatnt_ptimer_schedule(rivatnt);
        break;
    case 0x009400:
        rivatnt_ptimer_update(rivatnt);
        rivatnt->ptimer.time &= 0x0fffffff00000000ULL;
        rivatnt->ptimer.time |= val & 0xffffffe0;
        rivatnt_ptimer_schedule(rivatnt);
        break;
    case 0x009410:
        rivatnt_ptimer_update(rivatnt);
        rivatnt->ptimer.time &= 0xffffffe0;
        rivatnt->ptimer.time |= (uint64_t)(val & 0x0fffffff) << 32;
        rivatnt_ptimer_schedule(rivatnt);
        break;
    case 0x009420:
        rivatnt_ptimer_update(rivatnt);
        rivatnt->ptimer.alarm = val & 0xffffffe0;
        rivatnt_ptimer_schedule(rivatnt);
        break;
    }
}

uint32_t
rivatnt_pfb_read(uint32_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    switch(addr)
    {
        case 0x100000:
            switch(rivatnt->vram_size)
            {
                case 4 << 20: return 0x1015;
                case 8 << 20: return 0x1016;
                case 16 << 20: return 0x101f;
            }
            break;
    }

    return rivatnt->pfb.regs[(addr & 0xfff) >> 2];
}

void
rivatnt_pfb_write(uint32_t addr, uint32_t val, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    rivatnt->pfb.regs[(addr & 0xfff) >> 2] = val;
}

uint32_t
rivatnt_pextdev_read(uint32_t addr, void *p)
{
    //rivatnt_t *rivatnt = (rivatnt_t *)p;

    switch(addr)
    {
        case 0x101000:
            return 0x0000019e;
    }

    return 0;
}

uint32_t
rivatnt_pcrtc_read(uint32_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    switch(addr)
    {
        case 0x600100:
            return rivatnt->pcrtc.intr;
        case 0x600140:
            return rivatnt->pcrtc.intr_en;
    }
    return 0;
}

void
rivatnt_pcrtc_write(uint32_t addr, uint32_t val, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    switch(addr)
    {
        case 0x600100:
            rivatnt->pcrtc.intr &= ~val;
            rivatnt_pmc_recompute_intr(rivatnt);
            break;
        case 0x600140:
            rivatnt->pcrtc.intr_en = val & 1;
            rivatnt_pmc_recompute_intr(rivatnt);
            break;
    }
}

uint32_t
rivatnt_pramdac_read(uint32_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    switch(addr)
    {
        case 0x680500:
            return rivatnt->pramdac.nvpll;
        case 0x680504:
            return rivatnt->pramdac.mpll;
        case 0x680508:
            return rivatnt->pramdac.vpll;
    }
    return 0;
}

void
rivatnt_pramdac_write(uint32_t addr, uint32_t val, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    switch(addr)
    {
        case 0x680500:
            rivatnt->pramdac.nvpll = val;
            break;
        case 0x680504:
            rivatnt->pramdac.mpll = val;
            break;
        case 0x680508:
            rivatnt->pramdac.vpll = val;
            break;
    }
    svga_recalctimings(&rivatnt->svga);
}

/* NV_PRMCIO (0x601000), NV_PRMVIO (0x0c0000) and NV_PRMDIO (0x681000) alias
   the VGA registers at their I/O port offsets. Returns the port a byte of
   BAR0 aliases, or 0. */
static uint16_t
rivatnt_vga_alias(uint32_t addr)
{
    switch (addr) {
    case 0x6013b4: case 0x6013b5: case 0x6013ba:
    case 0x6013c0: case 0x6013c1: case 0x6013c2:
    case 0x6013d4: case 0x6013d5: case 0x6013da:

    case 0x0c03c2: case 0x0c03c3: case 0x0c03c4:
    case 0x0c03c5: case 0x0c03cc: case 0x0c03ce:
    case 0x0c03cf:

    case 0x6813c6: case 0x6813c7: case 0x6813c8:
    case 0x6813c9: case 0x6813ca: case 0x6813cb:
        return addr & 0x3ff;
    }
    return 0;
}

static int
rivatnt_is_vga_window(uint32_t addr)
{
    return ((addr & 0xfff000) == 0x601000) || ((addr & 0xff8000) == 0x0c0000) || ((addr & 0xfff000) == 0x681000);
}

/* Wider accesses reach the VGA registers one byte lane at a time; the RM
   writes index and data pairs, and reads status as aligned dwords. */
static uint32_t
rivatnt_vga_window_read(uint32_t addr, int len, void *p)
{
    uint32_t ret = 0;

    for (int i = 0; i < len; i++) {
        uint16_t port = rivatnt_vga_alias(addr + i);
        if (port)
            ret |= (uint32_t) rivatnt_in(port, p) << (i << 3);
    }
    return ret;
}

static void
rivatnt_vga_window_write(uint32_t addr, int len, uint32_t val, void *p)
{
    for (int i = 0; i < len; i++) {
        uint16_t port = rivatnt_vga_alias(addr + i);
        if (port)
            rivatnt_out(port, (val >> (i << 3)) & 0xff, p);
    }
}

uint32_t
rivatnt_mmio_read_l(uint32_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    addr &= 0xffffff;

    uint32_t ret = 0;

    if (rivatnt_is_vga_window(addr))
        return rivatnt_vga_window_read(addr, 4, p);

    addr &= 0xfffffc;

    if ((addr >= 0x000000) && (addr <= 0x000fff)) ret = rivatnt_pmc_read(addr, rivatnt);
    if ((addr >= 0x002000) && (addr <= 0x003fff)) ret = rivatnt_pfifo_read(addr, rivatnt);
    if ((addr >= 0x009000) && (addr <= 0x009fff)) ret = rivatnt_ptimer_read(addr, rivatnt);
    if ((addr >= 0x100000) && (addr <= 0x100fff)) ret = rivatnt_pfb_read(addr, rivatnt);
    if ((addr >= 0x101000) && (addr <= 0x101fff)) ret = rivatnt_pextdev_read(addr, rivatnt);
    if ((addr >= 0x400000) && (addr <= 0x401fff)) ret = rivatnt_pgraph_read(addr, rivatnt);
    if (addr >= 0x800000) ret = rivatnt_user_read(rivatnt, addr);
    if ((addr >= 0x600000) && (addr <= 0x600fff)) ret = rivatnt_pcrtc_read(addr, rivatnt);
    if ((addr >= 0x680000) && (addr <= 0x680fff)) ret = rivatnt_pramdac_read(addr, rivatnt);
    if ((addr >= 0x700000) && (addr <= 0x7fffff)) ret = rivatnt_ramin_read_l(addr & 0xfffff, rivatnt);
    if ((addr >= 0x300000) && (addr <= 0x30ffff)) ret = ((uint32_t *) rivatnt->bios_rom.rom)[(addr & rivatnt->bios_rom.mask) >> 2];

    if ((addr >= 0x1800) && (addr <= 0x18ff))
        ret = (rivatnt_pci_read(0,(addr+0) & 0xff,1,p) << 0) | (rivatnt_pci_read(0,(addr+1) & 0xff,1,p) << 8) | (rivatnt_pci_read(0,(addr+2) & 0xff,1,p) << 16) | (rivatnt_pci_read(0,(addr+3) & 0xff,1,p) << 24);

    //if(addr != 0x9400) pclog("[RIVA TNT] MMIO read %08x returns value %08x\n", addr, ret);

    return ret;
}


uint8_t
rivatnt_mmio_read(uint32_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    addr &= 0xffffff;

    if ((addr >= 0x300000) && (addr <= 0x30ffff)) return rivatnt->bios_rom.rom[addr & rivatnt->bios_rom.mask];

    if ((addr >= 0x1800) && (addr <= 0x18ff))
    return rivatnt_pci_read(0,addr & 0xff,1,p);

    if (rivatnt_is_vga_window(addr))
        return rivatnt_vga_window_read(addr, 1, p);

    return (rivatnt_mmio_read_l(addr & 0xffffff, rivatnt) >> ((addr & 3) << 3)) & 0xff;
}


uint16_t
rivatnt_mmio_read_w(uint32_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    addr &= 0xffffff;

    if ((addr >= 0x300000) && (addr <= 0x30ffff)) return ((uint16_t *) rivatnt->bios_rom.rom)[(addr & rivatnt->bios_rom.mask) >> 1];

    if ((addr >= 0x1800) && (addr <= 0x18ff))
    return (rivatnt_pci_read(0,(addr+0) & 0xff,1,p) << 0) | (rivatnt_pci_read(0,(addr+1) & 0xff,1,p) << 8);

    if (rivatnt_is_vga_window(addr))
        return rivatnt_vga_window_read(addr, 2, p);

   return (rivatnt_mmio_read_l(addr & 0xffffff, rivatnt) >> ((addr & 3) << 3)) & 0xffff;
}


void
rivatnt_mmio_write_l(uint32_t addr, uint32_t val, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    addr &= 0xffffff;

    //pclog("[RIVA TNT] MMIO write %08x %08x\n", addr, val);

    if (rivatnt_is_vga_window(addr)) {
        rivatnt_vga_window_write(addr, 4, val, p);
        return;
    }

    if ((addr >= 0x1800) && (addr <= 0x18ff)) {
    rivatnt_pci_write(0, addr & 0xff, 1, val & 0xff, p);
    rivatnt_pci_write(0, (addr+1) & 0xff, 1, (val>>8) & 0xff, p);
    rivatnt_pci_write(0, (addr+2) & 0xff, 1, (val>>16) & 0xff, p);
    rivatnt_pci_write(0, (addr+3) & 0xff, 1, (val>>24) & 0xff, p);
    return;
    }

    if((addr >= 0x000000) && (addr <= 0x000fff)) rivatnt_pmc_write(addr, val, rivatnt);
    if((addr >= 0x002000) && (addr <= 0x003fff)) rivatnt_pfifo_write(addr, val, rivatnt);
    if((addr >= 0x009000) && (addr <= 0x009fff)) rivatnt_ptimer_write(addr, val, rivatnt);
    if((addr >= 0x100000) && (addr <= 0x100fff)) rivatnt_pfb_write(addr, val, rivatnt);
    if((addr >= 0x400000) && (addr <= 0x401fff)) rivatnt_pgraph_write(addr, val, rivatnt);
    if(addr >= 0x800000) rivatnt_user_write(rivatnt, addr, val);
    if((addr >= 0x600000) && (addr <= 0x600fff)) rivatnt_pcrtc_write(addr, val, rivatnt);
    if((addr >= 0x680000) && (addr <= 0x680fff)) rivatnt_pramdac_write(addr, val, rivatnt);
    if((addr >= 0x700000) && (addr <= 0x7fffff)) rivatnt_ramin_write_l(addr & 0xfffff, val, rivatnt);
}


void
rivatnt_mmio_write(uint32_t addr, uint8_t val, void *p)
{
    uint32_t tmp;

    addr &= 0xffffff;

    if ((addr >= 0x1800) && (addr <= 0x18ff)) {
    rivatnt_pci_write(0, addr & 0xff, 1, val & 0xff, p);
    return;
    }

    if (rivatnt_is_vga_window(addr)) {
        rivatnt_vga_window_write(addr, 1, val, p);
        return;
    }

    tmp = rivatnt_mmio_read_l(addr,p);
    tmp &= ~(0xff << ((addr & 3) << 3));
    tmp |= val << ((addr & 3) << 3);
    rivatnt_mmio_write_l(addr, tmp, p);
}


void
rivatnt_mmio_write_w(uint32_t addr, uint16_t val, void *p)
{
    uint32_t tmp;

    addr &= 0xffffff;

    if ((addr >= 0x1800) && (addr <= 0x18ff)) {
    rivatnt_pci_write(0, addr & 0xff, 1, val & 0xff, p);
    rivatnt_pci_write(0, (addr+1) & 0xff, 1, (val>>8) & 0xff, p);
    return;
    }

    if (rivatnt_is_vga_window(addr)) {
        rivatnt_vga_window_write(addr, 2, val, p);
        return;
    }

    tmp = rivatnt_mmio_read_l(addr,p);
    tmp &= ~(0xffff << ((addr & 3) << 3));
    tmp |= val << ((addr & 3) << 3);
    rivatnt_mmio_write_l(addr, tmp, p);
}

uint8_t
rivatnt_rma_in(uint16_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    svga_t *svga = &rivatnt->svga;
    uint8_t ret = 0;

    addr &= 0xff;

    //pclog("RIVA TNT RMA read %04X %04X:%08X\n", addr, CS, cpu_state.pc);

    switch(addr) {
    case 0x00:
        ret = 0x65;
        break;
    case 0x01:
        ret = 0xd0;
        break;
    case 0x02:
        ret = 0x16;
        break;
    case 0x03:
        ret = 0x2b;
        break;
    case 0x08:
    case 0x09:
    case 0x0a:
    case 0x0b:
        if (rivatnt->rma.rma_dst_addr < 0x1000000)
            ret = rivatnt_mmio_read((rivatnt->rma.rma_dst_addr + (addr & 3)) & 0xffffff, rivatnt);
        else
            ret = svga_read_linear((rivatnt->rma.rma_dst_addr - 0x1000000) & 0xffffff, svga);
        break;
    }

    return ret;
}


void
rivatnt_rma_out(uint16_t addr, uint8_t val, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    svga_t* svga = &rivatnt->svga;

    addr &= 0xff;

    //pclog("RIVA TNT RMA write %04X %02X %04X:%08X\n", addr, val, CS, cpu_state.pc);

    switch(addr) {
    case 0x04:
        rivatnt->rma.rma_dst_addr &= ~0xff;
        rivatnt->rma.rma_dst_addr |= val;
        break;
    case 0x05:
        rivatnt->rma.rma_dst_addr &= ~0xff00;
        rivatnt->rma.rma_dst_addr |= (val << 8);
        break;
    case 0x06:
        rivatnt->rma.rma_dst_addr &= ~0xff0000;
        rivatnt->rma.rma_dst_addr |= (val << 16);
        break;
    case 0x07:
        rivatnt->rma.rma_dst_addr &= ~0xff000000;
        rivatnt->rma.rma_dst_addr |= (val << 24);
        break;
    case 0x08:
    case 0x0c:
    case 0x10:
    case 0x14:
        rivatnt->rma.rma_data &= ~0xff;
        rivatnt->rma.rma_data |= val;
        break;
    case 0x09:
    case 0x0d:
    case 0x11:
    case 0x15:
        rivatnt->rma.rma_data &= ~0xff00;
        rivatnt->rma.rma_data |= (val << 8);
        break;
    case 0x0a:
    case 0x0e:
    case 0x12:
    case 0x16:
        rivatnt->rma.rma_data &= ~0xff0000;
        rivatnt->rma.rma_data |= (val << 16);
        break;
    case 0x0b:
    case 0x0f:
    case 0x13:
    case 0x17:
        rivatnt->rma.rma_data &= ~0xff000000;
        rivatnt->rma.rma_data |= (val << 24);
        if (rivatnt->rma.rma_dst_addr < 0x1000000)
            rivatnt_mmio_write_l(rivatnt->rma.rma_dst_addr & 0xffffff, rivatnt->rma.rma_data, rivatnt);
        else
            svga_writel_linear((rivatnt->rma.rma_dst_addr - 0x1000000) & 0xffffff, rivatnt->rma.rma_data, svga);
        break;
    }

    if (addr & 0x10)
    rivatnt->rma.rma_dst_addr+=4;
}


static void
rivatnt_out(uint16_t addr, uint8_t val, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    svga_t *svga = &rivatnt->svga;
    uint8_t old;

    if ((addr >= 0x3d0) && (addr <= 0x3d3)) {
    rivatnt->rma.rma_access_reg[addr & 3] = val;
    if(!(rivatnt->rma.rma_mode & 1))
        return;
    rivatnt_rma_out(((rivatnt->rma.rma_mode & 0xe) << 1) + (addr & 3), rivatnt->rma.rma_access_reg[addr & 3], rivatnt);
    }

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
    addr ^= 0x60;

    switch (addr) {
    case 0x3D4:
        svga->crtcreg = val;
        return;
    case 0x3D5:
        if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
            return;
        if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
            val = (svga->crtc[7] & ~0x10) | (val & 0x10);
        old = svga->crtc[svga->crtcreg];
        svga->crtc[svga->crtcreg] = val;
        //if(svga->seqregs[0x06] == 0x57)
        {
            switch(svga->crtcreg) {
                case 0x1e:
                    rivatnt->read_bank = val;
                    if (svga->chain4) svga->read_bank = rivatnt->read_bank << 15;
                    else              svga->read_bank = rivatnt->read_bank << 13;
                    break;
                case 0x1d:
                    rivatnt->write_bank = val;
                    if (svga->chain4) svga->write_bank = rivatnt->write_bank << 15;
                    else              svga->write_bank = rivatnt->write_bank << 13;
                    break;
                case 0x19: case 0x1a: case 0x25: case 0x28:
                case 0x2d:
                    svga_recalctimings(svga);
                    break;
                case 0x30:
		    		rivatnt->cursor_offset = (rivatnt->cursor_offset & ~(0x7f << 12)) | ((val & 0x7f) << 12);
	    			rivatnt->cursor_vram = !!(val & 0x80);
                    break;
			    case 0x31:
				    rivatnt->cursor_offset = (rivatnt->cursor_offset & ~(0xf8 << 4)) | ((val & 0xf8) << 4);
				    rivatnt->cursor_enabled = !!(val & 1);
				    svga->hwcursor.ena = !!(val & 1);
                    break;
                case 0x38:
                    rivatnt->rma.rma_mode = val & 0xf;
                    break;
                case 0x3f:
                    i2c_gpio_set(rivatnt->i2c, !!(val & 0x20), !!(val & 0x10));
                    break;
            }
        }
        //if (svga->crtcreg > 0x18 && svga->crtcreg != 0x38 && svga->crtcreg != 0x19 && svga->crtcreg != 0x1a && svga->crtcreg != 0x25 && svga->crtcreg != 0x28)
        //     pclog("RIVA TNT Extended CRTC write %02X %02x\n", svga->crtcreg, val);
        if (old != val) {
            if ((svga->crtcreg < 0xe) || (svga->crtcreg > 0x10)) {
                svga->fullchange = changeframecount;
                svga_recalctimings(svga);
            }
        }
        break;
    }

    svga_out(addr, val, svga);
}


static uint8_t
rivatnt_in(uint16_t addr, void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    svga_t *svga = &rivatnt->svga;
    uint8_t temp;

    if ((addr >= 0x3d0) && (addr <= 0x3d3)) {
    if (!(rivatnt->rma.rma_mode & 1))
        return 0x00;
    return rivatnt_rma_in(((rivatnt->rma.rma_mode & 0xe) << 1) + (addr & 3), rivatnt);
    }

    if (((addr&0xFFF0) == 0x3D0 || (addr&0xFFF0) == 0x3B0) && !(svga->miscout&1)) addr ^= 0x60;

    switch (addr) {
    case 0x3D4:
        temp = svga->crtcreg;
        break;
    case 0x3D5:
        switch(svga->crtcreg) {
            case 0x3e:
                    /* DDC status register */
                temp = (i2c_gpio_get_sda(rivatnt->i2c) << 3) | (i2c_gpio_get_scl(rivatnt->i2c) << 2);
                break;
            default:
                temp = svga->crtc[svga->crtcreg];
                break;
        }
        break;
    default:
        temp = svga_in(addr, svga);
        break;
    }

    return temp;
}

static void
rivatnt_recalctimings(svga_t *svga)
{
    rivatnt_t *rivatnt = (rivatnt_t *)svga->priv;

    svga->memaddr_latch += (svga->crtc[0x19] & 0x1f) << 16;
    svga->rowoffset += (svga->crtc[0x19] & 0xe0) << 3;
    /* NV_CIO_CRE_LSR (CR25): bit 10 of the vertical timings, bit 6 of HBE */
    if (svga->crtc[0x25] & 0x01) svga->vtotal      += 0x400;
    if (svga->crtc[0x25] & 0x02) svga->dispend     += 0x400;
    if (svga->crtc[0x25] & 0x04) svga->vsyncstart  += 0x400;
    if (svga->crtc[0x25] & 0x08) svga->vblankstart += 0x400;
    if (svga->crtc[0x25] & 0x10) svga->hblank_end_val |= 0x40;
    svga->hblank_end_mask = 0x7f;
    /* NV_CIO_CRE_HEB (CR2D): bit 8 of the horizontal timings, in character clocks */
    if (svga->crtc[0x2d] & 0x01) svga->htotal += 0x100;
    if (svga->crtc[0x2d] & 0x02) {
        svga->hdisp += 0x100 * svga->dots_per_clock;
        svga->hdisp_time += 0x100;
    }
    if (svga->crtc[0x2d] & 0x04) svga->hblankstart += 0x100;
    /* The effects of the large screen bit seem to just be doubling the row offset.
       However, these large modes still don't work. Possibly core SVGA bug? It does report 640x2 res after all. */

    switch(svga->crtc[0x28] & 3) {
    case 1:
        svga->bpp = 8;
        svga->lowres = 0;
        svga->render = svga_render_8bpp_highres;
        break;
    case 2:
        svga->bpp = 16;
        svga->lowres = 0;
        svga->render = svga_render_16bpp_highres;
        break;
    case 3:
        svga->bpp = 32;
        svga->lowres = 0;
        svga->render = svga_render_32bpp_highres;
        break;
    }

    double freq = 13500000;
    int nv_m = rivatnt->pramdac.nvpll & 0xff;
    int nv_n = (rivatnt->pramdac.nvpll >> 8) & 0xff;
    int nv_p = (rivatnt->pramdac.nvpll >> 16) & 7;

    if(nv_n == 0) nv_n = 1;
    if(nv_m == 0) nv_m = 1;

    /* NVCLK drives PTIMER; account for the time elapsed at the old rate. */
    rivatnt_ptimer_update(rivatnt);
    rivatnt->nvclk = (freq * nv_n) / (nv_m << nv_p);
    rivatnt_ptimer_schedule(rivatnt);

    freq = 13500000;
    int v_m = rivatnt->pramdac.vpll & 0xff;
    int v_n = (rivatnt->pramdac.vpll >> 8) & 0xff;
    int v_p = (rivatnt->pramdac.vpll >> 16) & 7;

    if(v_n == 0) v_n = 1;
    if(v_m == 0) v_m = 1;

    freq = (freq * v_n) / (v_m << v_p);
    if((svga->crtc[0x28] & 3) != 0) svga->clock = (cpuclock * (double)(1ull << 32)) / freq;
}

void
rivatnt_vblank_start(svga_t *svga)
{
    rivatnt_t *rivatnt = (rivatnt_t *)svga->priv;

    rivatnt->pcrtc.intr |= 1;

    rivatnt_pmc_recompute_intr(rivatnt);
}

static void
rivatnt_hwcursor_draw(svga_t *svga, int displine)
{
    rivatnt_t *rivatnt = (rivatnt_t *) svga->priv;
    uint16_t startx = rivatnt->pramdac.cursor_pos & 0xfff;
    uint16_t starty = (rivatnt->pramdac.cursor_pos >> 16) & 0xfff;
	uint32_t cursor_offset = rivatnt->cursor_offset;
	int         offset = svga->hwcursor_latch.x - svga->hwcursor_latch.xoff;

    if(startx >= svga->hdisp || starty >= svga->dispend) return;

    uint32_t cursor_bitmap = 0;
    int replace_bit = 0;
    int transparent = 0;

	cursor_offset <<= 4;
    for(int y = 0; y < 32; y++)
	{
    	for(int x = 0; x < 32; x++)
    	{
        	uint16_t raw = 0;
			raw = rivatnt_ramin_read_w(cursor_offset, rivatnt);
        	replace_bit = raw & 0x8000;
        	transparent = raw == 0;
        	cursor_bitmap = video_15to32[raw & 0x7fff];
        	cursor_offset += 2;
        	uint32_t current_col = buffer32->line[svga->hwcursor_latch.y + y][offset + x + svga->x_add];
        	if(replace_bit) buffer32->line[svga->hwcursor_latch.y + y][offset + x + svga->x_add] = cursor_bitmap | 0xff000000;
        	else buffer32->line[svga->hwcursor_latch.y + y][offset + x + svga->x_add] = transparent ? current_col | 0xff000000 : (current_col ^ cursor_bitmap) | 0xff000000;
    	}
	}
}


static void
*rivatnt_init(const device_t *info)
{
    rivatnt_t *rivatnt = malloc(sizeof(rivatnt_t));
    svga_t *svga;
    char *romfn = BIOS_RIVATNT_PATH;
    memset(rivatnt, 0, sizeof(rivatnt_t));
    svga = &rivatnt->svga;

    timer_add(&rivatnt->ptimer_alarm_timer, rivatnt_ptimer_alarm_poll, rivatnt, 0);
    rivatnt->ptimer_tsc_base = tsc;

    rivatnt->vram_size = device_get_config_int("memory") << 20;
    rivatnt->vram_mask = rivatnt->vram_size - 1;
    rivatnt->ramin_flip = rivatnt->vram_mask & 0xfffffff0;

    svga_init(info, &rivatnt->svga, rivatnt, rivatnt->vram_size,
          rivatnt_recalctimings, rivatnt_in, rivatnt_out,
          rivatnt_hwcursor_draw, NULL);

    svga->decode_mask = rivatnt->vram_mask;
    svga->force_old_addr = 1;

    rom_init(&rivatnt->bios_rom, romfn, 0xc0000, 0x10000, 0xffff, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_disable(&rivatnt->bios_rom.mapping);

    mem_mapping_add(&rivatnt->mmio_mapping, 0, 0, rivatnt_mmio_read, rivatnt_mmio_read_w, rivatnt_mmio_read_l, rivatnt_mmio_write, rivatnt_mmio_write_w, rivatnt_mmio_write_l,  NULL, MEM_MAPPING_EXTERNAL, rivatnt);
    mem_mapping_disable(&rivatnt->mmio_mapping);
    mem_mapping_add(&rivatnt->linear_mapping, 0, 0, svga_read_linear, svga_readw_linear, svga_readl_linear, svga_write_linear, svga_writew_linear, svga_writel_linear,  NULL, MEM_MAPPING_EXTERNAL, &rivatnt->svga);
    mem_mapping_disable(&rivatnt->linear_mapping);

    svga->vblank_start = rivatnt_vblank_start;

    io_sethandler(0x03a0, 0x0040, rivatnt_in, NULL, NULL, rivatnt_out,
			NULL, NULL, rivatnt);

    pci_add_card(PCI_ADD_NORMAL, rivatnt_pci_read, rivatnt_pci_write, rivatnt, &rivatnt->pci_slot);

    rivatnt->pci_regs[0x04] = 0x07;
    rivatnt->pci_regs[0x05] = 0x00;
    rivatnt->pci_regs[0x07] = 0x02;

    rivatnt->pci_regs[0x30] = 0x00;
    rivatnt->pci_regs[0x32] = 0x0c;
    rivatnt->pci_regs[0x33] = 0x00;

    rivatnt->pmc.intr_en = 1;

    //Default values for the RAMDAC PLLs
    rivatnt->pramdac.mpll = 0x03c20d;
    rivatnt->pramdac.nvpll = 0x03c20d;
    rivatnt->pramdac.vpll = 0x03c20d;


    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_rivatnt);

    rivatnt->i2c = i2c_gpio_init("ddc_rivatnt");
    rivatnt->ddc = ddc_init(i2c_gpio_get_bus(rivatnt->i2c));

    return rivatnt;
}


static int
rivatnt_available(void)
{
    return rom_present(BIOS_RIVATNT_PATH);
}


void
rivatnt_close(void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    
    svga_close(&rivatnt->svga);
    
    free(rivatnt);
}


void
rivatnt_speed_changed(void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;
    
    svga_recalctimings(&rivatnt->svga);
}


void
rivatnt_force_redraw(void *p)
{
    rivatnt_t *rivatnt = (rivatnt_t *)p;

    rivatnt->svga.fullchange = changeframecount;
}


static const device_config_t rivatnt_config[] = {
    {
        .name = "memory",
        .description = "Memory size",
        .type = CONFIG_SELECTION,
        .selection = {
            {
                .description = "4 MB",
                .value = 4
            },
            {
                .description = "8 MB",
                .value = 8
            },
            {
                .description = "16 MB",
                .value = 16
            },
            {
                .description = ""
            }
        },
        .default_int = 16
    },
    { .type = -1 }
};

const device_t rivatnt_pci_device = {
    .name = "nVidia RIVA TNT (PCI)",
    .internal_name = "rivatnt",
    .flags = DEVICE_PCI,
    .local = RIVATNT_DEVICE_ID,
    .init = rivatnt_init,
    .close = rivatnt_close, 
    .reset = NULL,
    .available = rivatnt_available,
    .speed_changed = rivatnt_speed_changed,
    .force_redraw = rivatnt_force_redraw,
    .config = rivatnt_config
};
