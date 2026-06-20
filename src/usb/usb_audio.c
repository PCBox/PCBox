/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          USB Audio emulation, ported from QEMU.
 *
 * Authors: Cacodemon345
 *          H. Peter Anvin <hpa@linux.intel.com>
 *          Gerd Hoffmann <kraxel@redhat.com>
 *
 *          Copyright 2024-2025 Cacodemon345
 *          Copyright 2010-2019 Gerd Hoffmann
 *          Copyright 2010      H. Peter Anvin
 *          
 *          Original code based on USB net device code:
 *          Copyright 2006 Thomas Sailer
 *          Copyright 2008 Andrzej Zaborowski
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <math.h>
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
#include <86box/sound.h>
#include <86box/fifo8.h>

#include "usb_common.h"

enum usb_audio_strings {
    STRING_NULL,
    STRING_MANUFACTURER,
    STRING_PRODUCT,
    STRING_SERIALNUMBER,
    STRING_CONFIG,
    STRING_USBAUDIO_CONTROL,
    STRING_INPUT_TERMINAL,
    STRING_FEATURE_UNIT,
    STRING_OUTPUT_TERMINAL,
    STRING_NULL_STREAM,
    STRING_REAL_STREAM,
};

static const char* usb_audio_stringtable[] = {
    [STRING_CONFIG]             = "Audio Configuration",
    [STRING_USBAUDIO_CONTROL]   = "Audio Device",
    [STRING_INPUT_TERMINAL]     = "Audio Output Pipe",
    [STRING_FEATURE_UNIT]       = "Audio Output Volume Control",
    [STRING_OUTPUT_TERMINAL]    = "Audio Output Terminal",
    [STRING_NULL_STREAM]        = "Audio Output - Disabled",
    [STRING_REAL_STREAM]        = "Audio Output - 48 kHz Stereo",
};


// Speaker
static const uint8_t bx_audio_dev_descriptor[] = {
    0x12,       /*  u8 bLength; */
    0x01,       /*  u8 bDescriptorType; Device */
    0x01, 0x01, /*  u16 bcdUSB; v1.1 */

    0x00, /*  u8  bDeviceClass; */
    0x00, /*  u8  bDeviceSubClass; */
    0x00, /*  u8  bDeviceProtocol; */
    0x40, /*  u8  bMaxPacketSize; 64 Bytes */

    0x27, 0x06, /*  u16 idVendor; */
    0x01, 0x05, /*  u16 idProduct; */
    0x00, 0x00, /*  u16 bcdDevice */

    0x01, /*  u8  iManufacturer; */
    0x02, /*  u8  iProduct; */
    0x03, /*  u8  iSerialNumber; */
    0x01  /*  u8  bNumConfigurations; */
};

static const uint8_t bx_audio_config_descriptor[] = 
{
    0x09,       /*  u8  bLength; */
    0x02,       /*  u8  bDescriptorType; Configuration */
    113, 0x00,  /*  u16 wTotalLength; */
    0x02,       /*  u8  bNumInterfaces; (2) */
    0x01,       /*  u8  bConfigurationValue; */
    0x00,       /*  u8  iConfiguration; */
    0b10100000, /*  u8  bmAttributes;
                           Bit 7: must be set,
                               6: Self-powered,
                               5: Remote wakeup,
                               4..0: resvd */
    50,         /*  u8  MaxPower; */

    /* Standard AudioControl Interface Descriptor */
    0x09,       /* u8 bLength; */
    0x04,       /* u8 bDescriptorType; */
    0x00,       /* u8 bInterfaceNumber; */
    0x00,       /* u8 bAlternateSetting; */
    0x00,       /* u8 bNumEndpoints; */
    0x01,       /* u8 bInterfaceClass; */
    0x01,       /* u8 bInterfaceSubclass; */
    0x00,       /* u8 bInterfaceProtocol; */
    0x00,       /* u8 iInterface; */

    /* Class-specific Interface Descriptor */
    0x09,       /* u8 bLength; */
    0x24,       /* u8 bDescriptor Type (CS_INTERFACE); */
    0x01,       /* u8 bDescriptorSubType (HEADER); */
    0x00, 0x01, /* u16 bcdADC; */
    43, 0x00, /* u16 wTotalLength; */
    0x01,       /* u8 bInCollection; */
    0x01,       /* u8 baInterfaceNr; */

    /* Input Terminal ID1 Descriptor */
    0x0c,       /* u8 bLength; */
    0x24,       /* u8 bDescriptor Type (CS_INTERFACE); */
    0x02,       /* u8 bDescriptorSubType (INPUT_TERMINAL); */
    0x01,       /* u8 bTerminalID; */
    0x01, 0x01, /* u16 wTerminalType; */
    0x00,       /* u8 bAssocTerminal; */
    0x02,       /* u8 bNrChannels; */
    0x03, 0x00, /* u16 wChannelConfig; */
    0x00,       /* u8 iChannelNames; */
    0x00,       /* u8 iTerminal; */

    /* Feature Unit ID2 Descriptor */
    0x0d,       /* u8 bLength; */
    0x24,       /* u8 bDescriptorType; */
    0x06,       /* u8 bDescriptorSubtype; */
    0x02,       /* u8 bUnitID; */
    0x01,       /* u8 bSourceID; */
    0x02,       /* u8 bControlSize; */
    0x01, 0x00, /* u16 bmaControls(0); */
    0x02, 0x00, /* u16 bmaControls(1); */
    0x02, 0x00, /* u16 bmaControls(2); */
    0x00,       /* u8 iFeature; */

    /* Output Terminal ID3 Descriptor */
    0x09,       /* u8 bLength; */
    0x24,       /* u8 bDescriptorType; */
    0x03,       /* u8 bDescriptorSubType; */
    0x03,       /* u8 bUnitID */
    0x04, 0x03,  /* u16 wTerminalType; */
    0x00,       /* u8 bAssocTerminal; */
    0x02,       /* u8 bSourceID; */
    0x00,       /* u8 iTerminal; */

    /* Standard AudioStreaming Interface Descriptor */
    0x09,       /* u8 bLength; */
    0x04,       /* u8 bDescriptorType; */
    0x01,       /* u8 bInterfaceNumber; */
    0x00,       /* u8 bAlternateSetting; */
    0x00,       /* u8 bNumEndpoints; */
    0x01,       /* u8 bInterfaceClass; */
    0x02,       /* u8 bInterfaceSubclass; */
    0x00,       /* u8 bInterfaceProtocol; */
    0x00,       /* u8 iInterface; */

    /* Standard AudioStreaming Interface Descriptor (non-zero-bandwidth) */
    0x09,       /* u8 bLength; */
    0x04,       /* u8 bDescriptorType; */
    0x01,       /* u8 bInterfaceNumber; */
    0x01,       /* u8 bAlternateSetting; */
    0x01,       /* u8 bNumEndpoints; */
    0x01,       /* u8 bInterfaceClass; */
    0x02,       /* u8 bInterfaceSubclass; */
    0x00,       /* u8 bInterfaceProtocol; */
    0x00,       /* u8 iInterface; */

    /* Class-specific Interface Descriptor */
    0x07,       /* u8 bLength; */
    0x24,       /* u8 bDescriptorType; */
    0x01,       /* u8 bDescriptorSubtype; */
    0x01,       /* u8 bTerminalLink; */
    0x00,       /* u8 bDelay; */
    0x01, 0x00, /* u16 wFormatTag; */

    /* Type-I Format Descriptor */
    0x0b,       /* u8 bLength; */
    0x24,       /* u8 bDescriptorType; */
    0x02,       /* u8 bDescriptorSubtype; */
    0x01,       /* u8 bFormatType; */
    0x02,       /* u8 bNrChannels; */
    0x02,       /* u8 bSubFrameSize; */
    16,         /* u8 bBitResolution; */
    1,          /* u8 bSamFreqType; */
    0x80, 0xbb, 0x00, /* u24 tSamFreq */

    /* Endpoint Descriptor */
    0x09,       /* u8 bLength; */
    0x05,       /* u8 bDescriptorType; */
    0x01,       /* u8 bEndpointAddress; */
    0x0d,       /* u8 bmAttributes; */
    48 * 2 * 2, 0, /* u16 wMaxPacketSize; */
    0x01,       /* u8 bInterval; */
    0x00,       /* u8 bRefresh; */
    0x00,       /* u8 bSynchAddress; */

    /* Class-specific Endpoint Descriptor */
    0x07,       /* u8 bLength; */
    0x25,       /* u8 bDescriptorType; */
    0x01,       /* u8 bDescriptorSubtype; */
    // Reminder to ask the host to pad packets if this is not sufficient enough.
    0x80,       /* u8 bmAttributes; */
    0x00,       /* u8 bLockDelayUnits; */
    0x00, 0x00  /* u16 wLockDelay; */
};

/*
 * Class-specific control requests
 */
#define CR_SET_CUR      0x01
#define CR_GET_CUR      0x81
#define CR_SET_MIN      0x02
#define CR_GET_MIN      0x82
#define CR_SET_MAX      0x03
#define CR_GET_MAX      0x83
#define CR_SET_RES      0x04
#define CR_GET_RES      0x84
#define CR_SET_MEM      0x05
#define CR_GET_MEM      0x85
#define CR_GET_STAT     0xff

/*
 * Feature Unit Control Selectors
 */
#define MUTE_CONTROL                    0x01
#define VOLUME_CONTROL                  0x02
#define BASS_CONTROL                    0x03
#define MID_CONTROL                     0x04
#define TREBLE_CONTROL                  0x05
#define GRAPHIC_EQUALIZER_CONTROL       0x06
#define AUTOMATIC_GAIN_CONTROL          0x07
#define DELAY_CONTROL                   0x08
#define BASS_BOOST_CONTROL              0x09
#define LOUDNESS_CONTROL                0x0a


typedef struct usb_device_audio
{
    usb_device_c device;

    Fifo8 audio_buf;
    int16_t buffer[SOUNDBUFLEN * 2];

    int16_t vol[2];
    bool mute;
    
    int alt_iface_enabled;
    bool transfer_detected;
} usb_device_audio;

void
usb_device_audio_handle_reset(usb_device_c *device)
{
    usb_device_audio* usb_audio = (usb_device_audio*) device->priv;

    usb_audio->alt_iface_enabled = 0;
    fifo8_reset(&usb_audio->audio_buf);
}

void
usb_device_audio_handle_iface_change(usb_device_c* device, int iface)
{
    usb_device_audio* usb_audio = (usb_device_audio*) device->priv;

    usb_audio->alt_iface_enabled = !!iface;
    if (usb_audio->alt_iface_enabled == 0)
        fifo8_reset(&usb_audio->audio_buf);
}

#define ATTRIB_ID(cs, attrib, idif)     \
    (((cs) << 24) | ((attrib) << 16) | (idif))

static int usb_device_audio_get_control(usb_device_audio *device, uint8_t attrib,
                                 uint16_t cscn, uint16_t idif,
                                 int length, uint8_t *data)
{
    uint8_t val = cscn >> 8;
    uint8_t cn = cscn - 1;      /* -1 for the non-present master control */
    uint32_t aid = ATTRIB_ID(val, attrib, idif);
    int ret = USB_RET_STALL;

    switch (aid)
    {
    case ATTRIB_ID(MUTE_CONTROL, CR_GET_CUR, 0x0200):
    {
        data[0] = device->mute;
        ret = 1;
        break;
    }
    case ATTRIB_ID(VOLUME_CONTROL, CR_GET_CUR, 0x0200):
        if (cn < 2) {
            uint16_t vol = (uint16_t)device->vol[cn];
            data[0] = vol;
            data[1] = vol >> 8;
            ret = 2;
        }
        break;
    case ATTRIB_ID(VOLUME_CONTROL, CR_GET_MIN, 0x0200):
        if (cn < 2) {
            data[0] = 0x00;
            data[1] = 0xc2;
            ret = 2;
        }
        break;
    case ATTRIB_ID(VOLUME_CONTROL, CR_GET_MAX, 0x0200):
        if (cn < 2) {
            data[0] = 0x00;
            data[1] = 0x00;
            ret = 2;
        }
        break;
    case ATTRIB_ID(VOLUME_CONTROL, CR_GET_RES, 0x0200):
        if (cn < 2) {
            data[0] = 0x01;
            data[1] = 0x00;
            ret = 2;
        }
        break;
    }

    if (ret < 0)
        pclog("ATTRIB_ID(0x%X, 0x%X, 0x%04X), ret = %d (get)\n", val, attrib, idif, ret);

    return ret;
}

static int usb_device_audio_set_control(usb_device_audio *device, uint8_t attrib,
                                 uint16_t cscn, uint16_t idif,
                                 int length, uint8_t *data)
{
    uint8_t val = cscn >> 8;
    uint8_t cn = cscn - 1;      /* -1 for the non-present master control */
    uint32_t aid = ATTRIB_ID(val, attrib, idif);
    int ret = USB_RET_STALL;

    if (aid == ATTRIB_ID(MUTE_CONTROL, CR_SET_CUR, 0x0200))
    {
        device->mute = data[0] & 1;
        ret = 0;
    }

    if (aid == ATTRIB_ID(VOLUME_CONTROL, CR_SET_CUR, 0x0200) && cn < 2)
    {
        int16_t vol = data[0] + (data[1] << 8);
        device->vol[cn] = vol;
        ret = 0;
    }

    return ret;
}

int
usb_device_audio_handle_control(usb_device_c *device, int request, int value, int index, int length, uint8_t *data)
{
    int               ret = 0;
    usb_device_audio* usb_audio = (usb_device_audio*) device->priv;

    ret = usb_device_handle_control_common(device, request, value, index, length, data);
    if (ret >= 0) {
        return ret;
    }

    switch (request)
    {
        case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
            goto fail;
            break;
        case DeviceOutRequest | USB_REQ_SET_FEATURE:
            goto fail;
            break;
        case EndpointRequest | USB_REQ_GET_STATUS:
            // if the endpoint is currently halted, return bit 0 = 1
            if (value == USB_ENDPOINT_HALT) {
                if (index == 0x1) {
                    data[0] = 0x00 | (usb_device_get_halted(device, index) ? 1 : 0);
                    data[1] = 0x00;
                    ret     = 2;
                } else {
                    goto fail;
                }
            } else {
                goto fail;
            }
            break;
        case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
            if ((value & 0xff) > 3 && (value & 0xff) <= 8 && (value >> 8) == USB_DT_STRING)
                ret = usb_set_usb_string(data, usb_audio_stringtable[value & 0xff]);
            else
                goto fail;
            break;
        /* Class-specific requests. */
        case InterfaceInClassRequest | CR_GET_CUR:
        case InterfaceInClassRequest | CR_GET_MIN:
        case InterfaceInClassRequest | CR_GET_MAX:
        case InterfaceInClassRequest | CR_GET_RES:
            ret = usb_device_audio_get_control(usb_audio, request & 0xff, value, index,
                                        length, data);
            break;


        case InterfaceOutClassRequest | CR_SET_CUR:
        case InterfaceOutClassRequest | CR_SET_MIN:
        case InterfaceOutClassRequest | CR_SET_MAX:
        case InterfaceOutClassRequest | CR_SET_RES:
            ret = usb_device_audio_set_control(usb_audio, request & 0xff, value, index,
                                        length, data);
            break;

        default:
        fail:
            device->stall = 1;
            ret           = USB_RET_STALL;
            break;
    }

    if (ret < 0)
        pclog("request = 0x%04X, value = 0x%04X, index = 0x%04X, length = %d\n", request, value, index, length);

    return ret;
}

int
usb_device_audio_handle_data(usb_device_c *device, USBPacket *p)
{
    int ret = 0;
    usb_device_audio* usb_audio = (usb_device_audio*)device;

    switch (p->pid)
    {
        case USB_TOKEN_OUT:
            {
                if (p->devep == 1 && usb_audio->alt_iface_enabled)
                {
                    // Avoid overruns.
                    if (usb_audio->transfer_detected && p->len > 0) {
                        //pclog("Buffer overrun!");
                        fifo8_drop(&usb_audio->audio_buf, MIN(fifo8_num_used(&usb_audio->audio_buf), p->len));
                    }
                    // Null packets must be accepted as well.
                    if (p->len > 0)
                        fifo8_push_all(&usb_audio->audio_buf, p->data, p->len);
                    ret = p->len;
                    if (p->len > 0)
                        usb_audio->transfer_detected = true;
                }
                else
                {
                    goto fail;
                }
            }
            break;
        default:
fail:
            device->stall = 1;
            ret           = USB_RET_STALL;
            break;
    }

    return ret;
}

/*  We need to maintain a constant stream of data to the audio backend. The OS isn't
    obliged to send zero-length packets in isochronous transfers to signal end of audio buffers.
*/

static uint8_t zero[192];
static void
usb_audio_sof(void *priv)
{
    usb_device_audio* usb_audio = (usb_device_audio*)priv;

    if (!usb_audio->transfer_detected) {
        fifo8_push_all(&usb_audio->audio_buf, zero, 192);
    }
        
    usb_audio->transfer_detected = false;
}

static void
usb_audio_get_buffer(int32_t *buffer, int len, void *priv)
{
    usb_device_audio* usb_audio = (usb_device_audio*)priv;

    if (fifo8_num_used(&usb_audio->audio_buf) < (SOUNDBUFLEN * 2 * 2))
    {
        return;
    }

    fifo8_pop_buf(&usb_audio->audio_buf, (uint8_t*)usb_audio->buffer, sizeof (usb_audio->buffer));
    if (usb_audio->mute)
        return; // Utter silence.

    for (int c = 0; c < len * 2; c++) {
        double decibels = (usb_audio->vol[c & 1] / 32768.0) * 128.0;
        double gain = (usb_audio->vol[c & 1] == ((int16_t)0x8000)) ? 0.0 : (double)pow(10, (double)decibels / 20.0);
        buffer[c] += usb_audio->buffer[c] * gain;
    }
}

void *
usb_audio_device_create(const device_t *info)
{
    usb_device_audio* usb_audio = (usb_device_audio*) calloc(1, sizeof(usb_device_audio));
    usb_port_t* port = usb_search_for_ports();

    if (!port) {
        free(usb_audio);
        return NULL;
    }

    usb_device_create(&usb_audio->device);
    usb_audio->device.type     = 0;
    usb_audio->device.minspeed = USB_SPEED_FULL;
    usb_audio->device.maxspeed = USB_SPEED_FULL;
    usb_audio->device.speed    = usb_audio->device.minspeed;
    usb_audio->device.priv     = usb_audio;

    usb_audio->device.vendor_desc  = "86Box";
    usb_audio->device.product_desc = "USB Audio";
    usb_audio->device.serial_num   = "1";

    usb_audio->device.config_descriptor = bx_audio_config_descriptor;
    usb_audio->device.config_desc_size = sizeof(bx_audio_config_descriptor);
    usb_audio->device.dev_descriptor = bx_audio_dev_descriptor;
    usb_audio->device.device_desc_size = sizeof(bx_audio_dev_descriptor);

    usb_audio->device.endpoint_info[USB_CONTROL_EP].max_packet_size = 64; // Control ep0
    usb_audio->device.endpoint_info[USB_CONTROL_EP].max_burst_size  = 0;
    usb_audio->device.endpoint_info[1].max_packet_size              = 192; // Out ep1
    usb_audio->device.endpoint_info[1].max_burst_size               = 0;
    usb_audio->device.connected                                     = true;
    usb_audio->device.iface_alt                                     = 1;
    usb_audio->device.alt_iface_max                                 = 1;
    usb_audio->device.sof_callback                                  = usb_audio_sof;

    usb_audio->device.handle_iface_change = usb_device_audio_handle_iface_change;
    usb_audio->device.handle_control = usb_device_audio_handle_control;
    usb_audio->device.handle_data = usb_device_audio_handle_data;
    usb_audio->device.handle_reset = usb_device_audio_handle_reset;

    if (!port->connect(port, &usb_audio->device)) {
        free(usb_audio);
        return NULL;
    }

    fifo8_create(&usb_audio->audio_buf, 65536 * 4);

    sound_add_handler(usb_audio_get_buffer, usb_audio);
    return usb_audio;
}

void
usb_device_audio_destroy(void* priv)
{
    usb_device_audio* usb_audio = (usb_device_audio*) priv;

    fifo8_destroy(&usb_audio->audio_buf);
    free(usb_audio);
}

const device_t usb_audio_device = {
    .name          = "Desktop Speakers",
    .internal_name = "usb_audio",
    .flags         = DEVICE_USB,
    .local         = 0,
    .init          = usb_audio_device_create,
    .close         = usb_device_audio_destroy,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
