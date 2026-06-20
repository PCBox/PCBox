/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the Distributed DMA emulation.
 *
 * Authors: Miran Grca, <mgrca8@gmail.com>
 *
 *          Copyright 2020 Miran Grca.
 */

#ifndef USB_H
#define USB_H

#ifdef __cplusplus
extern "C" {
#endif

struct usb_device_c;
typedef struct usb_device_c usb_device_c;

typedef struct usb_params_t {
    /* Pointer to PCI device slot. */
    uint8_t* pci_dev;
    /* PCI configuration space array. */
    uint8_t* pci_conf;

    void* priv; /* The implementation. */
    /* Raises SMI with also setting implementation-specific bits. */
    void (*do_smi_raise)(void* priv);
    /* Raises SMI with also setting implementation-specific bits (on OwnershipChangeRequest). */
    void (*do_smi_ocr_raise)(void* priv);
    /* Asserts PCI interrupt. */
    void (*do_pci_irq)(void* priv, int level);

    uint8_t* test_reg_enable;
} usb_params_t;

typedef struct usb_t {
    uint8_t       uhci_io[32];
    uint8_t       ohci_mmio[4096];
    uint16_t      uhci_io_base;
    int           uhci_enable;
    int           ohci_enable;
    uint32_t      ohci_mem_base;
    mem_mapping_t ohci_mmio_mapping;

    void* usb_uhci_priv;
    void* usb_ohci_priv;
} usb_t;

typedef struct usb_port_t {
    uint8_t number;
    void *priv; /* host controller interface implementation-specific data. */

    int (*is_free)(struct usb_port_t* port);
    int (*connect)(struct usb_port_t* port, usb_device_c* device);
} usb_port_t;

/* Global variables. */
extern const device_t usb_device, usb_uhci_device;

/* Functions. */
extern void uhci_update_io_mapping(usb_t *dev, uint8_t base_l, uint8_t base_h, int enable);
extern void ohci_update_mem_mapping(usb_t *dev, uint8_t base1, uint8_t base2, uint8_t base3, int enable);

extern void uhci_register_usb(usb_t *dev);
extern void ohci_register_usb(usb_t *dev);

/* Returns NULL if none found, else pointer to port. */
extern usb_port_t* usb_search_for_ports(void);

/* Registers port. */
extern void usb_register_port(uint8_t number, void *priv, int (*is_free)(struct usb_port_t* port), int (*connect)(struct usb_port_t* port, usb_device_c* device));

#ifdef __cplusplus
}
#endif

#endif /*USB_H*/
