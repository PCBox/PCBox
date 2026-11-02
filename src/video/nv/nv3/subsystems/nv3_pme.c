/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 pme: Nvidia Mediaport - External MPEG Decode Interface
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2025 starfrost
 *          Copyright 2026 Marcos Alves
 */

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h> // DEPENDENT!!!
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

/* PRM Alpha 9, section 6.2: NOTIFY is buffer completion; VMI is bit 4
   and is not implemented on NV3. Only NOTIFY reaches PMC. */

uint32_t nv3_pme_read(uint32_t address) 
{ 
    uint32_t ret = 0x00;
    
    switch (address)
    {
        case NV3_PME_INTR:
            ret = nv3->pme.intr & NV3_PME_INTR_MASK;
            break;
        case NV3_PME_INTR_EN:
            ret = nv3->pme.intr_en;
            break;
    }

    return ret;
}

void nv3_pme_write(uint32_t address, uint32_t value) 
{
    switch (address)
    {
        case NV3_PME_INTR:
            nv3->pme.intr &= ~(value & NV3_PME_INTR_MASK);
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PME_INTR_EN:
            nv3->pme.intr_en = value & NV3_PME_INTR_MASK;
            nv3_pmc_handle_interrupts(true);
            break;
    }
}