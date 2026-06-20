/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          USB mass storage emulation, ported from Bochs and somewhat rewritten.
 *
 * Authors: Cacodemon345
 *          Paul Brook
 * 
 *          Copyright 2006      CodeSourcery
 *          Copyright 2024-2025 Cacodemon345
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
#include <86box/scsi.h>
#include <86box/scsi_device.h>

#include "usb_common.h"

static const uint8_t bx_msd_dev_descriptor[] = {
  0x12,       /*  u8 bLength; */
  0x01,       /*  u8 bDescriptorType; Device */
  0x01, 0x01, /*  u16 bcdUSB; v1.1 */

  0x00,       /*  u8  bDeviceClass; */
  0x00,       /*  u8  bDeviceSubClass; */
  0x00,       /*  u8  bDeviceProtocol; */
  0x40,       /*  u8  bMaxPacketSize; 64 Bytes */

  /* Vendor and product id are arbitrary.  */
  0x00, 0x00, /*  u16 idVendor; */
  0x00, 0x00, /*  u16 idProduct; */
  0x00, 0x01, /*  u16 bcdDevice */

  0x01,       /*  u8  iManufacturer; */
  0x02,       /*  u8  iProduct; */
  0x03,       /*  u8  iSerialNumber; */
  0x01        /*  u8  bNumConfigurations; */
};


// Full-speed
static const uint8_t bx_msd_config_descriptor[] = {
  /* one configuration */
  0x09,       /*  u8  bLength; */
  0x02,       /*  u8  bDescriptorType; Configuration */
  0x20, 0x00, /*  u16 wTotalLength; */
  0x01,       /*  u8  bNumInterfaces; (1) */
  0x01,       /*  u8  bConfigurationValue; */
  0x00,       /*  u8  iConfiguration; */
  0xc0,       /*  u8  bmAttributes;
                        Bit 7: must be set,
                            6: Self-powered,
                            5: Remote wakeup,
                            4..0: resvd */
  0x00,       /*  u8  MaxPower; */

  /* one interface */
  0x09,       /*  u8  if_bLength; */
  0x04,       /*  u8  if_bDescriptorType; Interface */
  0x00,       /*  u8  if_bInterfaceNumber; */
  0,          /*  u8  if_bAlternateSetting; */
  0x02,       /*  u8  if_bNumEndpoints; */
  0x08,       /*  u8  if_bInterfaceClass; MASS STORAGE */
  0x06,       /*  u8  if_bInterfaceSubClass; SCSI */
  0x50,       /*  u8  if_bInterfaceProtocol; Bulk Only */
  0x00,       /*  u8  if_iInterface; */

  /* Bulk-In endpoint */
  0x07,       /*  u8  ep_bLength; */
  0x05,       /*  u8  ep_bDescriptorType; Endpoint */
  0x81,       /*  u8  ep_bEndpointAddress; IN Endpoint */
  0x02,       /*  u8  ep_bmAttributes; Bulk */
  0x40, 0x00, /*  u16 ep_wMaxPacketSize; 64 */
  0x00,       /*  u8  ep_bInterval; */

  /* Bulk-Out endpoint */
  0x07,       /*  u8  ep_bLength; */
  0x05,       /*  u8  ep_bDescriptorType; Endpoint */
  0x02,        /*  u8  ep_bEndpointAddress; OUT Endpoint */
  0x02,       /*  u8  ep_bmAttributes; Bulk */
  0x40, 0x00, /*  u16 ep_wMaxPacketSize; 64 */
  0x00        /*  u8  ep_bInterval; */
};

#pragma pack(push, 1)
struct usb_msd_cbw {
  uint32_t dCBWSignature;
  uint32_t dCBWTag;
  uint32_t dCBWDataTransferLength;
  uint8_t bmCBWFlags;
  uint8_t bCBWLUN;
  uint8_t bCBWCBLength;
  uint8_t CBWCB[16];
};

struct usb_msd_csw {

    uint32_t dCSWSignature, dCSWTag, dCSWDataResidue;

    uint8_t bCSWStatus;
};
#pragma pack(pop)

typedef struct usb_msd_cbw usb_msd_cbw;
typedef struct usb_msd_csw usb_msd_csw;

enum USBMSDMode {
  USB_MSDM_CBW,
  USB_MSDM_DATAOUT,
  USB_MSDM_DATAIN,
  USB_MSDM_CSW
};

typedef enum USBMSDMode USBMSDMode;

// USB requests
#define MassStorageReset  0xff
#define GetMaxLun         0xfe

typedef struct usb_device_msd
{
    usb_device_c device;

    uint8_t scsi_bus;

    uint32_t expected_len, data_in_len;
    uint32_t tag;
    USBMSDMode phase;

    usb_msd_cbw current_cbw;
    usb_msd_csw current_csw;
    uint8_t current_lun;

    uint8_t* temp_data;
    uint32_t temp_index;

    uint32_t force_stall;
} usb_device_msd;

#ifdef ENABLE_USB_MSD_LOG
int usb_msd_do_log = ENABLE_USB_MSD_LOG;

static void
usb_msd_log(const char *fmt, ...)
{
    va_list ap;

    if (usb_msd_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define usb_msd_log(fmt, ...)
#endif

int
usb_device_msd_handle_control(usb_device_c *device, int request, int value, int index, int length, uint8_t *data)
{
    int             ret = 0;
    usb_device_msd* usb_msd = (usb_device_msd*) device->priv;

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
                if (index == 0x81 || index == 0x2) {
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
        case InterfaceOutClassRequest | MassStorageReset:
        case MassStorageReset:
            {
                usb_device_set_toggle(device, 1, 0);
                usb_device_set_toggle(device, 2, 0);
                usb_msd->phase = USB_MSDM_CBW;
                ret = 0;
                break;
            }
        case InterfaceInClassRequest | GetMaxLun:
        case GetMaxLun:
            {
                int max_lun = -1;

                for (int i = 0; i < 16; i++)
                {
                    if (scsi_devices[usb_msd->scsi_bus][i].sc)
                    {
                        max_lun++;
                    }
                }
                if (max_lun == -1)
                    goto fail;
                
                data[0] = max_lun & 0xFF;
                ret = 1;
                break;
            }
        default:
        fail:
            device->stall = 1;
            ret           = USB_RET_STALL;
            break;
    }


    if (ret < 0)
        usb_msd_log("request = 0x%04X, value = 0x%04X, index = 0x%04X, length = %d\n", request, value, index, length);

    return ret;
}

static uint8_t
get_actual_lun(usb_device_msd* usb_msd, uint8_t lun)
{
    int actual_lun = -1;
    for (int i = 0; i < 16; i++)
    {
        if (scsi_devices[usb_msd->scsi_bus][i].sc)
        {
            actual_lun++;
            if (actual_lun == lun)
                return actual_lun & 0xFF;
        }
    }
    return 0;
}

int
usb_device_msd_handle_data(usb_device_c *device, USBPacket *p)
{
    int             ret = 0;
    usb_device_msd* usb_msd = (usb_device_msd*) device->priv;
    int len = p->len;

    switch (p->pid) {
        case USB_TOKEN_OUT:
        {
            if (p->devep != 2)
                goto fail;

            if (usb_msd->phase != USB_MSDM_CBW && usb_msd->phase != USB_MSDM_CSW && scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].status == SCSI_STATUS_CHECK_CONDITION) {
                usb_msd_log("Command failed\n");
                usb_msd->phase = USB_MSDM_CSW;
                usb_msd->current_csw.bCSWStatus = 0x01;
                usb_msd->current_csw.dCSWDataResidue = 0x0;
                scsi_device_command_stop(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun]);
                goto fail;
            }

            if (usb_msd->phase != USB_MSDM_CBW && usb_msd->phase != USB_MSDM_CSW && usb_msd->current_cbw.dCBWDataTransferLength < scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].buffer_length)
            {
                usb_msd_log("Phase error\n");
                usb_msd->phase = USB_MSDM_CSW;
                usb_msd->current_csw.bCSWStatus = 0x02;
                goto fail;
            }

            if (usb_msd->force_stall) {
                usb_msd->force_stall--;
                goto fail;
            }

            switch (usb_msd->phase)
            {
                case USB_MSDM_CBW:
                {
                    usb_msd_cbw* cbw = (usb_msd_cbw*)p->data;
                    if (len != 31) {
                        usb_msd_log("len != 31\n");
                        goto fail;
                    }
                    
                    if (cbw->dCBWSignature != 0x43425355) {
                        usb_msd_log("Mismatched signature\n");
                        goto fail;
                    }
                    
                    usb_msd->tag = cbw->dCBWTag;
                    usb_msd->expected_len = cbw->dCBWDataTransferLength;
                    usb_msd->temp_index = 0;
                    
                    if (usb_msd->expected_len == 0) {
                        usb_msd->phase = USB_MSDM_CSW;
                    } else if (cbw->bmCBWFlags & 0x80) {
                        usb_msd->phase = USB_MSDM_DATAIN;
                    } else {
                        usb_msd->phase = USB_MSDM_DATAOUT;
                        usb_msd->temp_data = calloc(1, usb_msd->expected_len);
                    }
                    usb_msd->current_cbw = *cbw;
                    usb_msd->current_lun = get_actual_lun(usb_msd, cbw->bCBWLUN) & 0xF;

                    usb_msd->current_csw.dCSWSignature = 0x53425355;
                    usb_msd->current_csw.dCSWTag       = cbw->dCBWTag;
                    scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].buffer_length = -1;
                    

                    ret = len;
                    usb_msd_log("CBWCB { 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X, 0x%02X }\n", cbw->CBWCB[0],
                                                                                                                                                                        cbw->CBWCB[1],
                                                                                                                                                                        cbw->CBWCB[2],
                                                                                                                                                                        cbw->CBWCB[3],
                                                                                                                                                                        cbw->CBWCB[4],
                                                                                                                                                                        cbw->CBWCB[5],
                                                                                                                                                                        cbw->CBWCB[6],
                                                                                                                                                                        cbw->CBWCB[7],
                                                                                                                                                                        cbw->CBWCB[8],
                                                                                                                                                                        cbw->CBWCB[9],
                                                                                                                                                                        cbw->CBWCB[10],
                                                                                                                                                                        cbw->CBWCB[11],
                                                                                                                                                                        cbw->CBWCB[12],
                                                                                                                                                                        cbw->CBWCB[13],
                                                                                                                                                                        cbw->CBWCB[14],
                                                                                                                                                                        cbw->CBWCB[15]);
                    scsi_device_command_phase0(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun], cbw->CBWCB);

                    usb_msd->temp_index = 0;
                    usb_msd->data_in_len = scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].buffer_length;
                    if (usb_msd->phase == USB_MSDM_CSW)
                    {
                        int cur_phase = scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].phase;
                        usb_msd->current_csw.dCSWDataResidue = 0;
                        if (scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].buffer_length == 0 && cur_phase == SCSI_PHASE_DATA_OUT)
                            scsi_device_command_phase1(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun]);
                        usb_msd->current_csw.bCSWStatus = (scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].phase == SCSI_PHASE_STATUS) ? (scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].status == SCSI_STATUS_CHECK_CONDITION) : 2;
                    } else {
                        if (usb_msd->current_cbw.dCBWDataTransferLength >= scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].buffer_length) {
                            usb_msd->current_csw.dCSWDataResidue = usb_msd->current_cbw.dCBWDataTransferLength - scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].buffer_length;
                            if (usb_msd->phase == USB_MSDM_DATAIN) {
                                scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].sc->temp_buffer = realloc(scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].sc->temp_buffer, usb_msd->current_cbw.dCBWDataTransferLength);
                                usb_msd->data_in_len = usb_msd->current_cbw.dCBWDataTransferLength;
                                scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].buffer_length = usb_msd->data_in_len;
                            }
                        }
                    }
                    break;
                }
                case USB_MSDM_DATAOUT:
                {
                    if (scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].status == SCSI_STATUS_CHECK_CONDITION) {
                        usb_msd_log("Command failed\n");
                        usb_msd->phase = USB_MSDM_CSW;
                        usb_msd->current_csw.bCSWStatus = 0x01;
                        usb_msd->current_csw.dCSWDataResidue = 0x0;
                        scsi_device_command_stop(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun]);
                        goto fail;
                    }
                    if (scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].phase != SCSI_PHASE_DATA_OUT) {
                        usb_msd_log("Phase error (not data out, phase 0x%02X)\n", scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].phase);
                        usb_msd->phase = USB_MSDM_CSW;
                        usb_msd->current_csw.bCSWStatus = 0x02;
                        scsi_device_command_stop(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun]);
                        goto fail;
                    }
                    
                    memcpy(&usb_msd->temp_data[usb_msd->temp_index], p->data, MIN(usb_msd->expected_len, p->len));
                    usb_msd->temp_index += MIN(usb_msd->expected_len, p->len);
                    usb_msd->expected_len -= MIN(usb_msd->expected_len, p->len);
                    if (usb_msd->expected_len == 0 || p->len < 64)
                    {
                        usb_msd_log("Transfer finished (data out)\n");
                        int residue = usb_msd->current_cbw.dCBWDataTransferLength - scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].buffer_length;
                        if (usb_msd->temp_index < scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].buffer_length)
                        {
                            usb_msd->phase = USB_MSDM_CSW;
                            usb_msd->current_csw.bCSWStatus = 0x02;
                            scsi_device_command_stop(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun]);
                            goto fail;
                        }
                        memcpy(scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].sc->temp_buffer, usb_msd->temp_data, scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].buffer_length);
                        scsi_device_command_phase1(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun]);
                        
                        usb_msd->phase = USB_MSDM_CSW;
                        usb_msd->current_csw.bCSWStatus = (scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].status == SCSI_STATUS_CHECK_CONDITION);
                        usb_msd->current_csw.dCSWDataResidue = residue;
                    }
                    ret = p->len;
                    break;
                }
                case USB_MSDM_CSW:
                    goto fail;
                default:
                {
                    usb_msd->phase = USB_MSDM_CSW;
                    usb_msd->current_csw.bCSWStatus = 0x02;
                    scsi_device_command_stop(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun]);
                    goto fail;
                }
            }
            break;
        }
        case USB_TOKEN_IN:
        {
            if (p->devep != 0x01)
                goto fail;

            if (usb_msd->phase != USB_MSDM_CBW && usb_msd->phase != USB_MSDM_CSW && scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].status == SCSI_STATUS_CHECK_CONDITION) {
                usb_msd_log("Command failed (data in)\n");
                usb_msd->phase = USB_MSDM_CSW;
                usb_msd->current_csw.bCSWStatus = 0x01;
                usb_msd->current_csw.dCSWDataResidue = 0x0;
                scsi_device_command_stop(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun]);
                goto fail;
            }

            if (usb_msd->phase != USB_MSDM_CBW && usb_msd->phase != USB_MSDM_CSW && usb_msd->current_cbw.dCBWDataTransferLength < scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].buffer_length)
            {
                usb_msd_log("Phase error\n");
                usb_msd->phase = USB_MSDM_CSW;
                usb_msd->current_csw.bCSWStatus = 0x02;
                usb_msd->force_stall = 0;
                goto fail;
            }

            if (usb_msd->force_stall) {
                usb_msd->force_stall--;
                goto fail;
            }

            switch (usb_msd->phase)
            {
                case USB_MSDM_DATAOUT:
                {
                    usb_msd_log("Data out error (not data in)\n");
                    usb_msd->phase = USB_MSDM_CSW;
                    usb_msd->current_csw.bCSWStatus = 0x02;
                    scsi_device_command_stop(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun]);
                    goto fail;
                }
                case USB_MSDM_DATAIN:
                {
                    uint32_t len = p->len;
                    if (scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].phase != SCSI_PHASE_DATA_IN) {
                        usb_msd_log("Phase error (not data in, phase 0x%02X)\n", scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].phase);
                        usb_msd->phase = USB_MSDM_CSW;
                        usb_msd->current_csw.bCSWStatus = 0x02;
                        scsi_device_command_stop(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun]);
                        goto fail;
                    }

                    len = MIN(p->len, usb_msd->data_in_len);
                    memcpy(p->data, &scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].sc->temp_buffer[usb_msd->temp_index], len);
                    usb_msd->temp_index += len;
                    usb_msd->data_in_len -= len;
                    ret = len;
                    if (usb_msd->data_in_len == 0)
                    {
                        usb_msd_log("Transfer finished (data in)\n");
                        scsi_device_command_phase1(&scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun]);
                        usb_msd->current_csw.bCSWStatus = (scsi_devices[usb_msd->scsi_bus][usb_msd->current_lun].status == SCSI_STATUS_CHECK_CONDITION);
                        usb_msd->phase = USB_MSDM_CSW;
//                        if (usb_msd->current_csw.dCSWDataResidue)
//                            usb_msd->force_stall = 1;
                    }
                    break;
                }
                case USB_MSDM_CBW:
                    goto fail;
                case USB_MSDM_CSW:
                {
                    memcpy(p->data, (void*)&usb_msd->current_csw, MIN(p->len, 13));
                    ret = MIN(p->len, 13);
                    usb_msd->phase = USB_MSDM_CBW;
                    usb_msd_log("CSW: 0x%08X, 0x%08X, 0x%08X, 0x%02X\n", usb_msd->current_csw.dCSWSignature, usb_msd->current_csw.dCSWTag, usb_msd->current_csw.dCSWDataResidue, usb_msd->current_csw.bCSWStatus);
                    break;
                }
            }
            break;
        }
        default:
        fail:
            device->stall = 1;
            ret           = USB_RET_STALL;
            break;
    }

    if ((usb_msd->phase == USB_MSDM_CSW || usb_msd->phase == USB_MSDM_CBW) && usb_msd->temp_data) {
        free(usb_msd->temp_data);
        usb_msd->temp_data = NULL;
    }

    return ret;
}

void
usb_device_msd_handle_reset(usb_device_c *device)
{
    usb_device_msd* usb_msd = (usb_device_msd*)device->priv;

    usb_msd->phase = USB_MSDM_CBW;
}

void *
usb_msd_device_create(const device_t *info)
{
    usb_device_msd* usb_msd = (usb_device_msd*) calloc(1, sizeof(usb_device_msd));
    usb_port_t* port = usb_search_for_ports();

    if (!port) {
        free(usb_msd);
        return NULL;
    }

    usb_device_create(&usb_msd->device);
    usb_msd->device.type     = 0;
    usb_msd->device.minspeed = USB_SPEED_FULL;
    usb_msd->device.maxspeed = USB_SPEED_FULL;
    usb_msd->device.speed    = usb_msd->device.minspeed;
    usb_msd->device.priv     = usb_msd;

    usb_msd->device.vendor_desc  = "86Box";
    usb_msd->device.product_desc = "USB MSD";
    usb_msd->device.serial_num   = "1";

    usb_msd->device.config_descriptor = bx_msd_config_descriptor;
    usb_msd->device.config_desc_size = sizeof(bx_msd_config_descriptor);
    usb_msd->device.dev_descriptor = bx_msd_dev_descriptor;
    usb_msd->device.device_desc_size = sizeof(bx_msd_dev_descriptor);

    usb_msd->device.endpoint_info[USB_CONTROL_EP].max_packet_size = 64; // Control ep0
    usb_msd->device.endpoint_info[USB_CONTROL_EP].max_burst_size  = 0;
    usb_msd->device.endpoint_info[1].max_packet_size              = 64;
    usb_msd->device.endpoint_info[1].max_burst_size               = 0;
    usb_msd->device.endpoint_info[2].max_packet_size              = 64;
    usb_msd->device.endpoint_info[2].max_burst_size               = 0;
    usb_msd->device.connected                                     = true;

    usb_msd->device.handle_control = usb_device_msd_handle_control;
    usb_msd->device.handle_data = usb_device_msd_handle_data;
    usb_msd->device.handle_reset = usb_device_msd_handle_reset;

    if (!port->connect(port, &usb_msd->device)) {
        free(usb_msd);
        return NULL;
    }

    usb_msd->scsi_bus = scsi_get_bus();
    scsi_bus_set_speed(usb_msd->scsi_bus, 10000000.0);

    for (uint8_t j = 0; j < SCSI_ID_MAX; j++) {
        scsi_devices[usb_msd->scsi_bus][j].removable_bus = 1;
    }

    return usb_msd;
}

static void usb_msd_device_close(void* priv)
{
    free(priv);
}

const device_t usb_msd_device = {
    .name          = "Mass Storage Device",
    .internal_name = "usb_msd",
    .flags         = DEVICE_USB,
    .local         = 0,
    .init          = usb_msd_device_create,
    .close         = usb_msd_device_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
