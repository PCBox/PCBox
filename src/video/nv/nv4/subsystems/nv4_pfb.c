/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PFB (memory interface) and PEXTDEV
 *          (the power-on straps).
 *
 *          PFB_BOOT_0 holds the memory size, width and type. The video
 *          BIOS sizes memory by programming a configuration, writing a
 *          pattern and reading it back; on real boards a configuration
 *          larger than the fitted chips mangles the pattern. Here the
 *          RAM_AMOUNT and RAM_WIDTH fields never go above what the board
 *          has, which leads the BIOS to the same result. The timing and
 *          tiling registers are stored; no timing is modeled.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/nv/vid_nv4.h>

/* RAM_TYPE (5:3) */
#define NV4_RAM_TYPE_SGRAM_16MBIT 1
#define NV4_RAM_TYPE_SDRAM_16MBIT 3
#define NV4_RAM_TYPE_SDRAM_64MBIT 4

static uint32_t
nv4_ram_amount_mb(uint32_t code)
{
    static const uint32_t mb[4] = { 32, 4, 8, 16 };
    return mb[code & 3];
}

/* PFB_BOOT_0 with RAM_AMOUNT and RAM_WIDTH held to the fitted memory. */
static uint32_t
nv4_pfb_clamp_boot_0(const nv4_t *nv4, uint32_t val)
{
    if (nv4_ram_amount_mb(val) > (nv4->vram_size >> 20))
        val = (val & ~3u) | nv4_vram_size_code(nv4);
    if (nv4->board->mem_64bit)
        val &= ~0x04u;
    return val;
}

/* The straps: SUB_VENDOR (subsystem ids from the ROM), the RAM type and
   width, the bus, the crystal, and TV out disabled. AGP boards also
   strap the 66 MHz bus.

   The TNT2 and Vanta moved them about (their datasheets' section 12,
   "Power-on reset configuration"): bit 0 the PCI address pinout (1:
   normal), bits 5:2 a memory configuration the video BIOS looks up in a
   table of PFB_BOOT_0 values, bits 10:9 the AGP modes exposed (bit 9 4x,
   bit 10 sideband; the AGP status register follows them), bits 13:12
   the low device id bits, bit 14 a 32 MB BAR1 (every board here decodes
   32 MB) and bit 15 strapped to VDD. The ELSA ERAZOR III BIOSes' table
   starts with 32 MB SDRAM at 128 bits (0) and at 64 bits (1);
   PFB_BOOT_0 is held to the fitted memory, and their probe then finds
   it. The Vanta has no sideband (bit 10 reserved, to GND). */
uint32_t
nv4_straps(const nv4_t *nv4)
{
    uint32_t straps = 0x02 | (3u << 7);

    /* The NV10's (nv10_ref.h, PEXTDEV_BOOT_0): bit 0 the PCI address
       pinout, 1 SUB_VENDOR, 5:2 the memory configuration the video BIOS
       looks up, 6 the crystal, 8:7 TV mode (3: none), 9 AGP 4x disabled,
       10 sideband disabled, 11 fast writes disabled, 13:12 the low device
       id bits, 14 the bus (1 AGP), 15 a 12-bit flat panel interface. The
       AGP bits are active low: an AGP board offers all three. */
    if (NV4_IS_NV10(nv4)) {
        straps = 0x03 | (3u << 7) | ((nv4->board->device_id & 3) << 12);
        if (nv4->board->ddr)
            straps |= 1u << 2;
        if (nv4->board->crystal_14m)
            straps |= 0x40;
        if (nv4->is_agp)
            straps |= 1u << 14;
        else
            straps |= 7u << 9;
        return straps;
    }

    if (nv4->chip == NV4_CHIP_TNT2) {
        straps |= 0x01 | (1u << 14) | (1u << 15);
        if (nv4->board->mem_64bit)
            straps |= 1u << 2;
        if (nv4->board->crystal_14m)
            straps |= 0x40;
        if (nv4->is_agp)
            straps |= (nv4->board->device_id == NV4_PCI_DEVICE_VANTA) ? (1u << 9) : (3u << 9);
        straps |= (nv4->board->device_id & 3) << 12;
        return straps;
    }

    if (nv4->is_agp)
        straps |= 0x01 | 0x20;
    if (!nv4->board->mem_64bit)
        straps |= 0x10;
    if (!nv4->board->sgram)
        straps |= 0x0c; /* 1024K x 2 banks */
    if (nv4->board->crystal_14m)
        straps |= 0x40;
    return straps;
}

/* The straps as software sees them: the NV4 and NV5 override bits
   10:0 with OVERWRITE (bit 11), the NV10 bits 30:0 with bit 31. */
uint32_t
nv4_straps_now(const nv4_t *nv4)
{
    if (NV4_IS_NV10(nv4))
        return (nv4->straps_ovr & 0x80000000) ? nv4->straps_ovr : nv4_straps(nv4);
    return (nv4->straps_ovr & 0x800) ? nv4->straps_ovr : nv4_straps(nv4);
}

double
nv4_crystal_hz(const nv4_t *nv4)
{
    return (nv4_straps_now(nv4) & 0x40) ? 14318180.0 : 13500000.0;
}

void
nv4_pfb_reset(nv4_t *nv4)
{
    uint32_t type;

    memset(nv4->pfb_regs, 0, sizeof(nv4->pfb_regs));
    if (NV4_IS_NV10(nv4)) {
        /* CFG: SDR (0) or DDR (1) type, 128-bit bus; the BIOS's init
           script programs the rest */
        nv4->pfb_regs[(NV10_PFB_CFG - NV4_PFB_START) >> 2] = (nv4->board->ddr ? 1 : 0) | (1u << 4);
        nv4->pfb_debug_0 = 0x20001000;
        nv4->straps_ovr  = 0;
        return;
    }

    if (nv4->board->sgram)
        type = NV4_RAM_TYPE_SGRAM_16MBIT;
    else
        type = (nv4->chip == NV4_CHIP_TNT2) ? NV4_RAM_TYPE_SDRAM_64MBIT : NV4_RAM_TYPE_SDRAM_16MBIT;

    nv4->pfb_boot_0   = nv4_pfb_clamp_boot_0(nv4, nv4_vram_size_code(nv4) | 0x04 | (type << 3));
    nv4->pfb_debug_0  = 0x20001000;
    nv4->pfb_green_0  = 0x00000003;
    nv4->pfb_config_0 = 0x00001114;
    nv4->pfb_config_1 = 0xc1016293;
    nv4->pfb_rtl      = 0;
    nv4->straps_ovr   = 0;
}

uint32_t
nv4_pfb_read(nv4_t *nv4, uint32_t addr)
{
    if (NV4_IS_NV10(nv4)) {
        switch (addr) {
            case NV4_PFB_DEBUG_0:
                return nv4->pfb_debug_0;
            case NV10_PFB_CSTATUS:
                /* RAM_AMOUNT 27:20 in MB: the resman reads the register
                   as the framebuffer's size in bytes */
                return nv4->vram_size & 0x0ff00000;
            default:
                return nv4->pfb_regs[((addr - NV4_PFB_START) >> 2) & 0x3ff];
        }
    }
    switch (addr) {
        case NV4_PFB_BOOT_0:
            return nv4->pfb_boot_0;
        case NV4_PFB_DEBUG_0:
            return nv4->pfb_debug_0;
        case NV4_PFB_GREEN_0:
            return nv4->pfb_green_0;
        case NV4_PFB_CONFIG_0:
            return nv4->pfb_config_0;
        case NV4_PFB_CONFIG_1:
            return nv4->pfb_config_1;
        case NV4_PFB_RTL:
            return nv4->pfb_rtl;
        default:
            nv4_log("NV4 PFB: read %06x\n", addr);
            return 0;
    }
}

void
nv4_pfb_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    if (NV4_IS_NV10(nv4)) {
        switch (addr) {
            case NV4_PFB_DEBUG_0:
                nv4->pfb_debug_0 = val;
                break;
            case NV10_PFB_CSTATUS:
                break;
            default:
                nv4->pfb_regs[((addr - NV4_PFB_START) >> 2) & 0x3ff] = val;
                break;
        }
        return;
    }
    switch (addr) {
        case NV4_PFB_BOOT_0:
            nv4->pfb_boot_0 = nv4_pfb_clamp_boot_0(nv4, val & 0xf13f);
            nv4_log("NV4 PFB: BOOT_0 = %08x -> %08x\n", val, nv4->pfb_boot_0);
            break;
        case NV4_PFB_DEBUG_0:
            nv4->pfb_debug_0 = val;
            break;
        case NV4_PFB_GREEN_0:
            nv4->pfb_green_0 = val & 3;
            break;
        case NV4_PFB_CONFIG_0:
            nv4->pfb_config_0 = val;
            break;
        case NV4_PFB_CONFIG_1:
            nv4->pfb_config_1 = val;
            break;
        case NV4_PFB_RTL:
            nv4->pfb_rtl = val & 0x1f;
            break;
        default:
            nv4_log("NV4 PFB: write %06x = %08x\n", addr, val);
            break;
    }
}

/* PEXTDEV_BOOT_0: the straps, unless software set OVERWRITE (bit 11),
   in which case bits 10:0 read back as written. The video BIOS sets it
   to pick the AGP mode. */
uint32_t
nv4_pextdev_read(nv4_t *nv4, uint32_t addr)
{
    if (addr == NV4_PEXTDEV_BOOT_0)
        return nv4_straps_now(nv4);
    return 0;
}

void
nv4_pextdev_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    if ((addr == NV4_PEXTDEV_BOOT_0) && NV4_IS_NV10(nv4)) {
        nv4->straps_ovr = (val & 0x80000000) ? val : 0;
        return;
    }
    if (addr == NV4_PEXTDEV_BOOT_0)
        /* bits 15:12 are not overwritten */
        nv4->straps_ovr = (val & 0x800) ? ((val & 0xfff) | (nv4_straps(nv4) & 0xf000)) : 0;
}
