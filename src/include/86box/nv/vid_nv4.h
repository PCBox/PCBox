/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 * 
 *          Riva TNT hardware defines
 * 
 * Authors: Connor Hyde <mario64crashed@gmail.com>
 *
 *          Copyright 2024-2025 Connor Hyde
 */


#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <86Box/nv/vid_nv4_defines.h>

//
// Structures
//


// PBUS
// RMA: Access the GPU from real-mode
typedef struct nv4_pbus_rma_s
{
    uint32_t addr;                      // Address to RMA to
    uint32_t data;                      // Data to send to MMIO
    uint8_t mode;                       // the current state of the rma shifting engin
    uint8_t rma_regs[NV4_RMA_NUM_REGS]; // The rma registers (saved)
} nv4_pbus_rma_t;

// Bus Configuration
typedef struct nv4_pbus_s
{
    uint32_t debug_0;
    uint32_t intr;          // Interrupt status
    uint32_t intr_en;          // Interrupt enable
    nv4_pbus_rma_t rma;
} nv4_pbus_t;

// PMC: Master Control
typedef struct nv4_pmc_s
{
    uint32_t enable;                    // Global system enable
    uint32_t intr;                      // Global system interrupt state
    uint32_t intr_en;                   // Global system interrupt enablement
    uint32_t intr_read;                 // Global system interrupt read
} nv4_pmc_t;

// PFIFO
typedef struct nv4_pfifo_s
{
    uint32_t intr;                      // FIFO Interrupt State
    uint32_t intr_en;                   // FIFO Interrupt Enabled
} nv4_pfifo_t;

// PGRAPH
typedef struct nv4_pgraph_s
{
    uint32_t intr;                      // Graphics Engine Interrupt State
    uint32_t intr_en;                   // Graphics Engine Interrupt Enabled
} nv4_pgraph_t;

// PTIMER
typedef struct nv4_ptimer_s
{
    uint32_t intr;                      // PTIMER Interrupt status
    uint32_t intr_en;                   // PTIMER Interrupt enable
    uint32_t clock_numerator;           // PTIMER (tick?) numerator
    uint32_t clock_denominator;         // PTIMER (tick?) denominator
    uint64_t time;                      // time
    uint32_t alarm;                     // The value of time when there should be an alarm
} nv4_ptimer_t; 

// PCRTC
typedef struct nv4_pcrtc_s
{
    uint32_t intr;                      // VGA CRTC Interrupt State
    uint32_t intr_en;                   // VGA CRTC Interrupt Enabled
} nv4_pcrtc_t;

// Mediaport (not emulated)
typedef struct nv4_pme_s
{
    uint32_t intr;                      // VGA CRTC Interrupt State
    uint32_t intr_en;                   // VGA CRTC Interrupt Enabled
} nv4_pme_t;

// PVIDEO (overlay etc)
typedef struct nv4_pvideo_s
{
    uint32_t intr;                      // VGA CRTC Interrupt State
    uint32_t intr_en;                   // VGA CRTC Interrupt Enabled
} nv4_pvideo_t;

// PRAMDAC
typedef struct nv4_pramdac_s
{
    uint32_t intr;                      // RAMDAC Interrupt State
    uint32_t intr_en;                   // RAMDAC Interrupt Enabled
    uint32_t mclk;
    uint32_t vclk;
    uint32_t nvclk;
    uint32_t clk_coeff_select;          // Clock coefficient selection
    uint32_t cursor_address;
} nv4_pramdac_t;

// Device Core
typedef struct nv4_s
{
    nv_base_t nvbase;                   // Base Nvidia structure
    uint32_t straps;                    // Straps. See defines
    nv4_pmc_t pmc;                      // Controls everything else
    nv4_pbus_t pbus;                    // Bus
    nv4_pfifo_t pfifo;                  // Timeslice scheduled FIFO
    nv4_pgraph_t pgraph;                // Graphics engine
    nv4_ptimer_t ptimer;                // Programmable interval timer
    nv4_pcrtc_t pcrtc;                  // CRTC
    nv4_pramdac_t pramdac;              // RAMDAC
    nv4_pvideo_t pvideo;                // Video Engine
    nv4_pme_t pme;                      // Mediaport
} nv4_t;

//
// Globals
//

extern const device_config_t nv4_config[];                              

extern nv4_t* nv4;                                                      // Allocated at device startup

#ifdef ENABLE_NV_LOG
// Debug register list
extern nv_register_t nv4_registers[];
#endif

//
// Functions
//

// Device Core
bool        nv4_init();

void*       nv4_init_stb4400(const device_t* info);

void        nv4_close(void* priv);
void        nv4_speed_changed(void *priv);
void        nv4_draw_cursor(svga_t* svga, int32_t drawline);
void        nv4_recalc_timings(svga_t* svga);
void        nv4_force_redraw(void* priv);

// I/O
uint8_t     nv4_mmio_read8(uint32_t addr, void* priv);
uint16_t    nv4_mmio_read16(uint32_t addr, void* priv);
uint32_t    nv4_mmio_read32(uint32_t addr, void* priv);
void        nv4_mmio_write8(uint32_t addr, uint8_t val, void* priv);
void        nv4_mmio_write16(uint32_t addr, uint16_t val, void* priv);
void        nv4_mmio_write32(uint32_t addr, uint32_t val, void* priv);
uint8_t     nv4_dfb_read8(uint32_t addr, void* priv);
uint16_t    nv4_dfb_read16(uint32_t addr, void* priv);
uint32_t    nv4_dfb_read32(uint32_t addr, void* priv);
void        nv4_dfb_write8(uint32_t addr, uint8_t val, void* priv);
void        nv4_dfb_write16(uint32_t addr, uint16_t val, void* priv);
void        nv4_dfb_write32(uint32_t addr, uint32_t val, void* priv);
uint8_t     nv4_ramin_read8(uint32_t addr, void* priv);
uint16_t    nv4_ramin_read16(uint32_t addr, void* priv);
uint32_t    nv4_ramin_read32(uint32_t addr, void* priv);
void        nv4_ramin_write8(uint32_t addr, uint8_t val, void* priv);
void        nv4_ramin_write16(uint32_t addr, uint16_t val, void* priv);
void        nv4_ramin_write32(uint32_t addr, uint32_t val, void* priv);
uint8_t     nv4_pci_read(int32_t func, int32_t addr, void* priv);
void        nv4_pci_write(int32_t func, int32_t addr, uint8_t val, void* priv);

// SVGA
uint8_t     nv4_svga_read(uint16_t addr, void* priv);
void        nv4_svga_write(uint16_t addr, uint8_t val, void* priv);

// Memory
void        nv4_update_mappings();

// PRAMDAC  
uint32_t    nv4_pramdac_read(uint32_t address);
void        nv4_pramdac_write(uint32_t address, uint32_t data);

// We don't implement NVCLK/VCLK because they are too fast
void        nv4_pramdac_set_vclk();

void        nv4_vclk_tick();

// PTIMER
uint32_t    nv4_ptimer_read(uint32_t address);
void        nv4_ptimer_write(uint32_t address, uint32_t data);

// PMC
uint32_t    nv4_pmc_read(uint32_t address);
void        nv4_pmc_write(uint32_t address, uint32_t data);