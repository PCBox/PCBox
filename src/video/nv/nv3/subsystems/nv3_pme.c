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
/* The media port (PRM Alpha 9, ch. 6) captures CCIR656 video and VBI data from a decoder or MPEG
   daughterboard into frame buffer buffers that the resman hands over through the SU/ME/RM state
   bits. No board here has anything on the port: it never gets a pixel clock or a SAV/EAV code, so
   the media engine never finishes a buffer -- ME_STATE changes only when the host writes it, and
   NOTIFY never sets by itself. The registers keep what is written to their defined bits. */

/* Writable bits of 0x380-0x3A4 (VBI) and 0x400-0x42C (image), one per register:
   BUFF0/1_START 22:4, BUFF0/1_PITCH 13:4, BUFF0/1_LENGTH 19:4, ME_STATE (notify 0/4, error codes
   10:8/14:12, in use 16/20, current buffer 24), SU_STATE (field 8/12, in use 16/20), RM_STATE (notify
   0/4), then VBI (start line 4:0, line count 20:16) or the image's two SCALE_INCRs (Y 26:16, X 10:0)
   and Y_CROP (8:0). BUFF1_SCALE_INCR sits at 0x428: the PRM lists its fields but not its address. */
static const uint32_t nv3_pme_vbi_mask[10] = {
    0x007FFFF0, 0x007FFFF0, 0x00003FF0, 0x00003FF0, 0x000FFFF0, 0x000FFFF0,
    0x01117711, 0x00111100, 0x00000011, 0x001F001F
};

static const uint32_t nv3_pme_image_mask[12] = {
    0x007FFFF0, 0x007FFFF0, 0x00003FF0, 0x00003FF0, 0x000FFFF0, 0x000FFFF0,
    0x01117711, 0x00111100, 0x00000011, 0x07FF07FF, 0x07FF07FF, 0x000001FF
};

uint32_t nv3_pme_read(uint32_t address)
{
    uint32_t ret = 0x00;

    address &= ~3;

    if (address >= NV3_PME_VBI_BUFF0_START && address <= NV3_PME_VBI)
        return nv3->pme.vbi[(address - NV3_PME_VBI_BUFF0_START) >> 2];
    if (address >= NV3_PME_IMAGE_BUFF0_START && address <= NV3_PME_IMAGE_Y_CROP)
        return nv3->pme.image[(address - NV3_PME_IMAGE_BUFF0_START) >> 2];

    switch (address)
    {
        case NV3_PME_DEBUG_0:
        case NV3_PME_DEBUG_1:
            ret = nv3->pme.debug[(address - NV3_PME_DEBUG_0) >> 2];
            break;
        case NV3_PME_INTR:
            ret = nv3->pme.intr & NV3_PME_INTR_MASK;
            break;
        case NV3_PME_INTR_EN:
            ret = nv3->pme.intr_en;
            break;
        case NV3_PME_CONFIG_0:
        case NV3_PME_CONFIG_1:
            ret = nv3->pme.config[(address - NV3_PME_CONFIG_0) >> 2];
            break;
        /* The FIFO pointers (debug only): the media engine never runs, so they stay at 0 */
        case NV3_PME_FIFO_LINE_START:
        case NV3_PME_FIFO_CURRENT:
            break;
        default:
            nv_log("PME read from an undefined register 0x%06x\n", address);
            break;
    }

    return ret;
}

void nv3_pme_write(uint32_t address, uint32_t value)
{
    address &= ~3;

    if (address >= NV3_PME_VBI_BUFF0_START && address <= NV3_PME_VBI) {
        uint32_t index = (address - NV3_PME_VBI_BUFF0_START) >> 2;

        nv3->pme.vbi[index] = value & nv3_pme_vbi_mask[index];
        return;
    }
    if (address >= NV3_PME_IMAGE_BUFF0_START && address <= NV3_PME_IMAGE_Y_CROP) {
        uint32_t index = (address - NV3_PME_IMAGE_BUFF0_START) >> 2;

        nv3->pme.image[index] = value & nv3_pme_image_mask[index];
        return;
    }

    switch (address)
    {
        case NV3_PME_DEBUG_0:
        case NV3_PME_DEBUG_1:
            nv3->pme.debug[(address - NV3_PME_DEBUG_0) >> 2] = value;
            break;
        /* NOTIFY (0) and VMI (4, never raised on the RIVA 128): writing 1 clears */
        case NV3_PME_INTR:
            nv3->pme.intr &= ~(value & NV3_PME_INTR_MASK);
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PME_INTR_EN:
            nv3->pme.intr_en = value & NV3_PME_INTR_MASK;
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PME_CONFIG_0:
            nv3->pme.config[0] = value & 0x00011311;
            break;
        case NV3_PME_CONFIG_1:
            nv3->pme.config[1] = value & 0x00000011;
            break;
        default:
            nv_log("PME write to an undefined or read-only register 0x%06x value 0x%08x\n", address, value);
            break;
    }
}
