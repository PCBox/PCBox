/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 PTIMER - PIT emulation
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

// ptimer init code
void nv3_ptimer_init(void)
{
    nv_log("Initialising PTIMER...");

    nv_log("Done!\n");    
}

// Handles the PTIMER alarm interrupt
void nv3_ptimer_interrupt(uint32_t num)
{
    nv3->ptimer.intr |= (1 << num);

    nv3_pmc_handle_interrupts(true);
}

// Ticks the timer.
void nv3_ptimer_tick(double real_time)
{
    // prevent a divide by zero
    if (nv3->ptimer.clock_numerator == 0
    || nv3->ptimer.clock_denominator == 0)
        return; 

    // get the current time

    // See Envytools. We need to use the frequency as a source. 
    // We need to figure out how many cycles actually occurred because this counts up every cycle...
    // However it seems that their formula is wrong. I can't be bothered to figure out what's going on and, based on documentation from NVIDIA,
    // timer_0 is meant to roll over every 4 seconds. Multiplying by 10 basically does the job.

    // Convert to microseconds
    double freq_base = (real_time / 1000000.0f) / ((double)1.0 / nv3->nvbase.memory_clock_frequency) * 10.0f;
    double current_time = freq_base * ((double)nv3->ptimer.clock_numerator) / (double)nv3->ptimer.clock_denominator; // *10.0?

    // truncate it 
    nv3->ptimer.time += (uint64_t)current_time;

    // Check if the alarm has actually triggered..
    // Only log on ptimer alarm. Otherwise, it's too much spam.
    if (nv3->ptimer.time >= nv3->ptimer.alarm)
    {
        nv_log_verbose_only("PTIMER alarm interrupt fired (if interrupts enabled) because we reached TIME value 0x%08x\n", nv3->ptimer.alarm);
        nv3_ptimer_interrupt(NV3_PTIMER_INTR_ALARM);
    }
}

uint32_t nv3_ptimer_read(uint32_t address) 
{ 
    // always enabled

    uint32_t ret = 0x00;

    switch (address)
    {
        case NV3_PTIMER_INTR:
            ret = nv3->ptimer.intr;
            break;
        case NV3_PTIMER_INTR_EN:
            ret = nv3->ptimer.intr_en;
            break;
        case NV3_PTIMER_NUMERATOR:
            ret = nv3->ptimer.clock_numerator; // 15:0
            break;
        case NV3_PTIMER_DENOMINATOR:
            ret = nv3->ptimer.clock_denominator ; //15:0
            break;
        // 64-bit value
        // High part
        case NV3_PTIMER_TIME_0_NSEC:
            ret = nv3->ptimer.time & 0xFFFFFFFF; //28:0
            break;
        // Low part
        case NV3_PTIMER_TIME_1_NSEC:
            ret = nv3->ptimer.time >> 32; // 31:5
            break;
        case NV3_PTIMER_ALARM_NSEC: 
            ret = nv3->ptimer.alarm; // 31:5
            break;
    }

    return ret;
}

void nv3_ptimer_write(uint32_t address, uint32_t value) 
{
    // before doing anything, check the subsystem enablement

    switch (address)
    {
        // Interrupt state:
        // Bit 0 - Alarm

        case NV3_PTIMER_INTR:
            nv3->ptimer.intr &= ~value;
            nv3_pmc_clear_interrupts();
            break;

        // Interrupt enablement state
        case NV3_PTIMER_INTR_EN:
            nv3->ptimer.intr_en = value & 0x1;
            break;
        // nUMERATOR
        case NV3_PTIMER_NUMERATOR:
            nv3->ptimer.clock_numerator = value & 0xFFFF; // 15:0
            break;
        case NV3_PTIMER_DENOMINATOR:
            // prevent Div0
            if (!value)
                value = 1;

            nv3->ptimer.clock_denominator = value & 0xFFFF; //15:0
            break;
        // 64-bit value
        // High part
        case NV3_PTIMER_TIME_0_NSEC:
            nv3->ptimer.time |= (value) & 0xFFFFFFE0; //28:0
            break;
        // Low part
        case NV3_PTIMER_TIME_1_NSEC:
            nv3->ptimer.time |= ((uint64_t)(value & 0xFFFFFFE0) << 32); // 31:5
            break;
        case NV3_PTIMER_ALARM_NSEC: 
            nv3->ptimer.alarm = value & 0xFFFFFFE0; // 31:5
            break;
    }
}