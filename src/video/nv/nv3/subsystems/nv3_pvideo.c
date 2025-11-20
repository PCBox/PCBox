/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PVIDEO - Video Overlay
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

// pvideo init code
void nv3_pvideo_init(void)
{
    nv_log("Initialising PVIDEO...Done!");
}

uint32_t nv3_pvideo_read(uint32_t address) 
{ 
    // before doing anything, check the subsystem enablement state for this subsystem

    uint32_t ret = 0x00;

    // Interrupt state:
    // Bit 0 - Notifier

    switch (address)
    {
        case NV3_PVIDEO_INTR:
            ret = nv3->pvideo.intr;
            break;
        case NV3_PVIDEO_INTR_EN:
            ret = nv3->pvideo.intr_en;
            break;
        case NV3_PVIDEO_FIFO_THRESHOLD:
            ret = nv3->pvideo.fifo_threshold;
            break;
        case NV3_PVIDEO_FIFO_BURST_LENGTH:
            ret = nv3->pvideo.fifo_burst_size & 0x03;
            break;
        case NV3_PVIDEO_OVERLAY:
            ret = nv3->pvideo.overlay_settings & 0xFF;
            break;
        
    }

    return ret;
}

void nv3_pvideo_write(uint32_t address, uint32_t value) 
{
    // before doing anything, check the subsystem enablement state for this subsystem

    switch (address)
    {
        // Interrupt state:
        // Bit 0 - Notifier

        case NV3_PVIDEO_INTR:
            nv3->pvideo.intr &= ~value;
            nv3_pmc_clear_interrupts();
            break;
        case NV3_PVIDEO_INTR_EN:
            nv3->pvideo.intr_en = value & 0x00000001;
            break;
        case NV3_PVIDEO_FIFO_THRESHOLD:
            // only bits 6:3 matter
            nv3->pvideo.fifo_threshold = ((value >> 3) & 0x0F) << 3;
            break;
        case NV3_PVIDEO_FIFO_BURST_LENGTH:
            nv3->pvideo.fifo_burst_size = value & 0x03;
            break;
        case NV3_PVIDEO_OVERLAY:
            nv3->pvideo.overlay_settings = value & 0xFF;
            break;
    }
}