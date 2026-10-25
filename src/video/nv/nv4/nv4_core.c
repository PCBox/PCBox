/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- device shell.
 *
 *          PCI configuration space, the two BARs (16 MB of registers and
 *          the linear framebuffer), the expansion ROM, the dispatch that
 *          hands each register offset to the unit that owns it, chip
 *          reset, and the board definitions.
 *
 *          Public references: the RIVA TNT datasheet (configuration
 *          registers and straps), envytools (register map, rnndb),
 *          Linux rivafb and nouveau, the XFree86 nv driver.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#define HAVE_STDARG_H
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/i2c.h>
#include <86box/vid_ddc.h>
#include <86box/plat_unused.h>
#include <86box/nv/vid_nv4.h>

/* CPU access to the framebuffer sees the 3D engine's pixels: the work
   that reaches the <n> bytes at VRAM offset <a> is drawn and waited for */
#define NV4_3D_ACCESS(nv4, a, n) nv4_3d_access(nv4, a, (a) + (n) - 1, NV4_SYNC_LFB)

#ifdef ENABLE_NV4_LOG
int nv4_do_log = ENABLE_NV4_LOG;

void
nv4_log(const char *fmt, ...)
{
    va_list ap;

    if (nv4_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#endif

#define NV4_ROM_WINDOW 0x10000 /* the chip decodes 64 KB of ROM */

static video_timings_t timing_nv4_pci = { .type = VIDEO_PCI, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };
static video_timings_t timing_nv4_agp = { .type = VIDEO_AGP, .write_b = 2, .write_w = 2, .write_l = 1, .read_b = 20, .read_w = 20, .read_l = 21 };

/* ------------------------------------------------------------------ */
/* Video BIOS.                                                         */
/* ------------------------------------------------------------------ */
/* The image read through the expansion ROM BAR and PROM. Once the BIOS
   has copied itself into instance memory and set PCI_NV_20 ROM_SHADOW,
   ROM reads come from the first 64 KB of instance memory instead (the
   BIOS does this so that TV modes, which reuse the ROM pins, still read
   a ROM). */
uint8_t
nv4_rom_read(nv4_t *nv4, uint32_t off)
{
    off &= NV4_ROM_WINDOW - 1;
    if (nv4->pbus_pci_nv20 & 1)
        return nv4_ramin_read8(nv4, off);
    return nv4->bios_rom.rom ? nv4->bios_rom.rom[off] : 0xff;
}

static uint8_t
nv4_rom_readb(uint32_t addr, void *priv)
{
    return nv4_rom_read((nv4_t *) priv, addr);
}

static uint16_t
nv4_rom_readw(uint32_t addr, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    return nv4_rom_read(nv4, addr) | (nv4_rom_read(nv4, addr + 1) << 8);
}

static uint32_t
nv4_rom_readl(uint32_t addr, void *priv)
{
    return nv4_rom_readw(addr, priv) | ((uint32_t) nv4_rom_readw(addr + 2, priv) << 16);
}

static int
nv4_rom_load(nv4_t *nv4, const char *path)
{
    FILE *fp = rom_fopen(path, "rb");

    nv4->bios_rom.rom = malloc(NV4_ROM_WINDOW);
    memset(nv4->bios_rom.rom, 0xff, NV4_ROM_WINDOW);
    nv4->bios_rom.sz   = NV4_ROM_WINDOW;
    nv4->bios_rom.mask = NV4_ROM_WINDOW - 1;
    if (!fp)
        return 0;
    (void) !fread(nv4->bios_rom.rom, 1, NV4_ROM_WINDOW, fp);
    fclose(fp);
    return 1;
}

/* ------------------------------------------------------------------ */
/* Memory size.                                                        */
/* ------------------------------------------------------------------ */
/* PFB_BOOT_0 RAM_AMOUNT code for the fitted memory. */
uint32_t
nv4_vram_size_code(const nv4_t *nv4)
{
    switch (nv4->vram_size >> 20) {
        case 4:
            return NV4_PFB_RAM_4MB;
        case 8:
            return NV4_PFB_RAM_8MB;
        case 16:
            return NV4_PFB_RAM_16MB;
        default:
            return NV4_PFB_RAM_32MB;
    }
}

/* ------------------------------------------------------------------ */
/* Interrupt line.                                                     */
/* ------------------------------------------------------------------ */
void
nv4_pmc_update_irq(nv4_t *nv4)
{
    uint32_t pending = nv4_pmc_pending(nv4);
    int      assert  = 0;

    /* INTR_EN_0 INTA: 1 passes the hardware sources, 2 the software
       bit, 3 both. */
    if ((nv4->pmc_intr_en & 1) && (pending & ~NV4_PMC_INTR_SOFTWARE))
        assert = 1;
    if ((nv4->pmc_intr_en & 2) && (pending & NV4_PMC_INTR_SOFTWARE))
        assert = 1;

    if (assert)
        pci_set_irq(nv4->pci_slot, PCI_INTA, &nv4->irq_state);
    else
        pci_clear_irq(nv4->pci_slot, PCI_INTA, &nv4->irq_state);
}

/* ------------------------------------------------------------------ */
/* Register dispatch.                                                  */
/* ------------------------------------------------------------------ */
/* The units whose registers are bytes: the VGA port windows, the VGA
   memory window, the ROM and the PCI configuration mirror. */
static int
nv4_mmio_byte_wide(uint32_t addr)
{
    return ((addr >= NV4_PRMVGA_START) && (addr <= NV4_PRMVIO_END))
        || ((addr >= NV4_PRMCIO_START) && (addr <= NV4_PRMCIO_END))
        || ((addr >= NV4_PRMDIO_START) && (addr <= NV4_PRMDIO_END))
        || ((addr >= NV4_PROM_START) && (addr <= NV4_PROM_END))
        || ((addr >= NV4_PBUS_PCI_START) && (addr <= NV4_PBUS_PCI_END));
}

static uint8_t
nv4_mmio_read8_bytewide(nv4_t *nv4, uint32_t addr)
{
    if (addr >= NV4_PRMVGA_START && addr <= NV4_PRMVGA_END) {
        nv4_vga_mem_access(nv4, addr - NV4_PRMVGA_START + 0xa0000, 1, 0);
        return svga_read(addr - NV4_PRMVGA_START + 0xa0000, &nv4->svga);
    }
    if (addr >= NV4_PRMVIO_START && addr <= NV4_PRMVIO_END)
        return nv4_vga_in((addr & 0xffff) & 0x3ff, nv4);
    if (addr >= NV4_PRMCIO_START && addr <= NV4_PRMCIO_END)
        return nv4_vga_in((addr & 0xfff) & 0x3ff, nv4);
    if (addr >= NV4_PRMDIO_START && addr <= NV4_PRMDIO_END)
        return nv4_vga_in((addr & 0xfff) & 0x3ff, nv4);
    if (addr >= NV4_PROM_START && addr <= NV4_PROM_END)
        return nv4_rom_read(nv4, addr - NV4_PROM_START);
    return nv4_pci_cfg_read(nv4, addr & 0xff);
}

static void
nv4_mmio_write8_bytewide(nv4_t *nv4, uint32_t addr, uint8_t val)
{
    if (addr >= NV4_PRMVGA_START && addr <= NV4_PRMVGA_END) {
        nv4_vga_mem_access(nv4, addr - NV4_PRMVGA_START + 0xa0000, 1, 1);
        svga_write(addr - NV4_PRMVGA_START + 0xa0000, val, &nv4->svga);
    } else if (addr >= NV4_PRMVIO_START && addr <= NV4_PRMVIO_END)
        nv4_vga_out((addr & 0xffff) & 0x3ff, val, nv4);
    else if (addr >= NV4_PRMCIO_START && addr <= NV4_PRMCIO_END)
        nv4_vga_out((addr & 0xfff) & 0x3ff, val, nv4);
    else if (addr >= NV4_PRMDIO_START && addr <= NV4_PRMDIO_END)
        nv4_vga_out((addr & 0xfff) & 0x3ff, val, nv4);
    else if (addr >= NV4_PBUS_PCI_START && addr <= NV4_PBUS_PCI_END)
        nv4_pci_cfg_write(nv4, addr & 0xff, val);
    /* PROM is read-only */
}

/* A 32-bit register by its dword address. */
static uint32_t
nv4_reg_read32(nv4_t *nv4, uint32_t addr)
{
    if (addr < 0x1000)
        return nv4_pmc_read(nv4, addr);
    if (addr < 0x2000)
        return nv4_pbus_read(nv4, addr);
    if (addr <= NV4_PFIFO_END)
        return nv4_pfifo_read(nv4, addr);
    if (addr >= NV4_PRMIO_START && addr <= NV4_PRMIO_END)
        return nv4_prmio_read(nv4, addr);
    if (addr >= NV4_PTIMER_START && addr <= NV4_PTIMER_END)
        return nv4_ptimer_read(nv4, addr);
    if (addr >= NV4_PFB_START && addr <= NV4_PFB_END)
        return nv4_pfb_read(nv4, addr);
    if (addr >= NV4_PEXTDEV_START && addr <= NV4_PEXTDEV_END)
        return nv4_pextdev_read(nv4, addr);
    if (addr >= NV4_PME_START && addr <= NV4_PME_END)
        return nv4_pme_read(nv4, addr);
    if (addr >= NV4_PGRAPH_START && addr <= NV4_PGRAPH_END)
        return nv4_pgraph_read(nv4, addr);
    if (addr >= NV4_PCRTC_START && addr <= NV4_PCRTC_END)
        return nv4_pcrtc_read(nv4, addr);
    if (addr >= NV4_PVIDEO_START && addr <= NV4_PVIDEO_END)
        return nv4_pvideo_read(nv4, addr);
    if (addr >= NV4_PRAMDAC_START && addr <= NV4_PRAMDAC_END)
        return nv4_pramdac_read(nv4, addr);
    if (addr >= NV4_PRAMIN_START && addr <= NV4_PRAMIN_END)
        return nv4_ramin_read32(nv4, addr - NV4_PRAMIN_START);
    if (addr >= NV4_USER_START)
        return nv4_user_read(nv4, addr);

    nv4_log("NV4: read from unmapped register %06x\n", addr);
    return 0;
}

static void
nv4_reg_write32(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    if (addr < 0x1000)
        nv4_pmc_write(nv4, addr, val);
    else if (addr < 0x2000)
        nv4_pbus_write(nv4, addr, val);
    else if (addr <= NV4_PFIFO_END)
        nv4_pfifo_write(nv4, addr, val);
    else if (addr >= NV4_PRMIO_START && addr <= NV4_PRMIO_END)
        nv4_prmio_write(nv4, addr, val);
    else if (addr >= NV4_PTIMER_START && addr <= NV4_PTIMER_END)
        nv4_ptimer_write(nv4, addr, val);
    else if (addr >= NV4_PFB_START && addr <= NV4_PFB_END)
        nv4_pfb_write(nv4, addr, val);
    else if (addr >= NV4_PEXTDEV_START && addr <= NV4_PEXTDEV_END)
        nv4_pextdev_write(nv4, addr, val);
    else if (addr >= NV4_PME_START && addr <= NV4_PME_END)
        nv4_pme_write(nv4, addr, val);
    else if (addr >= NV4_PGRAPH_START && addr <= NV4_PGRAPH_END)
        nv4_pgraph_write(nv4, addr, val);
    else if (addr >= NV4_PCRTC_START && addr <= NV4_PCRTC_END)
        nv4_pcrtc_write(nv4, addr, val);
    else if (addr >= NV4_PVIDEO_START && addr <= NV4_PVIDEO_END)
        nv4_pvideo_write(nv4, addr, val);
    else if (addr >= NV4_PRAMDAC_START && addr <= NV4_PRAMDAC_END)
        nv4_pramdac_write(nv4, addr, val);
    else if (addr >= NV4_PRAMIN_START && addr <= NV4_PRAMIN_END)
        nv4_ramin_write32(nv4, addr - NV4_PRAMIN_START, val);
    else if (addr >= NV4_USER_START)
        nv4_user_write(nv4, addr, val);
    else
        nv4_log("NV4: write to unmapped register %06x = %08x\n", addr, val);
}

uint32_t
nv4_mmio_read(nv4_t *nv4, uint32_t addr, int len)
{
    addr &= NV4_MMIO_SIZE - 1;

    /* the registers and the channels' areas, not instance memory */
    if (nv4->dbg_reads_left && ((addr < NV4_PRAMIN_START) || (addr >= NV4_USER_START))) {
        int i;

        for (i = 0; (i < 64) && nv4->dbg_read_count[i] && (nv4->dbg_read_addr[i] != addr); i++)
            ;
        if (i < 64) {
            nv4->dbg_read_addr[i] = addr;
            nv4->dbg_read_count[i]++;
        }
        if (!--nv4->dbg_reads_left) {
            for (i = 0; (i < 64) && nv4->dbg_read_count[i]; i++)
                pclog("nv4 reads: %06x x%u\n", nv4->dbg_read_addr[i], nv4->dbg_read_count[i]);
        }
    }

    if (nv4_mmio_byte_wide(addr)) {
        uint32_t ret = 0;
        for (int i = 0; i < len; i++)
            ret |= (uint32_t) nv4_mmio_read8_bytewide(nv4, addr + i) << (i * 8);
        return ret;
    }

    /* Instance memory takes any width. */
    if (addr >= NV4_PRAMIN_START && addr <= NV4_PRAMIN_END) {
        uint32_t off = addr - NV4_PRAMIN_START;
        if (len == 1)
            return nv4_ramin_read8(nv4, off);
        if (len == 2)
            return nv4_ramin_read16(nv4, off);
        return nv4_ramin_read32(nv4, off);
    }

    /* A narrow read of a 32-bit register picks its bytes out. */
    uint32_t val = nv4_reg_read32(nv4, addr & ~3);
    if (len == 4)
        return val;
    val >>= (addr & 3) * 8;
    return (len == 1) ? (val & 0xff) : (val & 0xffff);
}

void
nv4_mmio_write(nv4_t *nv4, uint32_t addr, uint32_t val, int len)
{
    addr &= NV4_MMIO_SIZE - 1;

    /* the registers, not instance memory or the channels' areas */
    if (nv4->dbg_regtrace && (addr < NV4_PRAMIN_START) && nv4->dbg_regtrace--)
        pclog("nv4 regs: mmio %06x = %08x (%d)\n", addr, val, len);

    if (nv4_mmio_byte_wide(addr)) {
        for (int i = 0; i < len; i++)
            nv4_mmio_write8_bytewide(nv4, addr + i, (val >> (i * 8)) & 0xff);
        return;
    }

    if (addr >= NV4_PRAMIN_START && addr <= NV4_PRAMIN_END) {
        uint32_t off = addr - NV4_PRAMIN_START;
        if (len == 1)
            nv4_ramin_write8(nv4, off, val);
        else if (len == 2)
            nv4_ramin_write16(nv4, off, val);
        else
            nv4_ramin_write32(nv4, off, val);
        return;
    }

    if (len != 4) {
        /* A narrow write to a 32-bit register: merge it into the
           register's current value. */
        uint32_t shift = (addr & 3) * 8;
        uint32_t mask  = ((len == 1) ? 0xffu : 0xffffu) << shift;
        uint32_t cur   = nv4_reg_read32(nv4, addr & ~3);
        val            = (cur & ~mask) | ((val << shift) & mask);
    }
    nv4_reg_write32(nv4, addr & ~3, val);
}

static uint8_t
nv4_mmio_readb(uint32_t addr, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    return nv4_mmio_read(nv4, addr - nv4->mmio_base, 1);
}

static uint16_t
nv4_mmio_readw(uint32_t addr, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    return nv4_mmio_read(nv4, addr - nv4->mmio_base, 2);
}

static uint32_t
nv4_mmio_readl(uint32_t addr, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    return nv4_mmio_read(nv4, addr - nv4->mmio_base, 4);
}

static void
nv4_mmio_writeb(uint32_t addr, uint8_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    nv4_mmio_write(nv4, addr - nv4->mmio_base, val, 1);
}

static void
nv4_mmio_writew(uint32_t addr, uint16_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    nv4_mmio_write(nv4, addr - nv4->mmio_base, val, 2);
}

static void
nv4_mmio_writel(uint32_t addr, uint32_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    nv4_mmio_write(nv4, addr - nv4->mmio_base, val, 4);
}

/* ------------------------------------------------------------------ */
/* Linear framebuffer (BAR1).                                          */
/* ------------------------------------------------------------------ */
/* The fitted VRAM repeats through the 16 MB (NV4) or 32 MB (NV5)
   aperture. */
static uint8_t
nv4_lfb_readb(uint32_t addr, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    addr       = (addr - nv4->lfb_base) & nv4->vram_mask;
    NV4_3D_ACCESS(nv4, addr, 1);
    return nv4->svga.vram[addr];
}

static uint16_t
nv4_lfb_readw(uint32_t addr, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    addr       = (addr - nv4->lfb_base) & nv4->vram_mask & ~1;
    NV4_3D_ACCESS(nv4, addr, 2);
    return *(uint16_t *) &nv4->svga.vram[addr];
}

static uint32_t
nv4_lfb_readl(uint32_t addr, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    addr       = (addr - nv4->lfb_base) & nv4->vram_mask & ~3;
    NV4_3D_ACCESS(nv4, addr, 4);
    return *(uint32_t *) &nv4->svga.vram[addr];
}

static void
nv4_lfb_writeb(uint32_t addr, uint8_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    addr       = (addr - nv4->lfb_base) & nv4->vram_mask;
    NV4_3D_ACCESS(nv4, addr, 1);
    nv4->svga.vram[addr]                                    = val;
    nv4->svga.changedvram[addr >> 12] = nv4->svga.monitor->mon_changeframecount;
}

static void
nv4_lfb_writew(uint32_t addr, uint16_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    addr       = (addr - nv4->lfb_base) & nv4->vram_mask & ~1;
    NV4_3D_ACCESS(nv4, addr, 2);
    *(uint16_t *) &nv4->svga.vram[addr] = val;
    nv4->svga.changedvram[addr >> 12]   = nv4->svga.monitor->mon_changeframecount;
}

static void
nv4_lfb_writel(uint32_t addr, uint32_t val, void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    addr       = (addr - nv4->lfb_base) & nv4->vram_mask & ~3;
    NV4_3D_ACCESS(nv4, addr, 4);
    *(uint32_t *) &nv4->svga.vram[addr] = val;
    nv4->svga.changedvram[addr >> 12]   = nv4->svga.monitor->mon_changeframecount;
}

/* ------------------------------------------------------------------ */
/* PCI configuration space.                                            */
/* ------------------------------------------------------------------ */
#define NV4_CAP_PM  0x60
#define NV4_CAP_AGP 0x44

static void
nv4_update_mappings(nv4_t *nv4)
{
    if (!(nv4->pci_regs[PCI_REG_COMMAND] & PCI_COMMAND_MEM)) {
        mem_mapping_disable(&nv4->mmio_mapping);
        mem_mapping_disable(&nv4->lfb_mapping);
        mem_mapping_disable(&nv4->bios_rom.mapping);
        nv4_vga_update_mapping(nv4);
        return;
    }

    if (nv4->mmio_base)
        mem_mapping_set_addr(&nv4->mmio_mapping, nv4->mmio_base, NV4_MMIO_SIZE);
    else
        mem_mapping_disable(&nv4->mmio_mapping);

    if (nv4->lfb_base)
        mem_mapping_set_addr(&nv4->lfb_mapping, nv4->lfb_base, nv4->lfb_size);
    else
        mem_mapping_disable(&nv4->lfb_mapping);

    /* The ROM BAR decodes only with both MEMORY_SPACE and ROM_DECODE
       set (the datasheet's expansion ROM register). */
    if (nv4->pci_regs[0x30] & 0x01) {
        uint32_t base = ((uint32_t) nv4->pci_regs[0x33] << 24) | ((uint32_t) (nv4->pci_regs[0x32] & 0xc0) << 16);
        mem_mapping_set_addr(&nv4->bios_rom.mapping, base, NV4_ROM_WINDOW);
    } else
        mem_mapping_disable(&nv4->bios_rom.mapping);

    nv4_vga_update_mapping(nv4);
}

static uint32_t
nv4_agp_status(const nv4_t *nv4)
{
    /* RQ 32 deep, sideband addressing, 1x/2x (TNT) or 1x/2x/4x (TNT2) */
    return (0x1fu << 24) | (1u << 9) | ((nv4->chip == NV4_CHIP_TNT2) ? 0x07 : 0x03);
}

uint8_t
nv4_pci_cfg_read(nv4_t *nv4, int addr)
{
    switch (addr) {
        case 0x00:
            return NV4_PCI_VENDOR & 0xff;
        case 0x01:
            return NV4_PCI_VENDOR >> 8;
        case 0x02:
            return nv4->board->device_id & 0xff;
        case 0x03:
            return nv4->board->device_id >> 8;
        case PCI_REG_COMMAND:
            return nv4->pci_regs[PCI_REG_COMMAND];
        case PCI_REG_COMMAND_H:
            return nv4->pci_regs[PCI_REG_COMMAND_H];
        case 0x06:
            /* CAP_LIST, and 66MHZ on the AGP boards */
            return 0x10 | (nv4->is_agp ? 0x20 : 0x00);
        case 0x07:
            return 0x02 | (nv4->pci_regs[0x07] & 0x38); /* medium DEVSEL, abort flags */
        case 0x08:
            return nv4->board->revision;
        case 0x09:
        case 0x0a:
            return 0x00;
        case 0x0b:
            return 0x03; /* VGA compatible display controller */
        case 0x0c:
            return 0x00;
        case 0x0d:
            return nv4->pci_regs[0x0d] & 0xf8; /* latency timer 15:11 */
        case 0x0e:
            return 0x00;
        case 0x10:
            return 0x00; /* BAR0: registers, 16 MB, not prefetchable */
        case 0x11:
        case 0x12:
            return 0x00;
        case 0x13:
            return nv4->mmio_base >> 24;
        case 0x14:
            return 0x08; /* BAR1: framebuffer, prefetchable */
        case 0x15:
            return 0x00;
        case 0x16:
            return (nv4->lfb_base >> 16) & 0xff;
        case 0x17:
            return nv4->lfb_base >> 24;
        case 0x2c:
            return nv4->subsys_vendor & 0xff;
        case 0x2d:
            return nv4->subsys_vendor >> 8;
        case 0x2e:
            return nv4->subsys_id & 0xff;
        case 0x2f:
            return nv4->subsys_id >> 8;
        case 0x30:
            return nv4->pci_regs[0x30] & 0x01;
        case 0x31:
            return 0x00;
        case 0x32:
            return nv4->pci_regs[0x32] & 0xc0; /* base 31:22 */
        case 0x33:
            return nv4->pci_regs[0x33];
        case 0x34:
            return NV4_CAP_PM;
        case 0x3c:
            return nv4->pci_regs[0x3c];
        case 0x3d:
            return PCI_INTA;
        case 0x3e:
            return 0x03; /* MIN_GNT */
        case 0x3f:
            return 0x01; /* MAX_LAT */

        /* PCI_NV_16: the subsystem ids, writable here for a board whose
           system BIOS supplies them; read back as written */
        case 0x40:
            return nv4->subsys_vendor & 0xff;
        case 0x41:
            return nv4->subsys_vendor >> 8;
        case 0x42:
            return nv4->subsys_id & 0xff;
        case 0x43:
            return nv4->subsys_id >> 8;

        /* AGP capability */
        case 0x44:
            return nv4->is_agp ? 0x02 : 0x00;
        case 0x45:
            return 0x00;
        case 0x46:
            return nv4->is_agp ? ((nv4->chip == NV4_CHIP_TNT2) ? 0x20 : 0x10) : 0x00;
        case 0x47:
            return 0x00;
        case 0x48:
        case 0x49:
        case 0x4a:
        case 0x4b:
            return nv4->is_agp ? (nv4_agp_status(nv4) >> ((addr - 0x48) * 8)) & 0xff : 0x00;
        case 0x4c:
        case 0x4d:
        case 0x4e:
        case 0x4f:
            return nv4->is_agp ? nv4->pci_regs[addr] : 0x00;

        /* PCI_NV_20: ROM shadow */
        case 0x50:
            return nv4->pbus_pci_nv20 & 0x01;
        case 0x51:
        case 0x52:
        case 0x53:
            return 0x00;
        /* PCI_NV_21: VGA decode */
        case 0x54:
            return nv4->pci_regs[0x54] & 0x01;
        /* PCI_NV_22: scratch */
        case 0x58:
        case 0x59:
        case 0x5a:
            return (nv4->pbus_pci_nv22 >> ((addr - 0x58) * 8)) & 0xff;
        case 0x5c:
            return nv4->pbus_pci_nv23 & 0x0f;

        /* Power management capability: version 1, D0 and D3hot; the
           next pointer leads to the AGP capability on an AGP board,
           which is how the resman tells the bus apart. */
        case 0x60:
            return 0x01;
        case 0x61:
            return nv4->is_agp ? NV4_CAP_AGP : 0x00;
        case 0x62:
            return 0x01;
        case 0x63:
            return 0x00;
        case 0x64:
            return nv4->pci_regs[0x64] & 0x03;
        case 0x65:
        case 0x66:
        case 0x67:
            return 0x00;

        default:
            return 0x00;
    }
}

void
nv4_pci_cfg_write(nv4_t *nv4, int addr, uint8_t val)
{
    switch (addr) {
        case PCI_REG_COMMAND:
            nv4->pci_regs[PCI_REG_COMMAND] = val & 0x27;
            nv4_vga_close(nv4);
            nv4_vga_init(nv4);
            nv4_update_mappings(nv4);
            break;
        case PCI_REG_COMMAND_H:
            nv4->pci_regs[PCI_REG_COMMAND_H] = val & 0x01;
            break;
        case 0x07:
            /* the abort and SERR flags clear on a write of 1 */
            nv4->pci_regs[0x07] &= ~(val & 0x38);
            break;
        case 0x0d:
            nv4->pci_regs[0x0d] = val;
            break;
        case 0x13:
            nv4->mmio_base = (uint32_t) val << 24;
            nv4_update_mappings(nv4);
            break;
        case 0x16:
            if (nv4->lfb_size > 0x01000000) {
                nv4->lfb_base = (nv4->lfb_base & 0xff000000) | (((uint32_t) val << 16) & ~(nv4->lfb_size - 1) & 0x00ff0000);
                nv4_update_mappings(nv4);
            }
            break;
        case 0x17:
            nv4->lfb_base = ((nv4->lfb_base & 0x00ff0000) | ((uint32_t) val << 24)) & ~(nv4->lfb_size - 1);
            nv4_update_mappings(nv4);
            break;
        case 0x30:
        case 0x32:
        case 0x33:
            nv4->pci_regs[addr] = val;
            nv4_update_mappings(nv4);
            break;
        case 0x3c:
            nv4->pci_regs[0x3c] = val;
            break;
        case 0x40:
            nv4->subsys_vendor = (nv4->subsys_vendor & 0xff00) | val;
            break;
        case 0x41:
            nv4->subsys_vendor = (nv4->subsys_vendor & 0x00ff) | (val << 8);
            break;
        case 0x42:
            nv4->subsys_id = (nv4->subsys_id & 0xff00) | val;
            break;
        case 0x43:
            nv4->subsys_id = (nv4->subsys_id & 0x00ff) | (val << 8);
            break;
        case 0x4c:
            /* AGP command: DATA_RATE 2:0 */
            if (nv4->is_agp)
                nv4->pci_regs[0x4c] = val & 0x07;
            break;
        case 0x4d:
            /* SBA_ENABLE 9, AGP_ENABLE 8 */
            if (nv4->is_agp)
                nv4->pci_regs[0x4d] = val & 0x03;
            break;
        case 0x4f:
            /* RQ_DEPTH 28:24 */
            if (nv4->is_agp)
                nv4->pci_regs[0x4f] = val & 0x1f;
            break;
        case 0x50:
            nv4->pbus_pci_nv20 = val & 0x01;
            break;
        case 0x54:
            nv4->pci_regs[0x54] = val & 0x01;
            nv4_vga_close(nv4);
            nv4_vga_init(nv4);
            nv4_vga_update_mapping(nv4);
            break;
        case 0x58:
        case 0x59:
        case 0x5a:
            nv4->pbus_pci_nv22 = (nv4->pbus_pci_nv22 & ~(0xffu << ((addr - 0x58) * 8))) | ((uint32_t) val << ((addr - 0x58) * 8));
            break;
        case 0x5c:
            nv4->pbus_pci_nv23 = val & 0x0f;
            break;
        case 0x64:
            /* D0 or D3hot; D1 and D2 are not supported */
            if (((val & 3) == 0) || ((val & 3) == 3))
                nv4->pci_regs[0x64] = val & 0x03;
            break;
        default:
            break;
    }
}

static uint8_t
nv4_pci_read(UNUSED(int func), int addr, UNUSED(int len), void *priv)
{
    return nv4_pci_cfg_read((nv4_t *) priv, addr & 0xff);
}

static void
nv4_pci_write(UNUSED(int func), int addr, UNUSED(int len), uint8_t val, void *priv)
{
    nv4_pci_cfg_write((nv4_t *) priv, addr & 0xff, val);
}

/* ------------------------------------------------------------------ */
/* Lifecycle.                                                          */
/* ------------------------------------------------------------------ */
static void
nv4_reset(void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    /* With the SUB_VENDOR strap set the chip loads the subsystem ids from
       ROM bytes 0x54-0x57 at reset; otherwise they are 0 until the system
       BIOS writes them through PCI_NV_16 (the datasheet's power-on
       configuration). Every board here carries them in its ROM. */
    if ((nv4_straps(nv4) & 0x02) && nv4->bios_rom.rom) {
        const uint8_t *rom = nv4->bios_rom.rom;
        nv4->subsys_vendor = rom[0x54] | (rom[0x55] << 8);
        nv4->subsys_id     = rom[0x56] | (rom[0x57] << 8);
    } else {
        nv4->subsys_vendor = 0;
        nv4->subsys_id     = 0;
    }

    memset(nv4->pci_regs, 0, sizeof(nv4->pci_regs));
    nv4->pci_regs[0x3c] = 0xff;
    nv4->pci_regs[0x54] = 0x01; /* VGA decode on */
    nv4->pci_regs[PCI_REG_COMMAND] = PCI_COMMAND_IO | PCI_COMMAND_MEM;
    nv4->pbus_pci_nv20  = 0;
    nv4->pbus_pci_nv22  = 0x0023d6ce;
    nv4->pbus_pci_nv23  = 0x0f;
    nv4->mmio_base      = 0;
    nv4->lfb_base       = 0;

    nv4_pmc_reset(nv4);
    nv4_pbus_reset(nv4);
    nv4_ptimer_reset(nv4);
    nv4_pfb_reset(nv4);
    nv4_pcrtc_reset(nv4);
    nv4_pramdac_reset(nv4);
    nv4_pfifo_reset(nv4);
    nv4_pgraph_reset(nv4);
    nv4_pvideo_reset(nv4);
    nv4_pme_reset(nv4);
    nv4_vga_reset(nv4);

    nv4_update_mappings(nv4);
    nv4_pmc_update_irq(nv4);
}

/* The debug command file's "dev": what the display and the engines are
   doing. */
static nv4_t *nv4_debug_dev;

static void
nv4_debug_hook(const char *args)
{
    nv4_t       *nv4     = nv4_debug_dev;
    svga_t      *svga;
    nv4_pfifo_t *p;
    uint32_t     nonzero = 0;
    uint32_t     start;
    uint32_t     bytes;

    if (!nv4)
        return;
    if (args && !strncmp(args, "ramht", 5)) {
        /* ramht HANDLE: the hash table's entries for a handle (hex) */
        nv4_pfifo_ramht_dump(nv4, strtoul(args + 5, NULL, 16));
        return;
    }
    if (args && !strncmp(args, "logclass", 8)) {
        /* logclass C: log every method of class C (hex); "logclass" alone stops */
        nv4->dbg_log_cls = args[8] ? (int) strtol(args + 8, NULL, 16) : -1;
        pclog("nv4: logging class %02x\n", nv4->dbg_log_cls & 0xff);
        return;
    }
    if (args && !strncmp(args, "trace", 5)) {
        /* trace N [CH] [novtx]: the next N methods, pushes and user writes
           (of channel CH); novtx: methods and object binds only, without
           the 3D classes' vertex methods */
        char *end, *end2;
        long  ch;

        nv4->dbg_trace_novtx = strstr(args, "novtx") != NULL;
        nv4->dbg_trace       = strtoul(args + 5, &end, 0);
        ch                   = strtol(end, &end2, 0);
        nv4->dbg_trace_ch    = ((end2 != end) && (ch >= 0)) ? (int) ch : -1;
        pclog("nv4: tracing %u methods, channel %d%s\n", nv4->dbg_trace, nv4->dbg_trace_ch,
              nv4->dbg_trace_novtx ? ", no vertices" : "");
        return;
    }
    if (args && !strncmp(args, "reads", 5)) {
        /* reads N: after N register reads, list the registers read */
        memset(nv4->dbg_read_count, 0, sizeof(nv4->dbg_read_count));
        nv4->dbg_reads_left = strtoul(args + 5, NULL, 0);
        pclog("nv4: counting %u register reads\n", nv4->dbg_reads_left);
        return;
    }
    if (args && !strncmp(args, "regs", 4)) {
        /* regs N: log the next N register writes (MMIO and VGA ports) */
        nv4->dbg_regtrace = strtoul(args + 4, NULL, 0);
        pclog("nv4: logging %u register writes\n", nv4->dbg_regtrace);
        return;
    }
    if (args && !strncmp(args, "3d", 2)) {
        nv4_3d_stats(nv4);
        return;
    }
    if (args && !strncmp(args, "dma", 3)) {
        /* dma INST [OFFSET]: a DMA object and where OFFSET lands */
        char    *end;
        uint32_t inst = strtoul(args + 3, &end, 16);
        uint32_t off  = strtoul(end, NULL, 16);
        uint32_t addr = 0;
        int      target = 0;
        int      r      = nv4_dma_translate(nv4, inst, off, 0, &addr, &target);

        pclog("nv4: dma %04x: %08x %08x %08x %08x; offset %08x -> %s %08x\n", inst,
              nv4_ramin_read32(nv4, inst << 4), nv4_ramin_read32(nv4, (inst << 4) + 4),
              nv4_ramin_read32(nv4, (inst << 4) + 8), nv4_ramin_read32(nv4, (inst << 4) + 12), off,
              (r != NV4_DMA_OK) ? "fault" : (target == 0) ? "vram" : (target == 2) ? "pci" : (target == 3) ? "agp" : "?",
              addr);
        return;
    }
    svga  = &nv4->svga;
    p     = &nv4->pfifo;
    start = svga->memaddr_latch << 2;
    bytes = svga->hdisp * svga->dispend * ((svga->bpp + 7) >> 3);
    for (uint32_t i = 0; i < bytes; i++)
        nonzero += (svga->vram[(start + i) & nv4->vram_mask] != 0);
    pclog("nv4: vram %u MB, pfb boot_0 %08x\n", nv4->vram_size >> 20, nv4_pfb_read(nv4, NV4_PFB_BOOT_0));
    pclog("nv4: mode %dx%d bpp %d start %06x pcrtc_start %08x rowoffset %d CR28 %02x nonzero %u/%u\n",
          svga->hdisp, svga->dispend, svga->bpp, start, nv4->pcrtc_start, svga->rowoffset, svga->crtc[0x28], nonzero, bytes);
    pclog("nv4: pmc intr %08x en %08x enable %08x | pfifo intr %08x en %08x caches %08x mode %08x dma %08x\n",
          nv4_pmc_pending(nv4), nv4->pmc_intr_en, nv4->pmc_enable, p->intr, p->intr_en, p->caches, p->mode, p->dma);
    pclog("nv4: cache1 push0 %08x push1 %08x pull0 %08x put %d get %d dma push %08x put %08x get %08x state %08x inst %04x\n",
          p->c1_push0, p->c1_push1, p->c1_pull0, p->c1_put, p->c1_get, p->c1_dma_push, p->c1_dma_put, p->c1_dma_get,
          p->c1_dma_state, p->c1_dma_instance);
    pclog("nv4: pgraph intr %08x en %08x fifo %08x nsource %08x ctx_user %08x ctx_control %08x trapped %08x/%08x\n",
          NV4_GR(nv4, NV4_PGRAPH_INTR), NV4_GR(nv4, NV4_PGRAPH_INTR_EN), NV4_GR(nv4, NV4_PGRAPH_FIFO),
          NV4_GR(nv4, NV4_PGRAPH_NSOURCE), NV4_GR(nv4, NV4_PGRAPH_CTX_USER), NV4_GR(nv4, NV4_PGRAPH_CTX_CONTROL),
          NV4_GR(nv4, NV4_PGRAPH_TRAPPED_ADDR), NV4_GR(nv4, NV4_PGRAPH_TRAPPED_DATA));
    pclog("nv4: engine busy for %.0f us more (nvclk %.1f MHz, mclk %.1f MHz)\n",
          nv4_pfifo_engine_busy(nv4) ? (double) (nv4->eng_tsc - tsc) * 1000000.0 / cpuclock : 0.0,
          nv4->nvclk_hz / 1000000.0, nv4->mclk_hz / 1000000.0);
    pclog("nv4: switch %08x %08x %08x %08x debug %08x %08x %08x %08x\n", NV4_GR(nv4, NV4_PGRAPH_CTX_SWITCH1),
          NV4_GR(nv4, NV4_PGRAPH_CTX_SWITCH2), NV4_GR(nv4, NV4_PGRAPH_CTX_SWITCH3), NV4_GR(nv4, NV4_PGRAPH_CTX_SWITCH4),
          NV4_GR(nv4, NV4_PGRAPH_DEBUG_0), NV4_GR(nv4, NV4_PGRAPH_DEBUG_1), NV4_GR(nv4, NV4_PGRAPH_DEBUG_2),
          NV4_GR(nv4, NV4_PGRAPH_DEBUG_3));
    for (int i = 0; i < 4; i++) /* 2D source and destination, 3D colour and zeta */
        pclog("nv4: surf %d base %08x off %08x pitch %04x limit %08x fmt %x\n", i, NV4_GR(nv4, NV4_PGRAPH_BBASE + i * 4),
              NV4_GR(nv4, NV4_PGRAPH_BOFFSET + i * 4), NV4_GR(nv4, NV4_PGRAPH_BPITCH + i * 4),
              NV4_GR(nv4, NV4_PGRAPH_BLIMIT + i * 4), (NV4_GR(nv4, NV4_PGRAPH_BPIXEL) >> (i * 4)) & 0xf);
    pclog("nv4: 3D surface type %u (2: swizzled, %u x %u) | swizzled surface %u x %u\n", NV4_GR(nv4, NV4_PGRAPH_SURFACE),
          1u << ((NV4_GR(nv4, NV4_PGRAPH_BSWIZZLE2) >> 16) & 0xf), 1u << ((NV4_GR(nv4, NV4_PGRAPH_BSWIZZLE2) >> 24) & 0xf),
          1u << ((NV4_GR(nv4, NV4_PGRAPH_BSWIZZLE5) >> 16) & 0xf), 1u << ((NV4_GR(nv4, NV4_PGRAPH_BSWIZZLE5) >> 24) & 0xf));
    pclog("nv4: uclip x %05x..%05x y %05x..%05x | image clip x %05x..%05x y %05x..%05x\n",
          NV4_GR(nv4, NV4_PGRAPH_UCLIP_MIN), NV4_GR(nv4, NV4_PGRAPH_UCLIP_MAX),
          NV4_GR(nv4, NV4_PGRAPH_UCLIP_MIN + 4), NV4_GR(nv4, NV4_PGRAPH_UCLIP_MAX + 4),
          NV4_GR(nv4, NV4_PGRAPH_UCLIPA_MIN), NV4_GR(nv4, NV4_PGRAPH_UCLIPA_MAX),
          NV4_GR(nv4, NV4_PGRAPH_UCLIPA_MIN + 4), NV4_GR(nv4, NV4_PGRAPH_UCLIPA_MAX + 4));
    for (int i = 0; i < 256; i++)
        if (nv4->dbg_cls_count[i])
            pclog("nv4: class %02x: %u methods\n", i, nv4->dbg_cls_count[i]);
    for (int i = 0; i < 16; i++)
        if (nv4->dbg_user[i].reads || nv4->dbg_user[i].writes)
            pclog("nv4: user ch %d: %u reads, %u writes, last %04x = %08x%s\n", i, nv4->dbg_user[i].reads,
                  nv4->dbg_user[i].writes, nv4->dbg_user[i].last_off, nv4->dbg_user[i].last_val,
                  (nv4->pfifo.mode & (1u << i)) ? " (DMA)" : "");
    pclog("nv4: pvideo overlay %08x point %08x size %08x scale %08x start %06x/%06x pitch %x/%x key %08x shown %u intr %x/%x\n",
          nv4->pvideo_regs[0x244 >> 2], nv4->pvideo_regs[0x230 >> 2], nv4->pvideo_regs[0x234 >> 2],
          nv4->pvideo_regs[0x200 >> 2], nv4->pvideo_regs[0x20c >> 2], nv4->pvideo_regs[0x210 >> 2],
          nv4->pvideo_regs[0x214 >> 2], nv4->pvideo_regs[0x218 >> 2], nv4->pvideo_regs[0x240 >> 2],
          nv4->pvideo_shown, nv4->pvideo_intr, nv4->pvideo_intr_en);
    pclog("nv4: dma to system memory: %u writes, %u through not-present PTEs (last inst %04x offset %08x pte %08x)\n",
          nv4->dbg_sysw_n, nv4->dbg_dma_np, nv4->dbg_np_inst, nv4->dbg_np_offset, nv4->dbg_np_pte);
    for (uint32_t i = 0; (i < 8) && (i < nv4->dbg_sysw_n); i++) {
        uint32_t n = (nv4->dbg_sysw_n - 1 - i) & 7;

        pclog("nv4:   inst %04x offset %08x -> %08x (%u bytes)\n", nv4->dbg_sysw[n].inst, nv4->dbg_sysw[n].offset,
              nv4->dbg_sysw[n].addr, nv4->dbg_sysw[n].len);
    }
    for (int i = 0; i < nv4->dbg_gaps; i++)
        pclog("nv4: not modeled: class %02x method %04x: %s (%u times)\n", nv4->dbg_gap[i].cls,
              nv4->dbg_gap[i].mthd, nv4->dbg_gap[i].what, nv4->dbg_gap[i].count);
}

static void *
nv4_init(const device_t *info)
{
    nv4_t *nv4 = calloc(1, sizeof(nv4_t));
    int    mb;

    /* the chosen model, else the device's own board */
    nv4->board = nv4_board_find(device_get_config_bios("bios"));
    if (nv4->board == NULL)
        nv4->board = (const nv4_board_t *) info->local;
    pclog("nv4: %s, board %s (%s)\n", info->name, nv4->board->name, nv4->board->rom_path);
    nv4->chip   = nv4->board->chip;
    nv4->is_agp = !!(info->flags & DEVICE_AGP);

    mb = device_get_config_int("memory");
    if ((mb != 4) && (mb != 8) && (mb != 16) && (mb != 32))
        mb = 16;
    if ((nv4->chip == NV4_CHIP_TNT) && (mb > 16))
        mb = 16;
    nv4->vram_size  = (uint32_t) mb << 20;
    nv4->vram_mask  = nv4->vram_size - 1;
    nv4->lfb_size   = (nv4->chip == NV4_CHIP_TNT2) ? 0x02000000 : 0x01000000;
    /* PRAMIN offset x is VRAM (x ^ (size - 64)): instance memory sits at
       the top of VRAM in reverse, 64-byte units. The unit is modeled on
       the later chips (the RIVA 128 reverses 16-byte units); software
       reaches instance memory only through PRAMIN, which hides it. */
    nv4->ramin_flip = nv4->vram_size - 64;

    video_inform(VIDEO_FLAG_TYPE_SPECIAL, nv4->is_agp ? &timing_nv4_agp : &timing_nv4_pci);

    svga_init(info, &nv4->svga, nv4, nv4->vram_size,
              nv4_recalctimings, nv4_vga_in, nv4_vga_out,
              nv4_hwcursor_draw, nv4_pvideo_draw);
    nv4->svga.vblank_start      = nv4_vblank_start;
    nv4->svga.hwcursor.cur_xsize = 32;
    nv4->svga.hwcursor.cur_ysize = 32;

    nv4->i2c = i2c_gpio_init("ddc_nv4");
    nv4->ddc = ddc_init(i2c_gpio_get_bus(nv4->i2c));

    mem_mapping_add(&nv4->mmio_mapping, 0, 0,
                    nv4_mmio_readb, nv4_mmio_readw, nv4_mmio_readl,
                    nv4_mmio_writeb, nv4_mmio_writew, nv4_mmio_writel,
                    NULL, MEM_MAPPING_EXTERNAL, nv4);
    mem_mapping_add(&nv4->lfb_mapping, 0, 0,
                    nv4_lfb_readb, nv4_lfb_readw, nv4_lfb_readl,
                    nv4_lfb_writeb, nv4_lfb_writew, nv4_lfb_writel,
                    NULL, MEM_MAPPING_EXTERNAL, nv4);
    nv4_rom_load(nv4, nv4->board->rom_path);
    mem_mapping_add(&nv4->bios_rom.mapping, 0, 0,
                    nv4_rom_readb, nv4_rom_readw, nv4_rom_readl,
                    NULL, NULL, NULL,
                    NULL, MEM_MAPPING_EXTERNAL, nv4);
    mem_mapping_disable(&nv4->mmio_mapping);
    mem_mapping_disable(&nv4->lfb_mapping);
    mem_mapping_disable(&nv4->bios_rom.mapping);

    nv4_vga_init(nv4);
    nv4_ptimer_init(nv4);
    nv4_pfifo_init(nv4);

    pci_add_card(nv4->is_agp ? PCI_ADD_AGP : PCI_ADD_NORMAL,nv4_pci_read, nv4_pci_write, nv4, &nv4->pci_slot);

    nv4->render_threads = device_get_config_int("render_threads");
    /* NV4_GPU=0/1 overrides the option */
    nv4->gpu_raster = device_get_config_int("gpu_raster");
    nv4->dbg_log_cls = (getenv("NV4_LOGCLASS") && *getenv("NV4_LOGCLASS")) ? (int) strtol(getenv("NV4_LOGCLASS"), NULL, 16) : -1;
    if (getenv("NV4_GPU") && *getenv("NV4_GPU"))
        nv4->gpu_raster = atoi(getenv("NV4_GPU"));
    nv4_3d_init(nv4);
    nv4_reset(nv4);

    nv4_debug_dev = nv4;

    return nv4;
}

static void
nv4_close(void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;

    if (nv4_debug_dev == nv4) {
        nv4_debug_dev = NULL;
    }
    timer_disable(&nv4->ptimer_timer);
    nv4_pfifo_close(nv4);
    nv4_vga_close(nv4);
    ddc_close(nv4->ddc);
    i2c_gpio_close(nv4->i2c);
    nv4_3d_close(nv4);
    svga_close(&nv4->svga);
    free(nv4->bios_rom.rom);
    free(nv4);
}

static void
nv4_speed_changed(void *priv)
{
    svga_recalctimings(&((nv4_t *) priv)->svga);
}

static void
nv4_force_redraw(void *priv)
{
    nv4_t *nv4 = (nv4_t *) priv;
    nv4->svga.fullchange = nv4->svga.monitor->mon_changeframecount;
}

/* ------------------------------------------------------------------ */
/* Boards.                                                             */
/* ------------------------------------------------------------------ */
/* One device per chip and bus, as the RIVA 128 has; the board is its
   "bios" option, whose values are the names the boards had as devices of
   their own (vid_table.c moves old configurations over). The BIOSes are
   in roms/video/nvidia/nv4 (Xeon3D/roms). */
static const nv4_board_t board_nv4_erazor2_pci = { .name = "nv4_erazor2_pci", .rom_path = "roms/video/nvidia/nv4/elsa_erazor_ii_pci_2.03.01.rom", .device_id = NV4_PCI_DEVICE_TNT, .chip = NV4_CHIP_TNT, .revision = 0x04, .crystal_14m = 1 };
static const nv4_board_t board_nv4_erazor2_agp_203 = { .name = "nv4_erazor2_agp_203", .rom_path = "roms/video/nvidia/nv4/ER220300.ATN", .device_id = NV4_PCI_DEVICE_TNT, .chip = NV4_CHIP_TNT, .revision = 0x04, .crystal_14m = 1, .sgram = 1 };
static const nv4_board_t board_nv4_erazor2_agp = { .name = "nv4_erazor2_agp", .rom_path = "roms/video/nvidia/nv4/elsa_erazor_ii_agp_2.05.00.rom", .device_id = NV4_PCI_DEVICE_TNT, .chip = NV4_CHIP_TNT, .revision = 0x04, .crystal_14m = 1 };
static const nv4_board_t board_nv4_viper_v550_193 = { .name = "nv4_viper_v550_193", .rom_path = "roms/video/nvidia/nv4/nv4-diamond-reva.rom", .device_id = NV4_PCI_DEVICE_TNT, .chip = NV4_CHIP_TNT, .revision = 0x04, .crystal_14m = 1 };
static const nv4_board_t board_nv4_viper_v550_195e = { .name = "nv4_viper_v550_195e", .rom_path = "roms/video/nvidia/nv4/nv4-diamond-revb.rom", .device_id = NV4_PCI_DEVICE_TNT, .chip = NV4_CHIP_TNT, .revision = 0x04, .crystal_14m = 1 };
static const nv4_board_t board_nv4_viper_v550 = { .name = "nv4_viper_v550", .rom_path = "roms/video/nvidia/nv4/195c0550.rom", .device_id = NV4_PCI_DEVICE_TNT, .chip = NV4_CHIP_TNT, .revision = 0x04, .crystal_14m = 1 };
static const nv4_board_t board_nv4_creative_tnt = { .name = "nv4_creative_tnt", .rom_path = "roms/video/nvidia/nv4/nv4-creative.rom", .device_id = NV4_PCI_DEVICE_TNT, .chip = NV4_CHIP_TNT, .revision = 0x04, .crystal_14m = 1 };
static const nv4_board_t board_nv4_velocity_4400_101 = { .name = "nv4_velocity_4400_101", .rom_path = "roms/video/nvidia/nv4/nv4-stb-velocity.rom", .device_id = NV4_PCI_DEVICE_TNT, .chip = NV4_CHIP_TNT, .revision = 0x04, .crystal_14m = 1 };
static const nv4_board_t board_nv4_velocity_4400 = { .name = "nv4_velocity_4400", .rom_path = "roms/video/nvidia/nv4/nv4-stbvelocity4400-110.rom", .device_id = NV4_PCI_DEVICE_TNT, .chip = NV4_CHIP_TNT, .revision = 0x04, .crystal_14m = 1 };
static const nv4_board_t board_nv4_asus_v3400tnt = { .name = "nv4_asus_v3400tnt", .rom_path = "roms/video/nvidia/nv4/v3400tnt.rom", .device_id = NV4_PCI_DEVICE_TNT, .chip = NV4_CHIP_TNT, .revision = 0x04, .crystal_14m = 1, .sgram = 1 };
static const nv4_board_t board_nv5_erazor3 = { .name = "nv5_erazor3", .rom_path = "roms/video/nvidia/nv4/elsa_erazor_iii_3.05.00.rom", .device_id = NV4_PCI_DEVICE_TNT2, .chip = NV4_CHIP_TNT2, .revision = 0x11, .crystal_14m = 1 };
static const nv4_board_t board_nv5_erazor3_pro = { .name = "nv5_erazor3_pro", .rom_path = "roms/video/nvidia/nv4/E3230500.APN", .device_id = NV4_PCI_DEVICE_TNT2, .chip = NV4_CHIP_TNT2, .revision = 0x11, .crystal_14m = 1, .sgram = 1 };
static const nv4_board_t board_nv5_erazor3_pro_oem = { .name = "nv5_erazor3_pro_oem", .rom_path = "roms/video/nvidia/nv4/E3230500.AON", .device_id = NV4_PCI_DEVICE_TNT2, .chip = NV4_CHIP_TNT2, .revision = 0x11, .crystal_14m = 1, .sgram = 1 };
static const nv4_board_t board_nv5_erazor3_lt = { .name = "nv5_erazor3_lt", .rom_path = "roms/video/nvidia/nv4/elsa_erazor_iii_lt_3.05.00.rom", .device_id = NV4_PCI_DEVICE_TNT2_M64, .chip = NV4_CHIP_TNT2, .revision = 0x15, .crystal_14m = 1, .mem_64bit = 1 };
static const nv4_board_t board_nv5_synergy2_32 = { .name = "nv5_synergy2_32", .rom_path = "roms/video/nvidia/nv4/ELSA Synergy II-32 PCI.BIN", .device_id = NV4_PCI_DEVICE_TNT2, .chip = NV4_CHIP_TNT2, .revision = 0x11, .crystal_14m = 1 };
static const nv4_board_t board_nv5_viper_v770 = { .name = "nv5_viper_v770", .rom_path = "roms/video/nvidia/nv4/diamond-viper-v770.BIN", .device_id = NV4_PCI_DEVICE_TNT2, .chip = NV4_CHIP_TNT2, .revision = 0x11, .crystal_14m = 1 };
static const nv4_board_t board_nv5_winfast_s320ii = { .name = "nv5_winfast_s320ii", .rom_path = "roms/video/nvidia/nv4/rivatnt2leadteks320ii.BIN", .device_id = NV4_PCI_DEVICE_TNT2, .chip = NV4_CHIP_TNT2, .revision = 0x11, .crystal_14m = 1 };
static const nv4_board_t board_nv5_erazor3_ultra = { .name = "nv5_erazor3_ultra", .rom_path = "roms/video/nvidia/nv4/elsa_erazor_iii_ultra_3.05.00.rom", .device_id = NV4_PCI_DEVICE_TNT2_U, .chip = NV4_CHIP_TNT2, .revision = 0x11, .crystal_14m = 1, .sgram = 1 };

static const nv4_board_t *const nv4_boards[] = {
    &board_nv4_erazor2_pci,
    &board_nv4_erazor2_agp_203,
    &board_nv4_erazor2_agp,
    &board_nv4_viper_v550_193,
    &board_nv4_viper_v550_195e,
    &board_nv4_viper_v550,
    &board_nv4_creative_tnt,
    &board_nv4_velocity_4400_101,
    &board_nv4_velocity_4400,
    &board_nv4_asus_v3400tnt,
    &board_nv5_erazor3,
    &board_nv5_erazor3_pro,
    &board_nv5_erazor3_pro_oem,
    &board_nv5_erazor3_lt,
    &board_nv5_synergy2_32,
    &board_nv5_viper_v770,
    &board_nv5_winfast_s320ii,
    &board_nv5_erazor3_ultra,
    NULL
};

const nv4_board_t *
nv4_board_find(const char *name)
{
    for (int i = 0; (name != NULL) && (nv4_boards[i] != NULL); i++) {
        if (!strcmp(nv4_boards[i]->name, name))
            return nv4_boards[i];
    }
    return NULL;
}

/* clang-format off */
static const device_config_t nv4_tnt_pci_config[] = {
    {
        .name           = "bios",
        .description    = "Model",
        .type           = CONFIG_BIOS,
        .default_string = "nv4_erazor2_pci",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            { .name = "ELSA ERAZOR II PCI - Version 2.03.01", .internal_name = "nv4_erazor2_pci", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/elsa_erazor_ii_pci_2.03.01.rom", "" } },
            { .name = "ELSA ERAZOR II AGP (SGRAM) - Version 2.03.00", .internal_name = "nv4_erazor2_agp_203", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/ER220300.ATN", "" } },
            { .name = "ELSA ERAZOR II AGP - Version 2.05.00", .internal_name = "nv4_erazor2_agp", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/elsa_erazor_ii_agp_2.05.00.rom", "" } },
            { .name = "Diamond Viper V550 - Version 1.93E", .internal_name = "nv4_viper_v550_193", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/nv4-diamond-reva.rom", "" } },
            { .name = "Diamond Viper V550 - Version 1.95E5802", .internal_name = "nv4_viper_v550_195e", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/nv4-diamond-revb.rom", "" } },
            { .name = "Diamond Viper V550 - Version 1.95C0550", .internal_name = "nv4_viper_v550", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/195c0550.rom", "" } },
            { .name = "Creative Graphics Blaster RIVA TNT - Version 2.04.6.18", .internal_name = "nv4_creative_tnt", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/nv4-creative.rom", "" } },
            { .name = "STB Velocity 4400 - Version 1.01", .internal_name = "nv4_velocity_4400_101", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/nv4-stb-velocity.rom", "" } },
            { .name = "STB Velocity 4400 - Version 1.10", .internal_name = "nv4_velocity_4400", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/nv4-stbvelocity4400-110.rom", "" } },
            { .name = "ASUS AGP-V3400TNT - Version 2.04.20", .internal_name = "nv4_asus_v3400tnt", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/v3400tnt.rom", "" } },
            { .files_no = 0 }
        }
    },
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 16,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "8 MB", .value = 8 },
            { .description = "16 MB", .value = 16 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "render_threads",
        .description    = "Render threads",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "1", .value = 1 },
            { .description = "2", .value = 2 },
            { .description = "3", .value = 3 },
            { .description = "4", .value = 4 },
            { .description = "6", .value = 6 },
            { .description = "8", .value = 8 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "gpu_raster",
        .description    = "Draw 3D on the host GPU (Vulkan)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t nv4_tnt_agp_config[] = {
    {
        .name           = "bios",
        .description    = "Model",
        .type           = CONFIG_BIOS,
        .default_string = "nv4_erazor2_agp",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            { .name = "ELSA ERAZOR II PCI - Version 2.03.01", .internal_name = "nv4_erazor2_pci", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/elsa_erazor_ii_pci_2.03.01.rom", "" } },
            { .name = "ELSA ERAZOR II AGP (SGRAM) - Version 2.03.00", .internal_name = "nv4_erazor2_agp_203", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/ER220300.ATN", "" } },
            { .name = "ELSA ERAZOR II AGP - Version 2.05.00", .internal_name = "nv4_erazor2_agp", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/elsa_erazor_ii_agp_2.05.00.rom", "" } },
            { .name = "Diamond Viper V550 - Version 1.93E", .internal_name = "nv4_viper_v550_193", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/nv4-diamond-reva.rom", "" } },
            { .name = "Diamond Viper V550 - Version 1.95E5802", .internal_name = "nv4_viper_v550_195e", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/nv4-diamond-revb.rom", "" } },
            { .name = "Diamond Viper V550 - Version 1.95C0550", .internal_name = "nv4_viper_v550", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/195c0550.rom", "" } },
            { .name = "Creative Graphics Blaster RIVA TNT - Version 2.04.6.18", .internal_name = "nv4_creative_tnt", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/nv4-creative.rom", "" } },
            { .name = "STB Velocity 4400 - Version 1.01", .internal_name = "nv4_velocity_4400_101", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/nv4-stb-velocity.rom", "" } },
            { .name = "STB Velocity 4400 - Version 1.10", .internal_name = "nv4_velocity_4400", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/nv4-stbvelocity4400-110.rom", "" } },
            { .name = "ASUS AGP-V3400TNT - Version 2.04.20", .internal_name = "nv4_asus_v3400tnt", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/v3400tnt.rom", "" } },
            { .files_no = 0 }
        }
    },
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 16,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "8 MB", .value = 8 },
            { .description = "16 MB", .value = 16 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "render_threads",
        .description    = "Render threads",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "1", .value = 1 },
            { .description = "2", .value = 2 },
            { .description = "3", .value = 3 },
            { .description = "4", .value = 4 },
            { .description = "6", .value = 6 },
            { .description = "8", .value = 8 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "gpu_raster",
        .description    = "Draw 3D on the host GPU (Vulkan)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t nv5_tnt2_config[] = {
    {
        .name           = "bios",
        .description    = "Model",
        .type           = CONFIG_BIOS,
        .default_string = "nv5_erazor3",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            { .name = "ELSA ERAZOR III - Version 3.05.00", .internal_name = "nv5_erazor3", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/elsa_erazor_iii_3.05.00.rom", "" } },
            { .name = "ELSA ERAZOR III Pro - Version 3.05.00", .internal_name = "nv5_erazor3_pro", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/E3230500.APN", "" } },
            { .name = "ELSA ERAZOR III Pro OEM - Version 3.05.00", .internal_name = "nv5_erazor3_pro_oem", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/E3230500.AON", "" } },
            { .name = "ELSA ERAZOR III LT (TNT2 M64) - Version 3.05.00", .internal_name = "nv5_erazor3_lt", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/elsa_erazor_iii_lt_3.05.00.rom", "" } },
            { .name = "ELSA Synergy II-32 - Version 3.05.00", .internal_name = "nv5_synergy2_32", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/ELSA Synergy II-32 PCI.BIN", "" } },
            { .name = "Diamond Viper V770 - Version 2.01C4A00", .internal_name = "nv5_viper_v770", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/diamond-viper-v770.BIN", "" } },
            { .name = "Leadtek WinFast 3D S320 II - Version 99.04.12", .internal_name = "nv5_winfast_s320ii", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/rivatnt2leadteks320ii.BIN", "" } },
            { .files_no = 0 }
        }
    },
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 32,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "16 MB", .value = 16 },
            { .description = "32 MB", .value = 32 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "render_threads",
        .description    = "Render threads",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "1", .value = 1 },
            { .description = "2", .value = 2 },
            { .description = "3", .value = 3 },
            { .description = "4", .value = 4 },
            { .description = "6", .value = 6 },
            { .description = "8", .value = 8 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "gpu_raster",
        .description    = "Draw 3D on the host GPU (Vulkan)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

static const device_config_t nv5_tnt2_ultra_config[] = {
    {
        .name           = "bios",
        .description    = "Model",
        .type           = CONFIG_BIOS,
        .default_string = "nv5_erazor3_ultra",
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = {
            { .name = "ELSA ERAZOR III ULTRA - Version 3.05.00", .internal_name = "nv5_erazor3_ultra", .bios_type = BIOS_NORMAL,
              .files_no = 1, .local = 0, .size = 0, .files = { "roms/video/nvidia/nv4/elsa_erazor_iii_ultra_3.05.00.rom", "" } },
            { .files_no = 0 }
        }
    },
    {
        .name           = "memory",
        .description    = "Memory size",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 32,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "16 MB", .value = 16 },
            { .description = "32 MB", .value = 32 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "render_threads",
        .description    = "Render threads",
        .type           = CONFIG_SELECTION,
        .default_string = NULL,
        .default_int    = 2,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = {
            { .description = "1", .value = 1 },
            { .description = "2", .value = 2 },
            { .description = "3", .value = 3 },
            { .description = "4", .value = 4 },
            { .description = "6", .value = 6 },
            { .description = "8", .value = 8 },
            { .description = "" }
        },
        .bios           = { { 0 } }
    },
    {
        .name           = "gpu_raster",
        .description    = "Draw 3D on the host GPU (Vulkan)",
        .type           = CONFIG_BINARY,
        .default_string = NULL,
        .default_int    = 0,
        .file_filter    = NULL,
        .spinner        = { 0 },
        .selection      = { { 0 } },
        .bios           = { { 0 } }
    },
    { .name = "", .description = "", .type = CONFIG_END }
};

const device_t nv4_tnt_pci_device = {
    .name          = "nVIDIA RIVA TNT (NV4) PCI",
    .internal_name = "nv4_tnt_pci",
    .flags         = DEVICE_PCI,
    .local         = (uintptr_t) &board_nv4_erazor2_pci,
    .init          = nv4_init,
    .close         = nv4_close,
    .reset         = nv4_reset,
    .available     = NULL,
    .speed_changed = nv4_speed_changed,
    .force_redraw  = nv4_force_redraw,
    .config        = nv4_tnt_pci_config
};

const device_t nv4_tnt_agp_device = {
    .name          = "nVIDIA RIVA TNT (NV4) AGP",
    .internal_name = "nv4_tnt_agp",
    .flags         = DEVICE_AGP,
    .local         = (uintptr_t) &board_nv4_erazor2_agp,
    .init          = nv4_init,
    .close         = nv4_close,
    .reset         = nv4_reset,
    .available     = NULL,
    .speed_changed = nv4_speed_changed,
    .force_redraw  = nv4_force_redraw,
    .config        = nv4_tnt_agp_config
};

const device_t nv5_tnt2_pci_device = {
    .name          = "nVIDIA RIVA TNT2 (NV5) PCI",
    .internal_name = "nv5_tnt2_pci",
    .flags         = DEVICE_PCI,
    .local         = (uintptr_t) &board_nv5_erazor3,
    .init          = nv4_init,
    .close         = nv4_close,
    .reset         = nv4_reset,
    .available     = NULL,
    .speed_changed = nv4_speed_changed,
    .force_redraw  = nv4_force_redraw,
    .config        = nv5_tnt2_config
};

const device_t nv5_tnt2_agp_device = {
    .name          = "nVIDIA RIVA TNT2 (NV5) AGP",
    .internal_name = "nv5_tnt2_agp",
    .flags         = DEVICE_AGP,
    .local         = (uintptr_t) &board_nv5_erazor3,
    .init          = nv4_init,
    .close         = nv4_close,
    .reset         = nv4_reset,
    .available     = NULL,
    .speed_changed = nv4_speed_changed,
    .force_redraw  = nv4_force_redraw,
    .config        = nv5_tnt2_config
};

const device_t nv5_tnt2_ultra_pci_device = {
    .name          = "nVIDIA RIVA TNT2 Ultra (NV5) PCI",
    .internal_name = "nv5_tnt2_ultra_pci",
    .flags         = DEVICE_PCI,
    .local         = (uintptr_t) &board_nv5_erazor3_ultra,
    .init          = nv4_init,
    .close         = nv4_close,
    .reset         = nv4_reset,
    .available     = NULL,
    .speed_changed = nv4_speed_changed,
    .force_redraw  = nv4_force_redraw,
    .config        = nv5_tnt2_ultra_config
};

const device_t nv5_tnt2_ultra_agp_device = {
    .name          = "nVIDIA RIVA TNT2 Ultra (NV5) AGP",
    .internal_name = "nv5_tnt2_ultra_agp",
    .flags         = DEVICE_AGP,
    .local         = (uintptr_t) &board_nv5_erazor3_ultra,
    .init          = nv4_init,
    .close         = nv4_close,
    .reset         = nv4_reset,
    .available     = NULL,
    .speed_changed = nv4_speed_changed,
    .force_redraw  = nv4_force_redraw,
    .config        = nv5_tnt2_ultra_config
};

/* clang-format on */
