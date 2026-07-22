/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel TCO Handler.
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
 */

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/io.h>
#include <86box/device.h>
#include <86box/nmi.h>
#include <86box/pic.h>
#include <86box/pit.h>
#include <86box/tco.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_TCO_LOG
int tco_do_log = ENABLE_TCO_LOG;

static void
tco_log(const char *fmt, ...)
{
    va_list ap;

    if (tco_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define tco_log(fmt, ...)
#endif

#define TCO_RLD          0x00
#define TCO_TMR          0x01
#define TCO_DAT_IN       0x02
#define TCO_DAT_OUT      0x03
#define TCO1_STS         0x04
#define TCO2_STS         0x06
#define TCO1_CNT         0x08
#define TCO2_CNT         0x0a

#define TCO_COUNT_MASK   0x3f
#define TCO_TIMEOUT_STS  0x08
#define TCO_SECOND_STS   0x02
#define TCO_NMI_NOW      0x01
#define TCO_TMR_HLT      0x08

#define TCO_TICK_USEC    600000.0

static uint8_t
tco_initial_count(const tco_t *dev)
{
    return dev->regs[TCO_TMR] & TCO_COUNT_MASK;
}

static bool
tco_timer_halted(const tco_t *dev)
{
    return !!(dev->regs[TCO1_CNT + 1] & TCO_TMR_HLT);
}

static void
tco_schedule_timer(tco_t *dev)
{
    if (tco_timer_halted(dev) || !tco_initial_count(dev))
        timer_stop(&dev->timer);
    else
        timer_on_auto(&dev->timer, TCO_TICK_USEC);
}

static void
tco_reload(tco_t *dev)
{
    dev->regs[TCO_RLD] = tco_initial_count(dev);
    tco_schedule_timer(dev);
}

static void
tco_timer_handler(void *priv)
{
    tco_t *dev = (tco_t *) priv;

    if (tco_timer_halted(dev))
        return;

    if (dev->regs[TCO_RLD] > 0)
        dev->regs[TCO_RLD]--;

    if (dev->regs[TCO_RLD] > 0) {
        tco_schedule_timer(dev);
        return;
    }

    if (!(dev->regs[TCO1_STS] & TCO_TIMEOUT_STS)) {
        dev->regs[TCO1_STS] |= TCO_TIMEOUT_STS;
        dev->regs[TCO_RLD] = tco_initial_count(dev);

        if (dev->smi_enabled)
            smi_raise();

        tco_schedule_timer(dev);
        return;
    }

    dev->regs[TCO2_STS] |= TCO_SECOND_STS;
    tco_log("TCO: second timeout reached; guest reset suppressed\n");
}

void
tco_irq_update(tco_t *dev, uint16_t new_irq)
{
    tco_log("TCO: Update IRQ to %d\n", new_irq);
    dev->tco_irq = new_irq;
}

void
tco_set_smi_enable(tco_t *dev, int enable)
{
    if (dev != NULL)
        dev->smi_enabled = !!enable;
}

void
tco_write(uint16_t addr, uint8_t val, tco_t *dev)
{
    addr -= 0x60;
    tco_log("TCO: Write 0x%02x to Register 0x%02x\n", val, addr);

    switch (addr) {
        case TCO_RLD:
            tco_reload(dev);
            break;

        case TCO_TMR:
            val &= TCO_COUNT_MASK;
            if (val >= 4)
                dev->regs[addr] = val;
            break;

        case TCO_DAT_IN:
            dev->regs[addr] = val;
            dev->regs[TCO1_STS] |= 2;
            smi_raise();
            break;

        case TCO_DAT_OUT:
            dev->regs[addr] = val;
            dev->regs[TCO1_STS] |= 4;
            picint(1 << dev->tco_irq);
            break;

        case TCO1_STS:
            dev->regs[addr] &= ~(val & 0x7f);
            break;

        case TCO1_STS + 1:
            dev->regs[addr] &= ~(val & 0x1f);
            break;

        case TCO2_STS:
            dev->regs[addr] &= ~(val & 0x07);
            break;

        case TCO2_STS + 1:
            dev->regs[addr] &= val;
            break;

        case TCO1_CNT:
            dev->regs[addr] = val;
            break;

        case TCO1_CNT + 1:
            if (val & TCO_NMI_NOW) {
                if (!nmi)
                    nmi_raise();
                dev->regs[addr] |= TCO_NMI_NOW;
            } else {
                dev->regs[addr] &= ~TCO_NMI_NOW;
            }

            dev->regs[addr] = (dev->regs[addr] & TCO_NMI_NOW) | (val & 0x1e);
            tco_schedule_timer(dev);
            break;

        case TCO2_CNT:
            dev->regs[addr] = val & 0x06; // Intrusion Interrupt or SMI. We never get intruded so we never control it.
            break;

        case 0x0c ... 0x0d:
            dev->regs[addr] = val;
            break;

        case 0x10:
            dev->regs[addr] = val & 0x03;
            break;

        default:
            break;
    }
}

uint8_t
tco_read(uint16_t addr, tco_t *dev)
{
    addr -= 0x60;

    if (addr <= 0x10) {
        tco_log("TCO: Read 0x%02x from Register 0x%02x\n", dev->regs[addr], addr);
        return dev->regs[addr];
    } else
        return 0xff;
}

static void
tco_reset(void *priv)
{
    tco_t *dev = (tco_t *) priv;
    memset(dev->regs, 0, sizeof(dev->regs));

    dev->tco_irq = 9;
    dev->smi_enabled = 0;

    dev->regs[0x00] = 0x04;
    dev->regs[0x01] = 0x04;
    dev->regs[0x09] = TCO_TMR_HLT;
    dev->regs[0x10] = 0x03;

    timer_stop(&dev->timer);
}

static void
tco_close(void *priv)
{
    tco_t *dev = (tco_t *) priv;

    timer_stop(&dev->timer);
    free(dev);
}

static void *
tco_init(UNUSED(const device_t *info))
{
    tco_t *dev = (tco_t *) malloc(sizeof(tco_t));
    memset(dev, 0, sizeof(tco_t));

    timer_add(&dev->timer, tco_timer_handler, dev, 0);
    tco_reset(dev);

    return dev;
}

const device_t tco_device = {
    .name          = "Intel TCO",
    .internal_name = "tco",
    .flags         = 0,
    .local         = 0,
    .init          = tco_init,
    .close         = tco_close,
    .reset         = tco_reset,
    .available = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
