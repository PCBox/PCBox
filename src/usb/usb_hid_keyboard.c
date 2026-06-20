/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          USB HID emulation, ported from Bochs, which in turn was ported from QEMU.
 *
 * Authors: Cacodemon345
 *          Fabrice Bellard
 *          OpenMoko, Inc.  (andrew@openedhand.com)
 *          Benjamin D Lunt (fys [at] fysnet [dot] net)
 *          The Bochs Project
 * 
 *          Copyright 2005      Fabrice Bellard
 *          Copyright 2007      OpenMoko, Inc.  (andrew@openedhand.com)
 *          Copyright 2004-2023 Benjamin D Lunt (fys [at] fysnet [dot] net)
 *          Copyright 2009-2023 The Bochs Project
 *          Copyright 2024-2025 Cacodemon345
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>
#include <wchar.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/io.h>
#include <86box/mem.h>
#include <86box/usb.h>
#include <86box/dma.h>
#include "cpu.h"
#include <86box/pci.h>
#include <86box/timer.h>
#include <86box/keyboard.h>

#include "usb_common.h"

// one (or more) of our models uses the Report ID field. This is the ID value used.
#define HID_REPORT_ID 1

// our HID device(s) return two class specific strings, index 4 and index 5
#define HID_CLASS_STR4     4
#define HID_CLASS_STR5     5

#define BX_M_ELEMENTS_SIZE 8
#define BX_KBD_ELEMENTS    256

struct usb_device_hid {
    usb_device_c device;
    struct HID_STATE {
        bool   has_events;
        uint8_t  idle;
        uint8_t  kbd_packet[8];
        int    kbd_packet_indx;
        uint8_t  indicators;
        uint8_t  kbd_event_count;
        uint32_t kbd_event_buf[BX_KBD_ELEMENTS];
        // the remaining does not get cleared on a handle_reset()
        uint8_t        report_use_id; // id that we will use as soon as the HID report has been requested
        uint8_t        report_id;     // id that we will use after the HID report has been requested
        bool         boot_protocol; // 0 = boot protocol, 1 = report protocol

        /* Keyboard input is not thread-safe yet. */
        atomic_uint kbd_event_write;
        atomic_uint kbd_event_read;
        uint16_t kbd_events[256];
    } s;
    pc_timer_t idle_timer;
    pc_timer_t poll_timer;
};

typedef struct usb_device_hid usb_device_hid;

static usb_device_hid* usb_keyboard;

/* supported HID device types */
#define USB_HID_TYPE_KEYPAD   2
#define USB_HID_TYPE_KEYBOARD 3

/* HID IDLE time constant */
#define HID_IDLE_TIME 4000

/* HID interface requests */
#define GET_REPORT   0x01
#define GET_IDLE     0x02
#define GET_PROTOCOL 0x03
#define SET_REPORT   0x09
#define SET_IDLE     0x0A
#define SET_PROTOCOL 0x0B

/* BOOT Protocol or Report Protocol */
#define PROTOCOL_BOOT   0
#define PROTOCOL_REPORT 1

// If you change any of the Max Packet Size, or other fields within these
//  descriptors, you must also change the d.endpoint_info[] array
//  to match your changes.

////////////////////////////////////////////////
// keyboard/keypad
static const uint8_t bx_keypad_hid_report_descriptor[] = {
    0x05, 0x01,       // Usage Page (Generic Desktop Ctrls)
    0x09, 0x06,       // Usage (Keyboard)
    0xA1, 0x01,       // Collection (Application)
    0x05, 0x07,       //   Usage Page (Kbrd/Keypad)
    0x19, 0xE0,       //   Usage Minimum (Keyboard Left Control)
    0x29, 0xE7,       //   Usage Maximum (Keyboard Right GUI)
    0x15, 0x00,       //   Logical Minimum (0)
    0x25, 0x01,       //   Logical Maximum (1)
    0x75, 0x01,       //   Report Size (1)
    0x95, 0x08,       //   Report Count (8)
    0x81, 0x02,       //   Input (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x01,       //   Report Count (1)
    0x75, 0x08,       //   Report Size (8)
    0x81, 0x01,       //   Input (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0x95, 0x03,       //   Report Count (3)
    0x75, 0x01,       //   Report Size (1)
    0x05, 0x08,       //   Usage Page (LEDs)
    0x19, 0x01,       //   Usage Minimum (Num Lock)
    0x29, 0x03,       //   Usage Maximum (Scroll Lock)
    0x91, 0x02,       //   Output (Data,Var,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x05,       //   Report Count (5)
    0x75, 0x01,       //   Report Size (1)
    0x91, 0x01,       //   Output (Const,Array,Abs,No Wrap,Linear,Preferred State,No Null Position,Non-volatile)
    0x95, 0x06,       //   Report Count (6)
    0x75, 0x08,       //   Report Size (8)
    0x15, 0x00,       //   Logical Minimum (0)
    0x26, 0xFF, 0x00, //   Logical Maximum (255)
    0x05, 0x07,       //   Usage Page (Kbrd/Keypad)
    0x19, 0x00,       //   Usage Minimum (0)
    0x29, 0xE7,       //   Usage Maximum (Keyboard Right GUI)
    0x81, 0x00,       //   Input (Data,Array,Abs,No Wrap,Linear,Preferred State,No Null Position)
    0xC0,             // End Collection
};

static const uint8_t bx_keypad_dev_descriptor[] = {
    0x12,       /*  u8 bLength; */
    0x01,       /*  u8 bDescriptorType; Device */
    0x01, 0x01, /*  u16 bcdUSB; v1.1 */

    0x00, /*  u8  bDeviceClass; */
    0x00, /*  u8  bDeviceSubClass; */
    0x00, /*  u8  bDeviceProtocol; */
    0x08, /*  u8  bMaxPacketSize; 8 Bytes */

    0xB4, 0x04, /*  u16 idVendor; */
    0x01, 0x01, /*  u16 idProduct; */
    0x01, 0x00, /*  u16 bcdDevice */

    0x01, /*  u8  iManufacturer; */
    0x02, /*  u8  iProduct; */
    0x03, /*  u8  iSerialNumber; */
    0x01  /*  u8  bNumConfigurations; */
};

static const uint8_t bx_keypad_dev_descriptor2[] = {
    0x12,       /*  u8 bLength; */
    0x01,       /*  u8 bDescriptorType; Device */
    0x00, 0x02, /*  u16 bcdUSB; v2.0 */

    0x00, /*  u8  bDeviceClass; */
    0x00, /*  u8  bDeviceSubClass; */
    0x00, /*  u8  bDeviceProtocol; */
    0x40, /*  u8  bMaxPacketSize; 64 Bytes */

    0xB4, 0x04, /*  u16 idVendor; */
    0x01, 0x01, /*  u16 idProduct; */
    0x01, 0x00, /*  u16 bcdDevice */

    0x01, /*  u8  iManufacturer; */
    0x02, /*  u8  iProduct; */
    0x03, /*  u8  iSerialNumber; */
    0x01  /*  u8  bNumConfigurations; */
};

static const uint8_t bx_keypad_config_descriptor[] = {
    /* one configuration */
    0x09,       /*  u8  bLength; */
    0x02,       /*  u8  bDescriptorType; Configuration */
    0x22, 0x00, /*  u16 wTotalLength; */
    0x01,       /*  u8  bNumInterfaces; (1) */
    0x01,       /*  u8  bConfigurationValue; */
    0x04,       /*  u8  iConfiguration; */
    0xa0,       /*  u8  bmAttributes;
                           Bit 7: must be set,
                               6: Self-powered,
                               5: Remote wakeup,
                               4..0: resvd */
    50,         /*  u8  MaxPower; */

    /* one interface */
    0x09, /*  u8  if_bLength; */
    0x04, /*  u8  if_bDescriptorType; Interface */
    0x00, /*  u8  if_bInterfaceNumber; */
    0x00, /*  u8  if_bAlternateSetting; */
    0x01, /*  u8  if_bNumEndpoints; */
    0x03, /*  u8  if_bInterfaceClass; */
    0x01, /*  u8  if_bInterfaceSubClass; */
    0x01, /*  u8  if_bInterfaceProtocol; */
    0x05, /*  u8  if_iInterface; */

    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    sizeof(bx_keypad_hid_report_descriptor),
    0x00, /*  u16 len */

    /* one endpoint (status change endpoint) */
    0x07,       /*  u8  ep_bLength; */
    0x05,       /*  u8  ep_bDescriptorType; Endpoint */
    0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
    0x03,       /*  u8  ep_bmAttributes; Interrupt */
    0x08, 0x00, /*  u16 ep_wMaxPacketSize; */
    0x0a,       /*  u8  ep_bInterval; (255ms -- usb 2.0 spec) */
};

static const uint8_t bx_keypad_config_descriptor2[] = {
    /* one configuration */
    0x09,       /*  u8  bLength; */
    0x02,       /*  u8  bDescriptorType; Configuration */
    0x22, 0x00, /*  u16 wTotalLength; */
    0x01,       /*  u8  bNumInterfaces; (1) */
    0x01,       /*  u8  bConfigurationValue; */
    0x04,       /*  u8  iConfiguration; */
    0xa0,       /*  u8  bmAttributes;
                           Bit 7: must be set,
                               6: Self-powered,
                               5: Remote wakeup,
                               4..0: resvd */
    50,         /*  u8  MaxPower; */

    /* one interface */
    0x09, /*  u8  if_bLength; */
    0x04, /*  u8  if_bDescriptorType; Interface */
    0x00, /*  u8  if_bInterfaceNumber; */
    0x00, /*  u8  if_bAlternateSetting; */
    0x01, /*  u8  if_bNumEndpoints; */
    0x03, /*  u8  if_bInterfaceClass; */
    0x01, /*  u8  if_bInterfaceSubClass; */
    0x01, /*  u8  if_bInterfaceProtocol; */
    0x05, /*  u8  if_iInterface; */

    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    sizeof(bx_keypad_hid_report_descriptor),
    0x00, /*  u16 len */

    /* one endpoint (status change endpoint) */
    0x07,       /*  u8  ep_bLength; */
    0x05,       /*  u8  ep_bDescriptorType; Endpoint */
    0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint 1 */
    0x03,       /*  u8  ep_bmAttributes; Interrupt */
    0x08, 0x00, /*  u16 ep_wMaxPacketSize; */
    0x07,       /*  u8  ep_bInterval; (2 ^ (8-1) * 125 usecs = 8 ms) */
};

static const uint8_t bx_keypad_hid_descriptor[] = {
    /* HID descriptor */
    0x09,       /*  u8  bLength; */
    0x21,       /*  u8 bDescriptorType; */
    0x01, 0x01, /*  u16 HID_class (0x0101) */
    0x00,       /*  u8 country_code */
    0x01,       /*  u8 num_descriptors */
    0x22,       /*  u8 type; Report */
    sizeof(bx_keypad_hid_report_descriptor),
    0x00, /*  u16 len */
};

#define BX_ERROR(x) pclog x; pclog ("\n")
#define BX_INFO(x)  pclog x; pclog ("\n")
#define BX_DEBUG(x) pclog x; pclog ("\n")

// https://stackoverflow.com/a/69600455, altered for 86Box.
struct usb_scancode
{
    uint16_t set1;
    uint8_t usb;
} translation_table[] = 
{
    {0x00FF, 0x01}, // Overrun Error
    {0x00FC, 0x02}, // POST Fail
    {0x001E, 0x04}, // a A
    {0x0030, 0x05}, // b B
    {0x002E, 0x06}, // c C
    {0x0020, 0x07}, // d D
    {0x0012, 0x08}, // e E
    {0x0021, 0x09}, // f F
    {0x0022, 0x0A}, // g G
    {0x0023, 0x0B}, // h H
    {0x0017, 0x0C}, // i I
    {0x0024, 0x0D}, // j J
    {0x0025, 0x0E}, // k K
    {0x0026, 0x0F}, // l L
    {0x0032, 0x10}, // m M
    {0x0031, 0x11}, // n N
    {0x0018, 0x12}, // o O
    {0x0019, 0x13}, // p P
    {0x0010, 0x14}, // q Q
    {0x0013, 0x15}, // r R
    {0x001F, 0x16}, // s S
    {0x0014, 0x17}, // t T
    {0x0016, 0x18}, // u U
    {0x002F, 0x19}, // v V
    {0x0011, 0x1A}, // w W
    {0x002D, 0x1B}, // x X
    {0x0015, 0x1C}, // y Y
    {0x002C, 0x1D}, // z Z
    {0x0002, 0x1E}, // 1 !
    {0x0003, 0x1F}, // 2 @
    {0x0004, 0x20}, // 3 #
    {0x0005, 0x21}, // 4 $
    {0x0006, 0x22}, // 5 %
    {0x0007, 0x23}, // 6 ^
    {0x0008, 0x24}, // 7 &
    {0x0009, 0x25}, // 8 *
    {0x000A, 0x26}, // 9 (
    {0x000B, 0x27}, // 0 )
    {0x001C, 0x28}, // Return
    {0x0001, 0x29}, // Escape
    {0x000E, 0x2A}, // Backspace
    {0x000F, 0x2B}, // Tab
    {0x0039, 0x2C}, // Space
    {0x000C, 0x2D}, // - _
    {0x000D, 0x2E}, // = +
    {0x001A, 0x2F}, // [ {
    {0x001B, 0x30}, // ] }
    {0x002B, 0x31}, // \ |
    {0x002B, 0x32}, // Europe 1 (Note 2)
    {0x0027, 0x33}, // ; :
    {0x0028, 0x34}, // ' "
    {0x0029, 0x35}, // ` ~
    {0x0033, 0x36}, // , <
    {0x0034, 0x37}, // . >
    {0x0035, 0x38}, // / ?
    {0x003A, 0x39}, // Caps Lock
    {0x003B, 0x3A}, // F1
    {0x003C, 0x3B}, // F2
    {0x003D, 0x3C}, // F3
    {0x003E, 0x3D}, // F4
    {0x003F, 0x3E}, // F5
    {0x0040, 0x3F}, // F6
    {0x0041, 0x40}, // F7
    {0x0042, 0x41}, // F8
    {0x0043, 0x42}, // F9
    {0x0044, 0x43}, // F10
    {0x0057, 0x44}, // F11
    {0x0058, 0x45}, // F12
    {0x0137, 0x46}, // Print Screen (Note 1)
    {0x0046, 0x47}, // Scroll Lock
    {0x0152, 0x49}, // Insert (Note 1)
    {0x0147, 0x4A}, // Home (Note 1)
    {0x0149, 0x4B}, // Page Up (Note 1)
    {0x0153, 0x4C}, // Delete (Note 1)
    {0x014F, 0x4D}, // End (Note 1)
    {0x0151, 0x4E}, // Page Down (Note 1)
    {0x014D, 0x4F}, // Right Arrow (Note 1)
    {0x014B, 0x50}, // Left Arrow (Note 1)
    {0x0150, 0x51}, // Down Arrow (Note 1)
    {0x0148, 0x52}, // Up Arrow (Note 1)
    {0x0045, 0x53}, // Num Lock
    {0x0135, 0x54}, // Keypad / (Note 1)
    {0x0037, 0x55}, // Keypad *
    {0x004A, 0x56}, // Keypad -
    {0x004E, 0x57}, // Keypad +
    {0x011C, 0x58}, // Keypad Enter
    {0x004F, 0x59}, // Keypad 1 End
    {0x0050, 0x5A}, // Keypad 2 Down
    {0x0051, 0x5B}, // Keypad 3 PageDn
    {0x004B, 0x5C}, // Keypad 4 Left
    {0x004C, 0x5D}, // Keypad 5
    {0x004D, 0x5E}, // Keypad 6 Right
    {0x0047, 0x5F}, // Keypad 7 Home
    {0x0048, 0x60}, // Keypad 8 Up
    {0x0049, 0x61}, // Keypad 9 PageUp
    {0x0052, 0x62}, // Keypad 0 Insert
    {0x0053, 0x63}, // Keypad . Delete
    {0x0056, 0x64}, // Europe 2 (Note 2)
    {0x015D, 0x65}, // App
    {0x0059, 0x67}, // Keypad =
    {0x005D, 0x68}, // F13
    {0x005E, 0x69}, // F14
    {0x005F, 0x6A}, // F15
    {0x007E, 0x85}, // Keypad , (Brazilian Keypad .)
    {0x0073, 0x87}, // Keyboard Int'l 1 ろ (Ro)
    {0x0070, 0x88}, // Keyboard Int'l 2 かたかな ひらがな ローマ字 (Katakana/Hiragana)
    {0x007D, 0x89}, // Keyboard Int'l 3 ￥ (Yen)
    {0x0079, 0x8A}, // Keyboard Int'l 4 前候補 変換 (次候補) 全候補 (Henkan)
    {0x007B, 0x8B}, // Keyboard Int'l 5 無変換 (Muhenkan)
    {0x005C, 0x8C}, // Keyboard Int'l 6 (PC9800 Keypad , )
    {0x00F2, 0x90}, // Keyboard Lang 1 한/영 (Hanguel/English)
    {0x00F1, 0x91}, // Keyboard Lang 2 한자 (Hanja)
    {0x0078, 0x92}, // Keyboard Lang 3 かたかな (Katakana)
    {0x0077, 0x93}, // Keyboard Lang 4 ひらがな (Hiragana)
    {0x0076, 0x94}, // Keyboard Lang 5 半角/全角 (Zenkaku/Hankaku)
    {0x001D, 0xE0}, // Left Control
    {0x002A, 0xE1}, // Left Shift
    {0x0038, 0xE2}, // Left Alt
    {0x015B, 0xE3}, // Left GUI
    {0x011D, 0xE4}, // Right Control
    {0x0036, 0xE5}, // Right Shift
    {0x0138, 0xE6}, // Right Alt
    {0x015C, 0xE7}  // Right GUI
};

/* 512-bool buffer for tracking keyboard input. */
static bool down_keys[512];

void
usb_device_hid_kb_keyboard_input(int down, uint16_t key)
{
    if (!!down != down_keys[key & 0x1ff]) {
        down_keys[key & 0x1ff] = !!down;
        if (usb_keyboard && ((usb_keyboard->s.kbd_event_write - usb_keyboard->s.kbd_event_read) & 0xFF) < 255) {
            usb_keyboard->s.kbd_events[usb_keyboard->s.kbd_event_write & 0xFF] = key | (down << 9);
            usb_keyboard->s.kbd_event_write++;
        }
    }
}

void
usb_device_hid_kb_idle_timer(void *priv)
{
    usb_device_hid *hid = (usb_device_hid *) priv;

    hid->s.has_events = 1;
}

void
usb_device_hid_kb_poll_timer(void *priv)
{
    usb_device_hid *hid = (usb_device_hid *) priv;

    if (hid->s.kbd_event_read != hid->s.kbd_event_write) {
        if (hid->s.has_events == 0)
            usb_device_hc_event(&hid->device, USB_EVENT_WAKEUP, &hid->device);
        hid->s.has_events = 1;
    }
}

void
usb_device_hid_kb_start_timer(usb_device_hid *hid)
{
    if (hid->s.idle > 0)
        timer_on_auto(&hid->idle_timer, HID_IDLE_TIME * hid->s.idle);
}

void usb_device_keyboard_gen_scancode(usb_device_hid *hid, uint32_t code)
{
    bool down = (code >> 9) & 1;
    bool modkey = true;
    code &= 0x1FF;

    if (down) {
        switch (code)
        {
            case 0x1d:
                hid->s.kbd_packet[0] |= 0x1;
                break;
            case 0x2a:
                hid->s.kbd_packet[0] |= 0x2;
                break;
            case 0x38:
                hid->s.kbd_packet[0] |= 0x4;
                break;
            case 0x15b:
                hid->s.kbd_packet[0] |= 0x8;
                break;
            case 0x11d:
                hid->s.kbd_packet[0] |= 0x10;
                break;
            case 0x12a:
                hid->s.kbd_packet[0] |= 0x20;
                break;
            case 0x138:
                hid->s.kbd_packet[0] |= 0x40;
                break;
            case 0x15c:
                hid->s.kbd_packet[0] |= 0x80;
                break;
            default:
                modkey = false;
                break;
        }
    } else {
        switch (code)
        {
            case 0x1d:
                hid->s.kbd_packet[0] &= ~0x1;
                break;
            case 0x2a:
                hid->s.kbd_packet[0] &= ~0x2;
                break;
            case 0x38:
                hid->s.kbd_packet[0] &= ~0x4;
                break;
            case 0x15b:
                hid->s.kbd_packet[0] &= ~0x8;
                break;
            case 0x11d:
                hid->s.kbd_packet[0] &= ~0x10;
                break;
            case 0x12a:
                hid->s.kbd_packet[0] &= ~0x20;
                break;
            case 0x138:
                hid->s.kbd_packet[0] &= ~0x40;
                break;
            case 0x15c:
                hid->s.kbd_packet[0] &= ~0x80;
                break;
            default:
                modkey = false;
                break;
        }
    }

    if (!modkey) {
        uint32_t i = 0;
        uint8_t usb_code = 0;

        for (i = 0; i < (sizeof(translation_table) / sizeof(struct usb_scancode)); i++) {
            if (translation_table[i].set1 == code)
            {
                usb_code = translation_table[i].usb;
                break;
            }
        }

        if (usb_code == 0)
            return; // This is not a code we recognize.

        if (down)
        {
            hid->s.kbd_packet_indx++;
            if (hid->s.kbd_packet_indx > 7)
            {
                hid->s.kbd_packet[2] =
                hid->s.kbd_packet[3] =
                hid->s.kbd_packet[4] =
                hid->s.kbd_packet[5] =
                hid->s.kbd_packet[6] =
                hid->s.kbd_packet[7] = 1;
            } else {
                hid->s.kbd_packet[hid->s.kbd_packet_indx] = usb_code;
            }
        } else {
            for (i = 2; i < 8; i++)
            {
                uint32_t j = 0;
                if (hid->s.kbd_packet[i] == usb_code)
                {
                    j = i;
                    for (j = i; j < 7; j++)
                    {
                        hid->s.kbd_packet[j] = hid->s.kbd_packet[j+1];
                    }
                    hid->s.kbd_packet[7] = 0;
                    if (hid->s.kbd_packet_indx > 1) {
                        bool reconstruct_packet = hid->s.kbd_packet_indx == 8;
                        hid->s.kbd_packet_indx--;
                        if (hid->s.kbd_packet_indx > 7) {
                            hid->s.kbd_packet[2] =
                            hid->s.kbd_packet[3] =
                            hid->s.kbd_packet[4] =
                            hid->s.kbd_packet[5] =
                            hid->s.kbd_packet[6] =
                            hid->s.kbd_packet[7] = 1;
                        } else if (reconstruct_packet) {
                            // Reconstruct the packet.
                            uint32_t set1_code = 0;
                            uint8_t packet_index = 2;
                            for (set1_code = 0; set1_code <= 0x1FF; set1_code++)
                            {
                                if (packet_index >= 8)
                                    break;
                                if (set1_code == 0x1d || set1_code == 0x11d
                                    || set1_code == 0x2a || set1_code == 0x12a
                                    || set1_code == 0x38 || set1_code == 0x138
                                    || set1_code == 0x15c || set1_code == 0x15b)
                                continue;

                                if (down_keys[set1_code])
                                {
                                    uint32_t k = 0;
                                    for (k = 0; k < (sizeof(translation_table) / sizeof(struct usb_scancode)); k++) {
                                        if (translation_table[k].set1 == code)
                                        {
                                            hid->s.kbd_packet[packet_index++] = translation_table[k].usb;
                                            break;
                                        }
                                    }
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

int usb_device_hid_keyboard_poll(usb_device_hid *hid, uint8_t *buf, bool force)
{
  int l = USB_RET_NAK;

  if ((hid->device.type == USB_HID_TYPE_KEYPAD) ||
      (hid->device.type == USB_HID_TYPE_KEYBOARD)) {
    if (hid->s.has_events || force) {
      if (hid->s.kbd_event_read != hid->s.kbd_event_write) {
        while (hid->s.kbd_event_read != hid->s.kbd_event_write) {
            if (hid->s.kbd_event_count >= 255)
                break;
            hid->s.kbd_event_buf[hid->s.kbd_event_count++] = hid->s.kbd_events[hid->s.kbd_event_read & 0xFF];
            hid->s.kbd_event_read++;
        }
      }
      if (hid->s.kbd_event_count > 0) {
        usb_device_keyboard_gen_scancode(hid, hid->s.kbd_event_buf[0]);
        hid->s.kbd_event_count--;
        for (uint8_t i = 0; i < hid->s.kbd_event_count; i++) {
          hid->s.kbd_event_buf[i] = hid->s.kbd_event_buf[i + 1];
        }
      }
      memcpy(buf, hid->s.kbd_packet, 8);
      usb_device_hid_kb_start_timer(hid);
      hid->s.has_events = 0;
      return 8;
    }
  }

  return l;
}

int
usb_device_hid_kb_handle_control(usb_device_c *device, int request, int value, int index, int length, uint8_t *data)
{
    int             ret = 0;
    usb_device_hid *hid = (usb_device_hid *) device;

    ret = usb_device_handle_control_common(device, request, value, index, length, data);
    if (ret >= 0) {
        return ret;
    }

    ret = 0;

    switch (request) {
        case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
            BX_DEBUG(("HID: DeviceRequest | CLEAR_FEATURE:"));
            goto fail;
            break;
        case DeviceOutRequest | USB_REQ_SET_FEATURE:
            BX_DEBUG(("HID: DeviceRequest | SET_FEATURE:"));
            goto fail;
            break;
        case EndpointRequest | USB_REQ_GET_STATUS:
            BX_DEBUG(("USB_REQ_GET_STATUS: Endpoint."));
            // if the endpoint is currently halted, return bit 0 = 1
            if (value == USB_ENDPOINT_HALT) {
                if (index == 0x81) {
                    data[0] = 0x00 | (usb_device_get_halted(device, index) ? 1 : 0);
                    data[1] = 0x00;
                    ret     = 2;
                } else {
                    BX_ERROR(("EndpointRequest | USB_REQ_GET_STATUS: index > ep count: %d", index));
                    goto fail;
                }
            } else {
                BX_ERROR(("EndpointRequest | USB_REQ_SET_FEATURE: Unknown Get Status Request found: %d", value));
                goto fail;
            }
            break;
        case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
            BX_DEBUG(("HID: DeviceRequest | USB_REQ_GET_DESCRIPTOR:"));
            switch (value >> 8) {
                case USB_DT_STRING:
                    switch (value & 0xff) {
                        case HID_CLASS_STR4:
                            ret = usb_set_usb_string(data, "HID Keyboard");
                            break;
                        case HID_CLASS_STR5:
                            ret = usb_set_usb_string(data, "Endpoint1 Interrupt Pipe");
                            break;
                        default:
                            BX_ERROR(("USB HID handle_control: unknown string descriptor 0x%02x", value & 0xff));
                            goto fail;
                    }
                    break;
                default:
                    BX_ERROR(("USB HID handle_control: unknown descriptor type 0x%02x", value >> 8));
                    goto fail;
            }
            break;
            /* hid specific requests */
        case InterfaceRequest | USB_REQ_GET_DESCRIPTOR:
            BX_DEBUG(("HID: InterfaceRequest | USB_REQ_GET_DESCRIPTOR:"));
            switch (value >> 8) {
                case 0x21: // HID Descriptor
                    if ((value & 0xFF) != 0) {
                        BX_ERROR(("USB_REQ_GET_DESCRIPTOR: The Descriptor Index must be zero for this request."));
                    }
                    if ((device->type == USB_HID_TYPE_KEYPAD) || (device->type == USB_HID_TYPE_KEYBOARD)) {
                        memcpy(data, bx_keypad_hid_descriptor,
                               sizeof(bx_keypad_hid_descriptor));
                        ret = sizeof(bx_keypad_hid_descriptor);
                    } else {
                        goto fail;
                    }
                    break;
                case 0x22: // HID Report Descriptor
                    if ((value & 0xFF) != 0) {
                        BX_ERROR(("USB HID handle_control: The Descriptor Index must be zero for this request."));
                    }
                    if ((device->type == USB_HID_TYPE_KEYPAD) || (device->type == USB_HID_TYPE_KEYBOARD)) {
                        memcpy(data, bx_keypad_hid_report_descriptor,
                               sizeof(bx_keypad_hid_report_descriptor));
                        ret = sizeof(bx_keypad_hid_report_descriptor);
                    } else {
                        goto fail;
                    }
                    // now the guest knows the report id, so we need to use it
                    hid->s.report_id = hid->s.report_use_id;
                    break;
                case 0x23: // HID Physical Descriptor
                    BX_ERROR(("USB HID handle_control: Host requested the HID Physical Descriptor"));
                    goto fail;
                    break;
                default: // 0x24 -> 0x2F
                    BX_ERROR(("USB HID handle_control: unknown HID descriptor 0x%02x", value >> 8));
                    goto fail;
            }
            break;
        case InterfaceInClassRequest | GET_REPORT:
            BX_DEBUG(("HID: GET_REPORT:"));
            if ((value >> 8) == 1) { // Input report
                if ((value & 0xFF) == hid->s.report_id) {
                    if (device->type == USB_HID_TYPE_KEYBOARD) {
                        ret = usb_device_hid_keyboard_poll(hid, data, 1);
                        if (ret > length)
                            ret = length;
                    } else {
                        goto fail;
                    }
                } else {
                    BX_ERROR(("USB HID handle_control: Report ID (%d) doesn't match requested ID (%d)", hid->s.report_id, value & 0xFF));
                    goto fail;
                }
            } else {
                BX_ERROR(("USB HID handle_control: Requested report type (%d) must be Input(1)", (value >> 8) & 0xFF));
                goto fail;
            }
            break;
        case InterfaceOutClassRequest | SET_REPORT:
            BX_DEBUG(("HID: SET_REPORT:"));
            if (value == 0x0200) { // 0x02 = Report Type: Output, 0x00 = ID (our keyboard/keypad use an ID of zero)
                uint8_t modchange = (data[0] ^ hid->s.indicators);
                if (modchange != 0) {
                if (modchange & 0x01) {
                    //DEV_kbd_set_indicator(1, BX_KBD_LED_NUM, data[0] & 0x01);
                    BX_DEBUG(("NUM_LOCK %s", (data[0] & 0x01) ? "on" : "off"));
                } else if (hid->device.type == USB_HID_TYPE_KEYBOARD) {
                    if (modchange & 0x02) {
                    //DEV_kbd_set_indicator(1, BX_KBD_LED_CAPS, data[0] & 0x02);
                    BX_DEBUG(("CAPS_LOCK %s", (data[0] & 0x02) ? "on" : "off"));
                    } else if (modchange & 0x04) {
                    //DEV_kbd_set_indicator(1, BX_KBD_LED_SCRL, data[0] & 0x04);
                    BX_DEBUG(("SCRL_LOCK %s", (data[0] & 0x04) ? "on" : "off"));
                    }
                }
                hid->s.indicators = data[0];
                }
                ret = 0;
            } 
            break;
        case InterfaceInClassRequest | GET_IDLE:
            BX_DEBUG(("HID: GET_IDLE:"));
            // The wLength field should be 1 for this request
            if (length != 1) {
                BX_ERROR(("USB HID handle_control: The wLength field should be 1 for this request."));
            }
            if ((value & 0xFF00) != 0) {
                BX_ERROR(("USB HID handle_control: High byte of Value must be 0."));
            }
            if ((value & 0xFF) == hid->s.report_id) {
                data[0] = hid->s.idle;
                ret     = 1;
            } else {
                BX_ERROR(("USB HID handle_control: Report ID (%d) doesn't match requested ID (%d)", hid->s.report_id, value & 0xFF));
                goto fail;
            }
            break;
        case InterfaceOutClassRequest | SET_IDLE:
            BX_DEBUG(("HID: SET_IDLE:"));
            // The wLength field should be 0 for this request
            if (length != 0) {
                BX_ERROR(("USB HID handle_control: The wLength field should be 0 for this request."));
            }
            if ((value & 0xFF) == hid->s.report_id) {
                hid->s.idle = (value >> 8);
                if (hid->s.idle == 0)
                    pclog("Idle disabled\n");
                usb_device_hid_kb_start_timer(hid);
                ret = 0;
            } else {
                BX_ERROR(("USB HID handle_control: Report ID (%d) doesn't match requested ID (%d)", hid->s.report_id, value & 0xFF));
                goto fail;
            }
            break;
        case InterfaceOutClassRequest | SET_PROTOCOL:
            BX_DEBUG(("HID: SET_PROTOCOL:"));
            // The wLength field should be 0 for this request
            if (length != 0) {
                BX_ERROR(("HID SET_PROTOCOL: The wLength field should be 0 for this request."));
            }
            if ((value != 0) && (value != 1)) {
                BX_ERROR(("HID SET_PROTOCOL: The wValue field must be 0 or 1 for this request."));
            }
            if (value == 0) {
                BX_DEBUG(("HID SET_PROTOCOL: SET_PROTOCOL: Boot Protocol"));
                hid->s.boot_protocol = PROTOCOL_BOOT;
                ret                  = 0;
            } else if (value == 1) {
                BX_DEBUG(("HID SET_PROTOCOL: SET_PROTOCOL: Report Protocol"));
                hid->s.boot_protocol = PROTOCOL_REPORT;
                ret                  = 0;
            } else
                goto fail;
            break;
        case InterfaceInClassRequest | GET_PROTOCOL:
            BX_DEBUG(("HID: GET_PROTOCOL:"));
            // The wLength field should be 1 for this request
            if (length != 1) {
                BX_ERROR(("HID GET_PROTOCOL: The wLength field should be 1 for this request."));
            }
            data[0] = (hid->s.boot_protocol == PROTOCOL_BOOT) ? 0 : 1;
            ret     = 1;
            break;
        default:
            BX_ERROR(("USB HID handle_control: unknown request 0x%04x", request));
fail:
            device->stall = 1;
            ret           = USB_RET_STALL;
            break;
    }
    return ret;
}

int
usb_device_hid_kb_handle_data(usb_device_c *device, USBPacket *p)
{
    int ret = 0;
    usb_device_hid *hid = (usb_device_hid *) device;

    // check that the length is <= the max packet size of the device
    if (p->len > usb_device_get_mps(device, p->devep)) {
        BX_DEBUG(("EP%d transfer length (%d) is greater than Max Packet Size (%d).", p->devep, p->len, usb_device_get_mps(device, (p->devep))));
    }

    switch (p->pid) {
        case USB_TOKEN_IN:
            if (p->devep == 1) {
                if (device->type == USB_HID_TYPE_KEYBOARD) {
                    //ret = usb_mouse_poll((usb_device_hid *) device, p->data, 1);
                    ret = usb_device_hid_keyboard_poll((usb_device_hid *) device, p->data, (hid->s.kbd_event_read != hid->s.kbd_event_write) || (hid->s.kbd_event_count) || (hid->s.has_events));
                } else {
                    goto fail;
                }
            } else {
                goto fail;
            }
            break;
        case USB_TOKEN_OUT:
            BX_ERROR(("USB HID handle_data: unexpected pid TOKEN_OUT"));
        default:
fail:
            device->stall = 1;
            ret           = USB_RET_STALL;
            break;
    }

    return ret;
}

bool
usb_hid_kb_device_init(usb_device_c* device)
{
    usb_device_hid *hid = (usb_device_hid *) device;
    hid->device.dev_descriptor = bx_keypad_dev_descriptor;
    hid->device.device_desc_size = sizeof(bx_keypad_dev_descriptor);
    hid->device.config_descriptor = bx_keypad_config_descriptor;
    hid->device.config_desc_size = sizeof(bx_keypad_config_descriptor);
    hid->device.endpoint_info[USB_CONTROL_EP].max_packet_size = 8; // Control ep0
    hid->device.endpoint_info[USB_CONTROL_EP].max_burst_size = 0;
    hid->device.endpoint_info[1].max_packet_size = 8;  // In ep1
    hid->device.endpoint_info[1].max_burst_size = 0;
    hid->device.connected     = 1;
    hid->device.alt_iface_max = 0;
    return 1;
}

void
usb_hid_kb_handle_reset(usb_device_c *device)
{
    usb_device_hid *hid = (usb_device_hid *) device;
    memset((void *) &hid->s, 0, offsetof(struct HID_STATE, report_use_id));

    // HID 1.11, section 7.2.6, page 54(64):
    //  "When initialized, all devices default to report protocol."
    hid->s.boot_protocol = PROTOCOL_REPORT;
    // next will be byte 2 in the 8 byte packet
    hid->s.kbd_packet_indx = 1;
}

void *
usb_hid_kb_device_create(const device_t *info)
{
    usb_device_hid *hid = calloc(1, sizeof(usb_device_hid));
    usb_port_t* port = usb_search_for_ports();
    memset(down_keys, 0, sizeof(down_keys));

    if (!port) {
        free(hid);
        return NULL;
    }

    usb_device_create(&hid->device);
    hid->device.type     = USB_HID_TYPE_KEYBOARD;
    hid->device.minspeed = USB_SPEED_LOW;
    hid->device.maxspeed = USB_SPEED_FULL;
    hid->device.speed    = hid->device.minspeed;

    hid->device.vendor_desc  = "86Box";
    hid->device.product_desc = hid->device.devname;
    hid->device.serial_num   = "1";
    memset((void *) &hid->s, 0, sizeof(hid->s));

    // HID 1.11, section 7.2.6, page 54(64):
    //  "When initialized, all devices default to report protocol."
    hid->s.boot_protocol = PROTOCOL_REPORT;
    hid->s.report_id     = 0;
    // next will be byte 2 in the 8 byte packet
    hid->s.kbd_packet_indx = 1;

    hid->device.handle_data    = usb_device_hid_kb_handle_data;
    hid->device.handle_control = usb_device_hid_kb_handle_control;
    hid->device.handle_reset   = usb_hid_kb_handle_reset;
    hid->device.init           = usb_hid_kb_device_init;

    timer_add(&hid->idle_timer, usb_device_hid_kb_idle_timer, hid, 0);

    if (!port->connect(port, &hid->device)) {
        free(hid);
        return NULL;
    }
    
    timer_add(&hid->poll_timer, usb_device_hid_kb_poll_timer, hid, 0);
    timer_on_auto(&hid->poll_timer, 1000.);

    usb_keyboard = hid;
    keyboard_send_usb = usb_device_hid_kb_keyboard_input;
    pclog("USBK\n");
    return hid;
}

static void usb_hid_kb_device_close(void* priv)
{
    keyboard_send_usb = NULL;
    free(priv);
    usb_keyboard = NULL;
    memset(down_keys, 0, sizeof(down_keys));
}

const device_t usb_keyboard_device = {
    .name          = "USB Keyboard",
    .internal_name = "usb_keyboard",
    .flags         = DEVICE_USB,
    .local         = 0,
    .init          = usb_hid_kb_device_create,
    .close         = usb_hid_kb_device_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
