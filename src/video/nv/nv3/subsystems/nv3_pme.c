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
 *
 *          Copyright 2024-2025 starfrost
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

void nv3_pme_init(void)
{  
    nv_log("Initialising PME...");

    nv_log("Done\n");
}

uint32_t nv3_pme_read(uint32_t address) 
{ 

    uint32_t ret = 0x00;

    // todo: friendly logging

    // Interrupt state:
    // Bit 0 - Image Notifier
    // Bit 4 - Vertical Blank Interval Notifier
    // Bit 8 - Video Notifier
    // Bit 12 - Audio Notifier
    // Bit 16 - VMI Notifer
    switch (address)
    {
        case NV3_PME_INTR:
            ret = nv3->pme.intr;
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
        // Interrupt state:
        // Bit 0 - Image Notifier
        // Bit 4 - Vertical Blank Interfal Notifier
        // Bit 8 - Video Notifier
        // Bit 12 - Audio Notifier
        // Bit 16 - VMI Notifer

        case NV3_PME_INTR:
            nv3->pme.intr &= ~value;
            nv3_pmc_clear_interrupts();
            break;
        case NV3_PME_INTR_EN:
            nv3->pme.intr_en = value & 0x00001111;
            break;
    }

}