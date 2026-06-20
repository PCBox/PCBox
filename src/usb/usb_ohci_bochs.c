/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          USB OHCI host controller emulation, ported from Bochs.
 *
 * Authors: Cacodemon345
 *          Benjamin D Lunt (fys [at] fysnet [dot] net)
 *          The Bochs Project
 * 
 *          Copyright 2009-2024 Benjamin D Lunt (fys [at] fysnet [dot] net)
 *          Copyright 2009-2024 The Bochs Project
 *          Copyright 2024-2025 Cacodemon345
 * 
 * QEMU isochronous transfer code:
 *          Copyright 2004 Gianni Tedesco
 *          Copyright 2006 CodeSourcery
 *          Copyright 2006 Openedhand Ltd.
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
#include <86box/machine.h>

#include "usb_common.h"

#define USB_OHCI_PORTS 2

// HCFS values
#define  OHCI_USB_RESET       0x00
#define  OHCI_USB_RESUME      0x01
#define  OHCI_USB_OPERATIONAL 0x02
#define  OHCI_USB_SUSPEND     0x03

#define OHCI_INTR_SO          (1<<0) // Scheduling overrun
#define OHCI_INTR_WD          (1<<1) // HcDoneHead writeback
#define OHCI_INTR_SF          (1<<2) // Start of frame
#define OHCI_INTR_RD          (1<<3) // Resume detect
#define OHCI_INTR_UE          (1<<4) // Unrecoverable error
#define OHCI_INTR_FNO         (1<<5) // Frame number overflow
#define OHCI_INTR_RHSC        (1<<6) // Root hub status change
#define OHCI_INTR_OC          (1<<30) // Ownership change
#define OHCI_INTR_MIE         (1<<31) // Master Interrupt Enable

// Completion Codes
enum {
  NoError = 0,
  CRC,
  BitStuffing,
  DataToggleMismatch,
  Stall,
  DeviceNotResponding,
  PIDCheckFailure,
  UnexpectedPID,
  DataOverrun,
  DataUnderrun,
  BufferOverrun = 0xC,
  BufferUnderrun,
  NotAccessed
};


#define ED_GET_MPS(x)     (((x)->dword0 & 0x07FF0000) >> 16)
#define ED_GET_F(x)       (((x)->dword0 & 0x00008000) >> 15)
#define ED_GET_K(x)       (((x)->dword0 & 0x00004000) >> 14)
#define ED_GET_S(x)       (((x)->dword0 & 0x00002000) >> 13)
#define ED_GET_D(x)       (((x)->dword0 & 0x00001800) >> 11)
#define ED_GET_EN(x)      (((x)->dword0 & 0x00000780) >>  7)
#define ED_GET_FA(x)      (((x)->dword0 & 0x0000007F) >>  0)
#define ED_GET_TAILP(x)    ((x)->dword1 & 0xFFFFFFF0)
#define ED_GET_HEADP(x)    ((x)->dword2 & 0xFFFFFFF0)
#define ED_SET_HEADP(x, y) ((x)->dword2 = ((x)->dword2 & 0x0000000F) | ((y) & 0xFFFFFFF0))
#define ED_GET_C(x)       (((x)->dword2 & 0x00000002) >>  1)
#define ED_SET_C(x,y)      ((x)->dword2 = ((x)->dword2 & ~0x00000002) | ((y)<<1))
#define ED_GET_H(x)       (((x)->dword2 & 0x00000001) >>  0)
#define ED_SET_H(x,y)      ((x)->dword2 = ((x)->dword2 & ~0x00000001) | ((y)<<0))
#define ED_GET_NEXTED(x)   ((x)->dword3 & 0xFFFFFFF0)

struct OHCI_ED {
  uint32_t   dword0;
  uint32_t   dword1;
  uint32_t   dword2;
  uint32_t   dword3;
};


#define TD_GET_R(x)       (((x)->dword0 & 0x00040000) >> 18)
#define TD_GET_DP(x)      (((x)->dword0 & 0x00180000) >> 19)
#define TD_GET_DI(x)      (((x)->dword0 & 0x00E00000) >> 21)
#define TD_GET_T(x)       (((x)->dword0 & 0x03000000) >> 24)
#define TD_SET_T(x,y)      ((x)->dword0 = ((x)->dword0 & ~0x03000000) | ((y)<<24))
#define TD_GET_EC(x)      (((x)->dword0 & 0x0C000000) >> 26)
#define TD_SET_EC(x,y)     ((x)->dword0 = ((x)->dword0 & ~0x0C000000) | ((y)<<26))
#define TD_GET_CC(x)      (((x)->dword0 & 0xF0000000) >> 28)
#define TD_SET_CC(x,y)     ((x)->dword0 = ((x)->dword0 & ~0xF0000000) | ((y)<<28))
#define TD_GET_CBP(x)      ((x)->dword1)
#define TD_SET_CBP(x,y)    ((x)->dword1 = y)
#define TD_GET_NEXTTD(x)   ((x)->dword2 & 0xFFFFFFF0)
#define TD_SET_NEXTTD(x,y) ((x)->dword2 = (y & 0xFFFFFFF0))
#define TD_GET_BE(x)       ((x)->dword3)

struct OHCI_TD {
  uint32_t   dword0;
  uint32_t   dword1;
  uint32_t   dword2;
  uint32_t   dword3;
};


#define ISO_TD_GET_CC(x)      (((x)->dword0 & 0xF0000000) >> 28)
#define ISO_TD_GET_FC(x)      (((x)->dword0 & 0x0F000000) >> 24)
#define ISO_TD_GET_DI(x)      (((x)->dword0 & 0x00E00000) >> 21)
#define ISO_TD_GET_SF(x)       ((x)->dword0 & 0x0000FFFF)
#define ISO_TD_GET_BP0(x)     (((x)->dword1 & 0xFFFFF000) >> 12)
#define ISO_TD_GET_NEXTTD(x)   ((x)->dword2 & 0xFFFFFFF0)
#define ISO_TD_GET_BE(x)       ((x)->dword3)
#define ISO_TD_GET_PSW0(x)     ((x)->dword4 & 0x0000FFFF)
#define ISO_TD_GET_PSW1(x)    (((x)->dword4 & 0xFFFF0000) >> 16)
#define ISO_TD_GET_PSW2(x)     ((x)->dword5 & 0x0000FFFF)
#define ISO_TD_GET_PSW3(x)    (((x)->dword5 & 0xFFFF0000) >> 16)
#define ISO_TD_GET_PSW4(x)     ((x)->dword6 & 0x0000FFFF)
#define ISO_TD_GET_PSW5(x)    (((x)->dword6 & 0xFFFF0000) >> 16)
#define ISO_TD_GET_PSW6(x)     ((x)->dword7 & 0x0000FFFF)
#define ISO_TD_GET_PSW7(x)    (((x)->dword7 & 0xFFFF0000) >> 16)

#pragma pack(push, 2)
struct OHCI_ISO_TD {
  uint32_t   dword0;
  uint32_t   dword1;
  uint32_t   dword2;
  uint32_t   dword3;
  union
  {
  struct {
    uint32_t   dword4;
    uint32_t   dword5;
    uint32_t   dword6;
    uint32_t   dword7;
  };
  uint16_t offset[8];
  };
};
#pragma pack(pop)


typedef struct {
  struct OHCI_OP_REGS {
    uint16_t HcRevision;
    struct {
      uint32_t reserved;          // 21 bit reserved                    = 0x000000       R   R
      bool   rwe;               //  1 bit RemoteWakeupEnable          = 0b             RW  R
      bool   rwc;               //  1 bit RemoteWakeupConnected       = 0b             RW  RW
      bool   ir;                //  1 bit InterruptRouting            = 0b             RW  R
      uint8_t  hcfs;              //  2 bit HostControllerFuncState     = 00b            RW  RW
      bool   ble;               //  1 bit BulkListEnable              = 0b             RW  R
      bool   cle;               //  1 bit ControlListEnable           = 0b             RW  R
      bool   ie;                //  1 bit IsochronousEnable           = 0b             RW  R
      bool   ple;               //  1 bit PeriodicListEnable          = 0b             RW  R
      uint8_t  cbsr;              //  2 bit ControlBulkService Ratio    = 00b            RW  R
    } HcControl;                //                                    = 0x00000000
    struct {
      uint16_t reserved0;         // 14 bit reserved                    = 0x000000       R   R
      uint8_t  soc;               //  2 bit SchedulingOverrunCount      = 00b            R   RW
      uint16_t reserved1;         // 12 bit reserved                    = 0x000000       R   R
      bool   ocr;               //  1 bit OwnershipChangeRequest      = 0b             RW  RW
      bool   blf;               //  1 bit BulkListFilled              = 0b             RW  RW
      bool   clf;               //  1 bit ControlListFilled           = 0b             RW  RW
      bool   hcr;               //  1 bit HostControllerReset         = 0b             RW  RW
    } HcCommandStatus;          //                                    = 0x00000000
    uint32_t HcInterruptStatus;
    uint32_t HcInterruptEnable;
    uint32_t HcHCCA;
    uint32_t HcPeriodCurrentED;
    uint32_t HcControlHeadED;
    uint32_t HcControlCurrentED;
    uint32_t HcBulkHeadED;
    uint32_t HcBulkCurrentED;
    uint32_t HcDoneHead;
    struct {
      bool   fit;               //  1 bit FrameIntervalToggle         = 0b             RW  R
      uint16_t fsmps;             // 15 bit FSLargestDataPacket         = TBD (0)        RW  R
      uint8_t  reserved;          //  2 bit reserved                    = 00b            R   R
      uint16_t fi;                // 14 bit FrameInterval               = 0x2EDF         RW  R
    } HcFmInterval;             //                                    = 0x00002EDF
    bool   HcFmRemainingToggle; //  1 bit FrameRemainingToggle        = 0b             R   RW
    uint32_t HcFmNumber;
    uint32_t HcPeriodicStart;
    uint16_t HcLSThreshold;
    struct {
      uint8_t  potpgt;            //  8 bit PowerOnToPowerGoodTime      = 0x10           RW  R
      uint16_t reserved;          // 11 bit reserved                    = 0x000          R   R
      bool   nocp;              //  1 bit NoOverCurrentProtection     = 0b             RW  R
      bool   ocpm;              //  1 bit OverCurrentProtectionMode   = 1b             RW  R
      bool   dt;                //  1 bit DeviceType                  = 0b             R   R
      bool   nps;               //  1 bit NoPowerSwitching            = 0b             RW  R
      bool   psm;               //  1 bit PowerSwitchingMode          = 1b             RW  R
      uint8_t  ndp;               //  8 bit NumberDownstreamPorts       = NUMPORTS       RW  R
    } HcRhDescriptorA;          //                                    = 0x100009xx
    struct {
      uint16_t ppcm;              // 16 bit PortPowerControlMask        = 0x0002         RW  R
      uint16_t dr;                // 16 bit DeviceRemovable             = 0x0000         RW  R
    } HcRhDescriptorB;          //                                    = 0x00020000
    struct {
      bool   crwe;              //  1 bit ClearRemoteWakeupEnable     = 0b             WC  R
      uint16_t reserved0;         // 13 bit reserved                    = 0x000000       R   R
      bool   ocic;              //  1 bit OverCurrentIndicatorChange  = 0b             RW  RW
      bool   lpsc;              //  1 bit LocalPowerStatusChange(r)   = 0b             RW  R
      bool   drwe;              //  1 bit DeviceRemoteWakeupEnable(r) = 0b             RW  R
      uint16_t reserved1;         // 13 bit reserved                    = 0x000000       R   R
      bool   oci;               //  1 bit OverCurrentIndicator        = 0b             R   RW
      bool   lps;               //  1 bit LocalPowerStatus(r)         = 0b             RW  R
    } HcRhStatus;               //                                    = 0x00000000

    uint32_t HceInput, HceOutput, HceControl, HceStatus;
  } op_regs;

  struct {
    // our data
    usb_device_c *device;   // device connected to this port

    struct {
      uint16_t reserved0;         // 11 bit reserved                    = 0x000000       R   R
      bool   prsc;              //  1 bit PortResetStatusChange       = 0b             RW  RW
      bool   ocic;              //  1 bit OverCurrentIndicatorChange  = 0b             RW  RW
      bool   pssc;              //  1 bit PortSuspendStatusChange     = 0b             RW  RW
      bool   pesc;              //  1 bit PortEnableStatusChange      = 0b             RW  RW
      bool   csc;               //  1 bit ConnectStatusChange         = 0b             RW  RW
      uint8_t  reserved1;         //  6 bit reserved                    = 0x00           R   R
      bool   lsda;              //  1 bit LowSpeedDeviceAttached      = 0b             RW  RW
      bool   pps;               //  1 bit PortPowerStatus             = 0b             RW  RW
      uint8_t  reserved2;         //  3 bit reserved                    = 0x0            R   R
      bool   prs;               //  1 bit PortResetStatus             = 0b             RW  RW
      bool   poci;              //  1 bit PortOverCurrentIndicator    = 0b             RW  RW
      bool   pss;               //  1 bit PortSuspendStatus           = 0b             RW  RW
      bool   pes;               //  1 bit PortEnableStatus            = 0b             RW  RW
      bool   ccs;               //  1 bit CurrentConnectStatus        = 0b             RW  RW
    } HcRhPortStatus;
  } usb_port[USB_OHCI_PORTS];

  uint8_t* devfunc;
  uint8_t irq_state;
  unsigned ohci_done_count;
  bool     use_control_head;
  bool     use_bulk_head;
  uint64_t sof_time;
  USBAsync *packets;
  pc_timer_t timer;
  
  uint8_t* pci_conf;
  mem_mapping_t ohci_mmio_mapping;

  bool ohci_enable;
  uint32_t ohci_mem_base;

  void (*do_smi_raise)(void *priv);
  void (*do_smi_ocr_raise)(void *priv);
  void (*do_pci_irq)(void *priv, int level);
  void* card_priv;

  uint8_t* test_reg_enable;
} bx_ohci_core_t;

const char *usb_ohci_port_name[] = {
  "HCRevision        ",
  "HCControl         ",
  "HCCommandStatus   ",
  "HCInterruptStatus ",
  "HCInterruptEnable ",
  "HCInterruptDisable",
  "HCHCCA            ",
  "HCPeriodCurrentED ",
  "HCControlHeadED   ",
  "HCControlCurrentED",
  "HCBulkHeadED      ",
  "HCBulkCurrentED   ",
  "HCDoneHead        ",
  "HCFmInterval      ",
  "HCFmRemaining     ",
  "HCFmNumber        ",
  "HCPeriodicStart   ",
  "HCLSThreshold     ",
  "HCRhDescriptorA   ",
  "HCRhDescriptorB   ",
  "HCRhStatus        ",
  "HCRhPortStatus0   ",
  "HCRhPortStatus1   ",
  "HCRhPortStatus2   ",
  "HCRhPortStatus3   ",
  "  **unknown**     "
};

#ifdef ENABLE_OHCI_LOG
int ohci_do_log = ENABLE_OHCI_LOG;

static void
ohci_log(const char *fmt, ...)
{
    va_list ap;

    if (ohci_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ohci_log(fmt, ...)
#endif

#define BX_ERROR(x) ohci_log x ; ohci_log ("\n")
#define BX_INFO(x)  ohci_log x ; ohci_log ("\n")
#define BX_DEBUG(x) ohci_log x ; ohci_log ("\n")
#define BX_PANIC(x) fatal x ; ohci_log ("\n")

#define DEV_MEM_WRITE_PHYSICAL(addr, size, data) dma_bm_write(addr, (uint8_t*)data, size, 4);
#define DEV_MEM_READ_PHYSICAL(addr, size, data) dma_bm_read(addr, (uint8_t*)data, size, 4);

void usb_ohci_process_lists(bx_ohci_core_t* hub);

void usb_ohci_reset_port(bx_ohci_core_t* hub, int p)
{
  hub->usb_port[p].HcRhPortStatus.reserved0 = 0;
  hub->usb_port[p].HcRhPortStatus.prsc      = 0;
  hub->usb_port[p].HcRhPortStatus.ocic      = 0;
  hub->usb_port[p].HcRhPortStatus.pssc      = 0;
  hub->usb_port[p].HcRhPortStatus.pesc      = 0;
  hub->usb_port[p].HcRhPortStatus.reserved1 = 0;
  hub->usb_port[p].HcRhPortStatus.lsda      = 0;
  hub->usb_port[p].HcRhPortStatus.pps       = 0;
  hub->usb_port[p].HcRhPortStatus.reserved2 = 0;
  hub->usb_port[p].HcRhPortStatus.prs       = 0;
  hub->usb_port[p].HcRhPortStatus.poci      = 0;
  hub->usb_port[p].HcRhPortStatus.pss       = 0;
  hub->usb_port[p].HcRhPortStatus.pes       = 0;
}

void usb_ohci_update_irq(bx_ohci_core_t* hub)
{
  bool level = 0;

  if ((hub->op_regs.HcInterruptEnable & OHCI_INTR_MIE) &&
      (hub->op_regs.HcInterruptStatus & hub->op_regs.HcInterruptEnable)) {
      level = 1;
  }
  if (hub->op_regs.HcControl.ir && level) {
    /* Machine in question freezes itself when triggering USB SMI#. */
    if (!strcmp(machine_get_internal_name(), "p6f99"))
      return;
    if (hub->do_smi_raise && hub->card_priv)
      hub->do_smi_raise(hub->card_priv);
      /* Commented out; this causes problems in MSI MS-5172 BIOS. */
    //else        
    //  smi_raise();
  }
  else if (!hub->op_regs.HcControl.ir) {
    if (hub->do_pci_irq)
      hub->do_pci_irq(hub->card_priv, level);
    else if (level)
      pci_set_irq(*hub->devfunc, hub->pci_conf[0x3d], &hub->irq_state);
    else
      pci_clear_irq(*hub->devfunc, hub->pci_conf[0x3d], &hub->irq_state);
  }
}

void usb_ohci_set_interrupt(bx_ohci_core_t* hub, uint32_t value)
{
  hub->op_regs.HcInterruptStatus |= value;
  usb_ohci_update_irq(hub);
}

bool usb_ohci_set_connect_status(bx_ohci_core_t* hub, uint8_t port, bool connected)
{
  const bool ccs_org = hub->usb_port[port].HcRhPortStatus.ccs;
  const bool pes_org = hub->usb_port[port].HcRhPortStatus.pes;

  usb_device_c *device = hub->usb_port[port].device;
  if (device != NULL) {
    if (connected) {
      switch (device->speed) {
        case USB_SPEED_LOW:
          hub->usb_port[port].HcRhPortStatus.lsda = 1;
          break;
        case USB_SPEED_FULL:
          hub->usb_port[port].HcRhPortStatus.lsda = 0;
          break;
        default:
          BX_PANIC(("USB device returned invalid speed value"));
          return 0;
      }
      hub->usb_port[port].HcRhPortStatus.ccs = 1;
      if (!device->connected) {
        if (!device->init(device)) {
          BX_ERROR(("port #%d: connect failed", port+1));
          return 0;
        } else {
          //BX_INFO(("port #%d: connect: %s", port+1, device->get_info()));
        }
      }
    } else { // not connected
      BX_INFO(("port #%d: device disconnect", port+1));
      hub->usb_port[port].HcRhPortStatus.ccs = 0;
      hub->usb_port[port].HcRhPortStatus.pes = 0;
      hub->usb_port[port].HcRhPortStatus.lsda = 0;
    }
    hub->usb_port[port].HcRhPortStatus.csc |= (ccs_org != hub->usb_port[port].HcRhPortStatus.ccs);
    hub->usb_port[port].HcRhPortStatus.pesc |= (pes_org != hub->usb_port[port].HcRhPortStatus.pes);

    // we changed the value of the port, so show it
    usb_ohci_set_interrupt(hub, OHCI_INTR_RHSC);
  }
  return connected;
}

void usb_ohci_reset_soft(void* priv)
{
  bx_ohci_core_t* hub = (void*)priv;
  hub->ohci_enable = 0;
  // reset locals
  hub->ohci_done_count = 7;

  // HcRevision
  hub->op_regs.HcRevision         = 0x0110;

  // HcControl
  hub->op_regs.HcControl.reserved  =          0;
  hub->op_regs.HcControl.rwe       =          0;
  hub->op_regs.HcControl.rwc       =          0;
  hub->op_regs.HcControl.hcfs      =          OHCI_USB_SUSPEND;
  hub->op_regs.HcControl.ble       =          0;
  hub->op_regs.HcControl.cle       =          0;
  hub->op_regs.HcControl.ie        =          0;
  hub->op_regs.HcControl.ple       =          0;
  hub->op_regs.HcControl.cbsr      =          0;

  // HcCommandStatus
  hub->op_regs.HcCommandStatus.reserved0 = 0x000000;
  hub->op_regs.HcCommandStatus.soc       =        0;
  hub->op_regs.HcCommandStatus.reserved1 = 0x000000;
  hub->op_regs.HcCommandStatus.blf       =        0;
  hub->op_regs.HcCommandStatus.clf       =        0;
  hub->op_regs.HcCommandStatus.hcr       =        0;

  // HcInterruptStatus
  hub->op_regs.HcInterruptStatus  = 0x00000000;

  // HcInterruptEnable
  hub->op_regs.HcInterruptEnable  = OHCI_INTR_MIE;

  // HcHCCA
  hub->op_regs.HcHCCA             = 0x00000000;

  // HcPeriodCurrentED
  hub->op_regs.HcPeriodCurrentED  = 0x00000000;

  // HcControlHeadED
  hub->op_regs.HcControlHeadED    = 0x00000000;

  // HcControlCurrentED
  hub->op_regs.HcControlCurrentED = 0x00000000;

  // HcBulkHeadED
  hub->op_regs.HcBulkHeadED       = 0x00000000;

  // HcBulkCurrentED
  hub->op_regs.HcBulkCurrentED    = 0x00000000;

  // HcDoneHead
  hub->op_regs.HcDoneHead         = 0x00000000;

  // HcFmInterval
  hub->op_regs.HcFmInterval.fit      =      0;
  hub->op_regs.HcFmInterval.fsmps    =      0;
  hub->op_regs.HcFmInterval.reserved =      0;
  hub->op_regs.HcFmInterval.fi       = 0x2EDF;

  // HcFmRemaining
  hub->op_regs.HcFmRemainingToggle   =      0;

  // HcFmNumber
  hub->op_regs.HcFmNumber         = 0x00000000;

  // HcPeriodicStart
  hub->op_regs.HcPeriodicStart    = 0x00000000;

  // HcLSThreshold
  hub->op_regs.HcLSThreshold      = 0x0628;

  // HcRhDescriptorA
  hub->op_regs.HcRhDescriptorA.potpgt   = 0x10;
  hub->op_regs.HcRhDescriptorA.reserved =    0;
  hub->op_regs.HcRhDescriptorA.nocp     =    0;
  hub->op_regs.HcRhDescriptorA.ocpm     =    1;
  hub->op_regs.HcRhDescriptorA.dt       =    0;
  hub->op_regs.HcRhDescriptorA.nps      =    0;
  hub->op_regs.HcRhDescriptorA.psm      =    1;
  hub->op_regs.HcRhDescriptorA.ndp      =    USB_OHCI_PORTS;

  // HcRhDescriptorB
  hub->op_regs.HcRhDescriptorB.ppcm     = ((1 << USB_OHCI_PORTS) - 1) << 1;
  hub->op_regs.HcRhDescriptorB.dr       = 0x0000;

  // HcRhStatus
  hub->op_regs.HcRhStatus.crwe      = 0;
  hub->op_regs.HcRhStatus.reserved0 = 0;
  hub->op_regs.HcRhStatus.ocic      = 0;
  hub->op_regs.HcRhStatus.lpsc      = 0;
  hub->op_regs.HcRhStatus.drwe      = 0;
  hub->op_regs.HcRhStatus.reserved1 = 0;
  hub->op_regs.HcRhStatus.oci       = 0;
  hub->op_regs.HcRhStatus.lps       = 0;
  
  for (int i=0; i<USB_OHCI_PORTS; i++) {
    usb_ohci_reset_port(hub, i);
    if (hub->usb_port[i].device != NULL) {
      usb_ohci_set_connect_status(hub, i, 1);
    }
  }
  
  while (hub->packets != NULL) {
    usb_cancel_packet(&hub->packets->packet);
    remove_async_packet(&hub->packets, hub->packets);
  }
}

void usb_ohci_reset(void* priv)
{
  bx_ohci_core_t* hub = (void*)priv;
  mem_mapping_disable(&hub->ohci_mmio_mapping);
  hub->ohci_enable = 0;
  // reset locals
  hub->ohci_done_count = 7;

  // HcRevision
  hub->op_regs.HcRevision         = 0x0110;

  // HcControl
  hub->op_regs.HcControl.reserved  =          0;
  hub->op_regs.HcControl.rwe       =          0;
  hub->op_regs.HcControl.rwc       =          0;
  hub->op_regs.HcControl.ir        =          0;
  hub->op_regs.HcControl.hcfs      =          OHCI_USB_RESET;
  hub->op_regs.HcControl.ble       =          0;
  hub->op_regs.HcControl.cle       =          0;
  hub->op_regs.HcControl.ie        =          0;
  hub->op_regs.HcControl.ple       =          0;
  hub->op_regs.HcControl.cbsr      =          0;

  // HcCommandStatus
  hub->op_regs.HcCommandStatus.reserved0 = 0x000000;
  hub->op_regs.HcCommandStatus.soc       =        0;
  hub->op_regs.HcCommandStatus.reserved1 = 0x000000;
  hub->op_regs.HcCommandStatus.ocr       =        0;
  hub->op_regs.HcCommandStatus.blf       =        0;
  hub->op_regs.HcCommandStatus.clf       =        0;
  hub->op_regs.HcCommandStatus.hcr       =        0;

  // HcInterruptStatus
  hub->op_regs.HcInterruptStatus  = 0x00000000;

  // HcInterruptEnable
  hub->op_regs.HcInterruptEnable  = OHCI_INTR_MIE;

  // HcHCCA
  hub->op_regs.HcHCCA             = 0x00000000;

  // HcPeriodCurrentED
  hub->op_regs.HcPeriodCurrentED  = 0x00000000;

  // HcControlHeadED
  hub->op_regs.HcControlHeadED    = 0x00000000;

  // HcControlCurrentED
  hub->op_regs.HcControlCurrentED = 0x00000000;

  // HcBulkHeadED
  hub->op_regs.HcBulkHeadED       = 0x00000000;

  // HcBulkCurrentED
  hub->op_regs.HcBulkCurrentED    = 0x00000000;

  // HcDoneHead
  hub->op_regs.HcDoneHead         = 0x00000000;

  // HcFmInterval
  hub->op_regs.HcFmInterval.fit      =      0;
  hub->op_regs.HcFmInterval.fsmps    =      0;
  hub->op_regs.HcFmInterval.reserved =      0;
  hub->op_regs.HcFmInterval.fi       = 0x2EDF;

  // HcFmRemaining
  hub->op_regs.HcFmRemainingToggle   =      0;

  // HcFmNumber
  hub->op_regs.HcFmNumber         = 0x00000000;

  // HcPeriodicStart
  hub->op_regs.HcPeriodicStart    = 0x00000000;

  // HcLSThreshold
  hub->op_regs.HcLSThreshold      = 0x0628;

  // HcRhDescriptorA
  hub->op_regs.HcRhDescriptorA.potpgt   = 0x10;
  hub->op_regs.HcRhDescriptorA.reserved =    0;
  hub->op_regs.HcRhDescriptorA.nocp     =    0;
  hub->op_regs.HcRhDescriptorA.ocpm     =    1;
  hub->op_regs.HcRhDescriptorA.dt       =    0;
  hub->op_regs.HcRhDescriptorA.nps      =    0;
  hub->op_regs.HcRhDescriptorA.psm      =    1;
  hub->op_regs.HcRhDescriptorA.ndp      =    USB_OHCI_PORTS;

  // HcRhDescriptorB
  hub->op_regs.HcRhDescriptorB.ppcm     = ((1 << USB_OHCI_PORTS) - 1) << 1;
  hub->op_regs.HcRhDescriptorB.dr       = 0x0000;

  // HcRhStatus
  hub->op_regs.HcRhStatus.crwe      = 0;
  hub->op_regs.HcRhStatus.reserved0 = 0;
  hub->op_regs.HcRhStatus.ocic      = 0;
  hub->op_regs.HcRhStatus.lpsc      = 0;
  hub->op_regs.HcRhStatus.drwe      = 0;
  hub->op_regs.HcRhStatus.reserved1 = 0;
  hub->op_regs.HcRhStatus.oci       = 0;
  hub->op_regs.HcRhStatus.lps       = 0;
  
  for (int i=0; i<USB_OHCI_PORTS; i++) {
    usb_ohci_reset_port(hub, i);
    if (hub->usb_port[i].device != NULL) {
      usb_ohci_set_connect_status(hub, i, 1);
    }
  }
  
  while (hub->packets != NULL) {
    usb_cancel_packet(&hub->packets->packet);
    remove_async_packet(&hub->packets, hub->packets);
  }
}

uint32_t usb_ohci_get_frame_remaining(bx_ohci_core_t* hub)
{
  uint32_t bit_time = (1000 - (timer_get_remaining_us(&hub->timer))) * 12;
  uint16_t fr;

#ifdef USE_DYNAREC
            if (cpu_use_dynarec)
                update_tsc();
#endif
  if ((hub->op_regs.HcControl.hcfs != OHCI_USB_OPERATIONAL) ||
      (bit_time > hub->op_regs.HcFmInterval.fi)) {
    fr = 0;
  } else {
    fr = hub->op_regs.HcFmInterval.fi - bit_time;
  }
  return (hub->op_regs.HcFmRemainingToggle << 31) | fr;
}

uint32_t usb_ohci_mem_read(uint32_t addr, void *priv)
{
  uint32_t offset = addr & 0xFFF;
  uint32_t val = ~0u;
  uint32_t p = 0;
  bx_ohci_core_t* hub = priv;

  if (addr & 3)
    return val;

  switch (offset) {
    case 0x00: // HcRevision
      val = hub->op_regs.HcRevision;
      break;

    case 0x04: // HcControl
      val =   (hub->op_regs.HcControl.reserved     << 11)
            | (hub->op_regs.HcControl.rwe      ? 1 << 10 : 0)
            | (hub->op_regs.HcControl.rwc      ? 1 << 9 : 0)
            | (hub->op_regs.HcControl.ir       ? 1 << 8 : 0)
            | (hub->op_regs.HcControl.hcfs         << 6)
            | (hub->op_regs.HcControl.ble      ? 1 << 5 : 0)
            | (hub->op_regs.HcControl.cle      ? 1 << 4 : 0)
            | (hub->op_regs.HcControl.ie       ? 1 << 3 : 0)
            | (hub->op_regs.HcControl.ple      ? 1 << 2 : 0)
            | (hub->op_regs.HcControl.cbsr         << 0);
      break;

    case 0x08: // HcCommandStatus
      val =   (hub->op_regs.HcCommandStatus.reserved0     << 18)
            | (hub->op_regs.HcCommandStatus.soc           << 16)
            | (hub->op_regs.HcCommandStatus.reserved1     << 4)
            | (hub->op_regs.HcCommandStatus.ocr       ? 1 << 3 : 0)
            | (hub->op_regs.HcCommandStatus.blf       ? 1 << 2 : 0)
            | (hub->op_regs.HcCommandStatus.clf       ? 1 << 1 : 0)
            | (hub->op_regs.HcCommandStatus.hcr       ? 1 << 0 : 0);
      break;

    case 0x0C: // HcInterruptStatus
      val = hub->op_regs.HcInterruptStatus;
      break;

    case 0x10: // HcInterruptEnable
    case 0x14: // HcInterruptDisable (reading this one returns that one)
      val = hub->op_regs.HcInterruptEnable;
      break;

    case 0x18: // HcHCCA
      val = hub->op_regs.HcHCCA;
      break;

    case 0x1C: // HcPeriodCurrentED
      val = hub->op_regs.HcPeriodCurrentED;
      break;

    case 0x20: // HcControlHeadED
      val = hub->op_regs.HcControlHeadED;
      break;

    case 0x24: // HcControlCurrentED
      val = hub->op_regs.HcControlCurrentED;
      break;

    case 0x28: // HcBulkHeadED
      val = hub->op_regs.HcBulkHeadED;
      break;

    case 0x2C: // HcBulkCurrentED
      val = hub->op_regs.HcBulkCurrentED;
      break;

    case 0x30: // HcDoneHead
      val = hub->op_regs.HcDoneHead;
      break;

    case 0x34: // HcFmInterval
      val =   (hub->op_regs.HcFmInterval.fit      ? 1 << 31 : 0)
            | (hub->op_regs.HcFmInterval.fsmps        << 16)
            | (hub->op_regs.HcFmInterval.reserved     << 14)
            | (hub->op_regs.HcFmInterval.fi           << 0);
      break;

    case 0x38: // HcFmRemaining
      val = usb_ohci_get_frame_remaining(hub);
      break;

    case 0x3C: // HcFmNumber
      val = hub->op_regs.HcFmNumber;
      break;

    case 0x40: // HcPeriodicStart
      val = hub->op_regs.HcPeriodicStart;
      break;

    case 0x44: // HcLSThreshold
      val = hub->op_regs.HcLSThreshold;
      break;

    case 0x48: // HcRhDescriptorA
      val =   (hub->op_regs.HcRhDescriptorA.potpgt       << 24)
            | (hub->op_regs.HcRhDescriptorA.reserved     << 13)
            | (hub->op_regs.HcRhDescriptorA.nocp     ? 1 << 12 : 0)
            | (hub->op_regs.HcRhDescriptorA.ocpm     ? 1 << 11 : 0)
            | 0 //hub->op_regs.HcRhDescriptorA.dt       << 10
            | (hub->op_regs.HcRhDescriptorA.nps      ? 1 <<  9 : 0)
            | (hub->op_regs.HcRhDescriptorA.psm      ? 1 <<  8 : 0)
            | (hub->op_regs.HcRhDescriptorA.ndp          <<  0);
      break;

    case 0x4C: // HcRhDescriptorB
      val =   (hub->op_regs.HcRhDescriptorB.ppcm << 16)
            | (hub->op_regs.HcRhDescriptorB.dr   << 0);
      break;

    case 0x50: // HcRhStatus
      val =   (hub->op_regs.HcRhStatus.crwe      ? 1 << 31 : 0)
            | (hub->op_regs.HcRhStatus.reserved0     << 18)
            | (hub->op_regs.HcRhStatus.ocic      ? 1 << 17 : 0)
            | 0 //hub->op_regs.HcRhStatus.lpsc      << 16
            | (hub->op_regs.HcRhStatus.drwe      ? 1 << 15 : 0)
            | (hub->op_regs.HcRhStatus.reserved1     <<  2)
            | (hub->op_regs.HcRhStatus.oci       ? 1 <<  1 : 0)
            | (hub->op_regs.HcRhStatus.lps       ? 1 <<  0 : 0);
      break;

    case 0x60: // HcRhPortStatus[3]
#if (USB_OHCI_PORTS < 4)
      val = 0;
      break;
#endif
    case 0x5C: // HcRhPortStatus[2]
#if (USB_OHCI_PORTS < 3)
      val = 0;
      break;
#endif
    case 0x58: // HcRhPortStatus[1]
#if (USB_OHCI_PORTS < 2)
      val = 0;
      break;
#endif
    case 0x54: // HcRhPortStatus[0]
      p = (offset - 0x54) >> 2;
      val =   (hub->usb_port[p].HcRhPortStatus.reserved0  << 21)
            | (hub->usb_port[p].HcRhPortStatus.prsc      ? (1 << 20) : 0)
            | (hub->usb_port[p].HcRhPortStatus.ocic      ? (1 << 19) : 0)
            | (hub->usb_port[p].HcRhPortStatus.pssc      ? (1 << 18) : 0)
            | (hub->usb_port[p].HcRhPortStatus.pesc      ? (1 << 17) : 0)
            | (hub->usb_port[p].HcRhPortStatus.csc       ? (1 << 16) : 0)
            | (hub->usb_port[p].HcRhPortStatus.reserved1     << 10)
            | (hub->usb_port[p].HcRhPortStatus.lsda      ? (1 <<  9) : 0)
            | (hub->usb_port[p].HcRhPortStatus.pps       ? (1 <<  8) : 0)
            | (hub->usb_port[p].HcRhPortStatus.reserved2     <<  5)
            | (hub->usb_port[p].HcRhPortStatus.prs       ? (1 <<  4) : 0)
            | (hub->usb_port[p].HcRhPortStatus.poci      ? (1 <<  3) : 0)
            | (hub->usb_port[p].HcRhPortStatus.pss       ? (1 <<  2) : 0)
            | (hub->usb_port[p].HcRhPortStatus.pes       ? (1 <<  1) : 0)
            | (hub->usb_port[p].HcRhPortStatus.ccs       ? (1 <<  0) : 0);
      break;

    case 0x100: {
      val = (hub->op_regs.HceControl & 0xFF) | ((!!mem_a20_key) << 8);
      break;
    }

    case 0x104: {
      val = hub->op_regs.HceInput;
      break;
    }

    case 0x108: {
      val = hub->op_regs.HceOutput;
      break;
    }

    case 0x10C: {
      val = hub->op_regs.HceStatus;
      break;
    }

    default:
      BX_ERROR(("unsupported read from address=0x%08X!", (uint32_t)addr));
      break;
  }

  ohci_log("OHCI: Register read from addr 0x%08X, ret 0x%08X\n", addr, val);
  return val;
}

void usb_ohci_mem_writeb(uint32_t addr, uint8_t val, void *priv)
{
}

uint8_t usb_ohci_mem_readb(uint32_t addr, void *priv)
{
  return usb_ohci_mem_read(addr & ~3, priv) >> ((addr & 3) * 8);
}

void usb_ohci_mem_write(uint32_t addr, uint32_t value, void* priv)
{
  bx_ohci_core_t* hub = priv;
  uint32_t offset = addr & 0xFFF;
  int p, org_state;

  int name = offset >> 2;
  if (name > (0x60 >> 2))
    name = 25;

  if (addr & 3) {
    BX_INFO(("Misaligned write at 0x%08X", (uint32_t)addr));
    return;
  }
  ohci_log("OHCI: Write to addr 0x%X val 0x%X\n", addr, value);

  switch (offset) {
    case 0x00: // HcRevision
      BX_ERROR(("Write to HcRevision ignored"));
      break;

    case 0x04: // HcControl
      if (value & 0xFFFFF800)
        BX_ERROR(("Write to reserved field in HcControl"));
      org_state = hub->op_regs.HcControl.hcfs;
      hub->op_regs.HcControl.rwe      = (value & (1<<10)) ? 1 : 0;
      hub->op_regs.HcControl.rwc      = (value & (1<< 9)) ? 1 : 0;
      hub->op_regs.HcControl.ir       = (value & (1<< 8)) ? 1 : 0;
      hub->op_regs.HcControl.hcfs     = (value & (3<< 6)) >>  6;
      hub->op_regs.HcControl.ble      = (value & (1<< 5)) ? 1 : 0;
      hub->op_regs.HcControl.cle      = (value & (1<< 4)) ? 1 : 0;
      hub->op_regs.HcControl.ie       = (value & (1<< 3)) ? 1 : 0;
      hub->op_regs.HcControl.ple      = (value & (1<< 2)) ? 1 : 0;
      hub->op_regs.HcControl.cbsr     = (value & (3<< 0)) >>  0;
      if (hub->op_regs.HcControl.hcfs == OHCI_USB_OPERATIONAL) {
        hub->op_regs.HcFmRemainingToggle = 0;
        if (org_state != OHCI_USB_OPERATIONAL)
          hub->use_control_head = hub->use_bulk_head = 1;
      }
      break;

    case 0x08: // HcCommandStatus
      if (value & 0xFFFCFFF0)
        BX_ERROR(("Write to a reserved field in HcCommandStatus"));
      if (value & (3<<16))
        BX_ERROR(("Write to R/O field: HcCommandStatus.soc"));
      if (value & (1<< 3)) {
        hub->op_regs.HcCommandStatus.ocr = 1;
        hub->op_regs.HcInterruptStatus |= 0x40000000;
        if ((hub->op_regs.HcInterruptEnable & 0xC0000000) == 0xC0000000) {
          ohci_log("Assert SMI#\n");
          /* At least one SiS BIOS leave InterruptRouting on but disable USB SMI# requests.
             Turn it off in that case. */
          if (hub->test_reg_enable && !(*hub->test_reg_enable & 0x10) && hub->op_regs.HcControl.ir) {
            hub->op_regs.HcControl.ir = false;
          }
          /* FIXME: Figure out how is USB handed off from SMI on SiS 5600 chipsets. ACPI can be disabled on some of those as well */
          else if (!strcmp(machine_get_internal_name(), "p6f99"))
          {
            /* Do nothing for now*/
          }
          else if (hub->do_smi_ocr_raise && hub->card_priv)
            hub->do_smi_ocr_raise(hub->card_priv);
          else if (hub->do_smi_raise && hub->card_priv)
            hub->do_smi_raise(hub->card_priv);
          else
            smi_raise();
        }
      }
      if (value & (1<< 2)) hub->op_regs.HcCommandStatus.blf = 1;
      if (value & (1<< 1)) hub->op_regs.HcCommandStatus.clf = 1;
      if (value & (1<< 0)) {
        hub->op_regs.HcCommandStatus.hcr = 1;

        usb_ohci_reset_soft(hub);
        mem_mapping_enable(&hub->ohci_mmio_mapping);
        hub->op_regs.HcControl.hcfs = OHCI_USB_SUSPEND;
        for (unsigned i=0; i<USB_OHCI_PORTS; i++)
          if (hub->usb_port[i].HcRhPortStatus.ccs && (hub->usb_port[i].device != NULL))
            usb_device_send_msg(hub->usb_port[i].device, USB_MSG_RESET);
      }
      break;

    case 0x0C: // HcInterruptStatus /// all are WC
      if (value & 0xBFFFFF80)
        BX_DEBUG(("Write to a reserved field in HcInterruptStatus"));
      hub->op_regs.HcInterruptStatus &= ~value;
      usb_ohci_update_irq(hub);
      break;

    case 0x10: // HcInterruptEnable
      if (value & 0x3FFFFF80)
        BX_ERROR(("Write to a reserved field in HcInterruptEnable"));
      hub->op_regs.HcInterruptEnable |= (value & 0xC000007F);
      usb_ohci_update_irq(hub);
      break;

    case 0x14: // HcInterruptDisable
      if (value & 0x3FFFFF80)
        BX_ERROR(("Write to a reserved field in HcInterruptDisable"));
      hub->op_regs.HcInterruptEnable &= ~value;
      usb_ohci_update_irq(hub);
      break;

    case 0x18: // HcHCCA
      // the HCD can write 0xFFFFFFFF to this register to see what the alignement is
      //  by reading back the amount and seeing how many lower bits are clear.
      if ((value & 0x000000FF) && (value != 0xFFFFFFFF))
        BX_ERROR(("Write to lower byte of HcHCCA non zero."));
      hub->op_regs.HcHCCA = (value & 0xFFFFFF00);
      break;

    case 0x1C: // HcPeriodCurrentED
      BX_ERROR(("Write to HcPeriodCurrentED not allowed."));
      break;

    case 0x20: // HcControlHeadED
      if (value & 0x0000000F)
        BX_ERROR(("Write to lower nibble of HcControlHeadED non zero."));
      hub->op_regs.HcControlHeadED = (value & 0xFFFFFFF0);
      break;

    case 0x24: // HcControlCurrentED
      if (value & 0x0000000F)
        BX_ERROR(("Write to lower nibble of HcControlCurrentED non zero."));
      hub->op_regs.HcControlCurrentED = (value & 0xFFFFFFF0);
      break;

    case 0x28: // HcBulkHeadED
      if (value & 0x0000000F)
        BX_ERROR(("Write to lower nibble of HcBulkHeadED non zero."));
      hub->op_regs.HcBulkHeadED = (value & 0xFFFFFFF0);
      break;

    case 0x2C: // HcBulkCurrentED
      if (value & 0x0000000F)
        BX_ERROR(("Write to lower nibble of HcBulkCurrentED non zero."));
      hub->op_regs.HcBulkCurrentED = (value & 0xFFFFFFF0);
      break;

    case 0x30: // HcDoneHead
      BX_ERROR(("Write to HcDoneHead not allowed."));
      break;

    case 0x34: // HcFmInterval
      if (value & 0x0000C000)
        BX_ERROR(("Write to a reserved field in HcFmInterval."));
      hub->op_regs.HcFmInterval.fit      = (value & (1<<31)) ? 1 : 0;
      hub->op_regs.HcFmInterval.fsmps    = (value & 0x7FFF0000) >> 16;
      hub->op_regs.HcFmInterval.fi       = (value & 0x00003FFF) >> 0;
      break;

    case 0x38: // HcFmRemaining
      BX_ERROR(("Write to HcFmRemaining not allowed."));
      break;

    case 0x3C: // HcFmNumber
      BX_ERROR(("Write to HcFmNumber not allowed."));
      break;

    case 0x40: // HcPeriodicStart
      if (value & 0xFFFFC000)
        BX_ERROR(("Write to a reserved field in HcPeriodicStart."));
      hub->op_regs.HcPeriodicStart = (value & 0x00003FFF);
      break;

    case 0x44: // HcLSThreshold
      hub->op_regs.HcLSThreshold = (value & 0x00000FFF);
      break;

    case 0x48: // HcRhDescriptorA
      if (value & 0x00FFE000)
        BX_ERROR(("Write to a reserved field in HcRhDescriptorA."));
      if ((value & 0x000000FF) != hub->op_regs.HcRhDescriptorA.ndp)
        BX_ERROR(("Write to HcRhDescriptorA.ndp not allowed."));
      if (value & (1<<10))
        BX_ERROR(("Write to HcRhDescriptorA.dt not allowed."));
      hub->op_regs.HcRhDescriptorA.potpgt   = (value & 0xFF000000) >> 24;
      hub->op_regs.HcRhDescriptorA.nocp     = (value & (1<<12)) ? 1 : 0;
      hub->op_regs.HcRhDescriptorA.ocpm     = (value & (1<<11)) ? 1 : 0;
      hub->op_regs.HcRhDescriptorA.nps      = (value & (1<< 9)) ? 1 : 0;
      hub->op_regs.HcRhDescriptorA.psm      = (value & (1<< 8)) ? 1 : 0;
      if (hub->op_regs.HcRhDescriptorA.nps == 0) {  // psm is only valid if nps == 0
        if (hub->op_regs.HcRhDescriptorA.psm == 0) {
          
          BX_INFO(("Ben: hub->op_regs.HcRhDescriptorA.psm == 0"));
          // all ports have power, etc.
          // hub->usb_port[p].HcRhPortStatus.pps = 1
          //  Call a routine to set each ports dword (LS, Connected, etc.)
          
        } else {
          
          BX_INFO(("Ben: hub->op_regs.HcRhDescriptorA.psm == 1"));
          // only ports with bit set in rhstatus have power, etc.
          //  Call a routine to set each ports dword (LS, Connected, etc.)
          
        }
      }
      break;

    case 0x4C: // HcRhDescriptorB
      hub->op_regs.HcRhDescriptorB.ppcm = (value & 0xFFFF0000) >> 16;
      hub->op_regs.HcRhDescriptorB.dr   = (value & 0x0000FFFF) >>  0;
      break;

    case 0x50: { // HcRhStatus
      if (value & 0x7FFC7FFC)
        BX_ERROR(("Write to a reserved field in HcRhStatus."));
      if (value & (1<<1))
        BX_ERROR(("Write to HcRhStatus.oci not allowed."));
      // which one of these two takes presidence?
      if (value & (1<<31)) hub->op_regs.HcRhStatus.drwe = 0;
      if (value & (1<<15)) hub->op_regs.HcRhStatus.drwe = 1;

      if (value & (1<<17)) hub->op_regs.HcRhStatus.ocic = 1;
      if (hub->op_regs.HcRhDescriptorA.nps == 0) {  // psm is only valid if nps == 0
        if (value & (1<<16)) {
          if (hub->op_regs.HcRhDescriptorA.psm == 0) {
            for (p=0; p<USB_OHCI_PORTS; p++)
              hub->usb_port[p].HcRhPortStatus.pps = 1;
          } else {
            for (p=0; p<USB_OHCI_PORTS; p++)
              if ((hub->op_regs.HcRhDescriptorB.ppcm & (1<<p)) == 0)
                hub->usb_port[p].HcRhPortStatus.pps = 1;
          }
        }
        if (value & (1<<0)) {
          if (hub->op_regs.HcRhDescriptorA.psm == 0) {
            for (p=0; p<USB_OHCI_PORTS; p++)
              hub->usb_port[p].HcRhPortStatus.pps = 0;
          } else {
            for (p=0; p<USB_OHCI_PORTS; p++)
              if (!(hub->op_regs.HcRhDescriptorB.ppcm & (1<<p)))
                hub->usb_port[p].HcRhPortStatus.pps = 0;
          }
        }
      }
      break;
    }

    case 0x60: // HcRhPortStatus[3]
#if (USB_OHCI_PORTS < 4)
  #if BX_USE_WIN32USBDEBUG
      win32_usb_trigger(USB_DEBUG_OHCI, USB_DEBUG_NONEXIST, 0, 0);
  #endif
      break;
#endif
    case 0x5C: // HcRhPortStatus[2]
#if (USB_OHCI_PORTS < 3)
  #if BX_USE_WIN32USBDEBUG
      win32_usb_trigger(USB_DEBUG_OHCI, USB_DEBUG_NONEXIST, 0, 0);
  #endif
      break;
#endif
    case 0x58: // HcRhPortStatus[1]
#if (USB_OHCI_PORTS < 2)
  #if BX_USE_WIN32USBDEBUG
      win32_usb_trigger(USB_DEBUG_OHCI, USB_DEBUG_NONEXIST, 0, 0);
  #endif
      break;
#endif
    case 0x54: { // HcRhPortStatus[0]
      p = (offset - 0x54) >> 2;
      if (value & 0xFFE0FCE0)
        BX_ERROR(("Write to a reserved field in usb_port[%d].HcRhPortStatus", p));
      if (value & (1<<0))
        hub->usb_port[p].HcRhPortStatus.pes = 0;
      if (value & (1<<1)) {
        if (hub->usb_port[p].HcRhPortStatus.ccs == 0)
          hub->usb_port[p].HcRhPortStatus.csc = 1;
        else {
#if BX_USE_WIN32USBDEBUG
          win32_usb_trigger(USB_DEBUG_OHCI, USB_DEBUG_ENABLE, 0, 0);
#endif
          hub->usb_port[p].HcRhPortStatus.pes = 1;
        }
      }
      if (value & (1<<2)) {
        if (hub->usb_port[p].HcRhPortStatus.ccs == 0)
          hub->usb_port[p].HcRhPortStatus.csc = 1;
        else
          hub->usb_port[p].HcRhPortStatus.pss = 1;
      }
//      if (value & (1<<3))
//        if (hub->usb_port[p].HcRhPortStatus.pss)
//          ; // do a resume (or test this in the timer code and do the resume there)
      if (value & (1<<4)) {
        if (hub->usb_port[p].HcRhPortStatus.ccs == 0)
          hub->usb_port[p].HcRhPortStatus.csc = 1;
        else {
#if BX_USE_WIN32USBDEBUG
          win32_usb_trigger(USB_DEBUG_OHCI, USB_DEBUG_RESET, 0, 0);
#endif
          usb_ohci_reset_port(hub, p);
          hub->usb_port[p].HcRhPortStatus.pps = 1;
          hub->usb_port[p].HcRhPortStatus.pes = 1;
          hub->usb_port[p].HcRhPortStatus.prsc = 1;
          // are we are currently connected/disconnected
          if (hub->usb_port[p].device != NULL) {
            hub->usb_port[p].HcRhPortStatus.lsda =
              (hub->usb_port[p].device->speed == USB_SPEED_LOW);
            usb_ohci_set_connect_status(hub, p, 1);
            usb_device_send_msg(hub->usb_port[p].device, USB_MSG_RESET);
          }
          usb_ohci_set_interrupt(hub, OHCI_INTR_RHSC);
        }
      }
      if (value & (1<<8))
        hub->usb_port[p].HcRhPortStatus.pps = 1;
      if (value & (1<<9))
        hub->usb_port[p].HcRhPortStatus.pps = 0;
      if (value & (1<<16))
        hub->usb_port[p].HcRhPortStatus.csc = (value & ((1<<4) | (1<<1) | (1<<2))) ? 1 : 0;
      if (value & (1<<17))
        hub->usb_port[p].HcRhPortStatus.pesc = 0;
      if (value & (1<<18))
        hub->usb_port[p].HcRhPortStatus.pssc = 0;
      if (value & (1<<19))
        hub->usb_port[p].HcRhPortStatus.ocic = 0;
      if (value & (1<<20))
        hub->usb_port[p].HcRhPortStatus.prsc = 0;
      break;
    }

    case 0x100: {
      hub->op_regs.HceControl = value;
      break;
    }

    case 0x104: {
      hub->op_regs.HceInput = value;
      break;
    }

    case 0x108: {
      hub->op_regs.HceOutput = value;
      break;
    }

    case 0x10C: {
      hub->op_regs.HceStatus = value;
      break;
    }

    default:
      BX_ERROR(("unsupported write to address=0x%08X, val = 0x%08X!", (uint32_t)addr, value));
      break;
  }
}

int
usb_ohci_event_handler(int event, void *ptr, void *priv, int port)
{
    int             ret = 0;
    USBAsync       *p;
    bx_ohci_core_t *hub = (bx_ohci_core_t *) priv;
  uint32_t intr = 0;

  switch (event) {
    // packet events start here
    case USB_EVENT_ASYNC:
      BX_DEBUG(("Async packet completion"));
      p = (USBAsync*)ptr;
      p->done = 1;
      usb_ohci_process_lists(hub);
      break;
    case USB_EVENT_WAKEUP:
      if (hub->usb_port[port].HcRhPortStatus.pss) {
        hub->usb_port[port].HcRhPortStatus.pss = 0;
        hub->usb_port[port].HcRhPortStatus.pssc = 1;
        intr = OHCI_INTR_RHSC;
      }
      if (hub->op_regs.HcControl.hcfs == OHCI_USB_SUSPEND) {
        hub->op_regs.HcControl.hcfs = OHCI_USB_RESUME;
        intr = OHCI_INTR_RD;
      }
      usb_ohci_set_interrupt(hub, intr);
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
      BX_ERROR(("unknown/unsupported event (id=%d) on port #%d", event, port+1));
      ret = -1; // unknown event, event not handled
  }

  return ret;
}

int usb_ohci_broadcast_packet(bx_ohci_core_t* hub, USBPacket *p)
{
  int ret = USB_RET_NODEV;
  for (int i = 0; i < USB_OHCI_PORTS && ret == USB_RET_NODEV; i++) {
    if ((hub->usb_port[i].device != NULL) &&
        (hub->usb_port[i].HcRhPortStatus.ccs)) {
      ret = hub->usb_port[i].device->handle_packet(hub->usb_port[i].device, p);
    }
  }
  return ret;
}

/* Read/Write the contents of an ISO TD from/to main memory.  */
static int ohci_copy_iso_td(uint32_t start_addr, uint32_t end_addr,
                            uint8_t *buf, int len, bool read)
{
    uint32_t ptr, n;

    ptr = start_addr;
    n = 0x1000 - (ptr & 0xfff);
    if (n > len) {
        n = len;
    }
    if (read)
      dma_bm_read(ptr, buf, n, 4);
    else
      dma_bm_write(ptr, buf, n, 4);

    if (n == len) {
        return 0;
    }
    ptr = end_addr & ~0xfffu;
    buf += n;
    if (read)
      dma_bm_read(ptr, buf, len - n, 4);
    else
      dma_bm_write(ptr, buf, len - n, 4);

    return 0;
}


#define OHCI_PAGE_MASK    0xfffff000
#define OHCI_OFFSET_MASK  0xfff
int usb_ohci_process_iso_td(bx_ohci_core_t* hub, struct OHCI_ISO_TD *td, struct OHCI_ED *ed)
{
  unsigned pid = 0, len = 0;
  int ret2 = 1;
  uint8_t buf[8192];
  int ilen, ret = 0;
  uint32_t addr;
  USBAsync *p;
  bool completion;
  uint16_t starting_frame;
  int16_t relative_frame_number;
  int frame_count;
  uint32_t start_offset, next_offset, end_offset = 0;
  uint32_t start_addr, end_addr;

  addr = ED_GET_HEADP(ed);

  if (addr == 0)
    return 1;

  p = find_async_packet(&hub->packets, addr);
  completion = (p != NULL);
  if (completion && !p->done) {
    return 1;
  }

  starting_frame = ISO_TD_GET_SF(td);
  frame_count = ISO_TD_GET_FC(td);
  relative_frame_number = (int16_t)((uint16_t)(hub->op_regs.HcFmNumber) - (uint16_t)(starting_frame));

  if (relative_frame_number < 0){
    ohci_log("Relative frame number < 0\n");
    return 1;
  }
  else if (relative_frame_number > frame_count) {
    const uint32_t temp = ED_GET_HEADP(ed);
    ohci_log("Relative frame number > Frame Count\n");
    if (ISO_TD_GET_CC(td) == DataOverrun)
      return 1;
    else {
      TD_SET_CC(td, DataOverrun);
      ED_SET_HEADP(ed, ISO_TD_GET_NEXTTD(td));
      TD_SET_NEXTTD(td, hub->op_regs.HcDoneHead);
      hub->op_regs.HcDoneHead = temp;
      if (ISO_TD_GET_DI(td) < hub->ohci_done_count)
        hub->ohci_done_count = ISO_TD_GET_DI(td);
      return 0;
    }
  }

  if (ED_GET_D(ed) == 1)
    pid = USB_TOKEN_OUT;
  else if (ED_GET_D(ed) == 2)
    pid = USB_TOKEN_IN;
  else if (ED_GET_D(ed) == 0 || ED_GET_D(ed) == 3)
    pid = USB_TOKEN_SETUP;
  else
    return 1;
  
  if (ISO_TD_GET_BE(td) == 0 || ISO_TD_GET_BP0(td) == 0) {
    ohci_log("Zero-ed BE/BP0\n");
    return 1;
  }
  
  start_offset = td->offset[relative_frame_number];
  if (relative_frame_number < frame_count) {
      next_offset = td->offset[relative_frame_number + 1];
  } else {
      next_offset = ISO_TD_GET_BE(td);
  }
  
  if (!((start_offset >> 12) & 0xe) ||
      ((relative_frame_number < frame_count) &&
       !((next_offset >> 12) & 0xe))) {
      ohci_log("NOTACCESSED\n");
      return 1;
  }

  if ((relative_frame_number < frame_count) && (start_offset > next_offset)) {
      ohci_log("NOTACCESSED (2)\n");
      return 1;
  }

  if ((start_offset & 0x1000) == 0) {
      start_addr = (td->dword1 & OHCI_PAGE_MASK) |
          (start_offset & OHCI_OFFSET_MASK);
  } else {
      start_addr = (td->dword2 & OHCI_PAGE_MASK) |
          (start_offset & OHCI_OFFSET_MASK);
  }

  if (relative_frame_number < frame_count) {
      end_offset = next_offset - 1;
      if ((end_offset & 0x1000) == 0) {
          end_addr = (td->dword1 & OHCI_PAGE_MASK) |
              (end_offset & OHCI_OFFSET_MASK);
      } else {
          end_addr = (td->dword2 & OHCI_PAGE_MASK) |
              (end_offset & OHCI_OFFSET_MASK);
      }
  } else {
      /* Last packet in the ISO TD */
      end_addr = next_offset;
  }

  if (start_addr > end_addr) {
      ohci_log("start_addr > end_addr\n");
      return 1;
  }

  if ((start_addr & OHCI_PAGE_MASK) != (end_addr & OHCI_PAGE_MASK)) {
      len = (end_addr & OHCI_OFFSET_MASK) + 0x1001
          - (start_addr & OHCI_OFFSET_MASK);
  } else {
      len = end_addr - start_addr + 1;
  }

  if (len > sizeof(buf)) {
      len = sizeof(buf);
  }

  if (len && pid != USB_TOKEN_IN) {
      ohci_copy_iso_td(start_addr, end_addr, buf, len, true);
  }

  if (completion) {
    ret = p->packet.len;
  } else {
    p = create_async_packet(&hub->packets, addr, len);
    if (pid != USB_TOKEN_IN)
      memcpy(p->packet.data, buf, len);
    p->packet.pid = pid;
    p->packet.devaddr = ED_GET_FA(ed);
    p->packet.devep = ED_GET_EN(ed);
    p->packet.speed = ED_GET_S(ed) ? USB_SPEED_LOW : USB_SPEED_FULL;
#if HANDLE_TOGGLE_CONTROL
    p->packet.toggle = ED_GET_C(ed) ^ (relative_frame_number & 1);
#endif
    p->packet.complete_cb = usb_ohci_event_handler;
    p->packet.complete_dev = hub;
    ret = usb_ohci_broadcast_packet(hub, &p->packet);

    if (ret == USB_RET_ASYNC)
      return 1;
    
    if (ret >= 0 && pid == USB_TOKEN_SETUP)
      ret = len = 8;
  }

      
  if (pid == USB_TOKEN_IN && ret >= 0 && ret <= len) {
    ohci_copy_iso_td(start_addr, end_addr, buf, len, false);
    
    td->offset[relative_frame_number] = (NoError << 12) | (len & 0xfff);
  } else if (pid == USB_TOKEN_OUT && ret == len) {
    td->offset[relative_frame_number] = (NoError << 12);
  } else {
    if (ret > len)
      td->offset[relative_frame_number] = (DataOverrun << 12) | (len & 0xfff);
    else if (ret >= 0) {
      td->offset[relative_frame_number] &= 0xFFF;
      td->offset[relative_frame_number] |= (DataUnderrun << 12);
    } else {
      td->offset[relative_frame_number] = (ret == USB_RET_IOERROR || ret == USB_RET_NODEV) ? DeviceNotResponding : Stall;
    }
  }
  
  if (relative_frame_number == frame_count) {
    const uint32_t temp = ED_GET_HEADP(ed);
    TD_SET_CC(td, NoError);
    ED_SET_HEADP(ed, ISO_TD_GET_NEXTTD(td));
    TD_SET_NEXTTD(td, hub->op_regs.HcDoneHead);
    hub->op_regs.HcDoneHead = temp;
    if (ISO_TD_GET_DI(td) < hub->ohci_done_count)
      hub->ohci_done_count = ISO_TD_GET_DI(td);
  }
  
  dma_bm_write(addr, (uint8_t*)td, sizeof(struct OHCI_ISO_TD), 4);
  remove_async_packet(&hub->packets, p);
  return 1;
}

int usb_ohci_process_td(bx_ohci_core_t* hub, struct OHCI_TD *td, struct OHCI_ED *ed, int toggle)
{
  unsigned pid = 0, len = 0, len1, len2;
  int ret2 = 1;
  int ilen, ret = 0;
  uint32_t addr;
  uint16_t maxlen = 0;
  USBAsync *p;
  bool completion;

  addr = ED_GET_HEADP(ed);
  p = find_async_packet(&hub->packets, addr);
  completion = (p != NULL);
  if (completion && !p->done) {
    return 0;
  }

#if BX_USE_WIN32USBDEBUG
  win32_usb_trigger(USB_DEBUG_OHCI, USB_DEBUG_COMMAND, 0, 0);
#endif

  // The td->cc field should be 111x if it hasn't been processed yet.
  if (TD_GET_CC(td) < NotAccessed) {
    BX_ERROR(("Found TD with CC value not 111x"));
    return 0;
  }

  if (ED_GET_D(ed) == 1)
    pid = USB_TOKEN_OUT;
  else if (ED_GET_D(ed) == 2)
    pid = USB_TOKEN_IN;
  else {
    if (TD_GET_DP(td) == 0)
      pid = USB_TOKEN_SETUP;
    else if (TD_GET_DP(td) == 1)
      pid = USB_TOKEN_OUT;
    else if (TD_GET_DP(td) == 2)
      pid = USB_TOKEN_IN;
  }

  // calculate the length of the packet
  if (TD_GET_CBP(td) && TD_GET_BE(td)) {
    if ((TD_GET_CBP(td) & 0xFFFFF000) != (TD_GET_BE(td) & 0xFFFFF000))
      len = (TD_GET_BE(td) & 0xFFF) + 0x1001 - (TD_GET_CBP(td) & 0xFFF);
    else {
      ilen = ((int) TD_GET_BE(td) - TD_GET_CBP(td)) + 1;
      if (ilen < 0)
        len = 0x1001 + len;
      else
        len = (unsigned) ilen;
    }
  } else
    len = 0;

  if (completion) {
    ret = p->packet.len;
  } else {
    switch (pid) {
      case USB_TOKEN_SETUP:
      case USB_TOKEN_OUT:
        maxlen = (len <= ED_GET_MPS(ed)) ? len : ED_GET_MPS(ed); // limit the data length the the max packet size
        break;
      case USB_TOKEN_IN:
        maxlen = len;
        break;
    }
    p = create_async_packet(&hub->packets, addr, maxlen);
    p->packet.pid = pid;
    p->packet.devaddr = ED_GET_FA(ed);
    p->packet.devep = ED_GET_EN(ed);
    p->packet.speed = ED_GET_S(ed) ? USB_SPEED_LOW : USB_SPEED_FULL;
#if HANDLE_TOGGLE_CONTROL
    p->packet.toggle = toggle;
#endif
    p->packet.complete_cb = usb_ohci_event_handler;
    p->packet.complete_dev = hub;

    BX_DEBUG(("    pid = %s  addr = %d  endpnt = %d  len = %d  mps = %d s = %d (td->cbp = 0x%08X, td->be = 0x%08X)",
      (pid == USB_TOKEN_IN)? "IN" : (pid == USB_TOKEN_OUT) ? "OUT" : (pid == USB_TOKEN_SETUP) ? "SETUP" : "UNKNOWN",
      ED_GET_FA(ed), ED_GET_EN(ed), maxlen, ED_GET_MPS(ed), ED_GET_S(ed), TD_GET_CBP(td), TD_GET_BE(td)));
    BX_DEBUG(("    td->t = %d  ed->c = %d  td->di = %d  td->r = %d", TD_GET_T(td), ED_GET_C(ed), TD_GET_DI(td), TD_GET_R(td)));

    switch (pid) {
      case USB_TOKEN_SETUP:
        if (maxlen > 0)
          DEV_MEM_READ_PHYSICAL(TD_GET_CBP(td), maxlen, p->packet.data);
        // TODO: This is a hack.  dev->handle_packet() should return the amount of bytes
        //  it received, not the amount it anticipates on receiving/sending in the next packet.
        if ((ret = usb_ohci_broadcast_packet(hub, &p->packet)) >= 0)
          ret = 8;
        break;
      case USB_TOKEN_OUT:
        if (maxlen > 0)
          DEV_MEM_READ_PHYSICAL(TD_GET_CBP(td), maxlen, p->packet.data);
        ret = usb_ohci_broadcast_packet(hub, &p->packet);
        break;
      case USB_TOKEN_IN:
        ret = usb_ohci_broadcast_packet(hub, &p->packet);
        break;
      default:
        TD_SET_CC(td, UnexpectedPID);
        TD_SET_EC(td, 3);
        return 1;
    }

    if (ret == USB_RET_ASYNC) {
      BX_DEBUG(("Async packet deferred"));
      return 0;
    }
  }
  
  if ((ret > 0) && (pid == USB_TOKEN_IN)) {
    if (((TD_GET_CBP(td) & 0xfff) + ret) > 0x1000) {
      len1 = 0x1000 - (TD_GET_CBP(td) & 0xfff);
      len2 = ret - len1;
      DEV_MEM_WRITE_PHYSICAL(TD_GET_CBP(td), len1, p->packet.data);
      DEV_MEM_WRITE_PHYSICAL((TD_GET_BE(td) & ~0xfff), len2, p->packet.data + len1);
    } else {
      DEV_MEM_WRITE_PHYSICAL(TD_GET_CBP(td), ret, p->packet.data);
    }
  }
  
  if ((ret == (int) len) || 
     ((pid == USB_TOKEN_IN) && (ret >= 0) && TD_GET_R(td)) || 
     ((pid == USB_TOKEN_OUT) && (ret >= 0) && (ret <= (int) ED_GET_MPS(ed)))) {
    if (ret == (int) len)
      TD_SET_CBP(td, 0);
    else {
      if (((TD_GET_CBP(td) & 0xfff) + ret) >= 0x1000) {
        TD_SET_CBP(td, (TD_GET_CBP(td) + ret) & 0x0FFF);
        TD_SET_CBP(td, TD_GET_CBP(td) | (TD_GET_BE(td) & ~0x0FFF));
      } else {
        TD_SET_CBP(td, TD_GET_CBP(td) + ret);
      }
    }
    if ((pid != USB_TOKEN_OUT) || (ret == (int) len)) {
      TD_SET_CC(td, NoError);
      TD_SET_EC(td, 0);
    }
  } else {
    if (ret >= 0) {
      TD_SET_CC(td, DataUnderrun);
      if (((TD_GET_CBP(td) & 0xfff) + ret) >= 0x1000) {
        TD_SET_CBP(td, (TD_GET_CBP(td) + ret) & 0x0FFF);
        TD_SET_CBP(td, TD_GET_CBP(td) | (TD_GET_BE(td) & ~0x0FFF));
      } else {
        TD_SET_CBP(td, TD_GET_CBP(td) + ret);
      }
      if (!TD_GET_R(td))
        ED_SET_H(ed, 1);
    } else {
      switch (ret) {
        case USB_RET_NODEV:  // (-1)
          TD_SET_CC(td, DeviceNotResponding);
          break;
        case USB_RET_NAK:    // (-2)
          break;
        case USB_RET_STALL:  // (-3)
          TD_SET_CC(td, Stall);
          break;
        case USB_RET_BABBLE:  // (-4)
          TD_SET_CC(td, BufferOverrun);
          break;
        default:
          BX_ERROR(("Unknown error returned: %d", ret));
          break;
      }
      ret2 = ret;
    }
    if (ret != USB_RET_NAK) {
      TD_SET_EC(td, 3);
      ED_SET_H(ed, 1);
    }
  }

  BX_DEBUG((" td->cbp = 0x%08X   ret = %d  len = %d  td->cc = %d   td->ec = %d  ed->h = %d", TD_GET_CBP(td), ret, maxlen, TD_GET_CC(td), TD_GET_EC(td), ED_GET_H(ed)));
  BX_DEBUG(("    td->t = %d  ed->c = %d", TD_GET_T(td), ED_GET_C(ed)));
  remove_async_packet(&hub->packets, p);

  return ret2;
}

bool usb_ohci_process_ed(bx_ohci_core_t* hub, struct OHCI_ED *ed, const uint32_t ed_address)
{
  struct OHCI_TD cur_td;
  struct OHCI_ISO_TD cur_iso_td;
  int toggle;
  bool ret = 0;

  if (!ED_GET_H(ed) && !ED_GET_K(ed) && (ED_GET_HEADP(ed) != ED_GET_TAILP(ed))) {
    // if the isochronous is enabled and ed is a isochronous, do TD
    if (ED_GET_F(ed)) {
      if (hub->op_regs.HcControl.ie) {
        // load and do a isochronous TD list
        ohci_log("Found a valid ED that points to an isochronous TD\n");
        while (!ED_GET_H(ed) && (ED_GET_HEADP(ed) != ED_GET_TAILP(ed))) {
          dma_bm_read(ED_GET_HEADP(ed), (uint8_t*)&cur_iso_td, sizeof(struct OHCI_ISO_TD), 4);
          if (usb_ohci_process_iso_td(hub, &cur_iso_td, ed))
            break;
        }
      }
    } else {
      BX_DEBUG(("Found a valid ED that points to an control/bulk/int TD"));
      ret = 1;
      while (!ED_GET_H(ed) && (ED_GET_HEADP(ed) != ED_GET_TAILP(ed))) {
        toggle = ED_GET_C(ed);
        DEV_MEM_READ_PHYSICAL(ED_GET_HEADP(ed),      4, (uint8_t*) &cur_td.dword0);
        DEV_MEM_READ_PHYSICAL(ED_GET_HEADP(ed) +  4, 4, (uint8_t*) &cur_td.dword1);
        DEV_MEM_READ_PHYSICAL(ED_GET_HEADP(ed) +  8, 4, (uint8_t*) &cur_td.dword2);
        DEV_MEM_READ_PHYSICAL(ED_GET_HEADP(ed) + 12, 4, (uint8_t*) &cur_td.dword3);
        BX_DEBUG(("Head: 0x%08X  Tail: 0x%08X  Next: 0x%08X", ED_GET_HEADP(ed), ED_GET_TAILP(ed), TD_GET_NEXTTD(&cur_td)));
        if (TD_GET_T(&cur_td) & 2)
          toggle = TD_GET_T(&cur_td) & 1;
        int td_ret = usb_ohci_process_td(hub, &cur_td, ed, toggle);
        if (td_ret == 0) {
          // USB_RET_ASYNC or already processed TD, so done with ED (for now)
          break;
        } else if (td_ret > 0) {
          // Processed TD with no error
          const uint32_t temp = ED_GET_HEADP(ed);
          if (TD_GET_CC(&cur_td) < NotAccessed) {
            ED_SET_HEADP(ed, TD_GET_NEXTTD(&cur_td));
            TD_SET_NEXTTD(&cur_td, hub->op_regs.HcDoneHead);
            hub->op_regs.HcDoneHead = temp;
            if (TD_GET_DI(&cur_td) < hub->ohci_done_count)
              hub->ohci_done_count = TD_GET_DI(&cur_td);
          }
          ED_SET_C(ed, toggle ^ 1);
          DEV_MEM_WRITE_PHYSICAL(temp,      4, (uint8_t*) &cur_td.dword0);
          DEV_MEM_WRITE_PHYSICAL(temp +  4, 4, (uint8_t*) &cur_td.dword1);
          DEV_MEM_WRITE_PHYSICAL(temp +  8, 4, (uint8_t*) &cur_td.dword2);
        } else {
          // Processed TD with error, advance the toggle anyway
          ED_SET_C(ed, toggle ^ 1);
          break;
        }
      }
    }
    DEV_MEM_WRITE_PHYSICAL(ed_address +  8, 4, (uint8_t*) &ed->dword2);
  }
  return ret;
}

void usb_ohci_process_lists(bx_ohci_core_t* hub)
{
  struct OHCI_ED cur_ed;

  // TODO:  Rather than just comparing .fr to < 8000 here, and < 4000 below, see the statement on 
  //   page 45 of the OHCI specs:
  // "When a new frame starts, the Host Controller processes control and bulk Endpoint Descriptors until 
  //  the Remaining field of the HcFmRemaining register is less than or equal to the Start field of the 
  //  HcPeriodicStart register"

  // if the control list is enabled *and* the control list filled bit is set, do a control list ED
  if (hub->op_regs.HcControl.cle) {
    if (hub->use_control_head) {
      hub->op_regs.HcControlCurrentED = 0;
      hub->use_control_head = 0;
    }
    if (!hub->op_regs.HcControlCurrentED && hub->op_regs.HcCommandStatus.clf) {
      hub->op_regs.HcControlCurrentED = hub->op_regs.HcControlHeadED;
      hub->op_regs.HcCommandStatus.clf = 0;
    }
    while (hub->op_regs.HcControlCurrentED) {
      DEV_MEM_READ_PHYSICAL(hub->op_regs.HcControlCurrentED,      4, (uint8_t*) &cur_ed.dword0);
      DEV_MEM_READ_PHYSICAL(hub->op_regs.HcControlCurrentED +  4, 4, (uint8_t*) &cur_ed.dword1);
      DEV_MEM_READ_PHYSICAL(hub->op_regs.HcControlCurrentED +  8, 4, (uint8_t*) &cur_ed.dword2);
      DEV_MEM_READ_PHYSICAL(hub->op_regs.HcControlCurrentED + 12, 4, (uint8_t*) &cur_ed.dword3);
      usb_ohci_process_ed(hub, &cur_ed, hub->op_regs.HcControlCurrentED);
      hub->op_regs.HcControlCurrentED = ED_GET_NEXTED(&cur_ed);
      //if (get_frame_remaining() < 8000)
      //  break;
    }
  }

  // if the bulk list is enabled *and* the bulk list filled bit is set, do a bulk list ED
  if (hub->op_regs.HcControl.ble) {
    if (hub->use_bulk_head) {
      hub->op_regs.HcBulkCurrentED = 0;
      hub->use_bulk_head = 0;
    }
    if (!hub->op_regs.HcBulkCurrentED && hub->op_regs.HcCommandStatus.blf) {
      hub->op_regs.HcBulkCurrentED = hub->op_regs.HcBulkHeadED;
      hub->op_regs.HcCommandStatus.blf = 0;
    }
    while (hub->op_regs.HcBulkCurrentED) {
      DEV_MEM_READ_PHYSICAL(hub->op_regs.HcBulkCurrentED,      4, (uint8_t*) &cur_ed.dword0);
      DEV_MEM_READ_PHYSICAL(hub->op_regs.HcBulkCurrentED +  4, 4, (uint8_t*) &cur_ed.dword1);
      DEV_MEM_READ_PHYSICAL(hub->op_regs.HcBulkCurrentED +  8, 4, (uint8_t*) &cur_ed.dword2);
      DEV_MEM_READ_PHYSICAL(hub->op_regs.HcBulkCurrentED + 12, 4, (uint8_t*) &cur_ed.dword3);
      if (usb_ohci_process_ed(hub, &cur_ed, hub->op_regs.HcBulkCurrentED)) {
        hub->op_regs.HcCommandStatus.blf = 1;
      }
      hub->op_regs.HcBulkCurrentED = ED_GET_NEXTED(&cur_ed);
      //if (get_frame_remaining() < 4000)
      //  break;
    }
  }
}

void usb_ohci_timer(void* priv)
{
  struct OHCI_ED cur_ed;
  uint32_t address, ed_address;
  uint16_t zero = 0;
  bx_ohci_core_t* hub = priv;

  timer_on_auto(&hub->timer, 1000);
  if (hub->op_regs.HcControl.hcfs == OHCI_USB_OPERATIONAL) {
#if BX_USE_WIN32USBDEBUG
    win32_usb_trigger(USB_DEBUG_OHCI, USB_DEBUG_FRAME, 0, 0);
#endif
    // set remaining to the interval amount.
    hub->op_regs.HcFmRemainingToggle = hub->op_regs.HcFmInterval.fit;

    // The Frame Number Register is incremented
    //  every time bit 15 is changed (at 0x8000 or 0x0000), fno is fired.
    hub->op_regs.HcFmNumber++;
    hub->op_regs.HcFmNumber &= 0xffff;
    DEV_MEM_WRITE_PHYSICAL(hub->op_regs.HcHCCA + 0x80, 2, (uint8_t *) &hub->op_regs.HcFmNumber);
    DEV_MEM_WRITE_PHYSICAL(hub->op_regs.HcHCCA + 0x82, 2, (uint8_t *) &zero);
    if ((hub->op_regs.HcFmNumber == 0x8000) || (hub->op_regs.HcFmNumber == 0x0000)) {
      usb_ohci_set_interrupt(hub, OHCI_INTR_FNO);
    }

    //
    usb_ohci_set_interrupt(hub, OHCI_INTR_SF);

    // if interrupt delay (done_count) == 0, and status.wdh == 0, then update the donehead fields.
    //BX_DEBUG(("done_count = %d, status.wdh = %d", hub->ohci_done_count,
    //          ((hub->op_regs.HcInterruptStatus & OHCI_INTR_WD) > 0)));
    if ((hub->ohci_done_count == 0) && ((hub->op_regs.HcInterruptStatus & OHCI_INTR_WD) == 0)) {
      uint32_t temp = hub->op_regs.HcDoneHead;
      if (hub->op_regs.HcInterruptStatus & hub->op_regs.HcInterruptEnable)
        temp |= 1;
      BX_DEBUG(("Updating the hcca.DoneHead field to 0x%08X and setting the wdh flag", temp));
      DEV_MEM_WRITE_PHYSICAL(hub->op_regs.HcHCCA + 0x84, 4, (uint8_t *) &temp);
      hub->op_regs.HcDoneHead = 0;
      hub->ohci_done_count = 7;
      usb_ohci_set_interrupt(hub, OHCI_INTR_WD);
    }

    // if (6 >= done_count > 0) then decrement done_count
    if ((hub->ohci_done_count != 7) && (hub->ohci_done_count > 0))
      hub->ohci_done_count--;

    usb_ohci_process_lists(hub);

    // do the ED's in the interrupt table
    if (hub->op_regs.HcControl.ple) {
      address = hub->op_regs.HcHCCA + ((hub->op_regs.HcFmNumber & 0x1F) * 4);
      DEV_MEM_READ_PHYSICAL(address, 4, (uint8_t*) &ed_address);
      while (ed_address) {
        DEV_MEM_READ_PHYSICAL(ed_address,      4, (uint8_t*) &cur_ed.dword0);
        DEV_MEM_READ_PHYSICAL(ed_address +  4, 4, (uint8_t*) &cur_ed.dword1);
        DEV_MEM_READ_PHYSICAL(ed_address +  8, 4, (uint8_t*) &cur_ed.dword2);
        DEV_MEM_READ_PHYSICAL(ed_address + 12, 4, (uint8_t*) &cur_ed.dword3);
        usb_ohci_process_ed(hub, &cur_ed, ed_address);
        ed_address = ED_GET_NEXTED(&cur_ed);
      }
    }

  }  // end run schedule

  /* Iterate through all ports and invoke SOF callbacks if those exist. */
  for (int i = 0; i < 2; i++) {
      if (hub->usb_port[i].device != NULL && hub->usb_port[i].device->sof_callback)
          hub->usb_port[i].device->sof_callback(hub->usb_port[i].device->priv);
  }
}

void usb_ohci_set_port_device(bx_ohci_core_t *hub, int port, usb_device_c *dev)
{
  usb_device_c *olddev = hub->usb_port[port].device;
  if ((dev != NULL) && (olddev == NULL)) {
    // make sure we are calling the correct handler for the device
    usb_device_set_event_handler(dev, hub, usb_ohci_event_handler, port);
    hub->usb_port[port].device = dev;
    usb_ohci_set_connect_status(hub, port, 1);
  } else if ((dev == NULL) && (olddev != NULL)) {
    usb_ohci_set_connect_status(hub, port, 0);
    hub->usb_port[port].device = dev;
  }
}

void
ohci_update_mem_mapping_new(void* priv, uint8_t base1, uint8_t base2, uint8_t base3, int enable)
{
  bx_ohci_core_t *dev = (bx_ohci_core_t*)priv;
    if (!dev)
        return;
    if (dev->ohci_enable && (dev->ohci_mem_base != 0x00000000))
        mem_mapping_disable(&dev->ohci_mmio_mapping);

    dev->ohci_mem_base = ((base1 << 8) | (base2 << 16) | (base3 << 24)) & 0xfffff000;
    dev->ohci_enable   = enable;

    if (dev->ohci_enable && (dev->ohci_mem_base != 0x00000000))
        mem_mapping_set_addr(&dev->ohci_mmio_mapping, dev->ohci_mem_base, 0x1000);
}

int ohci_port_is_free(usb_port_t* port)
{
  bx_ohci_core_t *dev = (bx_ohci_core_t*)port->priv;

  if (port->number >= 2)
    return 0;

  return !(dev->usb_port[port->number].device);
}

int ohci_port_connect(usb_port_t* port, usb_device_c* device)
{
    if (!ohci_port_is_free(port))
        return 0;

    usb_ohci_set_port_device(port->priv, port->number, device);
    return 1;
}

void ohci_register_usb(usb_t *dev)
{
  bx_ohci_core_t *hub = (bx_ohci_core_t*)dev->usb_ohci_priv;

  usb_register_port(0, hub, ohci_port_is_free, ohci_port_connect);
  usb_register_port(1, hub, ohci_port_is_free, ohci_port_connect);
}

void *
usb_ohci_init(UNUSED(const device_t *info))
{
    bx_ohci_core_t *hub = NULL;
    usb_params_t* usb_params = (void*)info->local;

    hub = (bx_ohci_core_t *) calloc(1, sizeof(bx_ohci_core_t));
    hub->ohci_done_count = 7;
    hub->use_control_head = 0;
    hub->use_bulk_head = 0;
    hub->sof_time = 0;
    if (usb_params) {
      hub->devfunc = usb_params->pci_dev;
      hub->pci_conf = usb_params->pci_conf;
      hub->do_smi_raise = usb_params->do_smi_raise;
      hub->do_smi_ocr_raise = usb_params->do_smi_ocr_raise;
      hub->card_priv = usb_params->priv;
      hub->do_pci_irq = usb_params->do_pci_irq;
      hub->test_reg_enable = usb_params->test_reg_enable;
    }
    timer_add(&hub->timer, usb_ohci_timer, hub, 0);
    timer_on_auto(&hub->timer, 1000);
    mem_mapping_add(&hub->ohci_mmio_mapping, 0, 0, usb_ohci_mem_readb, NULL, usb_ohci_mem_read, usb_ohci_mem_writeb, NULL, usb_ohci_mem_write, NULL, MEM_MAPPING_EXTERNAL, hub);
    usb_ohci_reset(hub);

    return hub;
}

static void
usb_ohci_close(void *priv)
{
  free(priv);
}

const device_t usb_ohci_device = {
    .name          = "Universal Serial Bus (OHCI)",
    .internal_name = "usb_ohci",
    .flags         = DEVICE_PCI,
    .local         = 0,
    .init          = usb_ohci_init,
    .close         = usb_ohci_close,
    .reset         = usb_ohci_reset,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
