/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Generic USB emulation code, ported from Bochs.
 *
 * Authors: Cacodemon345
 *          Fabrice Bellard
 *          Benjamin D Lunt (fys [at] fysnet [dot] net)
 *          The Bochs Project
 * 
 *          Copyright 2009-2023 Benjamin D Lunt (fys [at] fysnet [dot] net)
 *          Copyright 2009-2023 The Bochs Project
 *          Copyright 2024-2025 Cacodemon345
 *          Copyright 2005      Fabrice Bellard
 */

#include <string.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>

#include <86box/86box.h>
#include "usb_common.h"

// Generic USB packet handler
#define SETUP_STATE_IDLE      0
#define SETUP_STATE_DATA_IN   1
#define SETUP_STATE_DATA_OUT  2

int usb_device_handle_packet(usb_device_c* device, USBPacket *p);

void usb_packet_init(USBPacket *p, int size)
{
  memset(p, 0, sizeof(USBPacket));
  if (size > 0) {
    p->data = calloc(1, size);
    if (!p->data) {
      return;
    }
  }
  p->len = size;
}

void usb_packet_cleanup(USBPacket *p)
{
  if (p->data) {
    free(p->data);
    p->data = NULL;
  }
}

void usb_defer_packet(USBPacket *p, usb_device_c *dev)
{
  p->dev = dev;
}

void usb_cancel_packet(USBPacket *p)
{
  p->dev->cancel_packet(p->dev, p);
}

void usb_packet_complete(USBPacket *p)
{
  p->complete_cb(USB_EVENT_ASYNC, p, p->complete_dev, 0);
}

// Async packet support

USBAsync* create_async_packet(USBAsync **base, uint64_t addr, int maxlen)
{
  USBAsync *p;

  p = calloc(1, sizeof(USBAsync));
  usb_packet_init(&p->packet, maxlen);
  p->td_addr = addr;
  p->done = 0;
  p->next = *base;
  *base = p;
  return p;
}

void remove_async_packet(USBAsync **base, USBAsync *p)
{
  USBAsync *last;

  if (*base == p) {
    *base = p->next;
  } else {
    last = *base;
    while (last != NULL) {
      if (last->next != p)
        last = last->next;
      else
        break;
    }
    if (last) {
      last->next = p->next;
    } else {
      return;
    }
  }
  usb_packet_cleanup(&p->packet);
  free(p);
}

USBAsync* find_async_packet(USBAsync **base, uint64_t addr)
{
  USBAsync *p = *base;

  while (p != NULL) {
    if (p->td_addr != addr)
      p = p->next;
    else
      break;
  }
  return p;
}

/* Default callbacks. */
void usb_device_handle_reset(usb_device_c* device) {}
void usb_device_handle_iface_change(usb_device_c* device, int iface) {}
int usb_device_handle_control(usb_device_c* device, int request, int value, int index, int length, uint8_t *data) { return -1; }
int usb_device_handle_data(usb_device_c* device, USBPacket *p) { return 0; }
void handle_iface_change(usb_device_c* device, int iface) {}
void usb_device_cancel_packet(usb_device_c* device, USBPacket *p) {}

uint8_t usb_device_get_type(usb_device_c* device) {
    return device->type;
}

uint8_t usb_device_get_alt_iace(usb_device_c* device) {
    return device->alt_iface;
}

uint8_t usb_device_get_address(usb_device_c* device) {
    return device->addr;
}

void usb_device_set_async_mode(usb_device_c* device, bool async) {
    device->async_mode = async;
}

void usb_device_set_event_handler(usb_device_c* device, void *dev, USBCallback *cb, int port)
{
    device->event.dev = dev;
    device->event.cb = cb;
    device->event.port = port;
}

int usb_device_hc_event(usb_device_c* host, int event, usb_device_c *device) {
    if (host->event.cb != NULL)
        return host->event.cb(event, device, host->event.dev, host->event.port);
    return -1;
}

bool usb_device_init(usb_device_c* device)
{
    device->connected = 1;
    return device->connected;
}

void usb_device_create(usb_device_c* device)
{
    int i = 0;
    memset(device, 0, sizeof(usb_device_c));

    device->async_mode = 1;
    device->speed = USB_SPEED_LOW;
    device->first8 = 0;
    device->iface_alt = 0;
    for (i = 0; i < USB_MAX_ENDPOINTS; i++)
        device->endpoint_info[i].toggle = 0;
    
    device->setup_state = SETUP_STATE_IDLE;

    device->handle_control = usb_device_handle_control;
    device->handle_data = usb_device_handle_data;
    device->cancel_packet = usb_device_cancel_packet;
    device->handle_reset = usb_device_handle_reset;
    device->handle_iface_change = usb_device_handle_iface_change;
    device->init = usb_device_init;
    device->handle_packet = usb_device_handle_packet;
}

#define BX_ERROR(x) 
#define BX_INFO(x) 
#define BX_DEBUG(x) 

int usb_device_get_toggle(usb_device_c* device, int ep) {
  return ((ep & 0x7F) < USB_MAX_ENDPOINTS) ? device->endpoint_info[(ep & 0x7F)].toggle : 0;
}

void usb_device_set_toggle(usb_device_c* device, int ep, int toggle) {
  if ((ep & 0x7F) < USB_MAX_ENDPOINTS) 
    device->endpoint_info[(ep & 0x7F)].toggle = toggle;
}

int usb_device_get_mps(usb_device_c* device, const int ep) {
  return (ep < USB_MAX_ENDPOINTS) ? device->endpoint_info[ep].max_packet_size : 0;
}

void usb_device_set_halted(usb_device_c* device, int ep, const bool halted) {
  if ((ep & 0x7F) < USB_MAX_ENDPOINTS) 
    device->endpoint_info[(ep & 0x7F)].halted = halted;
}

bool usb_device_get_halted(usb_device_c* device, int ep) {
  return ((ep & 0x7F) < USB_MAX_ENDPOINTS) ? device->endpoint_info[(ep & 0x7F)].halted : 0;
}

int usb_set_usb_string(uint8_t *buf, const char *str)
{
  uint8_t *q = buf;
  size_t len = strlen(str);
  if (len > 32) {
    *q = 0;
    return 0;
  }
  *q++ = (uint8_t)(2 * len + 2);
  *q++ = 3;
  for(size_t i = 0; i < len; i++) {
    *q++ = str[i];
    *q++ = 0;
  }
  return (int)(q - buf);
}

int usb_device_handle_control_common(usb_device_c* device, int request, int value, int index, int length, uint8_t *data)
{
  // if this function returns -1, the device's handle_control() function will have a chance to execute the request
  int ret = -1;

  switch (request) {
    case DeviceOutRequest | USB_REQ_SET_ADDRESS:
      BX_DEBUG(("USB_REQ_SET_ADDRESS:"));
      // with DeviceOutRequest, The wIndex and wLength fields must be zero
      if ((index != 0) || (length != 0)) {
        BX_ERROR(("USB_REQ_SET_ADDRESS: This type of request requires the wIndex and wLength fields to be zero."));
      }
      device->state = USB_STATE_ADDRESS;
      device->addr = value;
      ret = 0;
      break;
    case DeviceRequest | USB_REQ_GET_DESCRIPTOR:
      // the wIndex is the Language Idevice-> We only support the standard, so this field must be zero or 0x0409
      if ((index != 0) && (index != 0x0409)) {
        BX_ERROR(("USB_REQ_GET_DESCRIPTOR: This type of request requires the wIndex field to be zero or 0x0409. (0x%04X)", index));
      }
      // the standard Get Descriptor request only supports the Device, Config, and String descriptors.
      // if the quest requests the Interface or Endpoint, this is in error
      switch (value >> 8) {
        case USB_DT_DEVICE:
          BX_DEBUG(("USB_REQ_GET_DESCRIPTOR: Device"));
          memcpy(data, device->dev_descriptor, device->device_desc_size);
          ret = device->device_desc_size;
          break;
        case USB_DT_CONFIG:
          BX_DEBUG(("USB_REQ_GET_DESCRIPTOR: Config"));
          memcpy(data, device->config_descriptor, device->config_desc_size);
          ret = device->config_desc_size;
          break;
        case USB_DT_STRING:
          BX_DEBUG(("USB_REQ_GET_DESCRIPTOR: String"));
          switch(value & 0xff) {
            case 0:
              // language IDs
              data[0] = 4;
              data[1] = 3;
              data[2] = 0x09;
              data[3] = 0x04;
              ret = 4;
              break;
            case 1:
              // vendor description
              ret = usb_set_usb_string(data, device->vendor_desc);
              break;
            case 2:
              // product description
              ret = usb_set_usb_string(data, device->product_desc);
              break;
            case 3:
              // serial number
              ret = usb_set_usb_string(data, device->serial_num);
              break;
          }
          break;
        case USB_DT_INTERFACE:
          BX_ERROR(("USB_DT_INTERFACE: You cannot use the Get Descriptor request to retrieve the Interface descriptor(s)."));
          break;
        case USB_DT_ENDPOINT:
          BX_ERROR(("USB_DT_ENDPOINT: You cannot use the Get Descriptor request to retrieve the Endpoint descriptor(s)."));
          break;
      }
      break;
    case DeviceRequest | USB_REQ_GET_STATUS:
      BX_DEBUG(("USB_REQ_GET_STATUS:"));
      // with this request, the wIndex field must be zero
      if (index != 0) {
        BX_ERROR(("USB_REQ_GET_STATUS: This type of request requires the wIndex field to be zero."));
      }
      // standard request
      if (value == 0) {
        data[0] = 0x00;
        if (device->config_descriptor[7] & 0x40) {
          data[0] |= (1 << USB_DEVICE_SELF_POWERED);
        }
        if (device->remote_wakeup) {
          data[0] |= (1 << USB_DEVICE_REMOTE_WAKEUP);
        }
        data[1] = 0x00;
        ret = 2;
      
      // PTM Status
      } else if (value == 1) {
        BX_ERROR(("USB_REQ_GET_STATUS: Unsupported PTM status requestedevice->"));
        //ret = 4;

      // else reserved
      } else {
        BX_ERROR(("USB_REQ_GET_STATUS: Unknown type of status requested: %d", value));
      }
      break;
    case DeviceRequest | USB_REQ_GET_CONFIGURATION:
      BX_DEBUG(("USB_REQ_GET_CONFIGURATION:"));
      // with this request, the wValue and wIndex fields must be zero, and wLength must be 1
      if ((value != 0) || (index != 0) || (length != 1)) {
        BX_ERROR(("USB_REQ_GET_CONFIGURATION: This type of request requires the wValue and wIndex fields to be zero, and the wLength field to be 1."));
      }
      data[0] = device->config;
      ret = 1;
      break;
    case DeviceOutRequest | USB_REQ_SET_CONFIGURATION:
      BX_DEBUG(("USB_REQ_SET_CONFIGURATION: value=%d", value));
      // with DeviceOutRequest, The wIndex and wLength fields must be zero
      if ((index != 0) || (length != 0)) {
        BX_ERROR(("USB_REQ_SET_CONFIGURATION: This type of request requires the wIndex and wLength fields to be zero."));
      }
      // check to make sure the requested value is within range
      // (our one and only configuration, *or* zero indicating to de-configure the device)
      if ((value > 0) && (value != device->config_descriptor[5])) {
        BX_ERROR(("USB_REQ_SET_CONFIGURATION: Trying to set configuration value to non-existing configuration: %d", value));
      }
      device->config = value;
      device->state = USB_STATE_CONFIGURED;
#if HANDLE_TOGGLE_CONTROL
      // we must also clear all of the EP toggle bits
      for (int i=0; i<USB_MAX_ENDPOINTS; i++)
        usb_device_set_toggle(device, i, 0);
#endif
      ret = 0;
      break;
    case DeviceOutRequest | USB_REQ_CLEAR_FEATURE:
      BX_DEBUG(("USB_REQ_CLEAR_FEATURE: (%d)", value));
      // with DeviceOutRequest, The wIndex and wLength fields must be zero
      if ((index != 0) || (length != 0)) {
        BX_ERROR(("USB_REQ_CLEAR_FEATURE: This type of request requires the wIndex and wLength fields to be zero."));
      }
      if (value == USB_DEVICE_REMOTE_WAKEUP) {
        device->remote_wakeup = 0;
        ret = 0;
      //} else {
      //  BX_ERROR(("USB_REQ_CLEAR_FEATURE: Unknown Clear Feature Request found: %d", value));
      }
      break;
    case DeviceOutRequest | USB_REQ_SET_FEATURE:
      // with DeviceOutRequest, The wLength field must be zero
      if (length != 0) {
        BX_ERROR(("USB_REQ_SET_FEATURE: This type of request requires the wLength field to be zero."));
      }
      if (value == USB_DEVICE_REMOTE_WAKEUP) {
        device->remote_wakeup = 1;
        ret = 0;
      //} else {
      //  BX_ERROR(("USB_DEVICE_REMOTE_WAKEUP: Unknown Set Feature Request found: %d", value));
      }
      break;
    case InterfaceRequest | USB_REQ_GET_INTERFACE:
      // Ben: TODO: If the device is not in the configured state, this request should stall
      BX_DEBUG(("USB_REQ_GET_INTERFACE:"));
      // with InterfaceRequest, the wValue field must be zero and wLength field must be 1
      if ((value != device->iface_alt) || (length != 1)) {
        BX_ERROR(("USB_REQ_GET_INTERFACE: This type of request requires the wValue field to be zero and wLength field to be one."));
      }
      // all our devices only have one interface, and that value must be zero
      // if we ever add a device that has more than one interface (a video cam ?), we will need to modify this
      if (index == device->iface_alt) {
        data[0] = device->alt_iface;
        ret = 1;
      }
      break;
    case InterfaceOutRequest | USB_REQ_SET_INTERFACE:
      // Ben: TODO: If the device is not in the configured state, this request should stall
      BX_DEBUG(("USB_REQ_SET_INTERFACE: value=%d", value));
      // with InterfaceRequest, the wIndex and wLength fields must be zero
      if ((index != device->iface_alt) || (length != 0)) {
        BX_ERROR(("USB_REQ_SET_INTERFACE: This type of request requires the wIndex and wLength fields to be zero."));
      }
      // all our devices only have one interface, and that value must be zero
      // if we ever add a device that has more than one interface (a video cam ?), we will need to modify this
      if ((index == device->iface_alt) && (value <= device->alt_iface_max)) {
        device->alt_iface = value;         // alternate interface
        device->handle_iface_change(device, value);  // let the device know we changed the interface number
        ret = 0;
      }
      break;
    case EndpointOutRequest | USB_REQ_CLEAR_FEATURE:
      BX_DEBUG(("EndpointOutRequest | USB_REQ_CLEAR_FEATURE: ep = %d", index & 0x7F));
      // Value == 0 == Endpoint Halt (the Guest wants to reset the endpoint)
      if (value == USB_ENDPOINT_HALT) {
        if ((index & 0x7F) < USB_MAX_ENDPOINTS) {
#if HANDLE_TOGGLE_CONTROL
          usb_device_set_toggle(device, index, 0);
#endif
          usb_device_set_halted(device, index, 0);
          ret = 0;
        } else {
          BX_ERROR(("EndpointOutRequest | USB_REQ_CLEAR_FEATURE: index > ep count: %d", index));
        }
      } else {
        BX_ERROR(("EndpointOutRequest | USB_REQ_CLEAR_FEATURE: Unknown Clear Feature Request found: %d", value));
      }
      break;
    case EndpointOutRequest | USB_REQ_SET_FEATURE:
      // with EndpointRequest, The wLength field must be zero
      if (length != 0) {
        BX_ERROR(("USB_REQ_SET_FEATURE: This type of request requires the wLength field to be zero."));
      }
      if (value == USB_ENDPOINT_HALT) {
        if ((index & 0x7F) < USB_MAX_ENDPOINTS) {
          usb_device_set_halted(device, index, 1);
          ret = 0;
        } else {
          BX_ERROR(("EndpointOutRequest | USB_REQ_SET_FEATURE: index > ep count: %d", index));
        }
      } else {
        BX_ERROR(("EndpointOutRequest | USB_REQ_SET_FEATURE: Unknown Set Feature Request found: %d", value));
      }
      break;
    // should not have a default: here, so allowing the device's handle_control() to try to execute the request
  }
  
  // if 'ret' still equals -1, then the device's handle_control() has a chance to execute it
  return ret;
}

int usb_device_handle_packet(usb_device_c* device, USBPacket *p)
{
  int mps = 8;
  int ret = 0;
  int len = p->len;
  uint8_t *data = p->data;

  switch (p->pid) {
    case USB_MSG_ATTACH:
      device->state = USB_STATE_ATTACHED;
      break;
      
    case USB_MSG_DETACH:
      device->state = USB_STATE_NOTATTACHED;
      break;
      
    case USB_MSG_RESET:
      device->remote_wakeup = 0;
      device->addr = 0;
      device->state = USB_STATE_DEFAULT;
#if HANDLE_TOGGLE_CONTROL
      for (int i=0; i<USB_MAX_ENDPOINTS; i++)
        device->endpoint_info[i].toggle = 0;
#endif
      device->setup_state = SETUP_STATE_IDLE;
      device->handle_reset(device);
      break;
      
    case USB_TOKEN_SETUP:
      if (device->state < USB_STATE_DEFAULT || p->devaddr != device->addr)
        return USB_RET_NODEV;
      if (device->setup_state != SETUP_STATE_IDLE)
        BX_ERROR(("SETUP packet found while expecting Status Packet?"));
      if (len != 8) {
        BX_ERROR(("Packet length must be 8."));
        goto fail;
      }
      // check the speed indicator from the TD
      if (device->speed != p->speed) {
        BX_ERROR(("SETUP: Packet Speed indicator doesn't match Device Speed indicator. %d != %d", p->speed, device->speed));
        goto fail;
      }
#if HANDLE_TOGGLE_CONTROL
      // manage our toggle bit
      if (p->toggle > -1) {
        if (p->toggle != 0) {
          BX_ERROR(("SETUP: Packet Toggle indicator doesn't match Device Toggle indicator. %d != 0", p->toggle));
          goto fail;
        }
        usb_device_set_toggle(device, USB_CONTROL_EP, 1); // after a SETUP packet, the toggle bit is set for the next packet
      }
#endif
      device->stall = 0;
      memcpy(device->setup_buf, data, 8);
      device->setup_len = (device->setup_buf[7] << 8) | device->setup_buf[6];
      device->setup_index = 0;

      // check to see if the very first packet after an initial reset is an IN *and* 
      //  is for the first mps-bytes of the Device Descriptor. If not, give a warning.
      // Since we don't know the max packet size yet, we (the controller) assumes the following:
      switch (device->speed) {
        case USB_SPEED_LOW:
          mps = 8;
          break;
        case USB_SPEED_FULL:
          mps = 64;
          break;
      }
      if (!device->first8 && ((device->setup_len != mps) || (device->setup_buf[0] != (USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE)) || 
                        (device->setup_buf[1] != USB_REQ_GET_DESCRIPTOR) || (device->setup_buf[3] != USB_DT_DEVICE))) {
        BX_INFO(("The first request after an initial reset must be the Device Descriptor request with a length equal to the max packet size allowed for device."));
        BX_INFO((" For low-speed devices, use an 8 byte length. For full- and high-speed, use a 64 byte length. For super-speed, use a 512 byte length."));
        BX_INFO(("The device expects a reset, MPS-bytes of the descriptor, another reset, set address request, and then the full 18 byte descriptor."));
        BX_INFO(("Some devices (more than you think) will not initialize correctly without this (non-USB compliant) sequence."));
      }
      device->first8 = 1;
      
      if (device->setup_buf[0] & USB_DIR_IN) {
        ret = device->handle_control(device, (device->setup_buf[0] << 8) | device->setup_buf[1],
                             (device->setup_buf[3] << 8) | device->setup_buf[2],
                             (device->setup_buf[5] << 8) | device->setup_buf[4],
                              device->setup_len, device->data_buf);
        if (ret < 0)
          return ret;
        if (ret < device->setup_len)
          device->setup_len = ret;
        device->setup_state = SETUP_STATE_DATA_IN;
      } else
        device->setup_state = SETUP_STATE_DATA_OUT;
      break;
      
    case USB_TOKEN_IN:
      if (device->state < USB_STATE_DEFAULT || p->devaddr != device->addr)
        return USB_RET_NODEV;
      if (device->stall) goto fail;
      if (device->speed != p->speed) {
        BX_ERROR(("IN: Packet Speed indicator doesn't match Device Speed indicator. %d != %d", p->speed, device->speed));
        goto fail;
      }
      switch (p->devep) {
        case USB_CONTROL_EP:
          switch (device->setup_state) {
            // we are doing a data in packet
            case SETUP_STATE_DATA_IN:
              ret = device->setup_len - device->setup_index;
              if (ret > len)
                ret = len;

              // check that the length is <= the max packet size of the device
              if (ret > usb_device_get_mps(device, USB_CONTROL_EP)) {
                BX_ERROR(("EP%d transfer length (%d) is greater than Max Packet Size (%d).", p->devep, p->len, usb_device_get_mps(device, (USB_CONTROL_EP))));
              }
#if HANDLE_TOGGLE_CONTROL
              // manage our toggle bit
              if (p->toggle > -1) {
                if (p->toggle != usb_device_get_toggle(device, USB_CONTROL_EP)) {
                  BX_ERROR(("CONTROL IN: Packet Toggle indicator doesn't match Device Toggle indicator. %d != %d", p->toggle, usb_device_get_toggle(device, USB_CONTROL_EP)));
                  goto fail;
                }
                usb_device_set_toggle(device, USB_CONTROL_EP, usb_device_get_toggle(device, USB_CONTROL_EP) ^ 1); // toggle the bit
              }
#endif
              if (ret > 0) {
                memcpy(data, device->data_buf + device->setup_index, ret);
                device->setup_index += ret;
              }
              break;
              
            // we were doing data out packets, now we are expecting an IN STATUS packet
            case SETUP_STATE_DATA_OUT:
#if HANDLE_TOGGLE_CONTROL
              // manage our toggle bit
              if ((p->toggle > -1) && (p->toggle != 1)) {
                BX_ERROR(("STATUS: Packet Toggle indicator doesn't match Device Toggle indicator. %d != 1", p->toggle));
                goto fail;
              }
#endif
              device->setup_state = SETUP_STATE_IDLE;
              // found status packet from a Control OUT transfer, so process the transfer
              ret = device->handle_control(device, (device->setup_buf[0] << 8) | device->setup_buf[1],
                                   (device->setup_buf[3] << 8) | device->setup_buf[2],
                                   (device->setup_buf[5] << 8) | device->setup_buf[4],
                                    device->setup_len, device->data_buf);
              // if the transfer was successful, return 0 for the STATUS packet,
              //  else return the STALL, or other status.
              if (ret > 0)
                ret = 0;
              break;
              
            default:
              BX_ERROR(("Unknown Data state while finding Control In Packet. (state = %i)", device->setup_state));
              goto fail;
          }
          break;
          
        // a non-control endpoint found
        default:
#if HANDLE_TOGGLE_CONTROL
          // manage our toggle bit
          if (p->toggle > -1) {
            if (p->toggle != usb_device_get_toggle(device, p->devep)) {
              BX_ERROR(("DATA IN EP%d: Packet Toggle indicator doesn't match Device Toggle indicator. %d != %d", p->devep, p->toggle, usb_device_get_toggle(device, p->devep)));
              goto fail;
            }
            usb_device_set_toggle(device, p->devep, usb_device_get_toggle(device, p->devep) ^ 1); // toggle the bit
          }
#endif
          ret = device->handle_data(device, p);
          break;
      }
      break;
      
    case USB_TOKEN_OUT:
      if (device->state < USB_STATE_DEFAULT || p->devaddr != device->addr)
        return USB_RET_NODEV;
      if (device->stall) goto fail;
      if (device->speed != p->speed) {
        BX_ERROR(("OUT: Packet Speed indicator doesn't match Device Speed indicator. %d != %d", p->speed, device->speed));
        goto fail;
      }
      switch (p->devep) {
        case USB_CONTROL_EP:
          switch(device->setup_state) {
            case SETUP_STATE_DATA_OUT:
              ret = device->setup_len - device->setup_index;
              if (ret > len)
                ret = len;
              
              // check that the length is <= the max packet size of the device
              if (ret > usb_device_get_mps(device, USB_CONTROL_EP)) {
                BX_ERROR(("EP%d transfer length (%d) is greater than Max Packet Size (%d).", p->devep, p->len, usb_device_get_mps(device, USB_CONTROL_EP)));
              }
#if HANDLE_TOGGLE_CONTROL
              // manage our toggle bit
              if (p->toggle > -1) {
                if (p->toggle != usb_device_get_toggle(device, USB_CONTROL_EP)) {
                  BX_ERROR(("CONTROL OUT: Packet Toggle indicator doesn't match Device Toggle indicator. %d != %d", p->toggle, usb_device_get_toggle(device, USB_CONTROL_EP)));
                  goto fail;
                }
                usb_device_set_toggle(device, USB_CONTROL_EP, usb_device_get_toggle(device, USB_CONTROL_EP) ^ 1); // toggle the bit
              }
#endif
              if (ret > 0) {
                memcpy(device->data_buf + device->setup_index, data, ret);
                device->setup_index += ret;
              }
              break;

            // we were doing data in packets, now we are expecting an OUT STATUS packet
            case SETUP_STATE_DATA_IN:
#if HANDLE_TOGGLE_CONTROL
              // manage our toggle bit
              if ((p->toggle > -1) && (p->toggle != 1)) {
                BX_ERROR(("STATUS: Packet Toggle indicator doesn't match Device Toggle indicator. %d != 1", p->toggle));
                goto fail;
              }
#endif
              device->setup_state = SETUP_STATE_IDLE;
              ret = 0;
              break;
              
            default:
              BX_ERROR(("Unknown Data state while finding Control Out Packet. (state = %i)", device->setup_state));
              goto fail;
          }
          break;
        default:
#if HANDLE_TOGGLE_CONTROL
          // manage our toggle bit
          if (p->toggle > -1) {
            if (p->toggle != usb_device_get_toggle(device, p->devep)) {
              BX_ERROR(("DATA OUT EP%d: Packet Toggle indicator doesn't match Device Toggle indicator. %d != %d", p->devep, p->toggle, usb_device_get_toggle(device, p->devep)));
              goto fail;
            }
            usb_device_set_toggle(device, p->devep, usb_device_get_toggle(device, p->devep) ^ 1);  // toggle the bit
          }
#endif
          ret = device->handle_data(device, p);
          break;
      }
      break;
    default:
    fail:
      device->stall = 1;
      ret = USB_RET_STALL;
      break;
  }
  //pclog("ret = %d\n", ret);
  return ret;
}

void usb_device_send_msg(usb_device_c* device, int msg)
{
  USBPacket p;
  memset(&p, 0, sizeof(p));
  p.pid = msg;
  device->handle_packet(device, &p);
}
