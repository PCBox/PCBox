/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel 82815EP SPD Memory Hack
 *
 *
 *
 * Authors: Tiseno100,
 *          Jasmine Iwanek, <jriwanek@gmail.com>
 *
 *          Copyright 2022      Tiseno100.
 *          Copyright 2022-2023 Jasmine Iwanek.
 */

/* This is a hack because the 86Box SPD calculation algorithm is not made for the 815EP banking.
   This SHOULD ONLY be used with the 815EP chipset.                                              */

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/spd.h>

#define MEM_SIZE_MB (mem_size >> 10)

extern spd_t *spd_modules[SPD_MAX_SLOTS];

static int
intel_845_sdram_geometry(uint16_t row_size, uint8_t *row_bits, uint8_t *col_bits)
{
    switch (row_size) {
        case 32:
            *row_bits = 12;
            *col_bits = 8;
            return 1;

        case 64:
            *row_bits = 12;
            *col_bits = 9;
            return 1;

        case 128:
            *row_bits = 13;
            *col_bits = 9;
            return 1;

        case 256:
            *row_bits = 13;
            *col_bits = 10;
            return 1;

        case 512:
            *row_bits = 13;
            *col_bits = 11;
            return 1;

        default:
            return 0;
    }
}

static void
intel_845_spd_rechecksum(spd_t *spd)
{
    spd_sdram_t *sdram_data = &spd->sdram_data;

    sdram_data->checksum  = 0;
    sdram_data->checksum2 = 0;

    for (uint8_t i = 0; i < 63; i++)
        sdram_data->checksum += spd->data[i];

    for (uint8_t i = 0; i < 129; i++)
        sdram_data->checksum2 += spd->data[i];
}

static void
intel_845_spd_fix_geometry(void)
{
    uint8_t row_bits;
    uint8_t col_bits;

    for (uint8_t slot = 0; slot < SPD_MAX_SLOTS; slot++) {
        spd_t        *spd;
        spd_sdram_t  *sdram_data;
        uint8_t       row1_bits;
        uint8_t       col1_bits;
        uint8_t       row2_bits = 0;
        uint8_t       col2_bits = 0;

        spd = spd_modules[slot];
        if ((spd == NULL) || (spd->sdram_data.mem_type != SPD_TYPE_SDRAM))
            continue;

        if (!intel_845_sdram_geometry(spd->row1, &row_bits, &col_bits))
            continue;
        row1_bits = row_bits;
        col1_bits = col_bits;

        if ((spd->row2 != 0) && (spd->row1 != spd->row2)) {
            if (!intel_845_sdram_geometry(spd->row2, &row_bits, &col_bits))
                continue;
            row2_bits = row_bits;
            col2_bits = col_bits;
        }

        sdram_data           = &spd->sdram_data;
        sdram_data->row_bits = row1_bits | (row2_bits << 4);
        sdram_data->col_bits = col1_bits | (col2_bits << 4);
        sdram_data->banks    = 4;

        intel_845_spd_rechecksum(spd);
    }
}

uint8_t
intel_815ep_get_banking(void)
{
    switch (MEM_SIZE_MB) {
        case 32:
            return 0x02;

        case 64:
            return 0x01;

        case 96:
            return 0x21;

        case 128:
            return 0x04;

        case 160:
            return 0x24;

        case 192:
            return 0x06;

        case 256:
            return 0x07;

        case 320:
            return 0x57;

        case 384:
            return 0x97;

        case 512:
            return 0xed;

        default:
            return 0;
    }
}

void
intel_815ep_spd_init(void)
{
    switch (MEM_SIZE_MB) {
        case 32:
            spd_register(SPD_TYPE_SDRAM, 1, 32);
            break;

        case 64:
            spd_register(SPD_TYPE_SDRAM, 3, 32);
            break;

        case 96:
            spd_register(SPD_TYPE_SDRAM, 7, 32);
            break;

        case 128:
            spd_register(SPD_TYPE_SDRAM, 3, 64);
            break;

        case 160:
            spd_register(SPD_TYPE_SDRAM, 7, 64);
            break;

        case 192:
            spd_register(SPD_TYPE_SDRAM, 3, 96);
            break;

        case 256:
            spd_register(SPD_TYPE_SDRAM, 3, 128);
            break;

        case 320:
            spd_register(SPD_TYPE_SDRAM, 7, 128);
            break;

        case 384:
            spd_register(SPD_TYPE_SDRAM, 7, 128);
            break;

        case 512:
            spd_register(SPD_TYPE_SDRAM, 3, 256);
            break;

        default:
            pclog("Intel 815EP SPD Hack: Illegal Size %dMB\n", MEM_SIZE_MB);
            break;
    }
}

void
intel_845_spd_init(void)
{
    spd_register(SPD_TYPE_SDRAM, 7, 1024);
    intel_845_spd_fix_geometry();
}
