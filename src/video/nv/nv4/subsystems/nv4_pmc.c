/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
*          running old operating systems and software designed for IBM
*          PC systems and compatibles from 1981 through fairly recent
*          system designs based on the PCI bus.
*
*          This file is part of the 86Box distribution.
*
*          NV4/Riva TNT - Master Control
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
#include <86box/nv/vid_nv4.h>


void nv4_pmc_clear_interrupts(void)
{
    nv_log_verbose_only("Clearing IRQs\n");
    pci_clear_irq(nv4->nvbase.pci_slot, PCI_INTA, &nv4->nvbase.pci_irq_state);
}

// Handle hardware interrupts
// We only clear when we need to, in other functions...
uint32_t nv4_pmc_handle_interrupts(bool send_now)
{
    // TODO:
    // PGRAPH DMA INTR_EN (there is no DMA engine yet)
    // PRM Real-Mode Compatibility Interrupts

    uint32_t new_intr_value = 0x00;

    // set the new interrupt value
    // the registers are designed to line up so you can enable specific interrupts

    // Check Mediaport interrupts
    if (nv4->pme.intr & nv4->pme.intr_en)
        new_intr_value |= (NV4_PMC_INTR_0_PMEDIA_PENDING << NV4_PMC_INTR_0_PMEDIA);

    // Check FIFO interrupts
    if (nv4->pfifo.intr & nv4->pfifo.intr_en)
        new_intr_value |= (NV4_PMC_INTR_0_PFIFO_PENDING << NV4_PMC_INTR_0_PFIFO);

    // Is PFB interrupt used for VBLANK PGRAPH interrupt, like NV3?

    if (nv4->pgraph.intr & ~(1 << 8)
    && nv4->pgraph.intr_en & ~(1 << 8)) // otherwise PGRAPH-0 interurpt
        new_intr_value |= (NV4_PMC_INTR_0_PGRAPH_PENDING << NV4_PMC_INTR_0_PGRAPH);

    // check video overlay interrupts
    if (nv4->pvideo.intr & nv4->pvideo.intr_en)
        new_intr_value |= (NV4_PMC_INTR_0_PVIDEO_PENDING << NV4_PMC_INTR_0_PVIDEO);

    // check PIT interrupts
    if (nv4->ptimer.intr & nv4->ptimer.intr_en)
        new_intr_value |= (NV4_PMC_INTR_0_PTIMER_PENDING << NV4_PMC_INTR_0_PTIMER);

    // check crtc interrupts
    if (nv4->pcrtc.intr & nv4->pcrtc.intr_en)
        new_intr_value |= (NV4_PMC_INTR_0_PCRTC_PENDING << NV4_PMC_INTR_0_PCRTC);

    // check bus interrupts
    if (nv4->pbus.intr & nv4->pbus.intr_en)
        new_intr_value |= (NV4_PMC_INTR_0_PBUS_PENDING << NV4_PMC_INTR_0_PBUS);

    // check SW interrupts
    if (nv4->pmc.intr & (1 << NV4_PMC_INTR_0_SOFTWARE))
        new_intr_value |= (NV4_PMC_INTR_0_SOFTWARE_PENDING << NV4_PMC_INTR_0_SOFTWARE);

    nv4->pmc.intr = new_intr_value;

    // ***TODO: DOes INTR still change if INTR_EN=0???***
    // If interrupts are disabled don't bother

    if (!nv4->pmc.intr_en)
    {
        nv4_pmc_clear_interrupts();
        return nv4->pmc.intr;
    }
        

    // if we actually need to send the interrupt (i.e. this is a write) send it now
    if (send_now)
    {
        // no interrupts to send
        if (!(nv4->pmc.intr)
         || nv4->pmc.intr & 0x80000000)
        {
            nv4_pmc_clear_interrupts();
            return nv4->pmc.intr;
        }
            
        if ((nv4->pmc.intr & 0x7FFFFFFF))
        {
            if (nv4->pmc.intr_en & NV4_PMC_INTR_EN_0_INTA_HARDWARE)
            {
                nv_log_verbose_only("Firing hardware-originated interrupt NV4_PMC_INTR_0=0x%08x\n", nv4->pmc.intr);
                pci_set_irq(nv4->nvbase.pci_slot, PCI_INTA, &nv4->nvbase.pci_irq_state);
            }
            else
                nv_log_verbose_only("NOT firing hardware-originated interrupt NV4_PMC_INTR_0=0x%08x, BECAUSE HARDWARE INTERRUPTS ARE DISABLED\n", nv4->pmc.intr);      
        }
        else   
        {
            if (nv4->pmc.intr_en & NV4_PMC_INTR_EN_0_INTA_SOFTWARE)
            {
                nv_log_verbose_only("Firing software-originated interrupt NV4_PMC_INTR_0=0x%08x\n", nv4->pmc.intr);
                pci_set_irq(nv4->nvbase.pci_slot, PCI_INTA, &nv4->nvbase.pci_irq_state);
            }
            else
                nv_log_verbose_only("NOT firing software-originated interrupt NV4_PMC_INTR_0=0x%08x, BECAUSE SOFTWARE INTERRUPTS ARE DISABLED\n", nv4->pmc.intr); 
        }
    }

    return nv4->pmc.intr;
}


uint32_t nv4_pmc_read(uint32_t address)
{
    uint32_t ret = 0x00;

    switch (address)
    {
        case NV4_PMC_BOOT_0:
            ret = NV4_PMC_BOOT_0_GENERIC_VALUE;
            break; 
        case NV4_PMC_INTR_0:
            ret = nv4->pmc.intr;
            nv4_pmc_handle_interrupts(false);
            break;
        case NV4_PMC_INTR_EN_0:
            ret = nv4->pmc.intr_en;
            break; 
        case NV4_PMC_INTR_READ_0: // do we need to trigger interrupts here?
            ret = nv4->pmc.intr_read;
            break;
        case NV4_PMC_ENABLE:
            ret = nv4->pmc.enable;
            break; 
    }

    return ret; 

    nv_log_verbose_only("PMC Read %08x <- %08x", address, ret);
}

void nv4_pmc_write(uint32_t address, uint32_t val)
{
    switch (address)
    {
        case NV4_PMC_ENABLE:
            nv4->pmc.enable = val;
            break; 
        case NV4_PMC_INTR_0:
            nv4->pmc.intr = val;
            nv4_pmc_handle_interrupts(true);
            break;
        case NV4_PMC_INTR_READ_0: // do we need to trigger interrupts here?
            nv4->pmc.intr_read = val;
            break;
        case NV4_PMC_INTR_EN_0:
            nv4->pmc.intr_en = val;
            nv4_pmc_handle_interrupts(val != NV4_PMC_INTR_EN_0_INTA_DISABLED);
            break;
    }

    nv_log_verbose_only("PMC Write %08x -> %08x", address, val);

}