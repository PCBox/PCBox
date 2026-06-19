#define IOAPIC_RED_TABL_SIZE 24

/* We only target little-endian architectures. */
typedef struct apic_ioredtable_t {
    uint8_t intvec;

    uint8_t delmod     : 3;
    uint8_t destmod    : 1;
    uint8_t delivs     : 1;
    uint8_t intpol     : 1;
    uint8_t rirr       : 1;
    uint8_t trigmode   : 1;

    uint8_t intr_mask  : 1;
    uint8_t timer_mode : 1;
    uint8_t reserved   : 6;
    
    uint8_t reserved2[4];

    uint8_t dest_mask;
} apic_ioredtable_t;

#pragma pack(push, 1)
typedef struct ioapic_t
{
    /* I/O APIC parts */
    union {
        uint32_t ioapic_regs[256];
        struct {
            uint32_t ioapicd;
            uint32_t ioapicver;
            uint32_t ioapicarb;
            union {
                uint64_t ioredtabl[IOAPIC_RED_TABL_SIZE];
                apic_ioredtable_t ioredtabl_s[IOAPIC_RED_TABL_SIZE];
                uint32_t ioredtabl_l[IOAPIC_RED_TABL_SIZE * 2];
            };
        };
    };
    uint8_t ioapic_index;
    mem_mapping_t ioapic_mem_window;
    uint32_t irr;

    uint32_t irq_eoi[IOAPIC_RED_TABL_SIZE];
    uint32_t irq_level;
    
    pc_timer_t ioapic_service;

    int extended;
} ioapic_t;

typedef struct lapic_t
{
    /* Local APIC parts. */
    pc_timer_t apic_timer;
    union {
        uint64_t isr_ll[4];
        uint32_t isr_l[8];
        uint8_t isr_b[8 * sizeof(uint32_t)];
    };
    union {
        uint64_t irr_ll[4];
        uint32_t irr_l[8];
        uint8_t irr_b[8 * sizeof(uint32_t)];
    };
    union {
        uint64_t tmr_ll[4];
        uint32_t tmr_l[8];
        uint8_t tmr_b[8 * sizeof(uint32_t)];
    };
    union {
        uint64_t icr;
        struct {
            uint32_t icr0;
            uint32_t icr1;
        };
    };
    uint32_t lapic_id;
    uint32_t lapic_arb;
    uint32_t lapic_spurious_interrupt;
    uint32_t lapic_dest_format;
    uint32_t lapic_local_dest;
    uint32_t lapic_tpr;
    uint64_t old_tsc;

    uint32_t lapic_timer_divider;
    uint32_t lapic_timer_current_count;
    uint32_t lapic_timer_initial_count;
    uint32_t lapic_timer_remainder;

    union { apic_ioredtable_t lapic_lvt_lvt0; uint64_t lapic_lvt_lvt0_val; };
    union { apic_ioredtable_t lapic_lvt_lvt1; uint64_t lapic_lvt_lvt1_val; };
    union { apic_ioredtable_t lapic_lvt_timer; uint64_t lapic_lvt_timer_val; };
    union { apic_ioredtable_t lapic_lvt_perf; uint64_t lapic_lvt_perf_val; };
    union { apic_ioredtable_t lapic_lvt_thermal; uint64_t lapic_lvt_thermal_val; }; /* Unused */

    union { apic_ioredtable_t lapic_lvt_error; uint64_t lapic_lvt_error_val; };
    union { apic_ioredtable_t lapic_lvt_read_error; uint64_t lapic_lvt_read_error_val; };

    //pc_timer_t lapic_timer;

    uint8_t irq_queue_num;
    struct
    {
        uint8_t vector;
        apic_ioredtable_t vectorconf;
    } irq_queue[2];
    mem_mapping_t lapic_mem_window;
    
    /* Common parts. */
    uint32_t lines; /* For level triggered interrupts. */
    uint32_t ref_count; /* Structure reference count. */

    double bus_ticks;
} lapic_t;
#pragma pack(pop)

/* IOREDTABL masks */
#define IOAPIC_INTVEC_MASK    0xFF
#define IOAPIC_DELMOD_MASK    0x700
#define IOAPIC_DESTMOD_MASK   0x800
#define IOAPIC_DELIVS_MASK    0x1000
#define IOAPIC_INTPOL_MASK    0x2000
#define IOAPIC_RIRR_MASK      0x4000
#define IOAPIC_TRIGMODE_MASK  0x8000
#define IOAPIC_INTERRUPT_MASK 0x10000
#define IOAPIC_DEST_MASK      0xE000000000000000ull

extern const device_t i82093aa_ioapic_device;
extern const device_t lapic_device;

/* Only one processor is emulated. */
extern lapic_t* current_lapic;

extern ioapic_t* current_ioapic;

extern void apic_ioapic_set_base(uint8_t x_base, uint8_t y_base);
extern void apic_lapic_set_base(uint32_t base);
extern uint8_t apic_lapic_is_irr_pending(void);
extern void apic_ioapic_lapic_interrupt_check(ioapic_t* ioapic, uint8_t irq);
extern void apic_ioapic_set_irq(ioapic_t* ioapic, uint8_t irq, int level);
extern void apic_ioapic_clear_irq(ioapic_t* ioapic, uint8_t irq);
extern void apic_lapic_ioapic_remote_eoi(ioapic_t* ioapic, uint8_t vector);
extern void lapic_service_interrupt(lapic_t *lapic, apic_ioredtable_t interrupt);
extern uint8_t apic_lapic_picinterrupt(void);
extern void apic_lapic_service_nmi(void);
extern void apic_lapic_service_extint(void);
extern void lapic_timer_poll(void* priv);
extern void lapic_timer_advance_ticks(uint32_t ticks);
extern int lapic_irq_pending(lapic_t *lapic);
extern int pic_pending_int(void);
extern int lapic_is_pic_enabled(void);