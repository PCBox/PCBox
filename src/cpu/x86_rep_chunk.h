/* Bounded REP memory operations on cached RAM. MOVS/STOS require untracked RAM. The caller retains the
   scalar path for faults, MMIO, debug accesses and anything crossing a bound. */
#ifndef X86_REP_CHUNK_H
#define X86_REP_CHUNK_H

/* Return the number of whole elements before a page, segment or address-size
   boundary. This is a non-faulting probe: the scalar handler must raise faults
   in guest iteration order, including when only one operand is valid. */
static __inline uint32_t
rep_chunk_limit(const x86seg *seg, uint32_t offset, uint32_t addr_mask,
                unsigned width, int backwards)
{
    uint32_t linear = seg->base + offset;
    uint32_t high   = seg->limit_high < addr_mask ? seg->limit_high : addr_mask;
    uint32_t count;
    uint64_t segment_count;

    if (seg->base == UINT32_MAX || offset < seg->limit_low || offset > high ||
        width - 1 > high - offset || (linear & (width - 1)))
        return 0;
    if ((msw & 1) && !(cpu_state.eflags & VM_FLAG) && !(seg->access & 0x80))
        return 0;

    /* Unaligned accesses retain their scalar misalignment timing. */
    if (backwards) {
        count         = (linear & 0xfff) / width + 1;
        segment_count = (uint64_t) (offset - seg->limit_low) / width + 1;
    } else {
        count         = (0x1000 - (linear & 0xfff)) / width;
        segment_count = ((uint64_t) high - offset + 1) / width;
    }
    return segment_count < count ? (uint32_t) segment_count : count;
}

static __inline uint32_t
rep_movs_stos_chunk(uint32_t count, uint32_t src_offset, uint32_t dst_offset,
                    uint32_t addr_mask, unsigned width, int movs, int cycles_end)
{
#ifdef USE_GDBSTUB
    /* Debugger memory watchpoints must continue through the scalar accesses. */
    return 0;
#else
    uint32_t  limit, bytes;
    uintptr_t src = 0, dst;
    uint32_t  dst_linear = es + dst_offset;
    int       backwards = !!(cpu_state.flags & D_FLAG);
    int       cost      = movs ? (is486 ? 3 : 4) : (is486 ? 4 : 5);
    int64_t   budget    = (int64_t) cycles - cycles_end;

    if (count < 2 || trap || (cpu_state.flags & T_FLAG) || (dr[7] & 0xff) || cpu_state.abrt)
        return 0;

    /* Match the scalar loop's strict "cycles < cycles_end" exit: the last
       iteration is allowed to take the budget below zero. Never extend a REP
       invocation's existing interrupt/timer service interval. */
    limit = budget < 0 ? 1 : (uint32_t) (budget / cost + 1);
    if (count > limit)
        count = limit;
    if (count > 256)
        count = 256;

    limit = rep_chunk_limit(&cpu_state.seg_es, dst_offset, addr_mask, width, backwards);
    if (count > limit)
        count = limit;
    dst = writelookup2[dst_linear >> 12];
    /* Code pages, including aliases and blocks spanning pages, are routed
       through page_lookup instead of direct writable RAM by addwritelookup. */
    if (dst == (uintptr_t) LOOKUP_INV || page_lookup[dst_linear >> 12])
        return 0;
    dst += dst_linear;

    if (movs) {
        uint32_t src_linear = cpu_state.ea_seg->base + src_offset;
        limit = rep_chunk_limit(cpu_state.ea_seg, src_offset, addr_mask, width, backwards);
        if (count > limit)
            count = limit;
        src = readlookup2[src_linear >> 12];
        if (src == (uintptr_t) LOOKUP_INV)
            return 0;
        src += src_linear;
    }
    if (count < 2)
        return 0;

    bytes = count * width;
    if (backwards) {
        dst -= bytes - width;
        if (movs)
            src -= bytes - width;
    }
    if (movs) {
        /* Distinct guest pages can alias RAM. For overlapping HOST ranges,
           preserve guest element order: neither memcpy nor memmove implements
           REP's propagation when a store changes a later source element. */
        if ((dst >= src ? dst - src : src - dst) < bytes) {
            for (uint32_t i = 0; i < count; i++) {
                uint32_t offset = backwards ? bytes - (i + 1) * width : i * width;
                if (width == 1) {
                    *(uint8_t *) (dst + offset) = *(const uint8_t *) (src + offset);
                } else if (width == 2) {
                    uint16_t value;
                    memcpy(&value, (const void *) (src + offset), sizeof(value));
                    memcpy((void *) (dst + offset), &value, sizeof(value));
                } else {
                    uint32_t value;
                    memcpy(&value, (const void *) (src + offset), sizeof(value));
                    memcpy((void *) (dst + offset), &value, sizeof(value));
                }
            }
        } else {
            memcpy((void *) dst, (const void *) src, bytes);
        }
        high_page = 0;
    } else if (width == 1) {
        memset((void *) dst, AL, bytes);
    } else if (width == 2) {
        uint16_t value = AX;
        for (uint32_t i = 0; i < count; i++)
            memcpy((void *) (dst + i * 2), &value, sizeof(value));
    } else {
        uint32_t value = EAX;
        for (uint32_t i = 0; i < count; i++)
            memcpy((void *) (dst + i * 4), &value, sizeof(value));
    }
    cycles -= (int) count * cost;
    return count;
#endif
}

/* Both CMPS operands have already passed their segment/translation checks.
   Use its separate source cache, and set is_compare only around a slow read
   which may populate that cache. The handler still returns after one element. */
static __inline uint32_t
rep_cmps_read(uint32_t offset, unsigned width, uint32_t *translated)
{
    uint32_t linear = cpu_state.ea_seg->base + offset;
    uintptr_t base = readlookup2[1048576 | (linear >> 12)];
    uint32_t value;

    if (base != (uintptr_t) LOOKUP_INV && cpu_state.ea_seg->base != UINT32_MAX &&
        !(dr[7] & 0xff) && !(linear & (width - 1))) {
        if (width == 1) return *(const uint8_t *) (base + linear);
        if (width == 2) {
            uint16_t word;
            memcpy(&word, (const void *) (base + linear), sizeof(word));
            return word;
        }
        memcpy(&value, (const void *) (base + linear), sizeof(value));
        return value;
    }
    is_compare = 1;
    if (width == 1) value = readmembl_no_mmut(linear, translated[0]);
    else if (width == 2) value = readmemwl_no_mmut(linear, translated);
    else value = readmemll_no_mmut(linear, translated);
    is_compare = 0;
    return value;
}

static __inline uint32_t
rep_scas_chunk(uint32_t count, uint32_t offset, uint32_t addr_mask,
               unsigned width, int equal, int cycles_end)
{
#ifdef USE_GDBSTUB
    return 0;
#else
    uint32_t linear = es + offset;
    uint32_t limit, done = 0, value = 0;
    uint32_t accumulator = width == 1 ? AL : width == 2 ? AX : EAX;
    uintptr_t base;
    int backwards = !!(cpu_state.flags & D_FLAG);
    int cost = is486 ? 5 : 8;
    int64_t budget = (int64_t) cycles - cycles_end;

    if (count < 2 || trap || (cpu_state.flags & T_FLAG) || (dr[7] & 0xff) || cpu_state.abrt)
        return 0;
    /* Keep the existing SCAS service interval, including its final iteration
       below cycles_end. Page/segment boundaries go back through scalar code. */
    limit = budget < 0 ? 1 : (uint32_t) (budget / cost + 1);
    if (count > limit) count = limit;
    if (count > 256) count = 256;
    limit = rep_chunk_limit(&cpu_state.seg_es, offset, addr_mask, width, backwards);
    if (count > limit) count = limit;
    base = readlookup2[linear >> 12];
    if (count < 2 || base == (uintptr_t) LOOKUP_INV)
        return 0;
    base += linear;

    /* RAM cannot fault within this range. Only the final comparison's flags
       are observable, but the stopping element still consumes a count/cycle. */
    do {
        if (width == 1) value = *(const uint8_t *) base;
        else if (width == 2) {
            uint16_t word;
            memcpy(&word, (const void *) base, sizeof(word));
            value = word;
        } else
            memcpy(&value, (const void *) base, sizeof(value));
        done++;
        if ((accumulator == value) != equal)
            break;
        base += backwards ? -(intptr_t) width : (intptr_t) width;
    } while (done < count);
    if (width == 1) setsub8(accumulator, value);
    else if (width == 2) setsub16(accumulator, value);
    else setsub32(accumulator, value);
    cycles -= (int) done * cost;
    return done;
#endif
}

#endif
