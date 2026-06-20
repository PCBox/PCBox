#
# 86Box    A hypervisor and IBM PC system emulator that specializes in
#          running old operating systems and software designed for IBM
#          PC systems and compatibles from 1981 through fairly recent
#          system designs based on the PCI bus.
#
#          This file is part of the 86Box distribution.
#
#          CMake build script.
#
# Authors: Cacodemon345
#
#          Copyright 2024-2025 Cacodemon345.
#

add_library(usb OBJECT
    usb_audio.c
    usb_common.c
    usb_hid_keyboard.c
    usb_hid_mouse.c
    usb_msd.c
    usb_ohci_bochs.c
    usb_uhci_bochs.c
)
