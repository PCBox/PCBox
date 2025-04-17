/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          82093AA I/O APIC emulation.
 *
 *
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2023 Cacodemon345.
 */

/* Code based on https://github.com/copy/v86/blob/master/src/ioapic.js */
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>

#include <86box/86box.h>
#include "cpu/cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/mem.h>

#include <86box/apic.h>

/* Only one processor is emulated */
ioapic_t* current_ioapic = NULL;

void apic_ioapic_set_base(uint8_t x_base, uint8_t y_base)
{
    if (!current_ioapic)
        return;

    mem_mapping_set_addr(&current_ioapic->ioapic_mem_window, 0xFEC00000 | ((y_base & 0x3) << 8) | ((x_base & 0xF) << 16), 0x20);

    pclog("I/O APIC base: 0x%08X\n", current_ioapic->ioapic_mem_window.base);
}

void
ioapic_i82093aa_reset(ioapic_t* ioapic)
{
    int i = 0;

    ioapic->ioapic_index = 0;

    for (i = 0; i < 256; i++) {
        ioapic->ioapic_regs[i] = 0;
    }
    for (i = 0; i < IOAPIC_RED_TABL_SIZE; i++) {
        ioapic->ioredtabl_s[i].intr_mask = 1;
    }
    pclog("IOAPIC: RESET!\n");
}

void
apic_ioapic_lapic_interrupt_check(ioapic_t* ioapic, uint8_t irq)
{
    uint32_t mask = 1 << irq;
    apic_ioredtable_t service_parameters;

    if (irq >= 24)
        return;

    service_parameters = ioapic->ioredtabl_s[irq];

    if (service_parameters.delmod == 0b111) {
        return;
    }

    if (!(ioapic->irr & mask))
        return;

    if (ioapic->ioredtabl_s[irq].intr_mask)
        return;
    
    if (service_parameters.delmod == 2 || service_parameters.delmod == 4 || service_parameters.delmod == 5)
        service_parameters.trigmode = 0;
    
    if (service_parameters.trigmode == 0) {
        ioapic->irr &= ~mask;
    } else {
        ioapic->ioredtabl_s[irq].rirr = 1;
        if (service_parameters.rirr == 1) {
            return;
        }
    }


    if(current_lapic) {
        if (service_parameters.destmod == 0 && (service_parameters.dest_mask & 0xf) != ((current_lapic->lapic_id >> 24) & 0xf)) {
            pclog("LAPIC ID does not match\n");
            return;
        }
        if (service_parameters.destmod == 1 && !(((uint8_t)(service_parameters.dest_mask)) & (1 << ((current_lapic->lapic_id >> 24) & 0xff)))) {
            pclog("LAPIC ID is not in set\n");
            return;
        }
        lapic_service_interrupt(current_lapic, service_parameters);
    }
}

void apic_ioapic_service_all(void* priv);

void
apic_ioapic_set_irq(ioapic_t* ioapic, uint8_t irq, int level)
{
    uint32_t mask = 1 << irq;

    if (irq >= 24)
        return;

    if (level && (ioapic->irq_level & mask))
        return;
    else if (level)
        ioapic->irq_level |= mask;

    {
        if ((ioapic->ioredtabl[irq] & (IOAPIC_TRIGMODE_MASK | IOAPIC_INTERRUPT_MASK)) == (IOAPIC_INTERRUPT_MASK)) {
            if (level)
                apic_ioapic_clear_irq(ioapic, irq);

            return;
        }
        ioapic->irr |= mask;
    }

    apic_ioapic_service_all(ioapic);
    if (!level)
        apic_ioapic_clear_irq(ioapic, irq);
}

void
apic_ioapic_clear_irq(ioapic_t* ioapic, uint8_t irq)
{
    uint32_t mask = 1 << irq;

    if (irq >= 24)
        return;

    ioapic->irq_level &= ~mask;

    if ((ioapic->irr & mask) == mask) {
        ioapic->irr &= ~mask;
    }
}

void
apic_ioapic_service_all(void* priv)
{
    ioapic_t* ioapic = (ioapic_t*)priv;

    for (int i = 0; i < 24; i++)
        apic_ioapic_lapic_interrupt_check(ioapic, i);
}

void
apic_lapic_ioapic_remote_eoi(ioapic_t* ioapic, uint8_t vector)
{
    int i = 0;

    for (i = 0; i < IOAPIC_RED_TABL_SIZE; i++) {
        if (ioapic->ioredtabl_s[i].intvec == vector && ioapic->ioredtabl_s[i].rirr) {
            ioapic->ioredtabl_s[i].rirr = 0;
            apic_ioapic_lapic_interrupt_check(ioapic, i);
        }
    }
}

uint32_t
ioapic_i82093aa_readl(uint32_t addr, void *priv)
{
    ioapic_t *dev = (ioapic_t *)priv;
    uint32_t ret = (uint32_t)-1;

    switch (addr & 0xFF)
    {
        case 0x00:
            return dev->ioapic_index;
        case 0x10:
            addr = dev->ioapic_index;
            break;
        default:
            return -1;
    }

    switch (addr) {
        case 0:
            ret = dev->ioapicd;
            break;
        case 1:
            ret = dev->extended ? 0x170012 : 0x170011;
            break;
        case 2:
            ret = dev->ioapicarb;
            break;
        default:
            if (addr >= 0x10 && addr <= 0x3F)
                ret = dev->ioredtabl_l[addr - 0x10];
            break;
    }
    pclog("IOAPIC read reg 0x%08X (index 0x%X)\n", ret, dev->ioapic_index);
    return ret;
}

void
ioapic_i82093aa_writel(uint32_t addr, uint32_t val, void *priv)
{
    ioapic_t *dev = (ioapic_t *)priv;

    switch (addr & 0xFF)
    {
        case 0x00:
            dev->ioapic_index = val & 0xFF;
            pclog("IOAPIC index: 0x%02X\n", val & 0xFF);
            return;
        case 0x10:
            addr = dev->ioapic_index;
            pclog("IOAPIC write data: 0x%08X\n", val);
            break;
        case 0x20:
            if (dev->extended)
                apic_ioapic_set_irq(dev, val & 0x1f, 0);
            return;
        case 0x40:
            if (dev->extended)
                apic_lapic_ioapic_remote_eoi(dev, val & 0xff);
            return;
        default:
            return;
    }

    switch (addr) {
        case 0:
            dev->ioapicd = val;
            break;
        default:
            if (addr >= 0x10 && addr <= 0x3F)
            {
                addr -= 0x10;
                uint8_t orig_rirr   = dev->ioredtabl_s[addr >> 1].rirr;
                uint8_t orig_delivs = dev->ioredtabl_s[addr >> 1].delivs;
                dev->ioredtabl_l[addr] = val;
                dev->ioredtabl_s[addr >> 1].reserved = 0;
                dev->ioredtabl_s[addr >> 1].rirr = (dev->ioredtabl_s[addr >> 1].trigmode == 0) ? 0 : orig_rirr;
                dev->ioredtabl_s[addr >> 1].delivs = orig_delivs;
                apic_ioapic_lapic_interrupt_check(dev, addr >> 1);
            }
            break;
    }
}

uint8_t
ioapic_i82093aa_read(uint32_t addr, void *priv)
{
    return (ioapic_i82093aa_readl(addr & ~3, priv) >> (8 * (addr & 3))) & 0xFF;
}

uint16_t
ioapic_i82093aa_readw(uint32_t addr, void *priv)
{
    return ioapic_i82093aa_read(addr & ~3, priv) | (ioapic_i82093aa_read((addr & ~3) + 1, priv) << 8);
}

void*
ioapic_i82093aa_init(const device_t* info)
{
    ioapic_t *dev = NULL;
    dev = (ioapic_t *) calloc(sizeof(ioapic_t), 1);
    current_ioapic = dev;

    mem_mapping_add(&dev->ioapic_mem_window, 0xFEC00000, 0x20, ioapic_i82093aa_read, ioapic_i82093aa_readw, ioapic_i82093aa_readl, NULL, NULL, ioapic_i82093aa_writel, NULL, MEM_MAPPING_EXTERNAL, dev);
    timer_add(&dev->ioapic_service, apic_ioapic_service_all, dev, 0);
    ioapic_i82093aa_reset(dev);

    extern const device_t ioapic_device;
    device_add(&ioapic_device);
    return dev;
}

void
ioapic_i82093aa_close(void *priv)
{
    ioapic_t *dev = (ioapic_t *)priv;
    if(dev != NULL)
    {
        mem_mapping_disable(&dev->ioapic_mem_window);
        current_ioapic = NULL;
        free(priv);
    }
}

const device_t i82093aa_ioapic_device = {
    .name          = "Intel 82093AA I/O Advanced Programmable Interrupt Controller",
    .internal_name = "ioapic_82093aa",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = ioapic_i82093aa_init,
    .close         = ioapic_i82093aa_close,
    .reset         = (void (*)(void*))ioapic_i82093aa_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};