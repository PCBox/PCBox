/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          USB UHCI host controller emulation, ported from Bochs.
 *
 * Authors: Cacodemon345
 *          Benjamin D Lunt (fys [at] fysnet [dot] net)
 *          The Bochs Project
 * 
 *          Copyright 2009-2023 Benjamin D Lunt (fys [at] fysnet [dot] net)
 *          Copyright 2009-2023 The Bochs Project
 *          Copyright 2024-2025 Cacodemon345
 */

#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
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
#include <86box/plat_unused.h>
#include <86box/timer.h>

#include "usb_common.h"

#define USB_UHCI_PORTS               2

#define STATUS2_IOC                  (1 << 0)
#define STATUS2_SPD                  (1 << 1)

#define USB_UHCI_QUEUE_STACK_SIZE    256
#define USB_UHCI_LOOP_COUNT          256

#define USB_UHCI_IS_LINK_VALID(item) ((item & 1) == 0) // return TRUE if valid link address
#define USB_UHCI_IS_LINK_QUEUE(item) ((item & 2) == 2) // return TRUE if link is a queue pointer

// the standard max bandwidth (bytes per frame) for the UHCI is 1280 bytes
#define USB_UHCI_STD_MAX_BANDWIDTH 1280

struct USB_UHCI_QUEUE_STACK {
    int    queue_cnt;
    uint32_t queue_stack[USB_UHCI_QUEUE_STACK_SIZE];
};

typedef struct {
    int timer_index;

    // Registers
    // Base + 0x00  Command register
    // Base + 0x02  Status register
    // Base + 0x04  Interrupt Enable register
    // Base + 0x06  Frame Number register
    // Base + 0x08  Frame Base Register (32-bit)
    // Base + 0x0C  Start of Frame Modify register
    // Base + 0x0D
    // Base + 0x0E
    // Base + 0x0F
    // Base + 0x10  Eight(?) 16-bit ports (one for each port on hub)

    // Bit reps of registers above
    // Command Register
    //  Bits 15-8 are reserved
    //  Bit 7 = Maximum packet size
    //  Bit 6 = Host Controller has been configured (set by software)
    //  Bit 5 = software debug mode
    //  Bit 4 = force global resume
    //  Bit 3 = enter global suspend mode
    //  Bit 2 = global reset
    //  Bit 1 = host controller reset
    //  Bit 0 = run/stop schedule
    struct {
        bool max_packet_size; //(bit 7) 0 = 32 bytes, 1 = 64 bytes
        bool configured;      //(bit 6)
        bool debug;           //(bit 5)
        bool resume;          //(bit 4)
        bool suspend;         //(bit 3)
        bool reset;           //(bit 2)
        bool host_reset;      //(bit 1)
        bool schedule;        //(bit 0) 0 = Stop, 1 = Run
    } usb_command;

    // Status Register
    //  Bits 15-6 are reserved
    //  Bit 5 = Host controller halted
    //  Bit 4 = Host controller process error
    //  Bit 3 = PCI Bus error
    //  Bit 2 = resume received
    //  Bit 1 = USB error interrupt
    //  Bit 0 = USB interrupt
    struct {
        bool  host_halted;     //(bit 5)
        bool  host_error;      //(bit 4)
        bool  pci_error;       //(bit 3)
        bool  resume;          //(bit 2)
        bool  error_interrupt; //(bit 1)
        bool  interrupt;       //(bit 0)
        uint8_t status2;         // bit 0 and 1 are used to generate the interrupt
    } usb_status;

    // Interrupt Enable Register
    //  Bits 15-4 are reserved
    //  Bit 3 = enable short packet interrupts
    //  Bit 2 = enable interrupt On Complete
    //  Bit 1 = enable resume
    //  Bit 0 = enable timeout/crc
    struct {
        bool short_packet; //(bit 3)
        bool on_complete;  //(bit 2)
        bool resume;       //(bit 1)
        bool timeout_crc;  //(bit 0)
    } usb_enable;

    // Frame Number Register
    //  Bits 15-11 are reserved
    //  Bits 10-0  Frame List Current Index/Frame Number
    struct {
        uint16_t frame_num;
    } usb_frame_num;

    // Frame List Base Address Register
    //  Bits 31-12  Base
    //  Bits 11-0   *must* be zeros when written to
    struct {
        uint32_t frame_base;
    } usb_frame_base;

    // Start of Frame Modify Register
    //  Bit    7 reserved
    //  Bits 6-0 SOF timing value (default 64)
    // SOF cycle time equals 11936+timing value
    struct {
        uint8_t sof_timing;
    } usb_sof;

    // Port Register (0-1)
    //  Bits 15-13  are reserved
    //  Bit     12  suspend port
    //  Bit     11  over current change (read/wc)
    //  Bit     10  over current (read-only) 0 = over_current, 1 = normal
    //  Bit      9  port in reset state
    //  Bit      8  low-speed device is attached (read-only)
    //  Bit      7  reserved
    //  Bit      6  resume detected (read-only)
    //  Bit      5  line-status D+ (read-only)
    //  Bit      4  line-status D- (read-only)
    //  Bit      3  port enabled/disable status has changed
    //               (write 1 to this bit to clear it)
    //  Bit      2  port is enabled
    //  Bit      1  connect status has changed
    //               (write 1 to this bit to clear it)
    //  Bit      0  current connect status (read-only)
    //  Can only write in WORD sizes (Read in byte sizes???)
    struct {
        // our data
        usb_device_c *device; // device connected to this port

        // bit reps of actual port
        bool suspend;
        bool over_current_change;
        bool over_current;
        bool reset;
        bool low_speed;
        bool resume;
        bool line_dminus;
        bool line_dplus;
        bool enable_changed;
        bool enabled;
        bool connect_changed;
        bool status;
    } usb_port[USB_UHCI_PORTS];

    int   max_bandwidth; // standard USB 1.1 is 1280 bytes (VTxxxxx models allowed a few less (1023))
    int   loop_reached;  // did we reach our bandwidth loop limit
    uint8_t* devfunc;
    uint8_t irq_state;
    bool  global_reset;

    USBAsync *packets;

    pc_timer_t timer;

    int uhci_enable;
    int uhci_io_base;

    uint8_t* pci_conf;

} bx_uhci_core_t;

#pragma pack(push, 1)
struct TD {
    uint32_t dword0;
    uint32_t dword1;
    uint32_t dword2;
    uint32_t dword3;
};

struct QUEUE {
    uint32_t horz;
    uint32_t vert;
};
#pragma pack(pop)

// these must match USB_SPEED_*
const char *usb_speed[4] = {
    "low",  // USB_SPEED_LOW   = 0
    "full", // USB_SPEED_FULL  = 1
    "high", // USB_SPEED_HIGH  = 2
    "super" // USB_SPEED_SUPER = 3
};

#ifdef ENABLE_UHCI_LOG
int uhci_do_log = ENABLE_UHCI_LOG;

static void
uhci_log(const char *fmt, ...)
{
    va_list ap;

    if (uhci_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define uhci_log(fmt, ...)
#endif

#define BX_ERROR(x) uhci_log x ; uhci_log ("\n")
#define BX_INFO(x)  uhci_log x ; uhci_log ("\n")
#define BX_DEBUG(x) uhci_log x ; uhci_log ("\n")
#define BX_PANIC(x) fatal x ; pclog ("\n")

bool usb_uhci_do_transfer(bx_uhci_core_t *hub, uint32_t address, struct TD *td);

void
usb_uhci_update_irq(bx_uhci_core_t *hub)
{
    bool level = 0;

    if (((hub->usb_status.status2 & STATUS2_IOC) && hub->usb_enable.on_complete) || ((hub->usb_status.status2 & STATUS2_SPD) && hub->usb_enable.short_packet) || (hub->usb_status.error_interrupt && hub->usb_enable.timeout_crc) || (hub->usb_status.resume && hub->usb_enable.resume) || hub->usb_status.pci_error || hub->usb_status.host_error) {
        level = 1;
    }

    if ((hub->pci_conf[0xc1] & (1 << 5)) && !(hub->pci_conf[0xc0] & (1 << 4)))
        pci_irq(PCI_IIRQ_BASE | PCI_INTD, 0, 0, level, &hub->irq_state);
    else
        pci_irq(PCI_IIRQ_BASE | PCI_INTD, 0, 0, 0, &hub->irq_state);
}

bool
usb_uhci_set_connect_status(bx_uhci_core_t *hub, uint8_t port, bool connected)
{
    usb_device_c *device = hub->usb_port[port].device;
    if (device != NULL) {
        if (connected) {
            BX_DEBUG(("port #%d: speed = %s", port + 1, usb_speed[device->speed]));
            switch (device->speed) {
                case USB_SPEED_LOW:
                    hub->usb_port[port].low_speed = 1;
                    break;
                case USB_SPEED_FULL:
                    hub->usb_port[port].low_speed = 0;
                    break;
                default:
                    BX_PANIC(("USB device returned invalid speed value"));
                    return 0;
            }
            if (hub->usb_port[port].low_speed) {
                hub->usb_port[port].line_dminus = 1; //  dminus=1 & dplus=0 = low speed  (at idle time)
                hub->usb_port[port].line_dplus  = 0; //  dminus=0 & dplus=1 = full speed (at idle time)
            } else {
                hub->usb_port[port].line_dminus = 0;
                hub->usb_port[port].line_dplus  = 1;
            }
            hub->usb_port[port].status          = 1;
            hub->usb_port[port].connect_changed = 1;

            // if in suspend state, signal resume
            if (hub->usb_command.suspend) {
                hub->usb_port[port].resume = 1;
                hub->usb_status.resume     = 1;
                if (hub->usb_enable.resume) {
                    hub->usb_status.interrupt = 1;
                }
                usb_uhci_update_irq(hub);
            }

            if (!device->connected) {
                if (!device->init(device)) {
                    BX_ERROR(("port #%d: connect failed", port + 1));
                    return 0;
                } else {
                    // BX_INFO(("port #%d: connect: %s", port+1, device->get_info()));
                }
            }
        } else {
            BX_INFO(("port #%d: device disconnect", port + 1));
            hub->usb_port[port].status          = 0;
            hub->usb_port[port].connect_changed = 1;
            if (hub->usb_port[port].enabled) {
                hub->usb_port[port].enable_changed = 1;
                hub->usb_port[port].enabled        = 0;
            }
            hub->usb_port[port].low_speed   = 0;
            hub->usb_port[port].line_dminus = 0;
            hub->usb_port[port].line_dplus  = 0;
        }
    }
    return connected;
}

int
usb_uhci_broadcast_packet(bx_uhci_core_t *hub, USBPacket *p)
{
    int ret = USB_RET_NODEV;
    for (int i = 0; i < USB_UHCI_PORTS && ret == USB_RET_NODEV; i++) {
        if ((hub->usb_port[i].device != NULL) && (hub->usb_port[i].enabled)) {
            ret = hub->usb_port[i].device->handle_packet(hub->usb_port[i].device, p);
        }
    }
    return ret;
}

static void
set_status(struct TD *td, bool stalled, bool data_buffer_error, bool babble,
           bool nak, bool crc_time_out, bool bitstuff_error, uint16_t act_len)
{
    // clear out the bits we can modify and/or want zero
    td->dword1 &= 0xDF00F800;

    // now set the bits according to the passed param's
    td->dword1 |= stalled ? (1 << 22) : 0;           // stalled
    td->dword1 |= data_buffer_error ? (1 << 21) : 0; // data buffer error
    td->dword1 |= babble ? (1 << 20) : 0;            // babble
    td->dword1 |= nak ? (1 << 19) : 0;               // nak
    td->dword1 |= crc_time_out ? (1 << 18) : 0;      // crc/timeout
    td->dword1 |= bitstuff_error ? (1 << 17) : 0;    // bitstuff error
    td->dword1 |= (act_len & 0x7FF);                 // actual length
    if (stalled || data_buffer_error || babble || crc_time_out || bitstuff_error)
        td->dword1 &= ~((1 << 28) | (1 << 27)); // clear the c_err field if there was an error
}

int
usb_uhci_add_queue(bx_uhci_core_t *hub, struct USB_UHCI_QUEUE_STACK *stack, const uint32_t addr)
{
    // check to see if this queue has been processed before
    for (int i = 0; i < stack->queue_cnt; i++) {
        if (stack->queue_stack[i] == addr)
            return 1;
    }

    // if the stack is full, we return TRUE anyway
    if (stack->queue_cnt == USB_UHCI_QUEUE_STACK_SIZE) {
        if (hub->loop_reached == 0) {
            BX_ERROR(("Ben: We reached our UHCI bandwidth loop limit. Probably should increase it."));
            hub->loop_reached = 1; // don't print it again
        }
        return 1;
    }

    // add the queue's address
    stack->queue_stack[stack->queue_cnt] = addr;
    stack->queue_cnt++;

    return 0;
}

void
usb_uhci_timer(void *priv)
{
    bx_uhci_core_t *hub = (bx_uhci_core_t*)priv;
    timer_on_auto(&hub->timer, 1000.0);
    // If the "global reset" bit was set by software
    if (hub->global_reset) {
        for (int i = 0; i < USB_UHCI_PORTS; i++) {
            hub->usb_port[i].enable_changed      = 0;
            hub->usb_port[i].connect_changed     = 0;
            hub->usb_port[i].enabled             = 0;
            hub->usb_port[i].line_dminus         = 0;
            hub->usb_port[i].line_dplus          = 0;
            hub->usb_port[i].low_speed           = 0;
            hub->usb_port[i].reset               = 0;
            hub->usb_port[i].resume              = 0;
            hub->usb_port[i].status              = 0;
            hub->usb_port[i].over_current        = 0;
            hub->usb_port[i].over_current_change = 0;
            hub->usb_port[i].suspend             = 0;
        }
        return;
    }

    // if the run bit is set, let's see if we can process a few TDs
    if (hub->usb_command.schedule) {
        // our stack of queues we have processed
        struct USB_UHCI_QUEUE_STACK queue_stack;
        int                         td_count        = 0; // count of TD's processed under a queue
        int                         count           = USB_UHCI_LOOP_COUNT;
        int                         bytes_processed = 0; // The UHCI (USB 1.1) allows up to 1280 bytes to be processed per frame.
        bool                        interrupt = 0, shortpacket = 0, stalled = 0;
        uint32_t                      item, queue_addr = 0;
        struct QUEUE                queue;
        struct TD                   td;
        uint32_t                      address = hub->usb_frame_base.frame_base + ((hub->usb_frame_num.frame_num & 0x3FF) * sizeof(uint32_t));

        // reset our queue stack to zero
        queue_stack.queue_cnt = 0;

        // read in the frame pointer
        dma_bm_read(address, (uint8_t *) &item, sizeof(uint32_t), 4);

        // BX_DEBUG(("Start of Frame %d", hub->usb_frame_num.frame_num & 0x3FF));

        // start the loop. we allow USB_UHCI_LOOP_COUNT queues to be processed
        while (count--) {
            // The UHCI (USB 1.1) only allows so many bytes to be transfered per frame.
            // Due to control/bulk reclamation, we need to catch this and stop transferring
            //  or this code will just keep processing TDs.
            if (bytes_processed >= hub->max_bandwidth) {
                BX_DEBUG(("Process Bandwidth Limits for this frame (%d with a limit of %d).", bytes_processed, hub->max_bandwidth));
                break;
            }

            if (!USB_UHCI_IS_LINK_VALID(item)) // the the T bit is set, we are done
                break;

            // is it a queue?
            if (USB_UHCI_IS_LINK_QUEUE(item)) {
                // add it to our current list of queues
                if (usb_uhci_add_queue(hub, &queue_stack, item & ~0xF)) {
                    // this queue has been processed before. Did we process
                    //  any TD's between the last time and now? If not, be done.
                    if (td_count == 0) {
                        break;
                    } else {
                        // reset the queue stack to start here
                        td_count              = 0;
                        queue_stack.queue_cnt = 0;
                        usb_uhci_add_queue(hub, &queue_stack, item & ~0xF);
                    }
                }

                // read in the queue
                dma_bm_read(item & ~0xF, (uint8_t *) &queue, sizeof(struct QUEUE), 4);

                // this massively populates the log file, so I keep it commented out
                // BX_DEBUG(("Queue at 0x%08X:  horz = 0x%08X, vert = 0x%08X", item & ~0xF, queue.horz, queue.vert));

                // if the vert pointer is valid, there are td's in it to process
                //  else only the head pointer may be valid
                if (!USB_UHCI_IS_LINK_VALID(queue.vert)) {
                    // no vertical elements to process
                    // (clear queue_addr to indicate we are not processing
                    //  elements of the vertical part of a queue)
                    queue_addr = 0;
                    item       = queue.horz;
                } else {
                    // there are vertical elements to process
                    // (save the address of the horz pointer in queue_addr
                    //  so that we may update the queue's vertical pointer
                    //  member with the successfully processed TD's link pointer)
                    queue_addr = item;
                    item       = queue.vert;
                }
                continue;
            }

            // else, we found a Transfer Descriptor
            address = item & ~0xF;
            dma_bm_read(address, (uint8_t *) &td, sizeof(struct TD), 4);
            const bool depthbreadth = (td.dword0 & 0x0004) ? 1 : 0; // 1 = depth first, 0 = breadth first
            const bool is_active    = (td.dword1 & (1 << 23)) > 0;
            bool       was_short = 0, was_stall = 0;
            if (td.dword1 & (1 << 24))
                interrupt = 1;
            if (is_active) { // is it an active TD
                const bool spd = (td.dword1 & (1 << 29)) > 0;
                if (usb_uhci_do_transfer(hub, address, &td)) {
                    // issue short packet?
                    const int r_actlen = (((td.dword1 & 0x7FF) + 1) & 0x7FF);
                    const int r_maxlen = (((td.dword2 >> 21) + 1) & 0x7FF);
                    BX_DEBUG((" r_actlen = %d r_maxlen = %d", r_actlen, r_maxlen));
                    if (((td.dword2 & 0xFF) == USB_TOKEN_IN) && (queue_addr != 0) && (r_actlen < r_maxlen) && ((td.dword1 & 0x00FF0000) == 0)) {
                        if (spd) {
                            BX_DEBUG(("Short Packet Detected"));
                            shortpacket = was_short = 1;
                            td.dword1 |= (1 << 29);
                        } else {
                            BX_DEBUG(("A Short Packet was detected, but the SPD bit in DWORD1 was clear"));
                        }
                    }
                    if (td.dword1 & (1 << 22))
                        stalled = was_stall = 1;

                    // write back the status to the TD
                    dma_bm_write(address + sizeof(uint32_t), (uint8_t *) &td.dword1, sizeof(uint32_t), 4);
                    // we processed another td within this queue line
                    td_count++;
                    bytes_processed += r_actlen;

                    // move to the next item
                    if (!was_stall) {
                        item = td.dword0;
                        if (queue_addr != 0) {
                            if (!was_short) {
                                // copy pointer for next queue item into vert queue head
                                dma_bm_write((queue_addr & ~0xF) + sizeof(uint32_t), (uint8_t *) &item, sizeof(uint32_t), 4);
                            }
                            // if breadth first, short packet, or last in the element list, move on to next queue item
                            if (!depthbreadth || !USB_UHCI_IS_LINK_VALID(item) || was_short) {
                                item       = queue.horz;
                                queue_addr = 0;
                            }
                        }
                        continue;
                    } else {
                        // this is where we would check the CC member.
                        // if it is non-zero, decrement it.
                        // if it is still non-zero, or was originally zero,
                        //  set the TD back to active so that we can try it again.
                        // *however*, since it failed in our emulation, it will fail again.
                        // so, fall through to below to move to the next queue
                    }
                }
            }

            // move to next item (no queues) or queue head (queues found)
            item = (queue_addr != 0) ? queue.horz : td.dword0;
        } // while loop

        // set the status register bit:0 to 1 if SPD is enabled
        // and if interrupts not masked via interrupt register, raise irq interrupt.
        if (shortpacket)
            hub->usb_status.status2 |= STATUS2_SPD;
        if (shortpacket && hub->usb_enable.short_packet) {
            BX_DEBUG((" [SPD] We want it to fire here (Frame: %04i)", hub->usb_frame_num.frame_num));
        }

        // if one of the TD's in this frame had the ioc bit set, we need to
        //   raise an interrupt, if interrupts are not masked via interrupt register.
        //   always set the status register if IOC.
        hub->usb_status.status2 |= interrupt ? STATUS2_IOC : 0;
        if (interrupt && hub->usb_enable.on_complete) {
            BX_DEBUG((" [IOC] We want it to fire here (Frame: %04i)", hub->usb_frame_num.frame_num));
        }

        hub->usb_status.error_interrupt |= stalled;
        if (stalled && hub->usb_enable.timeout_crc) {
            BX_DEBUG((" [stalled] We want it to fire here (Frame: %04i)", hub->usb_frame_num.frame_num));
        }

        // The Frame Number Register is incremented every 1ms
        hub->usb_frame_num.frame_num++;
        hub->usb_frame_num.frame_num &= (2048 - 1);

        // The status.interrupt bit should be set regardless of the enable bits if a IOC or SPD is found
        if (interrupt || shortpacket) {
            hub->usb_status.interrupt = 1;
        }

        // if we needed to fire an interrupt now, lets do it *after* we increment the frame_num register
        usb_uhci_update_irq(hub);
    } // end run schedule

    // if host turned off the schedule, set the halted bit in the status register
    // Note: Can not use an else from the if() above since the host can changed this bit
    //  while we are processing a frame.
    if (hub->usb_command.schedule == 0)
        hub->usb_status.host_halted = 1;

    /* Iterate through all ports and invoke SOF callbacks if those exist. */
    for (int i = 0; i < 2; i++) {
        if (hub->usb_port[i].device != NULL && hub->usb_port[i].device->sof_callback)
            hub->usb_port[i].device->sof_callback(hub->usb_port[i].device->priv);
    }

    // TODO ?:
    //  If in Global_Suspend mode and any of usb_port[i] bits 6,3, or 1 are set,
    //    we need to issue a Global_Resume (set the global resume bit).
    //    However, since we don't do anything, let's not.
}

int
usb_uhci_event_handler(int event, void *ptr, void *priv, int port)
{
    int             ret = 0;
    USBAsync       *p;
    bx_uhci_core_t *hub = (bx_uhci_core_t *) priv;

    switch (event) {
        // packet events start here
        case USB_EVENT_ASYNC:
            BX_DEBUG(("Async packet completion"));
            p = ptr;
            p->done = 1;
            break;
        case USB_EVENT_WAKEUP:
            if (hub->usb_port[port].suspend && !hub->usb_port[port].resume) {
                hub->usb_port[port].resume = 1;
            }
            // if in suspend state, signal resume
            if (hub->usb_command.suspend) {
                hub->usb_command.resume = 1;
                hub->usb_status.resume  = 1;
                if (hub->usb_enable.resume) {
                    hub->usb_status.interrupt = 1;
                }
                usb_uhci_update_irq(hub);
            }
            break;

        // host controller events start here
        case USB_EVENT_DEFAULT_SPEED:
            // return default speed for specified port number
            return USB_SPEED_FULL;

        case USB_EVENT_CHECK_SPEED:
            if (ptr != NULL) {
                usb_device_c *usb_device = (usb_device_c *) ptr;
                if (usb_device->speed <= USB_SPEED_FULL)
                    ret = 1;
            }
            break;
        default:
            BX_ERROR(("unknown/unsupported event (id=%d) on port #%d", event, port + 1));
            ret = -1; // unknown event, event not handled
    }

    return ret;
}

bool
usb_uhci_do_transfer(bx_uhci_core_t *hub, uint32_t address, struct TD *td)
{
    uint16_t maxlen = (td->dword2 >> 21);
    uint8_t  addr   = (td->dword2 >> 8) & 0x7F;
    uint8_t  endpt  = (td->dword2 >> 15) & 0x0F;
    uint8_t  pid    = td->dword2 & 0xFF;

    USBAsync *p          = find_async_packet(&hub->packets, address);
    bool      completion = (p != NULL);
    if (completion && !p->done) {
        return 0;
    }

    BX_DEBUG(("TD found at address 0x%08X:  0x%08X  0x%08X  0x%08X  0x%08X", address, td->dword0, td->dword1, td->dword2, td->dword3));

    // check TD to make sure it is valid
    // A max length 0x500 to 0x77E is illegal
    if ((maxlen >= 0x500) && (maxlen != 0x7FF)) {
        BX_ERROR(("invalid max. length value 0x%04x", maxlen));
        return 0; // error = consistency check failure
    }

    // when the active bit is set, all others in the 'Status' byte must be zero
    // (active bit is set or we wouldn't be here)
    if (td->dword1 & (0x7F << 16)) {
        BX_ERROR(("UHCI Core: When Active bit is set, all others in the 'Status' byte must be zero. (0x%02X)",
                  (td->dword1 & (0x7F << 16)) >> 16));
    }

    // the reserved bit in the Link Pointer should be zero
    if (td->dword0 & (1 << 3)) {
        BX_INFO(("UHCI Core: Reserved bit in the Link Pointer is not zero."));
    }

    if (td->dword1 & (1 << 25))
        BX_INFO(("UHCI Core: Encountered Isochronous Packet.\n"));

    // the device should remain in a stall state until the next setup packet is recieved
    // For some reason, this doesn't work yet.
    // if (dev && dev->in_stall && (pid != USB_TOKEN_SETUP))
    //  return FALSE;

    maxlen++;
    maxlen &= 0x7FF;

    int len = 0, ret = 0;

    if (completion) {
        ret = p->packet.len;
    } else {
        p                 = create_async_packet(&hub->packets, address, maxlen);
        p->packet.pid     = pid;
        p->packet.devaddr = addr;
        p->packet.devep   = endpt;
        p->packet.speed   = (td->dword1 & (1 << 26)) ? USB_SPEED_LOW : USB_SPEED_FULL;
#if HANDLE_TOGGLE_CONTROL
        p->packet.toggle = (td->dword2 & (1 << 19)) > 0;
#endif
        p->packet.complete_cb = usb_uhci_event_handler;
        p->packet.complete_dev = hub;
        switch (pid) {
            case USB_TOKEN_OUT:
            case USB_TOKEN_SETUP:
                if (maxlen > 0) {
                    dma_bm_read(td->dword3, p->packet.data, maxlen, 4);
                }
                ret = usb_uhci_broadcast_packet(hub, &p->packet);
                len = maxlen;
                break;
            case USB_TOKEN_IN:
                ret = usb_uhci_broadcast_packet(hub, &p->packet);
                break;
            default:
                remove_async_packet(&hub->packets, p);
                hub->usb_status.host_error = 1;
                usb_uhci_update_irq(hub);
                return 0;
        }
        if (ret == USB_RET_ASYNC) {
            BX_DEBUG(("Async packet deferred"));
            return 0;
        }
    }
    if (pid == USB_TOKEN_IN) {
        if (ret >= 0) {
            len = ret;
            if (len > maxlen) {
                len = maxlen;
                ret = USB_RET_BABBLE;
            }
            if (len > 0) {
                dma_bm_write(td->dword3, p->packet.data, len, 4);
            }
        } else {
            len = 0;
        }
    }
    if (ret >= 0) {
        set_status(td, 0, 0, 0, 0, 0, 0, len - 1);
    } else if (ret == USB_RET_NAK) {
        set_status(td, 0, 0, 0, 1, 0, 0, len - 1); // NAK
        if (!(td->dword1 & (1 << 25)))
            td->dword1 |= (1 << 23);
    } else {
        set_status(td, 1, 0, 0, 0, 0, 0, 0x7FF); // stalled
    }
    remove_async_packet(&hub->packets, p);
    return 1;
}

void
usb_uhci_reset(void *priv)
{
    bx_uhci_core_t *hub = (bx_uhci_core_t *) priv;
    int             j   = 0;

    // Put the USB registers into their RESET state
    hub->usb_command.max_packet_size = 0;
    hub->usb_command.configured      = 0;
    hub->usb_command.debug           = 0;
    hub->usb_command.resume          = 0;
    hub->usb_command.suspend         = 0;
    hub->usb_command.reset           = 0;
    hub->usb_command.host_reset      = 0;
    hub->usb_command.schedule        = 0;
    hub->usb_status.error_interrupt  = 0;
    hub->usb_status.host_error       = 0;
    hub->usb_status.host_halted      = 0;
    hub->usb_status.interrupt        = 0;
    hub->usb_status.status2          = 0;
    hub->usb_status.pci_error        = 0;
    hub->usb_status.resume           = 0;
    hub->usb_enable.short_packet     = 0;
    hub->usb_enable.on_complete      = 0;
    hub->usb_enable.resume           = 0;
    hub->usb_enable.timeout_crc      = 0;
    hub->usb_frame_num.frame_num     = 0x0000;
    hub->usb_frame_base.frame_base   = 0x00000000;
    hub->usb_sof.sof_timing          = 0x40;

    for (j = 0; j < USB_UHCI_PORTS; j++) {
        hub->usb_port[j].connect_changed     = 0;
        hub->usb_port[j].line_dminus         = 0;
        hub->usb_port[j].line_dplus          = 0;
        hub->usb_port[j].low_speed           = 0;
        hub->usb_port[j].reset               = 0;
        hub->usb_port[j].resume              = 0;
        hub->usb_port[j].suspend             = 0;
        hub->usb_port[j].over_current_change = 0;
        hub->usb_port[j].over_current        = 0;
        hub->usb_port[j].enabled             = 0;
        hub->usb_port[j].enable_changed      = 0;
        hub->usb_port[j].status              = 0;
        if (hub->usb_port[j].device != NULL) {
            usb_uhci_set_connect_status(hub, j, 1);
        }
    }
    while (hub->packets != NULL) {
        usb_cancel_packet(&hub->packets->packet);
        remove_async_packet(&hub->packets, hub->packets);
    }
}

void
usb_uhci_set_port_device(void *priv, int port, usb_device_c *dev)
{
    bx_uhci_core_t *hub    = (bx_uhci_core_t *) priv;
    usb_device_c   *olddev = hub->usb_port[port].device;
    if ((dev != NULL) && (olddev == NULL)) {
        // make sure we are calling the correct handler for the device
        usb_device_set_event_handler(dev, hub, usb_uhci_event_handler, port);
        hub->usb_port[port].device = dev;
        usb_uhci_set_connect_status(hub, port, 1);
    } else if ((dev == NULL) && (olddev != NULL)) {
        usb_uhci_set_connect_status(hub, port, 0);
        hub->usb_port[port].device = dev;
    }
}

uint8_t
usb_uhci_read(uint16_t address, void *priv)
{
    uint32_t          val = 0x0;
    uint8_t           port;
    bx_uhci_core_t *hub    = (bx_uhci_core_t *) priv;
    uint8_t           offset = 0;
    bool            odd    = address & 1;

    // if the host driver has not cleared the reset bit, do nothing (reads are
    // undefined)
    if (hub->usb_command.reset)
        return 0;

    offset = address & 0x1e;

    switch (offset) {
        case 0x00: // command register (16-bit)
            val = hub->usb_command.max_packet_size << 7
                | hub->usb_command.configured << 6
                | hub->usb_command.debug << 5
                | hub->usb_command.resume << 4
                | hub->usb_command.suspend << 3
                | hub->usb_command.reset << 2
                | hub->usb_command.host_reset << 1
                | (uint16_t) hub->usb_command.schedule;
            break;

        case 0x02: // status register (16-bit)
            val = hub->usb_status.host_halted << 5
                | hub->usb_status.host_error << 4
                | hub->usb_status.pci_error << 3
                | hub->usb_status.resume << 2
                | hub->usb_status.error_interrupt << 1
                | (uint16_t) hub->usb_status.interrupt;
            break;

        case 0x04: // interrupt enable register (16-bit)
            val = hub->usb_enable.short_packet << 3
                | hub->usb_enable.on_complete << 2
                | hub->usb_enable.resume << 1
                | (uint16_t) hub->usb_enable.timeout_crc;
            break;

        case 0x06: // frame number register (16-bit)
            val = hub->usb_frame_num.frame_num;
#ifdef USE_DYNAREC
            if (cpu_use_dynarec)
                update_tsc();
#endif
            break;

        case 0x08: // frame base register (32-bit)
        case 0x09:
        case 0x0A:
        case 0x0B:
            val = hub->usb_frame_base.frame_base >> ((offset & 3) * 8);
            break;

        case 0x0C: // start of Frame Modify register (8-bit)
            val = hub->usb_sof.sof_timing;
            break;

        case 0x14: // port #3 non existent, but linux systems check it to see if there are more than 2
            BX_ERROR(("read from non existent offset 0x14 (port #3)"));
            val = 0xFF7F;
            break;

        case 0x10: // port #1
        case 0x11:
        case 0x12: // port #2
        case 0x13:
            port = (offset & 0x0F) >> 1;
            if (port < USB_UHCI_PORTS) {
                val = hub->usb_port[port].suspend << 12
                    | hub->usb_port[port].over_current_change << 11
                    | hub->usb_port[port].over_current << 10
                    | hub->usb_port[port].reset << 9
                    | hub->usb_port[port].low_speed << 8
                    | 1 << 7
                    | hub->usb_port[port].resume << 6
                    | hub->usb_port[port].line_dminus << 5
                    | hub->usb_port[port].line_dplus << 4
                    | hub->usb_port[port].enable_changed << 3
                    | hub->usb_port[port].enabled << 2
                    | hub->usb_port[port].connect_changed << 1
                    | (uint16_t) hub->usb_port[port].status;
                if (offset & 1) {
                    odd = false;
                    val >>= 8;
                }
                break;
            } // else fall through to default
        default:
            val = 0xFF7F; // keep compiler happy
            BX_ERROR(("unsupported io read from address=0x%04x!", (unsigned) address));
            break;
    }

    if (odd)
        val >>= 8;

    return (val & 0xFF);
}

void
usb_uhci_writew(uint16_t addr, uint16_t value, void *priv);

void
usb_uhci_write(uint16_t address, uint8_t value, void *priv)
{
    bx_uhci_core_t *hub = (bx_uhci_core_t *) priv;
    uint8_t           port;

    uint8_t offset = address & 0x1f;

    // if the reset bit is not cleared and this write is not clearing the bit,
    // do nothing
    if (hub->usb_command.reset && ((offset != 0) || (value & 0x04)))
        return;


    // BX_DEBUG(("register write to  address 0x%04X:  0x%08X (%2i bits)", (unsigned) address, (unsigned) value, io_len * 8));

    switch (offset) {
        case 0x00: // command register (16-bit) (R/W)
            if (value & 0xFF00)
                BX_DEBUG(("write to command register with bits 15:8 not zero: 0x%04x", value));

            hub->usb_command.max_packet_size = (value & 0x80) ? 1 : 0;
            hub->usb_command.configured      = (value & 0x40) ? 1 : 0;
            hub->usb_command.debug           = (value & 0x20) ? 1 : 0;
            hub->usb_command.resume          = (value & 0x10) ? 1 : 0;
            hub->usb_command.suspend         = (value & 0x08) ? 1 : 0;
            hub->usb_command.reset           = (value & 0x04) ? 1 : 0;
            hub->usb_command.host_reset      = (value & 0x02) ? 1 : 0;
            hub->usb_command.schedule        = (value & 0x01) ? 1 : 0;

            // HCRESET
            if (hub->usb_command.host_reset) {
                usb_uhci_reset(priv);
                for (unsigned i = 0; i < USB_UHCI_PORTS; i++) {
                    if (hub->usb_port[i].status) {
                        if (hub->usb_port[i].device != NULL) {
                            usb_device_send_msg(hub->usb_port[i].device, USB_MSG_RESET);
                        }
                        hub->usb_port[i].connect_changed = 1;
                        if (hub->usb_port[i].enabled) {
                            hub->usb_port[i].enable_changed = 1;
                            hub->usb_port[i].enabled        = 0;
                        }
                    }
                }
            }

            // If software set the GRESET bit, we need to send the reset to all USB.
            // The software should guarentee that the reset is for at least 10ms.
            // We hold the reset until software resets this bit
            if (hub->usb_command.reset) {
                hub->global_reset = 1;
                BX_DEBUG(("Global Reset"));
            } else {
                // if software cleared the reset, then we need to reset the usb registers.
                if (hub->global_reset) {
                    unsigned int running = hub->usb_command.schedule;
                    hub->global_reset    = 0;
                    usb_uhci_reset(hub);
                    hub->usb_status.host_halted = (running) ? 1 : 0;
                }
            }

            // If Run/Stop, identify in log
            if (hub->usb_command.schedule) {
                hub->usb_status.host_halted = 0;
                BX_DEBUG(("Schedule bit set in Command register"));
            } else {
                hub->usb_status.host_halted = 1;
                BX_DEBUG(("Schedule bit clear in Command register"));
            }

            // If Debug mode set, panic.  Not implemented
            if (hub->usb_command.debug)
                BX_PANIC(("Software set DEBUG bit in Command register. Not implemented"));

            break;

        case 0x02: // status register (16-bit) (R/WC)
            if (value & 0xFFC0)
                BX_DEBUG(("write to status register with bits 15:6 not zero: 0x%04x", value));

            // host_halted, even though not specified in the specs, is read only
            // hub->usb_status.host_halted = (value & 0x20) ? 0: hub->usb_status.host_halted;
            hub->usb_status.host_error      = (value & 0x10) ? 0 : hub->usb_status.host_error;
            hub->usb_status.pci_error       = (value & 0x08) ? 0 : hub->usb_status.pci_error;
            hub->usb_status.resume          = (value & 0x04) ? 0 : hub->usb_status.resume;
            hub->usb_status.error_interrupt = (value & 0x02) ? 0 : hub->usb_status.error_interrupt;
            hub->usb_status.interrupt       = (value & 0x01) ? 0 : hub->usb_status.interrupt;
            if (value & 0x01) {
                hub->usb_status.status2 = 0;
            }
            usb_uhci_update_irq(hub);
            break;

        case 0x04: // interrupt enable register (16-bit)
            if (value & 0xFFF0)
                BX_DEBUG(("write to interrupt enable register with bits 15:4 not zero: 0x%04x", value));

            hub->usb_enable.short_packet = (value & 0x08) ? 1 : 0;
            hub->usb_enable.on_complete  = (value & 0x04) ? 1 : 0;
            hub->usb_enable.resume       = (value & 0x02) ? 1 : 0;
            hub->usb_enable.timeout_crc  = (value & 0x01) ? 1 : 0;

            if (value & 0x08) {
                BX_DEBUG(("Host set Enable Interrupt on Short Packet"));
            }
            if (value & 0x04) {
                BX_DEBUG(("Host set Enable Interrupt on Complete"));
            }
            if (value & 0x02) {
                BX_DEBUG(("Host set Enable Interrupt on Resume"));
            }
            if (value & 0x01) {
                BX_DEBUG(("Host set Enable Interrupt on Timeout/CRC"));
            }
            usb_uhci_update_irq(hub);
            break;
          
        case 0x06: {
            if (hub->usb_status.host_halted)
                hub->usb_frame_num.frame_num = (hub->usb_frame_num.frame_num & 0x0700) | (value & 0x0FF);
            break;
        }

        case 0x07: {
            if (hub->usb_status.host_halted)
                hub->usb_frame_num.frame_num = (hub->usb_frame_num.frame_num & 0x00FF) | (value << 8);
            break;
        }

        case 0x09:
            hub->usb_frame_base.frame_base &= 0xFFFF0000;
            hub->usb_frame_base.frame_base |= (value & 0xF0) << 8;
            break;
        case 0x0a:
        case 0x0b:
            hub->usb_frame_base.frame_base &= (address & 1) ? 0x00FFFFFF : 0xFF00FFFF;
            hub->usb_frame_base.frame_base |= (value) << ((address & 1) ? 24 : 16);
            break;

        case 0x0C: // start of Frame Modify register (8-bit)
            if (value & 0x80)
                BX_DEBUG(("write to SOF Modify register with bit 7 not zero: 0x%04x", value));

            hub->usb_sof.sof_timing = value;
            break;

        case 0x14: // port #3 non existent, but linux systems check it to see if there are more than 2
            BX_ERROR(("write to non existent offset 0x14 (port #3)"));
#if BX_USE_WIN32USBDEBUG
            // Non existant Register Port (the next one after the last)
            win32_usb_trigger(USB_DEBUG_UHCI, USB_DEBUG_NONEXIST, 0, 0);
#endif
            break;

        default:
            BX_ERROR(("unsupported io write to address=0x%04x!", (unsigned) address));
            break;
    }
}

void
usb_uhci_writew(uint16_t addr, uint16_t value, void *priv)
{
    bx_uhci_core_t *hub    = (bx_uhci_core_t *) priv;
    uint8_t           offset = addr & 0x1f;
    uint8_t         port   = 0;

    if (hub->usb_command.reset && ((offset != 0) || (value & 0x04)))
        return;

    switch (offset) {
        default:
          usb_uhci_write(addr, value & 0xFF, priv);
          usb_uhci_write(addr + 1, (value >> 8) & 0xFF, priv);
          break;
        case 0x06:
            if (hub->usb_status.host_halted)
                hub->usb_frame_num.frame_num = (value & 0x07FF);

            break;
        case 0x10: // port #1
        case 0x12: // port #2
            port = (offset & 0x0F) >> 1;
            if ((port < USB_UHCI_PORTS)) {
                // If the ports reset bit is set, don't allow any writes unless the new write will clear the reset bit
                if (hub->usb_port[port].reset && ((value & (1 << 9)) != 0))
                    break;
#if BX_USE_WIN32USBDEBUG
                if ((value & (1 << 9)) && !hub->usb_port[port].reset)
                    win32_usb_trigger(USB_DEBUG_UHCI, USB_DEBUG_RESET, port, 0);
#endif
                if (value & ((1 << 5) | (1 << 4) | (1 << 0)))
                    BX_DEBUG(("write to one or more read-only bits in port #%d register: 0x%04x", port + 1, value));
                if (!(value & (1 << 7)))
                    BX_DEBUG(("write to port #%d register bit 7 = 0", port + 1));
                if (value & (1 << 8))
                    BX_DEBUG(("write to bit 8 in port #%d register ignored", port + 1));
                if ((value & (1 << 12)) && hub->usb_command.suspend)
                    BX_DEBUG(("write to port #%d register bit 12 when in Global-Suspend", port + 1));

                // some controllers don't successfully reset if the CSC bit is
                //  cleared during a reset. i.e.: if the CSC bit is cleared at
                //  the same time the reset bit is cleared, the controller may
                //  not successfully reset. If the guest is clearing the CSC
                //  bit at the same time it is clearing the reset bit, let's give
                //  an INFO message.
                if (hub->usb_port[port].reset && !(value & (1 << 9)) && (value & (1 << 1))) {
                    BX_INFO(("UHCI Core: Clearing the CSC while clearing the Reset may not successfully reset the port."));
                    BX_INFO(("UHCI Core: Clearing the CSC after the Reset has been cleared will ensure a successful reset."));
                }

                hub->usb_port[port].suspend = (value & (1 << 12)) ? 1 : 0;
                if (value & (1 << 11))
                    hub->usb_port[port].over_current_change = 0;
                hub->usb_port[port].reset  = (value & (1 << 9)) ? 1 : 0;
                hub->usb_port[port].resume = (value & (1 << 6)) ? 1 : 0;
                if (!hub->usb_port[port].enabled && (value & (1 << 2))) {
#if BX_USE_WIN32USBDEBUG
                    win32_usb_trigger(USB_DEBUG_UHCI, USB_DEBUG_ENABLE, port, 0);
#endif
                    hub->usb_port[port].enable_changed = 0;
                } else if (value & (1 << 3))
                    hub->usb_port[port].enable_changed = 0;
                hub->usb_port[port].enabled = (value & (1 << 2)) ? 1 : 0;
                if (value & (1 << 1))
                    hub->usb_port[port].connect_changed = 0;

                // if port reset, reset function(s)
                // TODO: only reset items on the downstream...
                // for now, reset the one and only
                // TODO: descriptors, etc....
                if (hub->usb_port[port].reset) {
                    hub->usb_port[port].suspend             = 0;
                    hub->usb_port[port].over_current_change = 0;
                    hub->usb_port[port].over_current        = 0;
                    hub->usb_port[port].resume              = 0;
                    hub->usb_port[port].enabled             = 0;
                    // are we are currently connected/disconnected
                    if (hub->usb_port[port].status) {
                        if (hub->usb_port[port].device != NULL) {
                            hub->usb_port[port].low_speed = (hub->usb_port[port].device->speed == USB_SPEED_LOW);
                            usb_uhci_set_connect_status(hub, port, 1);
                            usb_device_send_msg(hub->usb_port[port].device, USB_MSG_RESET);
                        }
                    }
                    BX_DEBUG(("Port%d: Reset", port + 1));
                }
                break;
            }
    }
}

void
uhci_update_io_mapping_new(void *priv, uint8_t base_l, uint8_t base_h, int enable)
{
  bx_uhci_core_t *dev = (bx_uhci_core_t*)priv;
    if (!dev)
        return;
    if (dev->uhci_enable && (dev->uhci_io_base != 0x0000))
        io_removehandler(dev->uhci_io_base, 0x20, usb_uhci_read, NULL, NULL, usb_uhci_write, usb_uhci_writew, NULL, dev);

    dev->uhci_io_base = base_l | (base_h << 8);
    dev->uhci_enable  = enable;

    if (dev->uhci_enable && (dev->uhci_io_base != 0x0000))
        io_sethandler(dev->uhci_io_base, 0x20, usb_uhci_read, NULL, NULL, usb_uhci_write, usb_uhci_writew, NULL, dev);
}

int uhci_port_is_free(usb_port_t* port)
{
  bx_uhci_core_t *dev = (bx_uhci_core_t*)port->priv;

  if (port->number >= 2)
    return 0;

  return !(dev->usb_port[port->number].device);
}

int uhci_port_connect(usb_port_t* port, usb_device_c* device)
{
    if (!uhci_port_is_free(port))
        return 0;

    usb_uhci_set_port_device(port->priv, port->number, device);
    return 1;
}

void uhci_register_usb(usb_t *dev)
{
  bx_uhci_core_t *hub = (bx_uhci_core_t*)dev->usb_uhci_priv;

  usb_register_port(0, hub, uhci_port_is_free, uhci_port_connect);
  usb_register_port(1, hub, uhci_port_is_free, uhci_port_connect);
}

void *
usb_uhci_init_ext(UNUSED(const device_t *info), void* params)
{
    bx_uhci_core_t *hub;
    usb_params_t* usb_params = params;
    usb_device_c* mouse_device;

    hub = (bx_uhci_core_t *) calloc(1, sizeof(bx_uhci_core_t));
    hub->max_bandwidth = 1280;
    if (params) {
      hub->devfunc = usb_params->pci_dev;
      hub->pci_conf = usb_params->pci_conf;
    }
    timer_add(&hub->timer, usb_uhci_timer, hub, 1);

    return hub;
}

static void
usb_uhci_close(void *priv)
{
  free(priv);
}

const device_t usb_uhci_device = {
    .name          = "Universal Serial Bus (UHCI)",
    .internal_name = "usb_uhci",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init_ext      = usb_uhci_init_ext,
    .close         = usb_uhci_close,
    .reset         = usb_uhci_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
