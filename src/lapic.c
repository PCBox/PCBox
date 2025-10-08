/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Local APIC emulation.
 *
 *
 *
 * Authors: Cacodemon345
 *
 *          Copyright 2023 Cacodemon345.
 */
#include <stdint.h>
#include <stdlib.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdbool.h>

#include <86box/86box.h>
#include "cpu/cpu.h"
#include <86box/timer.h>
#include <86box/device.h>
#include <86box/mem.h>

#include <86box/apic.h>
#include <86box/pic.h> /*ExtINT*/

/* Needed to set the default Local APIC base. */
#include "cpu/cpu.h"
#include "cpu/x86seg.h"

#define INITIAL_LAPIC_ADDRESS 0xFEE00000

extern double cpuclock;
extern int nmi;

lapic_t* current_lapic;

static __inline uint8_t
lapic_get_bit_irr(lapic_t *lapic, uint8_t bit)
{
    return lapic->irr_ll[bit / 64] & (1ull << (uint64_t)(bit & 63ull));
}

static __inline void
lapic_set_bit_irr(lapic_t *lapic, uint8_t bit, uint8_t val)
{
    lapic->irr_ll[bit / 64] &= ~(1ull << (bit & 63ull));
    lapic->irr_ll[bit / 64] |= (((uint64_t)!!val) << (uint64_t)(bit & 63ull));
}

static __inline uint8_t
lapic_get_bit_isr(lapic_t *lapic, uint8_t bit)
{
    return lapic->isr_ll[bit / 64] & (1ull << (uint64_t)(bit & 63ull));
}

static __inline void
lapic_set_bit_isr(lapic_t *lapic, uint8_t bit, uint8_t val)
{
    lapic->isr_ll[bit / 64] &= ~(1ull << (bit & 63));
    lapic->isr_ll[bit / 64] |= (((uint64_t)!!val) << (uint64_t)(bit & 63ull));
}

static __inline uint8_t
lapic_get_bit_tmr(lapic_t *lapic, uint8_t bit)
{
    return lapic->tmr_ll[bit / 64] & (1ull << (uint64_t)(bit & 63ull));
}

static __inline void
lapic_set_bit_tmr(lapic_t *lapic, uint8_t bit, uint8_t val)
{
    lapic->tmr_ll[bit / 64] &= ~(1ull << (uint64_t)(bit & 63ull));
    lapic->tmr_ll[bit / 64] |= (((uint64_t)!!val) << (uint64_t)(bit & 63ull));
}

int lapic_is_pic_enabled(void)
{
    if (!current_ioapic)
        return true;

    return (current_lapic->lapic_mem_window.enable == 0 || current_lapic->lapic_lvt_lvt0.intr_mask == 0 || (current_ioapic->ioredtabl_s[0].delmod == 7 && current_ioapic->ioredtabl_s[0].intr_mask == 0));
}

/* Find first bit starting from msb */
static int apic_fls_bit(uint32_t value)
{
    return 31 - __builtin_clz(value);
}

/* return -1 if no bit is set */
static int get_highest_priority_int(uint32_t *tab)
{
    int i;
    for (i = 7; i >= 0; i--) {
        if (tab[i] != 0) {
            return i * 32 + apic_fls_bit(tab[i]);
        }
    }
    return -1;
}

static int lapic_get_ppr(lapic_t *lapic)
{
    int tpr, isrv, ppr;

    tpr = (lapic->lapic_tpr >> 4);
    isrv = get_highest_priority_int(lapic->isr_l);
    if (isrv < 0)
        isrv = 0;
    isrv >>= 4;
    if (tpr >= isrv)
        ppr = lapic->lapic_tpr;
    else
        ppr = isrv << 4;
    return ppr;
}

static int lapic_get_apr(lapic_t *lapic)
{
  uint32_t tpr  = (lapic->lapic_tpr >> 4) & 0xf;
  int first_isr = get_highest_priority_int(lapic->isr_l);
  if (first_isr < 0) first_isr = 0;
  int first_irr = get_highest_priority_int(lapic->irr_l);
  if (first_irr < 0) first_irr = 0;
  uint32_t isrv = (first_isr >> 4) & 0xf;
  uint32_t irrv = (first_irr >> 4) & 0xf;
  uint8_t apr;

  if((tpr >= irrv) && (tpr > isrv)) {
    apr = lapic->lapic_tpr & 0xff;
  }
  else {
    apr = ((tpr & isrv) > irrv) ?(tpr & isrv) : irrv;
    apr <<= 4;
  }

  return (uint8_t)apr;
}

int lapic_irq_pending(lapic_t *lapic)
{
    int irrv, ppr;

    if (!(lapic->lapic_spurious_interrupt & 0x100)) {
        //pclog("APIC disabled\n");
        return 0;
    }

    irrv = get_highest_priority_int(lapic->irr_l);
    if (irrv < 0) {
        //pclog("APIC irrv < 0\n");
        return -1;
    }

    ppr = lapic_get_ppr(lapic);
    if (ppr && (irrv & 0xf0) <= (ppr & 0xf0)) {
        //pclog("APIC irrv < ppr\n");
        return -1;
    }

    return irrv;
}

void
lapic_reset(lapic_t *lapic)
{
    /* Always set lapic_id and lapic_arb to 0, regardless of soft/hard resets. */
    lapic->lapic_id = lapic->lapic_arb = 0;
    lapic->tmr_ll[0] = lapic->tmr_ll[1] = lapic->tmr_ll[2] = lapic->tmr_ll[3] = 
    lapic->irr_ll[0] = lapic->irr_ll[1] = lapic->irr_ll[2] = lapic->irr_ll[3] = 
    lapic->isr_ll[0] = lapic->isr_ll[1] = lapic->isr_ll[2] = lapic->isr_ll[3] = 0;

    lapic->lapic_timer_divider = lapic->lapic_timer_initial_count = lapic->lapic_timer_current_count = 0;
    lapic->lapic_tpr           = 0;
    lapic->icr                 = 0;
    lapic->lapic_id            = 0;

    lapic->lapic_lvt_timer_val   =
    lapic->lapic_lvt_perf_val    =
    lapic->lapic_lvt_lvt0_val    =
    lapic->lapic_lvt_lvt1_val    =
    lapic->lapic_lvt_thermal_val = 1 << 16;

    lapic->lapic_lvt_error_val = 0;
    lapic->lapic_lvt_read_error_val = 0;

    lapic->lapic_spurious_interrupt = 0xFF;
    lapic->lapic_dest_format        = -1;
    lapic->lapic_local_dest         = 0;

    pclog("LAPIC: RESET!\n");
}

void
apic_lapic_writel(uint32_t addr, uint32_t val, void *priv)
{
    lapic_t *dev = (lapic_t *)priv;
    int bit = 0;
    uint32_t timer_current_count = dev->lapic_timer_current_count;
    apic_ioredtable_t deliverstruct = { 0 };
    uint8_t mode;

    addr -= dev->lapic_mem_window.base;

    //if (addr != 0x80 && addr != 0x300)
      //  pclog("Local APIC: [W] %04X = %08X\n", addr, val);

    if (addr < 0x400)  switch (addr & 0x3ff) {
        case 0x020:
            dev->lapic_id = val;
            pclog("LAPIC ID: 0x%02X\n", val);
            break;

        case 0x080:
            dev->lapic_tpr = val & 0xFF;
            if (lapic_irq_pending(dev) > 0)
                cpu_block_end = 1;
            break;

        case 0x0b0:
            bit = get_highest_priority_int(dev->isr_l);
            if (bit != -1) {
                lapic_set_bit_isr(dev, bit, 0);
                if (lapic_get_bit_tmr(dev, bit) && current_ioapic) {
                    lapic_set_bit_tmr(dev, bit, 0);
                    apic_lapic_ioapic_remote_eoi(current_ioapic, bit);
                    if (lapic_irq_pending(dev) > 0)
                        cpu_block_end = 1;
                }
            }
            break;

        case 0x0d0:
            dev->lapic_local_dest = val & 0xff000000;
            break;

        case 0x0e0:
            dev->lapic_dest_format = val | 0xffffff;
            break;
        
        case 0x0f0:
            dev->lapic_spurious_interrupt = val;
            if (!(val & 0x100)) {
                dev->lapic_lvt_timer_val   =
                dev->lapic_lvt_perf_val    =
                dev->lapic_lvt_lvt0_val    =
                dev->lapic_lvt_lvt1_val    =
                dev->lapic_lvt_thermal_val = 1 << 16;
            }
            break;

        case 0x280:
            dev->lapic_lvt_read_error_val = dev->lapic_lvt_error_val;
            dev->lapic_lvt_error_val = 0;
            break;

        case 0x300:
            dev->icr0 = val;

            deliverstruct.intvec = dev->icr & 0xFF;
            deliverstruct.delmod = (dev->icr & 0x700) >> 8;
            deliverstruct.trigmode = 0;

            switch (deliverstruct.delmod) {
                case 5:
                    return; /* INIT Level Deassert not needed to be implemented. */
                case 6:
                    break;
            }

            switch ((dev->icr0 >> 18) & 3) {
                case 0:
                    mode = (dev->icr >> 11) & 1;
                    if (mode == 0 && ((dev->icr >> 56) & 0xf) != ((dev->lapic_id >> 24) & 0xf))
                        break;
                    if (mode == 1 && !(((uint8_t)(dev->icr >> 56)) & (1 << ((dev->lapic_id >> 24) & 0xff))))
                        break;
                case 1:
                case 2:
                    if (deliverstruct.delmod == 6) {
                        loadcs((deliverstruct.intvec & 0xff) << 8);
                        cpu_state.oldpc = cpu_state.pc;
                        cpu_state.pc = 0;
                        cpu_block_end = 1;
                        pclog("SIPI jump\n");
                    } else
                        lapic_service_interrupt(dev, deliverstruct);
                    break;
            }

            dev->icr0 &= ~(1 << 12);
            break;

        case 0x310:
            dev->icr1 = val;
            break;

        case 0x320:
            dev->lapic_lvt_timer_val = val;
            break;

        case 0x330:
            dev->lapic_lvt_thermal_val = val;
            break;

        case 0x340:
            dev->lapic_lvt_perf_val = val;
            break;

        case 0x350:
            dev->lapic_lvt_lvt0_val = val;
            break;

        case 0x360:
            dev->lapic_lvt_lvt1_val = val;
            break;

        case 0x370:
            dev->lapic_lvt_error_val = val;
            break;

        case 0x380:
#ifdef USE_DYNAREC
            if (cpu_use_dynarec)
                update_tsc();
#endif
            dev->lapic_timer_initial_count = dev->lapic_timer_current_count = val;
            dev->lapic_timer_remainder = 0;
            cpu_block_end = 1;
            //pclog("APIC: Timer count: %u\n", dev->lapic_timer_initial_count);
            break;

        case 0x3e0:
#ifdef USE_DYNAREC
            if (cpu_use_dynarec)
                update_tsc();
#endif
            dev->lapic_timer_divider = val & 0xb;
            //pclog("APIC: Timer divider: 0x%01X\n", dev->lapic_timer_divider);
            break;
    }
}

uint32_t
apic_lapic_readl(uint32_t addr, void *priv)
{
    lapic_t *dev = (lapic_t *)priv;
    uint32_t ret = 0x0;

    addr -= dev->lapic_mem_window.base;
    addr &= ~3;

    if ((addr < 0x400)) {
        if (addr >= 0x100 && addr <= 0x170)
            ret = dev->isr_l[(addr - 0x100) >> 4];
        else if (addr >= 0x180 && addr <= 0x1f0)
            ret = dev->tmr_l[(addr - 0x180) >> 4];
        else if (addr >= 0x200 && addr <= 0x270)
            ret = dev->irr_l[(addr - 0x200) >> 4];
        else switch (addr & 0x3ff) {
        case 0x020:
            ret = dev->lapic_id;
            break;

        case 0x030:
            ret = 0x00040012;
            break;

        case 0x080:
            ret = dev->lapic_tpr;
            break;

        case 0x090:
            ret = lapic_get_apr(dev);
            break;

        case 0x0a0:
            ret = lapic_get_ppr(dev);
            break;

        case 0x0d0:
            ret = dev->lapic_local_dest;
            break;

        case 0x0e0:
            ret = dev->lapic_dest_format;
            break;

        case 0x0f0:
            ret = dev->lapic_spurious_interrupt;
            break;

        case 0x280:
            ret = dev->lapic_lvt_read_error_val;
            break;

        case 0x300:
            ret = dev->icr0;
            break;

        case 0x310:
            ret = dev->icr1;
            break;

        case 0x320:
            ret = dev->lapic_lvt_timer_val;
            break;

        case 0x330:
            ret = dev->lapic_lvt_thermal_val;
            break;

        case 0x340:
            ret = dev->lapic_lvt_perf_val;
            break;

        case 0x350:
            ret = dev->lapic_lvt_lvt0_val;
            break;

        case 0x360:
            ret = dev->lapic_lvt_lvt1_val;
            break;

        case 0x370:
            ret = dev->lapic_lvt_error_val;
            break;

        case 0x380:
            ret = dev->lapic_timer_initial_count;
            break;

        case 0x390:
            //pclog("APIC: Read current timer count %u\n", dev->lapic_timer_current_count);
#ifdef USE_DYNAREC
            if (cpu_use_dynarec)
                update_tsc();
#endif
            ret = dev->lapic_timer_current_count;
            break;

        case 0x3e0:
            ret = dev->lapic_timer_divider;
            break;
    }
    }
    //if (addr != 0x80 && addr != 0x300)
      //  pclog("Local APIC: [R] %04X = %08X\n", addr, ret);

    return ret;
}

void
apic_lapic_write(uint32_t addr, uint8_t val, void *priv)
{
    uint32_t mask = 0xFFFFFFFF & ~(0xFF << (8 * (addr & 3)));

    return apic_lapic_writel(addr, (apic_lapic_readl(addr, priv) & mask) | ((val << (8 * (addr & 3)))), priv);
}

uint8_t
apic_lapic_read(uint32_t addr, void *priv)
{
    return (apic_lapic_readl(addr, priv) >> (8 * (addr & 3))) & 0xFF;
}

void apic_lapic_writew(uint32_t addr, uint16_t val, void *priv)
{
    apic_lapic_write(addr, val & 0xFF, priv);
    apic_lapic_write(addr + 1, (val >> 8) & 0xFF, priv);
}

uint16_t
apic_lapic_readw(uint32_t addr, void *priv)
{
    return apic_lapic_read(addr, priv) | (apic_lapic_read(addr + 1, priv) << 8);
}

void
apic_lapic_set_base(uint32_t base)
{
    if (!current_lapic)
        return;

    mem_mapping_set_addr(&current_lapic->lapic_mem_window, base & 0xFFFFF000, 0x100000);
    if (base & (1 << 11)) {
        mem_mapping_disable(&current_lapic->lapic_mem_window);
        current_lapic->lapic_spurious_interrupt &= ~0x100;
    }
}

static void
lapic_service_local_interrupt(lapic_t *lapic, apic_ioredtable_t interrupt, bool lvt01)
{
    if (interrupt.intr_mask) {
        //pclog("Local interrupt vector 0x%08llX masked.\n", *((uint64_t*)&interrupt));
        return;
    }

    switch (interrupt.delmod) {
        case 0:
        case 1:
        case 7:
            lapic_set_bit_irr(lapic, interrupt.intvec, 1);
            lapic_set_bit_tmr(lapic, interrupt.intvec, !lvt01 ? 0 : !!interrupt.trigmode);
            if (lapic_irq_pending(lapic) > 0)
                cpu_block_end = 1;
            break;
        case 2:
            smi_raise();
            return;
        case 4:
            nmi = 1;
            return;
        case 5: /*INIT*/
            {
                softresetx86();
                cpu_set_edx();
                flushmmucache();
                lapic_reset(lapic);
                return;
            }
    }
}

#if 0
void apic_deliver_pic_intr(lapic_t *dev, int level)
{
    if (level) {
        lapic_service_local_interrupt(dev, dev->lapic_lvt_lvt0, true);
    } else {
        switch ((dev->lapic_lvt_lvt0_val >> 8) & 7) {
        case 0:
            if (!(dev->lapic_lvt_lvt0.trigmode))
                break;
            lapic_set_bit_irr(dev, dev->lapic_lvt_lvt0.intvec, 0);
            break;
        }
    }
}

static bool lapic_check_pic(lapic_t *dev)
{
    if (!lapic_is_pic_enabled() || !pic.int_pending) {
        if (lapic_is_pic_enabled())
            apic_deliver_pic_intr(dev, 0);
        return false;
    }
    apic_deliver_pic_intr(dev, 1);
    return true;
}
#endif

void
lapic_timer_advance_ticks(uint32_t ticks)
{
    lapic_t *dev = (lapic_t *)current_lapic;
    uint32_t timer_divider_value = 1 << (1 + (dev->lapic_timer_divider & 3) + ((dev->lapic_timer_divider & 0x8) >> 1));

    if (dev->lapic_timer_divider == 0xB) {
        timer_divider_value = 1;
    }

    if (ticks == 0)
        return;

count:
    if (dev->lapic_timer_current_count) {
        dev->lapic_timer_remainder += ticks;
        if (dev->lapic_timer_remainder >= timer_divider_value) {
            ticks = dev->lapic_timer_remainder / timer_divider_value;
            dev->lapic_timer_remainder %= timer_divider_value;
            if (ticks >= dev->lapic_timer_current_count) {
                ticks -= dev->lapic_timer_current_count;
                dev->lapic_timer_current_count = 0;
                dev->lapic_timer_remainder = 0;
                lapic_service_local_interrupt(dev, dev->lapic_lvt_timer, false);
                if (dev->lapic_lvt_timer.timer_mode == 1) {
                    dev->lapic_timer_current_count = (dev->lapic_timer_initial_count);
                    //pclog("APIC: Timer restart\n");
                    if (ticks)
                        goto count;
                } else {
                    //pclog("APIC: Timer one-shot finish\n");
                }
            } else {
                dev->lapic_timer_current_count -= ticks;
            }
        }
    }
}

void*
lapic_init(const device_t* info)
{
    lapic_t *dev = NULL;
    
    dev = (lapic_t *) calloc(sizeof(lapic_t), 1);
    current_lapic = dev;

    msr.apic_base = INITIAL_LAPIC_ADDRESS | (1 << 11) | (1 << 8);
    mem_mapping_add(&dev->lapic_mem_window, INITIAL_LAPIC_ADDRESS, 0x100000, apic_lapic_read, apic_lapic_readw, apic_lapic_readl, NULL, NULL, apic_lapic_writel, NULL, MEM_MAPPING_EXTERNAL, dev);
    lapic_reset(dev);
    return dev;
}

uint8_t
apic_lapic_picinterrupt(void)
{
    lapic_t *lapic = current_lapic;
    int intno = lapic_irq_pending(lapic);

    if (intno < 0)
        return lapic->lapic_spurious_interrupt & 0xFF;

    lapic_set_bit_irr(lapic, intno, 0);
    lapic_set_bit_isr(lapic, intno, 1);

    //pclog("LAPIC: Service INTVEC 0x%02X\n", intno);
    if (lapic_irq_pending(lapic) > 0)
        cpu_block_end = 1;
    return intno;
}

void
apic_lapic_service_nmi(void)
{
    lapic_service_local_interrupt(current_lapic, current_lapic->lapic_lvt_lvt1, true);
}

void
lapic_service_interrupt(lapic_t *lapic, apic_ioredtable_t interrupt)
{
    switch (interrupt.delmod) {
        case 2:
            smi_raise();
            return;
        case 4:
            nmi = 1;
            return;
        case 5: /*INIT*/
            {
                softresetx86();
                cpu_set_edx();
                flushmmucache();
                lapic_reset(lapic);
                return;
            }
    }
    
    if (interrupt.delmod == 0 && lapic_get_bit_irr(lapic, interrupt.intvec))
        return;

    lapic_set_bit_irr(lapic, interrupt.intvec, 1);
    lapic_set_bit_tmr(lapic, interrupt.intvec, !!interrupt.trigmode);
    if (lapic_irq_pending(lapic) > 0)
        cpu_block_end = 1;
    //pclog("LAPIC: Interrupt 0x%X serviced\n", interrupt.intvec);
}

void
lapic_close(void* priv)
{
    lapic_t *dev = (lapic_t *)priv;
    if (dev != NULL) {
        mem_mapping_disable(&dev->lapic_mem_window);
        current_lapic = NULL;
        free(priv);
    }
}

void
lapic_speed_changed(void* priv)
{
    #if 0
    lapic_t* dev = (lapic_t*)priv;

    if (!dev->lapic_timer_current_count)
        return;

    if ((dev->lapic_timer_divider & 0xF) == 0xB)
        timer_on_auto(&dev->lapic_timer, (1000000. / cpuclock));
    else
        timer_on_auto(&dev->lapic_timer, (1000000. / cpuclock) * (1 << ((dev->lapic_timer_divider & 3) | ((dev->lapic_timer_divider & 0x8) >> 1)) + 1));
        #endif
}

int
pic_pending_int(void)
{
    if (!current_ioapic)
        return pic.int_pending;

    if (!lapic_is_pic_enabled()) {
        goto apic_only; /* PIC interrupts are completely disabled. */
    }

    if (pic.int_pending)
        return 1;

apic_only:
    return lapic_irq_pending(current_lapic) > 0;
}

const device_t lapic_device = {
    .name          = "Local Advanced Programmable Interrupt Controller",
    .internal_name = "lapic",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = lapic_init,
    .close         = lapic_close,
    .reset         = (void (*)(void*))lapic_reset,
    .available     = NULL,
    .speed_changed = lapic_speed_changed,
    .force_redraw  = NULL,
    .config        = NULL
};
