/*
 * 86Box	A hypervisor and IBM PC system emulator that specializes in
 *		running old operating systems and software designed for IBM
 *		PC systems and compatibles from 1981 through fairly recent
 *		system designs based on the PCI bus.
 *
 *		This file is part of the 86Box distribution.
 *
 *		Emulation of the original Radeon.
 *
 * Version:	@(#)vid_r100.c	1.0.0	2019/09/13
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

#define BIOS_R100_PATH "roms/video/radeon7200/radeon7200_155-155mhz.VBI"

#define R100_VENDOR_ID 0x1002
#define R100_DEVICE_ID 0x5144

typedef struct r100_t {
    mem_mapping_t mmio_mapping;
    mem_mapping_t linear_mapping;

    svga_t svga;

    rom_t bios_rom;

    uint32_t vram_size, vram_mask,
        mmio_base, lfb_base, io_base;

    uint8_t read_bank, write_bank;

    uint8_t pci_regs[256];
    uint8_t pci_slot;
    uint8_t io_regs[0x2000];
    uint8_t int_line;

    int card;

    void *i2c, *ddc;
} r100_t;

static video_timings_t timing_r100 = { VIDEO_PCI, 2, 2, 1, 20, 20, 21 };

static uint8_t r100_in(uint16_t addr, void *p);
static void    r100_out(uint16_t addr, uint8_t val, void *p);

static uint8_t
r100_pci_read(int func, int addr, void *p)
{
    r100_t *r100 = (r100_t *) p;
    // svga_t *svga = &r100->svga;

    pclog("RADEON PCI read %02x\n", addr);

    switch (addr) {
        case 0x00:
            return 0x02; /*ATi*/
        case 0x01:
            return 0x10;

        case 0x02:
            return 0x44;
        case 0x03:
            return 0x51;

        case 0x04:
            return (r100->pci_regs[0x04] & 0x17) | 0x80; /*Respond to IO and memory accesses*/
        case 0x05:
            return r100->pci_regs[0x05] & 0x03;

        case 0x06:
            return 0xb0;
        case 0x07:
            return 0x02;

        case 0x08:
            return 0x00; /*Revision ID*/
        case 0x09:
            return 0x00; /*Programming interface*/

        case 0x0a:
            return 0x00; /*Supports VGA interface*/
        case 0x0b:
            return 0x03;

        case 0x10:
            return 0x08;
        case 0x13:
            return r100->lfb_base >> 24;

        case 0x14:
            return 0x01;
        case 0x15:
            return r100->io_base >> 8;
        case 0x16:
            return r100->io_base >> 16;
        case 0x17:
            return r100->io_base >> 24;

        case 0x1b:
            return r100->mmio_base >> 24;

        case 0x2c:
        case 0x2d:
        case 0x2e:
        case 0x2f:
            return r100->pci_regs[addr];

        case 0x30:
            return (r100->pci_regs[0x30] & 0x01); /*BIOS ROM address*/
        case 0x31:
            return 0x00;
        case 0x32:
            return r100->pci_regs[0x32];
        case 0x33:
            return r100->pci_regs[0x33];

        case 0x34:
            return 0x50;

        case 0x3c:
            return r100->int_line;
        case 0x3d:
            return PCI_INTA;

        case 0x3e:
            return 0x03;
        case 0x3f:
            return 0x01;

        case 0x50:
            return 0x01;
        case 0x52:
            return 0x02;
        case 0x53:
            return 0x06;

        case 0x58:
            return 0x02;
        case 0x59:
            return 0x50;
        case 0x5a:
            return 0x20;

        case 0x5c:
            return 0x07;
        case 0x5d:
            return 0x02;
        case 0x5f:
            return 0x2f;

        case 0x61:
            return 0x02;
    }

    return 0x00;
}

static void
r100_recalc_mapping(r100_t *r100)
{
    svga_t *svga = &r100->svga;

    if (!(r100->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
        // pclog("PCI mem off\n");
        mem_mapping_disable(&svga->mapping);
        mem_mapping_disable(&r100->mmio_mapping);
        mem_mapping_disable(&r100->linear_mapping);
        return;
    }

    // pclog("PCI mem on\n");
    // pclog("r100->mmio_base = %08X\n", r100->mmio_base);
    if (r100->mmio_base)
        mem_mapping_set_addr(&r100->mmio_mapping, r100->mmio_base, 0x1000000);
    else
        mem_mapping_disable(&r100->mmio_mapping);

    // pclog("r100->lfb_base = %08X\n", r100->lfb_base);
    if (r100->lfb_base) {
        mem_mapping_set_addr(&r100->linear_mapping, r100->lfb_base, 0x2000000);
    } else {
        mem_mapping_disable(&r100->linear_mapping);
    }

    switch (svga->gdcreg[6] & 0x0c) {
        case 0x0: /*128k at A0000*/
            mem_mapping_set_addr(&svga->mapping, 0xa0000, 0x20000);
            svga->banked_mask = 0xffff;
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
r100_pci_write(int func, int addr, uint8_t val, void *p)
{
    r100_t *r100 = (r100_t *) p;

    pclog("RADEON PCI write %02x %02x\n", addr, val);

    switch (addr) {
        case PCI_REG_COMMAND:
            r100->pci_regs[PCI_REG_COMMAND] = val & 0x17;
            io_removehandler(0x03c0, 0x0020, r100_in, NULL, NULL, r100_out, NULL, NULL, r100);
            io_removehandler(r100->io_base, 0x0100, r100_in, NULL, NULL, r100_out, NULL, NULL, r100);
            if (val & PCI_COMMAND_IO) {
                io_sethandler(0x03c0, 0x0020, r100_in, NULL, NULL, r100_out, NULL, NULL, r100);
                if (r100->io_base)
                    io_sethandler(r100->io_base, 0x0100, r100_in, NULL, NULL, r100_out, NULL, NULL, r100);
            }
            r100_recalc_mapping(r100);
            break;

        case 0x05:
            r100->pci_regs[0x05] = val & 0x03;
            break;

        case 0x13:
            r100->lfb_base = (val & 0xfe) << 24;
            r100_recalc_mapping(r100);
            break;

        case 0x15:
            io_removehandler(r100->io_base & 0xff00, 0x0100, r100_in, NULL, NULL, r100_out, NULL, NULL, r100);
            r100->io_base = (r100->io_base & ~0xff00) | (val << 8);
            if (r100->io_base)
                io_sethandler(r100->io_base & 0xff00, 0x0100, r100_in, NULL, NULL, r100_out, NULL, NULL, r100);
            break;

        case 0x16:
            r100->io_base = (r100->io_base & ~0xff0000) | (val << 16);
            break;

        case 0x17:
            r100->io_base = (r100->io_base & ~0xff000000) | (val << 24);
            break;

        case 0x1b:
            r100->mmio_base = (r100->mmio_base & ~0xff000000) | (val << 24);
            r100_recalc_mapping(r100);
            break;

        case 0x30:
        case 0x32:
        case 0x33:
            r100->pci_regs[addr] = val;
            if (r100->pci_regs[0x30] & 0x01) {
                uint32_t addr = (r100->pci_regs[0x32] << 16) | (r100->pci_regs[0x33] << 24);
                mem_mapping_set_addr(&r100->bios_rom.mapping, addr, 0x10000);
            } else
                mem_mapping_disable(&r100->bios_rom.mapping);
            break;

        case 0x3c:
            r100->int_line = val;
            break;
    }
}

uint32_t
r100_mmio_read_l(uint32_t addr, void *p)
{
    r100_t *r100 = (r100_t *) p;
    svga_t *svga = &r100->svga;

    addr &= 0x1fff;

    uint32_t ret = 0;

    if ((addr >= 0xf00) && (addr <= 0xfff))
        return r100_pci_read(0, addr & 0xff, r100) | (r100_pci_read(0, (addr + 1) & 0xff, r100) << 8)
            | (r100_pci_read(0, (addr + 2) & 0xff, r100) << 16) | (r100_pci_read(0, (addr + 3) & 0xff, r100) << 24);

    switch (addr & 0x1ffc) {
        case 0x060:
            {
                ret = (i2c_gpio_get_scl(r100->i2c) << 9) | (i2c_gpio_get_sda(r100->i2c) << 8);
                break;
            }
        case 0x208:
            {
                ret = svga->vtotal | (svga->dispend << 16);
                break;
            }
    }

    pclog("[RADEON] MMIO read %08x returns value %08x\n", addr, ret);

    return ret;
}

uint8_t
r100_mmio_read(uint32_t addr, void *p)
{
    r100_t *r100 = (r100_t *) p;

    addr &= 0x1fff;

    pclog("[RADEON] MMIO read %08x\n", addr);

    if ((addr >= 0xf00) && (addr <= 0xfff))
        return r100_pci_read(0, addr & 0xff, r100);

    return (r100_mmio_read_l(addr & 0xffffff, r100) >> ((addr & 3) << 3)) & 0xff;
}

uint16_t
r100_mmio_read_w(uint32_t addr, void *p)
{
    r100_t *r100 = (r100_t *) p;

    addr &= 0x1fff;

    pclog("[RADEON] MMIO read %08x\n", addr);

    if ((addr >= 0xf00) && (addr <= 0xfff))
        return r100_pci_read(0, addr & 0xff, r100) | (r100_pci_read(0, (addr + 1) & 0xff, r100) << 8);

    return (r100_mmio_read_l(addr & 0xffffff, r100) >> ((addr & 3) << 3)) & 0xffff;
}

void
r100_mmio_write_l(uint32_t addr, uint32_t val, void *p)
{
    r100_t *r100 = (r100_t *) p;

    addr &= 0x1fff;

    if ((addr >= 0xf00) && (addr <= 0xfff)) {
        r100_pci_write(0, addr & 0xff, val, r100);
        r100_pci_write(0, (addr + 1) & 0xff, val, r100);
        r100_pci_write(0, (addr + 2) & 0xff, val, r100);
        r100_pci_write(0, (addr + 3) & 0xff, val, r100);
    }

    switch (addr & 0x1ffc) {
        case 0x60:
            {
                int scl = (val >> 17) & 1 ? (val >> 1) & 1 : 1;
                int sda = (val >> 16) & 1 ? (val >> 0) & 1 : 1;
                i2c_gpio_set(r100->i2c, scl, sda);
                break;
            }
    }

    pclog("[RADEON] MMIO write %08x %08x\n", addr, val);
}

void
r100_mmio_write(uint32_t addr, uint8_t val, void *p)
{
    uint32_t tmp;

    addr &= 0x1fff;

    pclog("[RADEON] MMIO write %08x %02x\n", addr, val);

    if ((addr >= 0xf00) && (addr <= 0xfff)) {
        r100_pci_write(0, addr & 0xff, val, p);
        return;
    }

    tmp = r100_mmio_read_l(addr, p);
    tmp &= ~(0xff << ((addr & 3) << 3));
    tmp |= val << ((addr & 3) << 3);
    r100_mmio_write_l(addr, tmp, p);
}

void
r100_mmio_write_w(uint32_t addr, uint16_t val, void *p)
{
    uint32_t tmp;

    addr &= 0x1fff;

    pclog("[RADEON] MMIO write %08x %04x\n", addr, val);

    if ((addr >= 0xf00) && (addr <= 0xfff)) {
        r100_pci_write(0, addr & 0xff, val, p);
        r100_pci_write(0, (addr + 1) & 0xff, val, p);
        return;
    }

    tmp = r100_mmio_read_l(addr, p);
    tmp &= ~(0xffff << ((addr & 3) << 3));
    tmp |= val << ((addr & 3) << 3);
    r100_mmio_write_l(addr, tmp, p);
}

static void
r100_out(uint16_t addr, uint8_t val, void *p)
{
    r100_t *r100 = (r100_t *) p;
    svga_t *svga = &r100->svga;
    uint8_t old;

    if (((addr & 0xfff0) == 0x3d0 || (addr & 0xfff0) == 0x3b0) && !(svga->miscout & 1))
        addr ^= 0x60;

    if (((addr >= r100->io_base) && (addr < (r100->io_base + 0x100))) || ((addr >= 0xd000) && (addr < 0xd100))) {
        if ((addr & 0xff) != 0x16 && (addr & 0xff) != 0x17)
            pclog("[RADEON] IO write %04x %02x\n", addr, val);
        r100->io_regs[addr & 0xff] = val;
        switch (addr & 0xff) {
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
                {
                    uint32_t mm_addr = r100->io_regs[0] | (r100->io_regs[1] << 8) | (r100->io_regs[2] << 16) | ((r100->io_regs[3] & 3) << 24);
                    if (r100->io_regs[3] & (1 << 7))
                        svga_writeb_linear(mm_addr + (addr & 3), val, svga);
                    else
                        r100_mmio_write((mm_addr & 0x1fff) + (addr & 3), val, r100);
                    break;
                }
        }
    }

    switch (addr) {
        case 0x3D4:
            svga->crtcreg = val & 0x3f;
            return;
        case 0x3D5:
            if ((svga->crtcreg < 7) && (svga->crtc[0x11] & 0x80))
                return;
            if ((svga->crtcreg == 7) && (svga->crtc[0x11] & 0x80))
                val = (svga->crtc[7] & ~0x10) | (val & 0x10);
            old                       = svga->crtc[svga->crtcreg];
            svga->crtc[svga->crtcreg] = val;
            if (svga->crtcreg > 0x18)
                pclog("RADEON Extended CRTC write %02X %02x\n", svga->crtcreg, val);
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
r100_in(uint16_t addr, void *p)
{
    r100_t *r100 = (r100_t *) p;
    svga_t *svga = &r100->svga;
    uint8_t temp;

    if (((addr & 0xFFF0) == 0x3D0 || (addr & 0xFFF0) == 0x3B0) && !(svga->miscout & 1))
        addr ^= 0x60;

    if (((addr >= r100->io_base) && (addr < (r100->io_base + 0x100))) || ((addr >= 0xd000) && (addr < 0xd100))) {
        if ((addr & 0xff) != 0x16 && (addr & 0xff) != 0x17)
            pclog("[RADEON] IO read %04x\n", addr);
        temp = r100->io_regs[addr & 0xff];
        switch (addr & 0xff) {
            case 0x04:
            case 0x05:
            case 0x06:
            case 0x07:
                {
                    uint32_t mm_addr = r100->io_regs[0] | (r100->io_regs[1] << 8) | (r100->io_regs[2] << 16) | ((r100->io_regs[3] & 3) << 24);
                    if (r100->io_regs[3] & (1 << 7))
                        temp = svga_readb_linear(mm_addr + (addr & 3), svga);
                    else
                        temp = r100_mmio_read((mm_addr & 0x1fff) + (addr & 3), r100);
                    break;
                }
            case 0xf8:
            case 0xf9:
            case 0xfa:
                temp = 0;
                break;
            case 0xfb:
                temp = 0x02;
                break;
        }
    }

    switch (addr) {
        case 0x3D4:
            temp = svga->crtcreg;
            break;
        default:
            temp = svga_in(addr, svga);
            break;
    }

    return temp;
}

static void
r100_recalctimings(svga_t *svga)
{
    // TODO
}

static void
    *
    r100_init(const device_t *info)
{
    r100_t *r100 = malloc(sizeof(r100_t));
    svga_t *svga;
    char   *romfn = BIOS_R100_PATH;
    memset(r100, 0, sizeof(r100_t));
    svga = &r100->svga;

    r100->vram_size = device_get_config_int("memory") << 20;
    r100->vram_mask = r100->vram_size - 1;

    svga_init(info, &r100->svga, r100, r100->vram_size,
              r100_recalctimings, r100_in, r100_out,
              NULL, NULL);

    svga->decode_mask = r100->vram_mask;

    rom_init(&r100->bios_rom, romfn, 0xc0000, 0xb000, 0xffff, 0, MEM_MAPPING_EXTERNAL);
    mem_mapping_disable(&r100->bios_rom.mapping);

    mem_mapping_add(&r100->mmio_mapping, 0, 0, r100_mmio_read, r100_mmio_read_w, r100_mmio_read_l, r100_mmio_write, r100_mmio_write_w, r100_mmio_write_l, NULL, MEM_MAPPING_EXTERNAL, r100);
    mem_mapping_disable(&r100->mmio_mapping);
    mem_mapping_add(&r100->linear_mapping, 0, 0, svga_read_linear, svga_readw_linear, svga_readl_linear, svga_write_linear, svga_writew_linear, svga_writel_linear, NULL, MEM_MAPPING_EXTERNAL, &r100->svga);
    mem_mapping_disable(&r100->linear_mapping);

    pci_add_card(PCI_ADD_AGP, r100_pci_read, r100_pci_write, r100, &r100->pci_slot);

    r100->pci_regs[0x30] = 0x00;
    r100->pci_regs[0x32] = 0x0c;
    r100->pci_regs[0x33] = 0x00;

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, &timing_r100);

    r100->i2c = i2c_gpio_init("ddc_r100");
    r100->ddc = ddc_init(i2c_gpio_get_bus(r100->i2c));

    // Undocumented mirror
    io_sethandler(0xd000, 0x0100, r100_in, NULL, NULL, r100_out, NULL, NULL, r100);

    return r100;
}

static int
r100_available(void)
{
    return rom_present(BIOS_R100_PATH);
}

void
r100_close(void *p)
{
    r100_t *r100 = (r100_t *) p;

    svga_close(&r100->svga);

    free(r100);
}

void
r100_speed_changed(void *p)
{
    r100_t *r100 = (r100_t *) p;

    svga_recalctimings(&r100->svga);
}

void
r100_force_redraw(void *p)
{
    r100_t *r100 = (r100_t *) p;

    r100->svga.fullchange = changeframecount;
}

static const device_config_t r100_config[] = {
  // clang-format off
    {
        .name = "memory",
        .description = "Memory size",
        .type = CONFIG_SELECTION,
        .default_int = 64,
        .selection = {
            {
                .description = "64 MB",
                .value = 64
            },
            {
                .description = ""
            }
        }
    },
    {
        .type = CONFIG_END
    }
  // clang-format on
};

const device_t ati_r100_device = {
    .name          = "ATi Radeon 7200 SDR",
    .internal_name = "radeon_r100",
    .flags         = DEVICE_AGP,
    .local         = 0,
    .init          = r100_init,
    .close         = r100_close,
    .reset         = NULL,
    .available = r100_available,
    .speed_changed = r100_speed_changed,
    .force_redraw  = r100_force_redraw,
    .config        = r100_config
};
