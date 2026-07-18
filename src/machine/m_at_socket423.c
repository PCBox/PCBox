/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Implementation of Socket 370 (PGA370) machines.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2016-2025 Miran Grca.
 */
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/mem.h>
#include <86box/io.h>
#include <86box/rom.h>
#include <86box/pci.h>
#include <86box/device.h>
#include <86box/chipset.h>
#include <86box/hdc.h>
#include <86box/hdc_ide.h>
#include <86box/keyboard.h>
#include <86box/flash.h>
#include <86box/sio.h>
#include <86box/hwm.h>
#include <86box/spd.h>
#include <86box/video.h>
#include "cpu.h"
#include <86box/machine.h>
#include <86box/clock.h>
#include <86box/sound.h>
#include <86box/snd_ac97.h>

int
machine_at_ms6529_init(const machine_t *model)
{
    int ret;

    ret = bios_load_linear("roms/machines/ms6529/ms6529_ivens_oem.BIN",
                           0x000c0000, 262144, 0);

    if (bios_only || !ret)
        return ret;

    machine_at_common_init(model);

    pci_init(PCI_CONFIG_TYPE_1);
    pci_register_bus_slot(0, 0x00, PCI_CARD_NORTHBRIDGE, 0, 0, 0, 0);
    pci_register_bus_slot(0, 0x01, PCI_CARD_AGPBRIDGE,   1, 2, 3, 4);
    pci_register_bus_slot(0, 0x1e, PCI_CARD_BRIDGE,      0, 0, 0, 0);
    pci_register_bus_slot(0, 0x1f, PCI_CARD_SOUTHBRIDGE, 1, 2, 8, 4);
    pci_register_bus_slot(2, 0x03, PCI_CARD_NORMAL,      1, 2, 3, 4);
    pci_register_bus_slot(2, 0x04, PCI_CARD_NORMAL,      2, 3, 4, 1);
    pci_register_bus_slot(2, 0x05, PCI_CARD_NORMAL,      3, 4, 1, 2);
    pci_register_bus_slot(2, 0x06, PCI_CARD_NORMAL,      4, 1, 2, 3);
    pci_register_bus_slot(2, 0x07, PCI_CARD_NORMAL,      1, 2, 3, 4);

    device_add(&intel_845_device);          /* Intel 845 MCH */
    device_add(&intel_ich2_device);         /* Intel ICH2 */
    device_add(&w83627hf_device);          /* Winbond W83627HF */
    device_add(&sst_flash_49lf002_device);   /* SST 49LF002 2 Mbit Firmware Hub */
    device_add(ics9xxx_get(ICS9502_08));    /* ICS950208 Clock Chip */
    intel_845_spd_init();                   /* SPD */
#if 0
    spd_register(SPD_TYPE_SDRAM, 0x7, 512); /* SPD */
#endif

    return ret;
}
