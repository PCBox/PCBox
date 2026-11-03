/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- PME, the media engine behind the
 *          bidirectional media port.
 *
 *          No board here has anything on its media port: no video
 *          decoder, no MPEG/DVD daughtercard. The port is idle, so the
 *          engine never fills a capture buffer, never polls a module
 *          and never raises an interrupt -- what a real card with an
 *          empty connector does.
 *
 *          The TNT datasheets describe the port's protocol but not the
 *          engine's registers; the RIVA 128 programming reference
 *          (chapter 6, "Privileged Media Engine"), the nearest
 *          document, gives the interrupt pair, taken here for the TNT
 *          too: INTR_0 at 100h, whose bits only the engine sets and a 1
 *          written clears, and INTR_EN_0 at 140h. The unit's other
 *          registers (configuration, buffer addresses and states) read
 *          back what was written: with the port idle the engine never
 *          changes them.
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

#define NV4_PME_INTR_0    0x200100
#define NV4_PME_INTR_EN_0 0x200140

#define NV4_PME_REG(a) nv4->pme_regs[(((a) - NV4_PME_START) >> 2) & 0x3ff]

void
nv4_pme_reset(nv4_t *nv4)
{
    memset(nv4->pme_regs, 0, sizeof(nv4->pme_regs));
}

uint32_t
nv4_pme_read(nv4_t *nv4, uint32_t addr)
{
    return NV4_PME_REG(addr);
}

void
nv4_pme_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    switch (addr & ~3u) {
        case NV4_PME_INTR_0:
            /* writing 1 resets a bit, 0 leaves it */
            NV4_PME_REG(addr) &= ~val;
            nv4_pmc_update_irq(nv4);
            break;
        case NV4_PME_INTR_EN_0:
            NV4_PME_REG(addr) = val;
            nv4_pmc_update_irq(nv4);
            break;
        default:
            NV4_PME_REG(addr) = val;
            break;
    }
}

/* PMC_INTR_0's PMEDIA bit: an engine interrupt pending and enabled. */
uint32_t
nv4_pme_pending(nv4_t *nv4)
{
    return NV4_PME_REG(NV4_PME_INTR_0) & NV4_PME_REG(NV4_PME_INTR_EN_0);
}
