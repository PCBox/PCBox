/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Intel 82093AA I/O APIC implementation.
 *
 *          Implements the full register set per the 82093AA datasheet:
 *          - Indirect register access via IOREGSEL / IOWIN
 *          - IOAPICID, IOAPICVER, IOAPICARB registers
 *          - 24 redirection table entries (64-bit each)
 *          - Interrupt routing to Local APIC via apic_set_irr()
 *
 *          The I/O APIC coexists with the 8259 PIC. At boot, the BIOS
 *          uses the PIC (virtual wire mode). The OS later switches to
 *          symmetric I/O mode by unmasking I/O APIC redirection entries.
 *
 * Authors: skiretic
 *
 *          Copyright 2026 skiretic.
 */
#include <stdarg.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#define HAVE_STDARG_H
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pic.h>
#include <86box/nmi.h>
#include <86box/apic.h>
#include <86box/ioapic.h>
#include <86box/plat_unused.h>

#ifdef ENABLE_IOAPIC_LOG
uint8_t ioapic_do_log = ENABLE_IOAPIC_LOG;

static void
ioapic_log(const char *fmt, ...)
{
    va_list ap;

    if (ioapic_do_log) {
        va_start(ap, fmt);
        pclog_ex(fmt, ap);
        va_end(ap);
    }
}
#else
#    define ioapic_log(fmt, ...)
#endif

/*
 * I/O APIC state structure.
 */
struct ioapic_t {
    /* MMIO mapping. */
    mem_mapping_t mem_mapping;

    /* Base address. */
    uint32_t base_addr;

    /* Index register (IOREGSEL). */
    uint8_t ioregsel;

    /* I/O APIC ID (bits 27:24 of the IOAPICID register). */
    uint32_t id;

    /* Arbitration ID (bits 27:24 of the IOAPICARB register). */
    uint32_t arb_id;

    /* 24 redirection table entries, 64 bits each.
       Even indices (0x10, 0x12, ...) access the low 32 bits.
       Odd indices (0x11, 0x13, ...) access the high 32 bits. */
    uint64_t redir[IOAPIC_NUM_INPUTS];

    /* Track which IRQ lines are currently asserted (for level-triggered). */
    uint8_t irq_state[IOAPIC_NUM_INPUTS];
};

/* Global pointer to the I/O APIC state. */
ioapic_t *ioapic = NULL;

/*
 * Deliver an interrupt from the I/O APIC to the Local APIC.
 * Called when an unmasked IRQ line is asserted.
 */
static void
ioapic_deliver(ioapic_t *dev, int irq)
{
    uint64_t redir   = dev->redir[irq];
    int      vector  = (int) (redir & IOAPIC_REDIR_VECTOR_MASK);
    int      delmod  = (int) ((redir >> IOAPIC_REDIR_DELMOD_SHIFT) & 7);
    int      destmod = !!(redir & IOAPIC_REDIR_DESTMOD);
    int      trigger = !!(redir & IOAPIC_REDIR_TRIGGER);
    int      dest    = (int) ((redir >> IOAPIC_REDIR_DEST_SHIFT) & 0xFF);

    ioapic_log("IOAPIC: Deliver IRQ %d -> vector %02X, delmod=%d, dest=%02X, "
               "destmod=%s, trigger=%s\n",
               irq, vector, delmod, dest,
               destmod ? "logical" : "physical",
               trigger ? "level" : "edge");

    switch (delmod) {
        case IOAPIC_DELMOD_FIXED:
        case IOAPIC_DELMOD_LOWPRI:
            /* Fixed / Lowest Priority: deliver the vector to the target
               Local APIC based on the destination field. */
            if (vector < 0x10) {
                ioapic_log("IOAPIC: Invalid vector %02X for IRQ %d (must be >= 0x10)\n",
                           vector, irq);
                return;
            }

            /* Route to the target CPU(s) based on destination mode. */
            if (!destmod) {
                /* Physical mode: dest is an APIC ID.
                   For dual-CPU, APIC ID matches cpu_id. */
                if (dest == 0xFF) {
                    /* Broadcast to all CPUs with APICs. */
                    for (int i = 0; i < APIC_MAX_CPUS; i++) {
                        if (apic_get_cpu(i))
                            apic_set_irr_cpu(i, vector);
                    }
                } else if (dest < APIC_MAX_CPUS) {
                    apic_set_irr_cpu(dest, vector);
                } else {
                    ioapic_log("IOAPIC: Invalid physical dest %02X for IRQ %d\n",
                               dest, irq);
                }
            } else {
                /* Logical mode: check each CPU's APIC LDR/DFR. */
                for (int i = 0; i < APIC_MAX_CPUS; i++) {
                    if (apic_match_logical_dest(i, (uint8_t) dest))
                        apic_set_irr_cpu(i, vector);
                }
            }

            /* For level-triggered interrupts, set the Remote IRR bit.
               It will be cleared when the Local APIC sends an EOI. */
            if (trigger)
                dev->redir[irq] |= IOAPIC_REDIR_REMOTE_IRR;
            break;

        case IOAPIC_DELMOD_SMI:
            /* SMI delivery -- raise SMI on the target CPU. */
            ioapic_log("IOAPIC: SMI delivery for IRQ %d (stubbed)\n", irq);
            break;

        case IOAPIC_DELMOD_NMI:
            /* NMI delivery. */
            ioapic_log("IOAPIC: NMI delivery for IRQ %d\n", irq);
            nmi = 1;
            break;

        case IOAPIC_DELMOD_INIT:
            /* INIT delivery. */
            ioapic_log("IOAPIC: INIT delivery for IRQ %d (stubbed)\n", irq);
            break;

        case IOAPIC_DELMOD_EXTINT:
            /* ExtINT: pass through as an 8259-compatible interrupt.
               The vector comes from the PIC acknowledge cycle. */
            ioapic_log("IOAPIC: ExtINT delivery for IRQ %d\n", irq);
            apic_lint0_raise();
            break;

        default:
            ioapic_log("IOAPIC: Unknown delivery mode %d for IRQ %d\n",
                       delmod, irq);
            break;
    }
}

/*
 * Read an internal register via the IOWIN data window.
 */
static uint32_t
ioapic_reg_read(ioapic_t *dev, uint8_t index)
{
    uint32_t ret = 0;

    switch (index) {
        case IOAPIC_REG_ID:
            ret = dev->id;
            break;

        case IOAPIC_REG_VER:
            /* Bits 7:0 = version, bits 23:16 = max redirection entry. */
            ret = IOAPIC_VERSION | (IOAPIC_MAX_REDIR << 16);
            break;

        case IOAPIC_REG_ARB:
            ret = dev->arb_id;
            break;

        default:
            /* Redirection table entries: index 0x10-0x3F.
               Even index = low 32 bits, odd index = high 32 bits. */
            if (index >= IOAPIC_REG_REDIR && index <= (IOAPIC_REG_REDIR + (IOAPIC_MAX_REDIR * 2) + 1)) {
                int entry = (index - IOAPIC_REG_REDIR) / 2;
                if (index & 1)
                    ret = (uint32_t) (dev->redir[entry] >> 32);
                else
                    ret = (uint32_t) dev->redir[entry];
            } else {
                ioapic_log("IOAPIC: Read from unknown register index %02X\n", index);
            }
            break;
    }

    return ret;
}

/*
 * Write an internal register via the IOWIN data window.
 */
static void
ioapic_reg_write(ioapic_t *dev, uint8_t index, uint32_t val)
{
    switch (index) {
        case IOAPIC_REG_ID:
            /* Only bits 27:24 are writable (I/O APIC ID). */
            dev->id = val & 0x0F000000;
            ioapic_log("IOAPIC: ID set to %X\n", (dev->id >> 24) & 0xF);
            break;

        case IOAPIC_REG_VER:
            /* Read-only. */
            break;

        case IOAPIC_REG_ARB:
            /* Read-only (reset by writing to IOAPICID). */
            break;

        default:
            /* Redirection table entries. */
            if (index >= IOAPIC_REG_REDIR && index <= (IOAPIC_REG_REDIR + (IOAPIC_MAX_REDIR * 2) + 1)) {
                int entry = (index - IOAPIC_REG_REDIR) / 2;

                if (index & 1) {
                    /* High 32 bits: only bits 31:24 (destination) are writable. */
                    dev->redir[entry] = (dev->redir[entry] & 0x00000000FFFFFFFFULL)
                        | ((uint64_t) (val & 0xFF000000) << 32);
                } else {
                    /* Low 32 bits: mask off read-only bits (delivery status bit 12,
                       remote IRR bit 14). */
                    uint32_t ro_mask  = IOAPIC_REDIR_DELIVS | IOAPIC_REDIR_REMOTE_IRR;
                    uint32_t old_low  = (uint32_t) dev->redir[entry];
                    uint32_t new_low  = (val & ~ro_mask) | (old_low & ro_mask);
                    dev->redir[entry] = (dev->redir[entry] & 0xFFFFFFFF00000000ULL) | new_low;
                }

                ioapic_log("IOAPIC: Redir[%d] = %016llX (vec=%02X dm=%d mask=%d dest=%02X trig=%s)\n",
                           entry,
                           (unsigned long long) dev->redir[entry],
                           (int) (dev->redir[entry] & 0xFF),
                           (int) ((dev->redir[entry] >> 8) & 7),
                           (int) !!(dev->redir[entry] & IOAPIC_REDIR_MASK),
                           (int) ((dev->redir[entry] >> 56) & 0xFF),
                           (dev->redir[entry] & IOAPIC_REDIR_TRIGGER) ? "level" : "edge");

                /* If the entry was just unmasked and the IRQ line is currently
                   asserted (level-triggered), deliver the interrupt now. */
                if (!(dev->redir[entry] & IOAPIC_REDIR_MASK)
                    && (dev->redir[entry] & IOAPIC_REDIR_TRIGGER)
                    && dev->irq_state[entry]) {
                    ioapic_deliver(dev, entry);
                }
            } else {
                ioapic_log("IOAPIC: Write to unknown register index %02X = %08X\n",
                           index, val);
            }
            break;
    }
}

/*
 * MMIO read handlers for the I/O APIC.
 * The I/O APIC occupies a 32-byte region at its base address.
 */
static uint8_t
ioapic_mem_readb(uint32_t addr, UNUSED(void *priv))
{
    /* Byte reads are not useful for the I/O APIC but must not crash. */
    return 0xFF;
}

static uint16_t
ioapic_mem_readw(uint32_t addr, UNUSED(void *priv))
{
    return 0xFFFF;
}

static uint32_t
ioapic_mem_readl(uint32_t addr, void *priv)
{
    ioapic_t *dev    = (ioapic_t *) priv;
    uint32_t  offset = addr & 0xFF;
    uint32_t  ret    = 0;

    switch (offset) {
        case IOAPIC_IOREGSEL:
            ret = dev->ioregsel;
            break;

        case IOAPIC_IOWIN:
            ret = ioapic_reg_read(dev, dev->ioregsel);
            ioapic_log("IOAPIC: Read IOWIN [index %02X] = %08X\n",
                       dev->ioregsel, ret);
            break;

        default:
            ioapic_log("IOAPIC: Read from unhandled MMIO offset %02X\n", offset);
            break;
    }

    return ret;
}

/*
 * MMIO write handlers for the I/O APIC.
 */
static void
ioapic_mem_writeb(uint32_t addr, uint8_t val, UNUSED(void *priv))
{
    /* Byte writes are ignored. */
}

static void
ioapic_mem_writew(uint32_t addr, uint16_t val, UNUSED(void *priv))
{
    /* Word writes are ignored. */
}

static void
ioapic_mem_writel(uint32_t addr, uint32_t val, void *priv)
{
    ioapic_t *dev    = (ioapic_t *) priv;
    uint32_t  offset = addr & 0xFF;

    switch (offset) {
        case IOAPIC_IOREGSEL:
            dev->ioregsel = val & 0xFF;
            break;

        case IOAPIC_IOWIN:
            ioapic_log("IOAPIC: Write IOWIN [index %02X] = %08X\n",
                       dev->ioregsel, val);
            ioapic_reg_write(dev, dev->ioregsel, val);
            break;

        default:
            ioapic_log("IOAPIC: Write to unhandled MMIO offset %02X = %08X\n",
                       offset, val);
            break;
    }
}

/*
 * Route an IRQ through the I/O APIC.
 *
 * Called from the interrupt dispatch layer when a device asserts
 * or deasserts an IRQ line.
 *
 * Returns 1 if the I/O APIC handled the interrupt (entry is unmasked),
 * 0 if the PIC should handle it instead.
 */
int
ioapic_irq(int irq, int set)
{
    if (!ioapic || irq < 0 || irq >= IOAPIC_NUM_INPUTS)
        return 0;

    ioapic_t *dev   = ioapic;
    uint64_t  redir = dev->redir[irq];

    /* If this entry is masked, let the PIC handle it. */
    if (redir & IOAPIC_REDIR_MASK)
        return 0;

    int trigger = !!(redir & IOAPIC_REDIR_TRIGGER);

    if (set) {
        if (trigger) {
            /* Level-triggered: assert the line. */
            dev->irq_state[irq] = 1;

            /* Only deliver if Remote IRR is not already set
               (one delivery at a time for level-triggered). */
            if (!(redir & IOAPIC_REDIR_REMOTE_IRR))
                ioapic_deliver(dev, irq);
        } else {
            /* Edge-triggered: deliver on the rising edge. */
            if (!dev->irq_state[irq]) {
                dev->irq_state[irq] = 1;
                ioapic_deliver(dev, irq);
            }
        }
    } else {
        /* Deassertion. */
        dev->irq_state[irq] = 0;
    }

    return 1;
}

/*
 * Clear the Remote IRR bit for a level-triggered interrupt.
 * Called when the Local APIC processes an EOI for a vector that
 * was delivered by the I/O APIC.
 *
 * After clearing Remote IRR, if the IRQ line is still asserted,
 * the interrupt is re-delivered.
 */
void
ioapic_eoi(int vector)
{
    if (!ioapic)
        return;

    ioapic_t *dev = ioapic;

    /* Find any redirection entry that matches this vector and has
       Remote IRR set. */
    for (int i = 0; i < IOAPIC_NUM_INPUTS; i++) {
        uint64_t redir = dev->redir[i];

        if ((redir & IOAPIC_REDIR_REMOTE_IRR)
            && (int) (redir & IOAPIC_REDIR_VECTOR_MASK) == vector) {
            /* Clear Remote IRR. */
            dev->redir[i] &= ~(uint64_t) IOAPIC_REDIR_REMOTE_IRR;

            ioapic_log("IOAPIC: EOI for vector %02X, cleared Remote IRR on entry %d\n",
                       vector, i);

            /* If the line is still asserted, re-deliver. */
            if (dev->irq_state[i] && !(redir & IOAPIC_REDIR_MASK))
                ioapic_deliver(dev, i);
        }
    }
}

/*
 * Returns 1 if the I/O APIC is active (initialized and present).
 */
int
ioapic_active(void)
{
    return ioapic != NULL;
}

/*
 * Reset the I/O APIC to power-on defaults.
 */
static void
ioapic_reset_state(ioapic_t *dev)
{
    /* I/O APIC ID: 0. */
    dev->id     = 0;
    dev->arb_id = 0;

    /* All redirection entries: masked, edge-triggered, fixed delivery,
       physical destination, vector 0. */
    for (int i = 0; i < IOAPIC_NUM_INPUTS; i++) {
        dev->redir[i]     = IOAPIC_REDIR_MASK;
        dev->irq_state[i] = 0;
    }

    /* IOREGSEL: 0. */
    dev->ioregsel = 0;

    ioapic_log("IOAPIC: Reset to defaults\n");
}

static void
ioapic_close(void *priv)
{
    ioapic_t *dev = (ioapic_t *) priv;

    mem_mapping_disable(&dev->mem_mapping);

    ioapic = NULL;
    free(dev);
}

static void *
ioapic_init(UNUSED(const device_t *info))
{
    ioapic_t *dev = (ioapic_t *) calloc(1, sizeof(ioapic_t));

    dev->base_addr = IOAPIC_DEFAULT_BASE;

    /* Set up MMIO mapping for the I/O APIC register window.
       The 82093AA only uses offsets 0x00 (IOREGSEL) and 0x10 (IOWIN),
       but we map a full page for simplicity. */
    mem_mapping_add(&dev->mem_mapping,
                    dev->base_addr,
                    0x100,
                    ioapic_mem_readb,
                    ioapic_mem_readw,
                    ioapic_mem_readl,
                    ioapic_mem_writeb,
                    ioapic_mem_writew,
                    ioapic_mem_writel,
                    NULL,
                    MEM_MAPPING_EXTERNAL,
                    dev);

    /* Reset to power-on defaults. */
    ioapic_reset_state(dev);

    /* Set global pointer. */
    ioapic = dev;

    ioapic_log("IOAPIC: Initialized at base %08X\n", dev->base_addr);

    return dev;
}

const device_t ioapic_device = {
    .name          = "I/O Advanced Programmable Interrupt Controller",
    .internal_name = "ioapic",
    .flags         = DEVICE_ISA16,
    .local         = 0,
    .init          = ioapic_init,
    .close         = ioapic_close,
    .reset         = NULL,
    .available     = NULL,
    .speed_changed = NULL,
    .force_redraw  = NULL,
    .config        = NULL
};
