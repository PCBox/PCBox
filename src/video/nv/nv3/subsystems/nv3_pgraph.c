/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PGRAPH (Scene Graph for 2D/3D Accelerated Graphics)
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
#include <86box/nv/classes/vid_nv3_classes.h>

// Initialise the PGRAPH subsystem.
void nv3_pgraph_init(void)
{
    nv_log("Initialising PGRAPH...");
    // Set up the vblank interrupt
    nv3->nvbase.svga.vblank_start = nv3_pgraph_vblank_start;
    nv_log("Done!\n");    
}

uint32_t nv3_pgraph_read(uint32_t address) 
{ 
    // before doing anything, check that this is even enabled..

    if (!((nv3->pmc.enable >> NV3_PMC_ENABLE_PGRAPH)
    & NV3_PMC_ENABLE_PGRAPH_ENABLED))
    {
        nv_log("Repressing PGRAPH read. The subsystem is disabled according to pmc_enable, returning 0\n");
        return 0x00;
    }

    uint32_t ret = 0x00;

    // todo: friendly logging
    
    switch (address)
    {
        case NV3_PGRAPH_DEBUG_0:
            ret = nv3->pgraph.debug_0;
            break;
        case NV3_PGRAPH_DEBUG_1:
            ret = nv3->pgraph.debug_1;
            break;
        case NV3_PGRAPH_DEBUG_2:
            ret = nv3->pgraph.debug_2;
            break;
        case NV3_PGRAPH_DEBUG_3:
            ret = nv3->pgraph.debug_3;
            break;
        //interrupt status and enable regs
        case NV3_PGRAPH_INTR_0:
            ret = nv3->pgraph.intr_0;
            nv3_pmc_clear_interrupts();
            break;
        case NV3_PGRAPH_INTR_1:
            ret = nv3->pgraph.intr_1;
            nv3_pmc_clear_interrupts();
            break;
        case NV3_PGRAPH_INTR_EN_0:
            ret = nv3->pgraph.intr_en_0;
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PGRAPH_INTR_EN_1:
            ret = nv3->pgraph.intr_en_1;
            nv3_pmc_handle_interrupts(true);
            break;
        // A lot of this is currently a temporary implementation so that we can just debug what the current state looks like
        // during the driver initialisation process            

        // In the future, these will most likely have their own functions...

        // Context Swithcing (THIS IS CONTROLLED BY PFIFO!)
        case NV3_PGRAPH_CTX_SWITCH:
            ret = nv3->pgraph.context_switch;
            break;
        case NV3_PGRAPH_CONTEXT_CONTROL:
            ret = *(uint32_t*)&nv3->pgraph.context_control;
            break;
        case NV3_PGRAPH_CONTEXT_USER:
            ret = *(uint32_t*)&nv3->pgraph.context_user;
            break;
        // Clip
        case NV3_PGRAPH_ABS_UCLIP_XMIN:
            ret = nv3->pgraph.abs_uclip_xmin;
            break;
        case NV3_PGRAPH_ABS_UCLIP_XMAX:
            ret = nv3->pgraph.abs_uclip_xmax;
            break;
        case NV3_PGRAPH_ABS_UCLIP_YMIN:
            ret = nv3->pgraph.abs_uclip_ymin;
            break;
        case NV3_PGRAPH_ABS_UCLIP_YMAX:
            ret = nv3->pgraph.abs_uclip_ymax;
            break;
        // Canvas
        case NV3_PGRAPH_SRC_CANVAS_MIN:
            ret = *(uint32_t*)&nv3->pgraph.src_canvas_min;
            break;
        case NV3_PGRAPH_SRC_CANVAS_MAX:
            ret = *(uint32_t*)&nv3->pgraph.src_canvas_max;
            break;
        // Pattern
        case NV3_PGRAPH_PATTERN_COLOR_0_RGB:
            ret = *(uint32_t*)&nv3->pgraph.pattern_color_0_rgb;
            break;
        case NV3_PGRAPH_PATTERN_COLOR_0_ALPHA:
            ret = *(uint32_t*)&nv3->pgraph.pattern_color_0_alpha;
            break;
        case NV3_PGRAPH_PATTERN_COLOR_1_RGB:
            ret = *(uint32_t*)&nv3->pgraph.pattern_color_1_rgb;
            break;
        case NV3_PGRAPH_PATTERN_COLOR_1_ALPHA:
            ret = *(uint32_t*)&nv3->pgraph.pattern_color_1_alpha;
            break;
        case NV3_PGRAPH_PATTERN_BITMAP_HIGH:
            ret = (nv3->pgraph.pattern_bitmap >> 32) & 0xFFFFFFFF;
            break;
        case NV3_PGRAPH_PATTERN_BITMAP_LOW:
            ret = (nv3->pgraph.pattern_bitmap & 0xFFFFFFFF);
            break;
        // Beta factor
        case NV3_PGRAPH_BETA:
            ret = nv3->pgraph.beta_factor;
            break; 
        // Todo: Massive table of ROP IDs or at least known ones?
        case NV3_PGRAPH_ROP3:
            ret = nv3->pgraph.rop;
            break; 
        case NV3_PGRAPH_CHROMA_KEY:
            ret = *(uint32_t*)&nv3->pgraph.chroma_key;
            break;
        case NV3_PGRAPH_PLANE_MASK:
            ret = nv3->pgraph.plane_mask;
            break;
        // DMA
        case NV3_PGRAPH_DMA:
            ret = *(uint32_t*)&nv3->pgraph.dma_settings;
            break;
        case NV3_PGRAPH_NOTIFY:
            ret = *(uint32_t*)&nv3->pgraph.notifier;
            break;
        // More clip
        case NV3_PGRAPH_CLIP0_MIN:
            ret = *(uint32_t*)&nv3->pgraph.clip0_min;
            break;
        case NV3_PGRAPH_CLIP0_MAX:
            ret = *(uint32_t*)&nv3->pgraph.clip0_max;
            break;
        case NV3_PGRAPH_CLIP1_MIN:
            ret = *(uint32_t*)&nv3->pgraph.clip1_min;
            break;
        case NV3_PGRAPH_CLIP1_MAX:
            ret = *(uint32_t*)&nv3->pgraph.clip1_max;
            break;
        case NV3_PGRAPH_CLIP_MISC:
            ret = *(uint32_t*)&nv3->pgraph.clip_misc_settings;
            break;
        
        // Overall Status
        case NV3_PGRAPH_STATUS:
            ret = *(uint32_t*)&nv3->pgraph.status;
            break;
        // Trapped Address
        case NV3_PGRAPH_TRAPPED_ADDRESS:
            ret = nv3->pgraph.trapped_address;
            break;
        case NV3_PGRAPH_TRAPPED_DATA:
            ret = nv3->pgraph.trapped_data;
            break;
        case NV3_PGRAPH_INSTANCE:
            ret = nv3->pgraph.instance;
            break;
        case NV3_PGRAPH_TRAPPED_INSTANCE:
            ret = nv3->pgraph.trapped_instance;
            break;

    }

    /* Special exception for memory areas */
    if (address >= NV3_PGRAPH_CONTEXT_CACHE(0)
    && address <= NV3_PGRAPH_CONTEXT_CACHE(NV3_PGRAPH_CONTEXT_CACHE_SIZE))
    {
        // Addresses should be aligned to 4 bytes.
        uint32_t entry = (address - NV3_PGRAPH_CONTEXT_CACHE(0));

        nv_log_verbose_only("PGRAPH Context Cache Read (Entry=%04x Value=%04x)\n", entry, nv3->pgraph.context_cache[entry]);
    }

    return ret; 
}

void nv3_pgraph_write(uint32_t address, uint32_t value) 
{
    if (!((nv3->pmc.enable >> NV3_PMC_ENABLE_PGRAPH)
    & NV3_PMC_ENABLE_PGRAPH_ENABLED))
    {
        nv_log("Repressing PGRAPH write. The subsystem is disabled according to pmc_enable\n");
        return;
    }

    switch (address)
    {
        case NV3_PGRAPH_DEBUG_0:
            nv3->pgraph.debug_0 = value;
            break;
        case NV3_PGRAPH_DEBUG_1:
            nv3->pgraph.debug_1 = value;
            break;
        case NV3_PGRAPH_DEBUG_2:
            nv3->pgraph.debug_2 = value;
            break;
        case NV3_PGRAPH_DEBUG_3:
            nv3->pgraph.debug_3 = value;
            break;
        //interrupt status and enable regs
        case NV3_PGRAPH_INTR_0:
            nv3->pgraph.intr_0 &= ~value;
            //we changed interrupt state
            nv3_pmc_clear_interrupts();
            break;
        case NV3_PGRAPH_INTR_1:
            nv3->pgraph.intr_1 &= ~value;
            //we changed interrupt state
            nv3_pmc_clear_interrupts();
            break;
        // Only bits divisible by 4 matter
        // and only bit0-16 is defined in intr_1 
        case NV3_PGRAPH_INTR_EN_0:
            nv3->pgraph.intr_en_0 = value & 0x11111111;                     
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PGRAPH_INTR_EN_1:
            nv3->pgraph.intr_en_1 = value & 0x00011111; 
            nv3_pmc_handle_interrupts(true);
            break;
        case NV3_PGRAPH_DMA_INTR_0:
            nv3->pgraph.intr_dma &= ~value;
            nv3_pmc_clear_interrupts();
            break;
        case NV3_PGRAPH_DMA_INTR_EN_0:
            nv3->pgraph.intr_en_dma = value & 0x000111111;
            nv_log("Handling PGRAPH_DMA interrupts not implemented");
            nv3_pmc_handle_interrupts(true);
            break;
        // A lot of this is currently a temporary implementation so that we can just debug what the current state looks like
        // during the driver initialisation process            

        // In the future, these will most likely have their own functions...

        // Context Swithcing (THIS IS CONTROLLED BY PFIFO!)
        case NV3_PGRAPH_CTX_SWITCH:
            nv3->pgraph.context_switch = value;
            break;
        case NV3_PGRAPH_CONTEXT_CONTROL:
            *(uint32_t*)&nv3->pgraph.context_control = value;
            break;
        case NV3_PGRAPH_CONTEXT_USER:
            *(uint32_t*)&nv3->pgraph.context_user = value;
            break;
        // Clip
        case NV3_PGRAPH_ABS_UCLIP_XMIN:
            nv3->pgraph.abs_uclip_xmin = value;
            break;
        case NV3_PGRAPH_ABS_UCLIP_XMAX:
            nv3->pgraph.abs_uclip_xmax = value;
            break;
        case NV3_PGRAPH_ABS_UCLIP_YMIN:
            nv3->pgraph.abs_uclip_ymin = value;
            break;
        case NV3_PGRAPH_ABS_UCLIP_YMAX:
            nv3->pgraph.abs_uclip_ymax = value;
            break;
        // Canvas
        case NV3_PGRAPH_SRC_CANVAS_MIN:
            *(uint32_t*)&nv3->pgraph.src_canvas_min = value;
            break;
        case NV3_PGRAPH_SRC_CANVAS_MAX:
            *(uint32_t*)&nv3->pgraph.src_canvas_max = value;
            break;
        // Pattern
        case NV3_PGRAPH_PATTERN_COLOR_0_RGB:
            *(uint32_t*)&nv3->pgraph.pattern_color_0_rgb = value;
            break;
        case NV3_PGRAPH_PATTERN_COLOR_0_ALPHA:
            *(uint32_t*)&nv3->pgraph.pattern_color_0_alpha = value;
            break;
        case NV3_PGRAPH_PATTERN_COLOR_1_RGB:
            *(uint32_t*)&nv3->pgraph.pattern_color_1_rgb = value;
            break;
        case NV3_PGRAPH_PATTERN_COLOR_1_ALPHA:
            *(uint32_t*)&nv3->pgraph.pattern_color_1_alpha = value;
            break;
        case NV3_PGRAPH_PATTERN_BITMAP_HIGH:
            nv3->pgraph.pattern_bitmap |= ((uint64_t)value << 32);
            break;
        case NV3_PGRAPH_PATTERN_BITMAP_LOW:
            nv3->pgraph.pattern_bitmap |= value;
            break;
        // Beta factor
        case NV3_PGRAPH_BETA:
            nv3->pgraph.beta_factor = value;
            break; 
        // Todo: Massive table of ROP IDs or at least known ones?
        case NV3_PGRAPH_ROP3:
            nv3->pgraph.rop = value & 0xFF;
            break; 
        case NV3_PGRAPH_CHROMA_KEY:
            nv3->pgraph.chroma_key = value;
            break;
        case NV3_PGRAPH_PLANE_MASK:
            nv3->pgraph.plane_mask = value;
            break;
        // DMA
        case NV3_PGRAPH_DMA:
            *(uint32_t*)&nv3->pgraph.dma_settings = value;
            break;
        case NV3_PGRAPH_NOTIFY:
            *(uint32_t*)&nv3->pgraph.notifier = value;
            break;
        // More clip
        case NV3_PGRAPH_CLIP0_MIN:
            *(uint32_t*)&nv3->pgraph.clip0_min = value;
            break;
        case NV3_PGRAPH_CLIP0_MAX:
            *(uint32_t*)&nv3->pgraph.clip0_max = value;
            break;
        case NV3_PGRAPH_CLIP1_MIN:
            *(uint32_t*)&nv3->pgraph.clip1_min = value;
            break;
        case NV3_PGRAPH_CLIP1_MAX:
            *(uint32_t*)&nv3->pgraph.clip1_max = value;
            break;
        case NV3_PGRAPH_CLIP_MISC:
            *(uint32_t*)&nv3->pgraph.clip_misc_settings = value;
            break;
        // Overall Status
        case NV3_PGRAPH_STATUS:
            *(uint32_t*)&nv3->pgraph.status = value;
            break;
        // Trapped Address
        case NV3_PGRAPH_TRAPPED_ADDRESS:
            nv3->pgraph.trapped_address = value;
            break;
        case NV3_PGRAPH_TRAPPED_DATA:
            nv3->pgraph.trapped_data = value;
            break;
        case NV3_PGRAPH_INSTANCE:
            nv3->pgraph.instance = value;
            break;
        case NV3_PGRAPH_TRAPPED_INSTANCE:
            nv3->pgraph.trapped_instance = value;
            break;

    }

    /* Special exception for memory areas */
    if (address >= NV3_PGRAPH_CONTEXT_CACHE(0)
    && address <= NV3_PGRAPH_CONTEXT_CACHE(NV3_PGRAPH_CONTEXT_CACHE_SIZE))
    {
        // Addresses should be aligned to 4 bytes.
        uint32_t entry = (address - NV3_PGRAPH_CONTEXT_CACHE(0)) >> 2;

        nv_log_verbose_only("PGRAPH Context Cache Write (Entry=%04x Value=0x%08x)\n", entry, value);
        nv3->pgraph.context_cache[entry] = value;
    }
}

// Fire a VALID Pgraph interrupt: num is the bit# of the interrupt in the GPU subsystem INTR_EN register.
void nv3_pgraph_interrupt_valid(uint32_t num)
{
    nv3->pgraph.intr_0 |= (1 << num);
    nv3_pmc_handle_interrupts(true);
}

// Fire an INVALID pgraph interrupt
void nv3_pgraph_interrupt_invalid(uint32_t num)
{
    nv3->pgraph.intr_1 |= (1 << num);

    // Some code in pcbox hat enables the "reserved" bit HERE if it's set in intr 0. What???
    nv3_pmc_handle_interrupts(true);
}

// VBlank. Fired every single frame.
void nv3_pgraph_vblank_start(svga_t* svga)
{
    nv3_pgraph_interrupt_valid(NV3_PGRAPH_INTR_0_VBLANK);
}

/* Sends off method execution to the right class */
void nv3_pgraph_arbitrate_method(uint32_t param, uint16_t method, uint8_t channel, uint8_t subchannel, uint8_t class_id, nv3_ramin_context_t context)
{
    /* Obtain the grobj information from the context in ramin */
    nv3_grobj_t grobj = {0};

    // we need to shift left by 4 to get the real address, something to do with the 16 byte unit of reversal 
    uint32_t real_ramin_base = context.ramin_offset << 4;

    // readin our grobj
    grobj.grobj_0 = nv3_ramin_read32(real_ramin_base, nv3);
    grobj.grobj_1 = nv3_ramin_read32(real_ramin_base + 4, nv3);
    grobj.grobj_2 = nv3_ramin_read32(real_ramin_base + 8, nv3);
    grobj.grobj_3 = nv3_ramin_read32(real_ramin_base + 12, nv3);

    nv_log_verbose_only("**** About to execute method **** method=0x%04x param=0x%08x, channel=%d.%d, class=%s, grobj=0x%08x 0x%08x 0x%08x 0x%08x\n",
        method, param, channel, subchannel, nv3_class_names[class_id], grobj.grobj_0, grobj.grobj_1, grobj.grobj_2, grobj.grobj_3);

    /* Methods below 0x104 are shared across all classids, so call generic_method for that*/
    if (method <= NV3_SET_NOTIFY)
    {
        nv3_generic_method(param, method, context, grobj);
    }
    else
    {
        // By this point, we already ANDed the class ID to 0x1F.
        // Send the grobj, the context, the method and the name off to actually be acted upon.
        switch (class_id)
        {
            case nv3_pgraph_class01_beta_factor:
                nv3_class_001_method(param, method, context, grobj);
                break; 
            case nv3_pgraph_class02_rop:
                nv3_class_002_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class03_chroma_key:
                nv3_class_003_method(param, method, context, grobj);
                break; 
            case nv3_pgraph_class04_plane_mask:
                nv3_class_004_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class05_clipping_rectangle:
                nv3_class_005_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class06_pattern:
                nv3_class_006_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class07_rectangle:
                nv3_class_007_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class08_point:
                nv3_class_008_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class09_line:
                nv3_class_009_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class0a_lin:
                nv3_class_00a_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class0b_triangle:
                nv3_class_00b_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class0c_w95txt:
                nv3_class_00c_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class0d_m2mf:
                nv3_class_00d_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class0e_scaled_image_from_memory:
                nv3_class_00e_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class10_blit:
                nv3_class_010_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class11_image:
                nv3_class_011_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class12_bitmap:
                nv3_class_012_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class14_transfer2memory:
                nv3_class_014_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class15_stretched_image_from_cpu:
                nv3_class_015_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class17_d3d5tri_zeta_buffer:
                nv3_class_017_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class18_point_zeta_buffer:
                nv3_class_018_method(param, method, context, grobj);
                break;
            case nv3_pgraph_class1c_image_in_memory:
                nv3_class_01c_method(param, method, context, grobj);
                break;             
            default:
                fatal("NV3 (nv3_pgraph_arbitrate_method): Attempted to execute method on invalid, or unimplemented, class ID %s", nv3_class_names[class_id]);
                return;
        }
    }

    nv3_notify_if_needed(param, method, context, grobj);
}

/* Arbitrates graphics object submission to the right object types */
void nv3_pgraph_submit(uint32_t param, uint16_t method, uint8_t channel, uint8_t subchannel, uint8_t class_id, nv3_ramin_context_t  context)
{
    // class id can be derived from the context but we debug log it before we get here
    // Do we need to read grobj here?
    
    switch (method)
    {
        default:
            // Object Method arbitration
            nv3_pgraph_arbitrate_method(param, method, channel, subchannel, class_id, context);
            break;
    }
}