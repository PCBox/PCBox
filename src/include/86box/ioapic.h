/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Definitions for the I/O APIC (Intel 82093AA) implementation.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#ifndef EMU_IOAPIC_H
#define EMU_IOAPIC_H

#include <stdint.h>

/* Default I/O APIC MMIO base address. */
#define IOAPIC_DEFAULT_BASE 0xFEC00000

/* MMIO register offsets within the I/O APIC page. */
#define IOAPIC_IOREGSEL 0x00 /* Index register (selects internal register). */
#define IOAPIC_IOWIN    0x10 /* Data window (read/write selected register). */

/* Internal register indices (written to IOREGSEL). */
#define IOAPIC_REG_ID    0x00 /* I/O APIC ID. */
#define IOAPIC_REG_VER   0x01 /* I/O APIC Version. */
#define IOAPIC_REG_ARB   0x02 /* I/O APIC Arbitration ID. */
#define IOAPIC_REG_REDIR 0x10 /* First redirection table entry (low). */

/* 82093AA version: version 0x11, max redirection entry 23 (0x17). */
#define IOAPIC_VERSION    0x11
#define IOAPIC_MAX_REDIR  23
#define IOAPIC_NUM_INPUTS 24

/* Redirection table entry bits. */
#define IOAPIC_REDIR_VECTOR_MASK  0x000000FF /* Bits 7:0 - Interrupt vector. */
#define IOAPIC_REDIR_DELMOD_MASK  0x00000700 /* Bits 10:8 - Delivery mode. */
#define IOAPIC_REDIR_DELMOD_SHIFT 8
#define IOAPIC_REDIR_DESTMOD      (1 << 11) /* Bit 11 - Destination mode. */
#define IOAPIC_REDIR_DELIVS       (1 << 12) /* Bit 12 - Delivery status (RO). */
#define IOAPIC_REDIR_POLARITY     (1 << 13) /* Bit 13 - Pin polarity. */
#define IOAPIC_REDIR_REMOTE_IRR   (1 << 14) /* Bit 14 - Remote IRR (RO). */
#define IOAPIC_REDIR_TRIGGER      (1 << 15) /* Bit 15 - Trigger mode. */
#define IOAPIC_REDIR_MASK         (1 << 16) /* Bit 16 - Interrupt mask. */
#define IOAPIC_REDIR_DEST_SHIFT   56        /* Bits 63:56 - Destination. */

/* Delivery modes. */
#define IOAPIC_DELMOD_FIXED  0 /* 000 - Fixed delivery. */
#define IOAPIC_DELMOD_LOWPRI 1 /* 001 - Lowest priority. */
#define IOAPIC_DELMOD_SMI    2 /* 010 - SMI. */
#define IOAPIC_DELMOD_NMI    4 /* 100 - NMI. */
#define IOAPIC_DELMOD_INIT   5 /* 101 - INIT. */
#define IOAPIC_DELMOD_EXTINT 7 /* 111 - ExtINT. */

#ifdef __cplusplus
extern "C" {
#endif

/* Forward declaration. */
typedef struct ioapic_t ioapic_t;

/* Route an IRQ through the I/O APIC if it is active for that IRQ.
   Returns 1 if the I/O APIC handled the interrupt, 0 if the PIC
   should handle it instead. */
extern int ioapic_irq(int irq, int set);

/* Clear the Remote IRR bit for a level-triggered vector.
   Called from the Local APIC's EOI handler to signal end-of-interrupt
   to the I/O APIC so it can re-deliver if the line is still asserted. */
extern void ioapic_eoi(int vector);

/* Returns 1 if the I/O APIC is active (initialized and present). */
extern int ioapic_active(void);

/* Global pointer to the I/O APIC state (NULL if not present). */
extern ioapic_t *ioapic;

#ifdef __cplusplus
}
#endif

#endif /* EMU_IOAPIC_H */
