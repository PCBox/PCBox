/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Intel 82845 Brookdale MCH Bridge
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *          Bachimus, <mszoopers@protonmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
 *          Copyright 2026      Bachimus.
 */

#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include "x86.h"
#include <86box/device.h>
#include <86box/plat_unused.h>

#include <86box/chipset.h>
#include <86box/mem.h>
#include <86box/agpgart.h>
#include <86box/pci.h>
#include <86box/smram.h>
#include <86box/spd.h>

#ifdef ENABLE_INTEL_845_LOG
int intel_845_do_log = ENABLE_INTEL_845_LOG;
static void
intel_845_log(const char *fmt, ...)
{
    va_list ap;

    if (intel_845_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define intel_845_log(fmt, ...)
#endif

typedef struct intel_845_t {
    uint8_t    pci_conf[256];
    uint8_t    mchbar_regs[0x200];
    uint8_t    subsystem_locked[4];
    uint8_t    pci_slot;
    uint8_t    pad[2];
    mem_mapping_t mchbar_mapping;
    uint32_t   tseg_base;
    uint32_t   tseg_size;
    smram_t   *c_segment;
    smram_t   *h_segment;
    smram_t   *tseg_segment;
    agpgart_t *agpgart;
} intel_845_t;

static uint32_t
intel_845_tom(const intel_845_t *dev)
{
    const uint16_t tom = (dev->pci_conf[0xc4] | (dev->pci_conf[0xc5] << 8)) & 0xfff0;

    return (uint32_t) tom << 16;
}

static uint16_t
intel_845_default_tom_reg(void)
{
    uint16_t tom = (mem_size >> 6) & 0xfff0;

    return (tom < 0x0200) ? 0x0200 : tom;
}

static void
intel_845_set_tom_reg(intel_845_t *dev, uint16_t tom)
{
    dev->pci_conf[0xc4] = tom & 0xff;
    dev->pci_conf[0xc5] = tom >> 8;
}

static void
intel_845_agp_aperture(intel_845_t *dev)
{
    uint32_t aperture_base;
    uint32_t aperture_size;
    int      aperture_enable;

    dev->pci_conf[0x10] = 0x08;
    dev->pci_conf[0x11] = 0x00;

    aperture_base = (dev->pci_conf[0x13] << 24) | (dev->pci_conf[0x12] << 16);
    aperture_base &= (((uint32_t) (dev->pci_conf[0xb4] & 0x3f) << 22) | 0xf0000000);

    dev->pci_conf[0x12] = (aperture_base >> 16) & 0xff;
    dev->pci_conf[0x13] = (aperture_base >> 24) & 0xff;

    aperture_size   = ((uint32_t) ((~dev->pci_conf[0xb4] & 0x3f) + 1)) << 22;
    aperture_enable = !!(dev->pci_conf[0x51] & 0x02) && (aperture_base != 0);

    if (aperture_enable)
        intel_845_log("Intel 845 MCH: AGP aperture enabled at %08x, size %u MB\n",
                      aperture_base, aperture_size >> 20);
    else
        intel_845_log("Intel 845 MCH: AGP aperture disabled\n");

    agpgart_set_aperture(dev->agpgart, aperture_base, aperture_size, aperture_enable);
}

static void
intel_845_gart_table(intel_845_t *dev)
{
    const uint32_t agp_gart_base = (dev->pci_conf[0xbb] << 24) | (dev->pci_conf[0xba] << 16) |
                                   (dev->pci_conf[0xb9] << 8) | dev->pci_conf[0xb8];

    intel_845_log("Intel 845 MCH: AGP GART base updated to %08x\n", agp_gart_base);

    agpgart_set_gart(dev->agpgart, agp_gart_base);
}

static uint32_t
intel_845_mchbar_base(const intel_845_t *dev)
{
    return ((dev->pci_conf[0x17] << 24) | (dev->pci_conf[0x16] << 16) |
            (dev->pci_conf[0x15] << 8) | dev->pci_conf[0x14]) & 0xfffff000;
}

static void
intel_845_mchbar_recalc(intel_845_t *dev)
{
    const uint32_t base = intel_845_mchbar_base(dev);

    if (base != 0) {
        intel_845_log("Intel 845 MCH: MCHBAR enabled at %08x\n", base);
        mem_mapping_set_addr(&dev->mchbar_mapping, base, 0x1000);
    } else {
        intel_845_log("Intel 845 MCH: MCHBAR disabled\n");
        mem_mapping_disable(&dev->mchbar_mapping);
    }
}

static int
intel_845_mchbar_is_shadowed(uint32_t reg)
{
    return ((reg >= 0x20) && (reg <= 0xdf)) ||
           ((reg >= 0x140) && (reg <= 0x1df));
}

static uint8_t
intel_845_mchbar_readb(uint32_t addr, void *priv)
{
    const intel_845_t *dev = (intel_845_t *) priv;
    const uint32_t     reg = addr & 0x1ff;
    uint8_t            ret = 0x00;

    switch (reg) {
        case 0x2c:
        case 0x30 ... 0x34:
            ret = dev->mchbar_regs[reg];
            break;

        default:
            if (intel_845_mchbar_is_shadowed(reg))
                ret = dev->mchbar_regs[reg];
            break;
    }

    intel_845_log("Intel 845 MCH: MCHBAR[%03x] (%02x)\n", reg, ret);
    return ret;
}

static uint16_t
intel_845_mchbar_readw(uint32_t addr, void *priv)
{
    return intel_845_mchbar_readb(addr, priv) | (intel_845_mchbar_readb(addr + 1, priv) << 8);
}

static uint32_t
intel_845_mchbar_readl(uint32_t addr, void *priv)
{
    return intel_845_mchbar_readw(addr, priv) | (intel_845_mchbar_readw(addr + 2, priv) << 16);
}

static void
intel_845_mchbar_writeb(uint32_t addr, uint8_t val, void *priv)
{
    intel_845_t   *dev = (intel_845_t *) priv;
    const uint32_t reg = addr & 0x1ff;

    intel_845_log("Intel 845 MCH: MCHBAR[%03x] = %02x\n", reg, val);

    switch (reg) {
        case 0x2c:
            dev->mchbar_regs[reg] = val & 0x3f;
            break;

        case 0x30 ... 0x33:
            dev->mchbar_regs[reg] = val & 0x77;
            break;

        case 0x34:
            dev->mchbar_regs[reg] = val & 0x07;
            break;

        default:
            if (intel_845_mchbar_is_shadowed(reg))
                dev->mchbar_regs[reg] = val;
            break;
    }
}

static void
intel_845_mchbar_writew(uint32_t addr, uint16_t val, void *priv)
{
    intel_845_mchbar_writeb(addr, val & 0xff, priv);
    intel_845_mchbar_writeb(addr + 1, val >> 8, priv);
}

static void
intel_845_mchbar_writel(uint32_t addr, uint32_t val, void *priv)
{
    intel_845_mchbar_writew(addr, val & 0xffff, priv);
    intel_845_mchbar_writew(addr + 2, val >> 16, priv);
}

static void
intel_845_pam_recalc(int addr, uint8_t val)
{
    int region = 0xc0000 + ((addr - 0x91) << 15);

    if (addr == 0x90)
        mem_set_mem_state_both(0xf0000, 0x10000,
                               ((val & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) |
                               ((val & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    else {
        mem_set_mem_state_both(region, 0x4000,
                               ((val & 0x01) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) |
                               ((val & 0x02) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
        mem_set_mem_state_both(region + 0x4000, 0x4000,
                               ((val & 0x10) ? MEM_READ_INTERNAL : MEM_READ_EXTANY) |
                               ((val & 0x20) ? MEM_WRITE_INTERNAL : MEM_WRITE_EXTANY));
    }

    flushmmucache_nopc();
}

static void
intel_845_fdhc_recalc(intel_845_t *dev)
{
    if ((mem_size << 10) > 0xf00000) {
        mem_set_mem_state_both(0xf00000, 0x100000,
                               (dev->pci_conf[0x97] & 0x80) ?
                               (MEM_READ_EXTANY | MEM_WRITE_EXTANY) :
                               (MEM_READ_INTERNAL | MEM_WRITE_INTERNAL));
        flushmmucache_nopc();
    }
}

static void
intel_845_smram_recalc(intel_845_t *dev)
{
    const uint8_t smram      = dev->pci_conf[0x9d];
    const uint8_t esmramc    = dev->pci_conf[0x9e];
    const int     g_smrame   = !!(smram & 0x08);
    const int     d_open     = ((smram & 0x50) == 0x40);
    const int     d_closed   = !!(smram & 0x20);
    uint32_t      tom        = intel_845_tom(dev);

    if (dev->tseg_size != 0) {
        mem_set_mem_state_both(dev->tseg_base, dev->tseg_size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        dev->tseg_base = dev->tseg_size = 0;
    }

    smram_disable(dev->c_segment);
    smram_disable(dev->h_segment);
    smram_disable(dev->tseg_segment);

    if (tom == 0 || (tom > (mem_size << 10)))
        tom = mem_size << 10;

    if (g_smrame) {
        smram_enable(dev->c_segment, 0x000a0000, 0x000a0000, 0x20000, d_open, 1);

        if (d_closed)
            mem_set_mem_state_smram_ex(1, 0x000a0000, 0x20000, ACCESS_SMRAM_W);

        if (esmramc & 0x80)
            smram_enable(dev->h_segment, 0xfeda0000, 0x000a0000, 0x20000, d_open, 1);

        if ((esmramc & 0x01) && (tom != 0)) {
            dev->tseg_size = 1 << (17 + ((esmramc >> 1) & 0x03));

            if (tom >= dev->tseg_size) {
                dev->tseg_base = tom - dev->tseg_size;

                if (!d_open)
                    mem_set_mem_state(dev->tseg_base, dev->tseg_size, MEM_READ_EXTANY | MEM_WRITE_EXTANY);

                smram_enable(dev->tseg_segment, dev->tseg_base, dev->tseg_base, dev->tseg_size, d_open, 1);
            } else
                dev->tseg_base = dev->tseg_size = 0;
        }
    }

    flushmmucache();
}

static void
intel_845_write(int func, int addr, UNUSED(int len), uint8_t val, void *priv)
{
    intel_845_t *dev = (intel_845_t *) priv;
    uint16_t     reg;

    intel_845_log("Intel 845 MCH: dev->regs[%02x] = %02x\n", addr, val);

    if (func)
        return;

    switch (addr) {
        case 0x05:
            dev->pci_conf[addr] = val & 0x01;
            break;

        case 0x07:
            dev->pci_conf[addr] &= ~(val & 0x70);
            break;

        case 0x10:
        case 0x11:
            break;

        case 0x12:
        case 0x13:
            dev->pci_conf[addr] = val;
            intel_845_agp_aperture(dev);
            break;

        case 0x14:
            dev->pci_conf[addr] = 0x00;
            intel_845_mchbar_recalc(dev);
            break;

        case 0x15:
            dev->pci_conf[addr] = val & 0xf0;
            intel_845_mchbar_recalc(dev);
            break;

        case 0x16:
        case 0x17:
            dev->pci_conf[addr] = val;
            intel_845_mchbar_recalc(dev);
            break;

        case 0x2c ... 0x2f:
            if (!dev->subsystem_locked[addr - 0x2c]) {
                dev->pci_conf[addr] = val;
                dev->subsystem_locked[addr - 0x2c] = 1;
            }
            break;

        case 0x51:
            dev->pci_conf[addr] = val & 0x02;
            intel_845_agp_aperture(dev);
            break;

        case 0x60 ... 0x67:
            dev->pci_conf[addr] = val;
            spd_write_drbs_intel_845(dev->pci_conf);
            break;

        case 0x70 ... 0x72:
            dev->pci_conf[addr] = val & 0x77;
            break;

        case 0x73:
            dev->pci_conf[addr] = 0x00;
            break;

        case 0x78:
            dev->pci_conf[addr] = val & 0x35;
            break;

        case 0x79:
            dev->pci_conf[addr] = val & 0x06;
            break;

        case 0x7a:
            dev->pci_conf[addr] = val & 0x07;
            break;

        case 0x7b:
            dev->pci_conf[addr] = 0x00;
            break;

        case 0x7c:
            dev->pci_conf[addr] = val & 0x70;
            break;

        case 0x7d:
            dev->pci_conf[addr] = val & 0x07;
            break;

        case 0x7e:
            dev->pci_conf[addr] = val & 0x30;
            break;

        case 0x7f:
            dev->pci_conf[addr] = val & 0x30;
            break;

        case 0x80 ... 0x85:
        case 0x87 ... 0x8b:
            dev->pci_conf[addr] = val;
            break;

        case 0x90:
            dev->pci_conf[addr] = val & 0x30;
            intel_845_pam_recalc(addr, dev->pci_conf[addr]);
            break;

        case 0x91 ... 0x96:
            dev->pci_conf[addr] = val & 0x33;
            intel_845_pam_recalc(addr, dev->pci_conf[addr]);
            break;

        case 0x97:
            dev->pci_conf[addr] = val & 0x80;
            intel_845_fdhc_recalc(dev);
            break;

        case 0x9d:
            if (dev->pci_conf[0x9d] & 0x10) {
                dev->pci_conf[0x9d] = (dev->pci_conf[0x9d] & ~0x20) | (val & 0x20) | 0x02;
            } else {
                dev->pci_conf[0x9d] = (val & 0x78) | 0x02;
                if (dev->pci_conf[0x9d] & 0x10)
                    dev->pci_conf[0x9d] &= ~0x40;
            }
            intel_845_smram_recalc(dev);
            break;

        case 0x9e:
            if (dev->pci_conf[0x9d] & 0x10)
                dev->pci_conf[0x9e] = (dev->pci_conf[0x9e] & ~(val & 0x40)) | 0x38;
            else
                dev->pci_conf[0x9e] = ((dev->pci_conf[0x9e] & 0x40) & ~(val & 0x40)) | (val & 0x87) | 0x38;
            intel_845_smram_recalc(dev);
            break;

        case 0xa8:
            dev->pci_conf[addr] = val & 0x17;
            break;

        case 0xa9:
            dev->pci_conf[addr] = val & 0x03;
            break;

        case 0xaa:
        case 0xab:
            dev->pci_conf[addr] = 0x00;
            break;

        case 0xb0:
            dev->pci_conf[addr] = val & 0x81;
            break;

        case 0xb1 ... 0xb3:
            dev->pci_conf[addr] = 0x00;
            break;

        case 0xb4:
            dev->pci_conf[addr] = val & 0x3f;
            intel_845_agp_aperture(dev);
            break;

        case 0xb8:
            dev->pci_conf[addr] = 0x00;
            intel_845_gart_table(dev);
            break;

        case 0xb9:
            dev->pci_conf[addr] = val & 0xf0;
            intel_845_gart_table(dev);
            break;

        case 0xba:
        case 0xbb:
            dev->pci_conf[addr] = val;
            intel_845_gart_table(dev);
            break;

        case 0xbc:
        case 0xbd:
            dev->pci_conf[addr] = val & 0xf8;
            break;

        case 0xc4:
            dev->pci_conf[addr] = val & 0xf0;
            intel_845_smram_recalc(dev);
            break;

        case 0xc5:
            dev->pci_conf[addr] = val;
            intel_845_smram_recalc(dev);
            break;

        case 0xc6:
            dev->pci_conf[addr] = (dev->pci_conf[addr] & 0x04) | (val & 0x22);
            break;

        case 0xc7:
            dev->pci_conf[addr] = val & 0x08;
            break;

        case 0xc8:
        case 0xc9:
            dev->pci_conf[addr] &= ~val;
            break;

        case 0xca:
            dev->pci_conf[addr] = val & 0x7f;
            break;

        case 0xcb:
            dev->pci_conf[addr] = val & 0x02;
            break;

        case 0xcc:
        case 0xce:
            dev->pci_conf[addr] = val & 0x03;
            break;

        case 0xcd:
        case 0xcf:
            dev->pci_conf[addr] = 0x00;
            break;

        case 0xde:
        case 0xdf:
            dev->pci_conf[addr] = val;
            break;

        case 0xf4:
        case 0xf5:
        case 0xf7:
            dev->pci_conf[addr] = 0x00;
            break;

        case 0xf6:
            dev->pci_conf[addr] = val & 0x40;
            break;

        case 0xfe:
            dev->pci_conf[addr] = val & 0x0c;
            break;

        case 0xff:
            dev->pci_conf[addr] = val;
            break;

        default:
            break;
    }

    reg = dev->pci_conf[0xc4] | (dev->pci_conf[0xc5] << 8);
    if ((addr == 0xc4 || addr == 0xc5) && (reg < 0x0200)) {
        intel_845_set_tom_reg(dev, intel_845_default_tom_reg());
        intel_845_smram_recalc(dev);
    }
}

static uint8_t
intel_845_read(int func, int addr, UNUSED(int len), void *priv)
{
    const intel_845_t *dev = (intel_845_t *) priv;
    uint8_t            ret;

    if (func)
        ret = 0xff;
    else if ((addr == 0xa4) && (dev->pci_conf[0xb0] & 0x01))
        ret = (dev->pci_conf[addr] & ~0x07) | 0x01;
    else
        ret = dev->pci_conf[addr];

    intel_845_log("Intel 845 MCH: dev->regs[%02x] (%02x)\n", addr, ret);

    return ret;
}

static void
intel_845_reset(void *priv)
{
    intel_845_t *dev = (intel_845_t *) priv;

    if (dev->tseg_size != 0) {
        mem_set_mem_state_both(dev->tseg_base, dev->tseg_size, MEM_READ_INTERNAL | MEM_WRITE_INTERNAL);
        dev->tseg_base = dev->tseg_size = 0;
    }

    memset(dev->pci_conf, 0x00, sizeof(dev->pci_conf));
    memset(dev->mchbar_regs, 0x00, sizeof(dev->mchbar_regs));
    memset(dev->subsystem_locked, 0x00, sizeof(dev->subsystem_locked));

    dev->pci_conf[0x00] = 0x86; /* VID - Intel */
    dev->pci_conf[0x01] = 0x80;
    dev->pci_conf[0x02] = 0x30; /* DID - 82845 MCH */
    dev->pci_conf[0x03] = 0x1a;
    dev->pci_conf[0x04] = 0x06; /* PCICMD */
    dev->pci_conf[0x06] = 0x90; /* PCISTS */
    dev->pci_conf[0x08] = 0x04; /* RID - B0 stepping */
    dev->pci_conf[0x0b] = 0x06; /* BCC - bridge */
    dev->pci_conf[0x10] = 0x08; /* APBASE */
    dev->pci_conf[0x34] = 0xa0; /* CAPPTR */
    dev->pci_conf[0x78] = 0x10; /* DRT */
    dev->pci_conf[0x9d] = 0x02; /* SMRAM */
    dev->pci_conf[0x9e] = 0x38; /* ESMRAMC */
    dev->pci_conf[0xa0] = 0x02; /* ACAPID */
    dev->pci_conf[0xa2] = 0x20;
    dev->pci_conf[0xa4] = 0x17; /* AGPSTAT */
    dev->pci_conf[0xa5] = 0x02;
    dev->pci_conf[0xa7] = 0x1f;
    intel_845_set_tom_reg(dev, intel_845_default_tom_reg()); /* TOM */
    dev->pci_conf[0xe4] = 0x09; /* CAPID */
    dev->pci_conf[0xe5] = 0xa0;
    dev->pci_conf[0xe6] = 0x04;
    dev->pci_conf[0xe7] = 0xf1;

    spd_write_drbs_intel_845(dev->pci_conf);
    intel_845_agp_aperture(dev);
    intel_845_gart_table(dev);
    intel_845_mchbar_recalc(dev);

    for (int i = 0x90; i <= 0x96; i++)
        intel_845_pam_recalc(i, dev->pci_conf[i]);

    intel_845_fdhc_recalc(dev);
    intel_845_smram_recalc(dev);
}

static void
intel_845_close(void *priv)
{
    intel_845_t *dev = (intel_845_t *) priv;

    smram_del(dev->c_segment);
    smram_del(dev->h_segment);
    smram_del(dev->tseg_segment);
    mem_mapping_disable(&dev->mchbar_mapping);
    free(dev);
}

static void *
intel_845_init(UNUSED(const device_t *info))
{
    intel_845_t *dev = (intel_845_t *) calloc(1, sizeof(intel_845_t));

    cpu_set_pci_speed(33333333);
    cpu_set_agp_speed(66666667);

    pci_add_card(PCI_ADD_NORTHBRIDGE, intel_845_read, intel_845_write, dev, &dev->pci_slot);

    device_add(&intel_845_agp_device);
    dev->agpgart = device_add(&agpgart_device);

    mem_mapping_add(&dev->mchbar_mapping, 0, 0,
                    intel_845_mchbar_readb,
                    intel_845_mchbar_readw,
                    intel_845_mchbar_readl,
                    intel_845_mchbar_writeb,
                    intel_845_mchbar_writew,
                    intel_845_mchbar_writel,
                    NULL, MEM_MAPPING_EXTERNAL, dev);

    cpu_cache_int_enabled = 1;
    cpu_cache_ext_enabled = 1;
    cpu_update_waitstates();

    dev->c_segment    = smram_add();
    dev->h_segment    = smram_add();
    dev->tseg_segment = smram_add();

    intel_845_reset(dev);
    return dev;
}

const device_t intel_845_device = {
    .name          = "Intel 845 MCH Bridge",
    .internal_name = "intel_845",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = intel_845_init,
    .close         = intel_845_close,
    .reset         = intel_845_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
