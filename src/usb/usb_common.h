#ifndef USB_COMMON_H
#define USB_COMMON_H

#include <stdbool.h>
#include <stdint.h>

// for the Packet Capture code to work, these four must remain as is
enum {
  USB_TRANS_TYPE_ISO     = 0,
  USB_TRANS_TYPE_INT     = 1,
  USB_TRANS_TYPE_CONTROL = 2,
  USB_TRANS_TYPE_BULK    = 3
};

#define USB_CONTROL_EP     0

#define USB_TOKEN_IN    0x69
#define USB_TOKEN_OUT   0xE1
#define USB_TOKEN_SETUP 0x2D

#define USB_MSG_ATTACH   0x100
#define USB_MSG_DETACH   0x101
#define USB_MSG_RESET    0x102

#define USB_RET_NODEV   (-1)
#define USB_RET_NAK     (-2)
#define USB_RET_STALL   (-3)
#define USB_RET_BABBLE  (-4)
#define USB_RET_IOERROR (-5)
#define USB_RET_ASYNC   (-6)

enum {
  USB_SPEED_LOW   = 0,
  USB_SPEED_FULL  = 1,
};

enum {
  USB_STATE_NOTATTACHED = 0,
  USB_STATE_ATTACHED    = 1,
//USB_STATE_POWERED     = 2,
  USB_STATE_DEFAULT     = 3,
  USB_STATE_ADDRESS     = 4,
  USB_STATE_CONFIGURED  = 5,
  USB_STATE_SUSPENDED   = 6
};

/* Refer to the USB standards for information on these. */

#define USB_DIR_OUT  0
#define USB_DIR_IN   0x80

#define USB_TYPE_MASK            (0x03 << 5)
#define USB_TYPE_STANDARD        (0x00 << 5)
#define USB_TYPE_CLASS           (0x01 << 5)
#define USB_TYPE_VENDOR          (0x02 << 5)
#define USB_TYPE_RESERVED        (0x03 << 5)

#define USB_RECIP_MASK            0x1f
#define USB_RECIP_DEVICE          0x00
#define USB_RECIP_INTERFACE       0x01
#define USB_RECIP_ENDPOINT        0x02
#define USB_RECIP_OTHER           0x03

#define DeviceRequest ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_DEVICE) << 8)     // Host to device / Standard Type / Recipient:Device
#define DeviceOutRequest ((USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_DEVICE) << 8) // Device to host / Standard Type / Recipient:Device
#define DeviceClassInRequest \
   ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_DEVICE) << 8)
#define InterfaceRequest \
   ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8)
#define InterfaceInClassRequest \
   ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
#define OtherInClassRequest \
   ((USB_DIR_IN | USB_TYPE_CLASS | USB_RECIP_OTHER) << 8)
#define InterfaceOutRequest \
   ((USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_INTERFACE) << 8)
#define DeviceOutClassRequest \
   ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_DEVICE) << 8)
#define InterfaceOutClassRequest \
   ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_INTERFACE) << 8)
#define OtherOutClassRequest \
   ((USB_DIR_OUT | USB_TYPE_CLASS | USB_RECIP_OTHER) << 8)
#define EndpointRequest ((USB_DIR_IN | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT) << 8)
#define EndpointOutRequest \
   ((USB_DIR_OUT | USB_TYPE_STANDARD | USB_RECIP_ENDPOINT) << 8)

#define USB_REQ_GET_STATUS        0x00
#define USB_REQ_CLEAR_FEATURE     0x01
#define USB_REQ_SET_FEATURE       0x03
#define USB_REQ_SET_ADDRESS       0x05
#define USB_REQ_GET_DESCRIPTOR    0x06
#define USB_REQ_SET_DESCRIPTOR    0x07
#define USB_REQ_GET_CONFIGURATION 0x08
#define USB_REQ_SET_CONFIGURATION 0x09
#define USB_REQ_GET_INTERFACE     0x0A
#define USB_REQ_SET_INTERFACE     0x0B
#define USB_REQ_SYNCH_FRAME       0x0C
#define USB_REQ_SET_SEL           0x30
#define USB_REQ_SET_ISO_DELAY     0x31

#define USB_ENDPOINT_HALT          0
#define USB_DEVICE_SELF_POWERED    0
#define USB_DEVICE_REMOTE_WAKEUP   1
#define USB_DEVICE_U1_ENABLE      48
#define USB_DEVICE_U2_ENABLE      49

// USB 1.1
#define USB_DT_DEVICE              0x01
#define USB_DT_CONFIG              0x02
#define USB_DT_STRING              0x03
#define USB_DT_INTERFACE           0x04
#define USB_DT_ENDPOINT            0x05
// USB 2.0
#define USB_DT_DEVICE_QUALIFIER         0x06
#define USB_DT_OTHER_SPEED_CONFIG       0x07
#define USB_DT_INTERFACE_POWER          0x08


typedef void (*usb_sof_timer)(void* priv);
typedef struct USBPacket USBPacket;

// packet events
/* Signals wakeup to host. */
#define USB_EVENT_WAKEUP        0
/* Signals packet completion to host. */
#define USB_EVENT_ASYNC         1
// controller events
#define USB_EVENT_DEFAULT_SPEED  10
#define USB_EVENT_CHECK_SPEED    11

// set this to 1 to monitor the TD's toggle bit
// setting to 0 will speed up the emualtion slightly

/* Toggling monitoring disabled for now; this causes problems with UHCI emulation under Windows 98 SE. */
#define HANDLE_TOGGLE_CONTROL 0

#define USB_MAX_ENDPOINTS   5   // we currently don't use more than 5 endpoints (ep0, ep1, ep2, ep3, and ep4)

typedef int USBCallback(int event, void *ptr, void *dev, int port);

struct usb_device_c;
typedef struct usb_device_c usb_device_c;

struct USBPacket {
  int pid;
  /* Target USB device address. */
  uint8_t devaddr;
  /* Target USB device endpoint. */
  uint8_t devep;
  uint8_t speed;           // packet's speed definition

  int   toggle;          // packet's toggle bit (0, 1, or -1 for xHCI)
//#endif
  /* Packet data. */
  uint8_t *data;
  /* Packet length. */
  int len;
  /* Completion/async callback (Set by host controller interface). */
  USBCallback *complete_cb;
  /* Opaque pointer to host controller interface-specific data */
  void *complete_dev;
  /* Pointer to target USB device. */
  usb_device_c *dev;
  int strm_pid;         // stream primary id
};

/* Asynchronous USB packet container. */
typedef struct USBAsync {
  /* USB packet. */
  USBPacket packet;
  /* Host-side packet data address. */
  uint64_t    td_addr;
  /* Packet handled. */
  bool done;
  /* Slot endpoint. */
  uint16_t  slot_ep;

  struct USBAsync *next;
} USBAsync;

// Items about the endpoint gathered from various places
// These values are set at init() time, this is so we
//  don't have to parse the descriptors at runtime.
typedef struct USBEndPoint {
  int  max_packet_size;  // endpoint max packet size
  int  max_burst_size;   // endpoint max burst size (super-speed endpoint companion only)
//#if HANDLE_TOGGLE_CONTROL
  int  toggle;           // the current toggle for the endpoint (0, 1, or -1 for xHCI)
//#endif
  bool halted;           // is the current ep halted?
} USBEndPoint;

struct usb_device_c {
    /* Type of device (specific to device implementation.)*/
    uint8_t type;
    /* Is the device connected? */
    bool connected;
    /* Minimum Speed. */
    int minspeed;  // must be no more than FULL speed for *any* device
    /* Maximum speed. */
    int maxspeed;
    /* Current Speed. */
    int speed;
    /* Address of device. */
    uint8_t addr;
    /* Current configuration of device. */
    uint8_t config;
    /* Currently selected alternate interface. */
    uint8_t alt_iface;
    /* Maximum alternate interface usable. */
    uint8_t alt_iface_max;
    /* Interface with alternate settings. */
    uint8_t iface_alt; // This naming is terrible...
    /* Name of device. */
    char devname[32];
    /* Endpoints. */
    USBEndPoint endpoint_info[USB_MAX_ENDPOINTS];

    bool first8;
    /* Pointer to device and configuration descriptors. */
    const uint8_t *dev_descriptor;
    const uint8_t *config_descriptor;
    /* Size of device and configuration descriptors. */
    int device_desc_size;
    int config_desc_size;
    /* String description of vendor. */
    const char *vendor_desc;
    /* String description of product. */
    const char *product_desc;
    /* Serial number. */
    const char *serial_num;

    /* Setup packets' state information. */
    int state;
    uint8_t setup_buf[64];
    uint8_t data_buf[1280];
    int remote_wakeup;
    int setup_state;
    int setup_len;
    int setup_index;
    /* Is device stalled (errored out)? */
    bool stall;
    /* Is device able to operate asynchronously? */
    bool async_mode;
    /* Event structure. */
    struct {
      USBCallback *cb;
      void *dev;
      int port;
    } event;

    /* Callbacks. */
    /* Cancels in-progress USB packets. No-op by default. */
    void (*cancel_packet)(usb_device_c* device, USBPacket* p);
    /* Handles USB packets. Returns 0 on success, or a negative value matching one of USB_RET_* defines on failure. */
    int (*handle_packet)(usb_device_c* device, USBPacket* p);
    /* Resets the USB device. */
    void (*handle_reset)(usb_device_c* device);
    /* Handles control packets. Returns 0 on success, or a negative value matching one of USB_RET_* defines on failure. */
    int (*handle_control)(usb_device_c* device, int request, int value, int index, int length, uint8_t *data);
    /* Handles data packets. Returns 0 on success, or a negative value matching one of USB_RET_* defines on failure. */
    int (*handle_data)(usb_device_c* device, USBPacket *p);
    /* Notifications on interface changes. */
    void (*handle_iface_change)(usb_device_c* device, int iface);
    /* Initializes the device. Returns true on success. */
    bool (*init)(usb_device_c *init);
    /* Called after each SOF from the host. */
    usb_sof_timer sof_callback;

    /* Opaque data. */
    void* priv;
};

/* Default inits the device. */
void usb_device_create(usb_device_c* device);
/* Allocate USB packet data of 'size' bytes. */
void usb_packet_init(USBPacket *p, int size);
/* Deallocates USB packet data. */
void usb_packet_cleanup(USBPacket *p);
/* Simply assigns 'dev' to the packet. */
void usb_defer_packet(USBPacket *p, usb_device_c *dev);
/* Cancels an in-progress packet. */
void usb_cancel_packet(USBPacket *p);
/* Signals completion of processing of packet. */
void usb_packet_complete(USBPacket *p);
/* Creates async packet container with host-side descriptor address with maximum length of `maxlen` bytes and assigns it to `base` list. */
USBAsync* create_async_packet(USBAsync **base, uint64_t addr, int maxlen);
/* Removes async packet container from `base` list. */
void remove_async_packet(USBAsync **base, USBAsync *p);
/* Finds in-progress packets with host-side descriptor address equal to `addr`. */
USBAsync* find_async_packet(USBAsync **base, uint64_t addr);

/* For host controller interface implementations. Sets message/event handler the device can use to signal the host. */
void usb_device_set_event_handler(usb_device_c* device, void *dev, USBCallback *cb, int port);
/* Sends message (USB_MSG_*) to device. */
void usb_device_send_msg(usb_device_c* device, int msg);
/* Called by USB device implementations to handle control messages targeted at default endpoint. */
int usb_device_handle_control_common(usb_device_c* device, int request, int value, int index, int length, uint8_t *data);
/* Is the endpoint of device halted? */
bool usb_device_get_halted(usb_device_c* device, int ep);
/* Processes 'str' to produce a string suitable for reporting back to the host. */
int usb_set_usb_string(uint8_t *buf, const char *str);
/* Get maximum packet size the device can handle for an endpoint. */
int usb_device_get_mps(usb_device_c* device, const int ep);
/* Events to send to the host controller. Send USB_EVENT_WAKEUP to wake up the host. */
extern int usb_device_hc_event(usb_device_c* host, int event, usb_device_c *device);

void usb_device_set_toggle(usb_device_c* device, int ep, int toggle);
int usb_device_get_toggle(usb_device_c* device, int ep);

#endif
