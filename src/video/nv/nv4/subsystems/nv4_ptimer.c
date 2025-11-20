/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Nvidia RIVA TNT (NV4 architecture) - Timer emulation
 *
 *
 *
 * Authors: Connor Hyde, <mario64crashed@gmail.com>
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

// ptimer init code
void nv4_ptimer_init(void)
{
    nv_log("Initialising PTIMER...");

    nv_log("Done!\n");    
}

// Handles the PTIMER alarm interrupt
void nv4_ptimer_interrupt(uint32_t num)
{
    nv4->ptimer.intr |= (1 << num);

    //todo
    //nv4_pmc_handle_interrupts(true);
}

// Ticks the timer.
void nv4_ptimer_tick(double real_time)
{
    // prevent a divide by zero
    if (nv4->ptimer.clock_numerator == 0
    || nv4->ptimer.clock_denominator == 0)
        return; 

    // get the current time

    // See Envytools. We need to use the frequency as a source. 
    // We need to figure out how many cycles actually occurred because this counts up every cycle...
    // However it seems that their formula is wrong. I can't be bothered to figure out what's going on and, based on documentation from NVIDIA,
    // timer_0 is meant to roll over every 4 seconds. Multiplying by 10 basically does the job.

    // Convert to microseconds
    double freq_base = (real_time / 1000000.0f) / ((double)1.0 / nv4->nvbase.memory_clock_frequency) * 10.0f;
    double current_time = freq_base * ((double)nv4->ptimer.clock_numerator) / (double)nv4->ptimer.clock_denominator; // *10.0?

    // truncate it 
    nv4->ptimer.time += (uint64_t)current_time;

    // Check if the alarm has actually triggered..
    // Only log on ptimer alarm. Otherwise, it's too much spam.
    if (nv4->ptimer.time >= nv4->ptimer.alarm)
    {
        nv_log_verbose_only("PTIMER alarm interrupt fired (if interrupts enabled) because we reached TIME value 0x%08x\n", nv4->ptimer.alarm);
        nv4_ptimer_interrupt(NV4_PTIMER_INTR_ALARM);
    }
}

uint32_t nv4_ptimer_read(uint32_t address) 
{ 
    // always enabled

    // Only log these when tehy actually tick
    if (address != NV4_PTIMER_TIME_0_NSEC
    && address != NV4_PTIMER_TIME_1_NSEC)
    {
        nv_log_verbose_only("PTIMER Read from 0x%08x", address);
    }

    uint32_t ret = 0x00;

    switch (address)
    {
        case NV4_PTIMER_INTR:
            ret = nv4->ptimer.intr;
            break;
        case NV4_PTIMER_INTR_EN:
            ret = nv4->ptimer.intr_en;
            break;
        case NV4_PTIMER_NUMERATOR:
            ret = nv4->ptimer.clock_numerator; // 15:0
            break;
        case NV4_PTIMER_DENOMINATOR:
            ret = nv4->ptimer.clock_denominator ; //15:0
            break;
        // 64-bit value
        // High part
        case NV4_PTIMER_TIME_0:
            ret = nv4->ptimer.time & 0xFFFFFFFF; //28:0
            break;
        // Low part
        case NV4_PTIMER_TIME_1:
            ret = nv4->ptimer.time >> 32; // 31:5
            break;
        case NV4_PTIMER_ALARM: 
            ret = nv4->ptimer.alarm; // 31:5
            break;
    }

    return ret;
}

void nv4_ptimer_write(uint32_t address, uint32_t value) 
{
    nv_log_verbose_only("PTIMER Write 0x%08x -> 0x%08x", value, address);

    switch (address)
    {
        // Interrupt state:
        // Bit 0 - Alarm

        case NV4_PTIMER_INTR:
            nv4->ptimer.intr &= ~value;
            //TODO nv4_pmc_clear_interrupts();
            break;

        // Interrupt enablement state
        case NV4_PTIMER_INTR_EN:
            nv4->ptimer.intr_en = value & 0x1;
            break;
        // nUMERATOR
        case NV4_PTIMER_NUMERATOR:
            nv4->ptimer.clock_numerator = value & 0xFFFF; // 15:0
            break;
        case NV4_PTIMER_DENOMINATOR:
            // prevent Div0
            if (!value)
                value = 1;

            nv4->ptimer.clock_denominator = value & 0xFFFF; //15:0
            break;
        // 64-bit value
        // High part
        case NV4_PTIMER_TIME_0:
            nv4->ptimer.time |= (value) & 0xFFFFFFE0; //28:0
            break;
        // Low part
        case NV4_PTIMER_TIME_1:
            nv4->ptimer.time |= ((uint64_t)(value & 0xFFFFFFE0) << 32); // 31:5
            break;
        case NV4_PTIMER_ALARM: 
            nv4->ptimer.alarm = value & 0xFFFFFFE0; // 31:5
            break;
    }
}