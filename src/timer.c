#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include "cpu.h"
#include <86box/timer.h>
#include <86box/nv/vid_nv_rivatimer.h>

uint64_t TIMER_USEC;
uint64_t timer_target;

/*Enabled timers are stored in a binary min-heap of pointers, ordered by
  expiry timestamp with the first timer to expire at the root. Each timer
  stores its own heap position in heap_idx so it can be removed or moved
  in O(log n) without searching.*/
static pc_timer_t **timer_heap  = NULL;
static uint32_t     timer_count = 0;
static uint32_t     timer_cap   = 0;

/* Are we initialized? */
int timer_inited = 0;

static void timer_advance_ex(pc_timer_t *timer, int start);

/*True if timer a expires strictly before timer b*/
#define TIMER_BEFORE(a, b) ((int64_t) ((a)->ts_integer - (b)->ts_integer) < 0)

static __inline void
timer_heap_set(uint32_t idx, pc_timer_t *timer)
{
    timer_heap[idx]  = timer;
    timer->heap_idx = idx;
}

static void
timer_sift_up(uint32_t idx, pc_timer_t *timer)
{
    while (idx > 0) {
        uint32_t    parent_idx = (idx - 1) >> 1;
        pc_timer_t *parent     = timer_heap[parent_idx];

        if (!TIMER_BEFORE(timer, parent))
            break;

        timer_heap_set(idx, parent);
        idx = parent_idx;
    }

    timer_heap_set(idx, timer);
}

static void
timer_sift_down(uint32_t idx, pc_timer_t *timer)
{
    while (1) {
        uint32_t child = (idx << 1) + 1;

        if (child >= timer_count)
            break;

        if (((child + 1) < timer_count) && TIMER_BEFORE(timer_heap[child + 1], timer_heap[child]))
            child++;

        if (!TIMER_BEFORE(timer_heap[child], timer))
            break;

        timer_heap_set(idx, timer_heap[child]);
        idx = child;
    }

    timer_heap_set(idx, timer);
}

static __inline void
timer_update_target(void)
{
    if (timer_count > 0)
        timer_target = timer_heap[0]->ts_integer;
    else
        /* No enabled timers - push the target out to the maximum period. */
        timer_target = (uint64_t) tsc + 0x7fffffffULL;
}

static void
timer_heap_grow(void)
{
    uint32_t     new_cap  = timer_cap ? (timer_cap << 1) : 256;
    pc_timer_t **new_heap = realloc(timer_heap, new_cap * sizeof(pc_timer_t *));

    if (new_heap == NULL)
        fatal("timer_heap_grow - out of memory\n");

    timer_heap = new_heap;
    timer_cap  = new_cap;
}

void
timer_enable(pc_timer_t *timer)
{
    uint32_t idx;

    if (timer->flags & TIMER_ENABLED) {
        /* Already in the heap - move it to its new position in place. */
        idx = timer->heap_idx;
        if ((idx >= timer_count) || (timer_heap[idx] != timer))
            fatal("timer_enable - enabled timer not in heap\n");

        timer_sift_up(idx, timer);
        if (timer->heap_idx == idx)
            timer_sift_down(idx, timer);
    } else {
        if (timer_count == timer_cap)
            timer_heap_grow();

        timer->flags |= TIMER_ENABLED;
        timer_sift_up(timer_count++, timer);
    }

    timer_update_target();
}

void
timer_disable(pc_timer_t *timer)
{
    pc_timer_t *last;
    uint32_t    idx;

    if (!timer_inited || (timer == NULL) || !(timer->flags & TIMER_ENABLED))
        return;

    idx = timer->heap_idx;
    if ((idx >= timer_count) || (timer_heap[idx] != timer))
        fatal("timer_disable - enabled timer not in heap\n");

    timer->flags &= ~TIMER_ENABLED;
    timer->in_callback = 0;

    timer_count--;
    if (idx < timer_count) {
        /* Fill the hole with the last heap entry. */
        last = timer_heap[timer_count];
        timer_sift_up(idx, last);
        if (last->heap_idx == idx)
            timer_sift_down(idx, last);
    }

    timer_update_target();
}

static __inline void
timer_remove_head(void)
{
    pc_timer_t *head = timer_heap[0];

    head->flags &= ~TIMER_ENABLED;

    timer_count--;
    if (timer_count > 0)
        timer_sift_down(0, timer_heap[timer_count]);
}

void
timer_process(void)
{
    while (timer_count > 0) {
        pc_timer_t *timer = timer_heap[0];

        if (!TIMER_LESS_THAN_VAL(timer, (uint64_t) tsc))
            break;

        timer_remove_head();

        if (timer->flags & TIMER_SPLIT)
            timer_advance_ex(timer, 0);   /* We're splitting a > 1 s period into
                                             multiple <= 1 s periods. */
        else if (timer->callback != NULL) {
            /*
               Make sure it's not NULL, so that we can
               have a NULL callback when no operation
               is needed.
             */
            timer->in_callback = 1;
            timer->callback(timer->priv);
            timer->in_callback = 0;
        }
    }

    timer_update_target();
}

void
timer_close(void)
{
    uint32_t c;

    /* Clear the enabled flag of all timers still in the heap, so that
       stale heap indices can't corrupt the next machine's timer heap. */
    for (c = 0; c < timer_count; c++)
        timer_heap[c]->flags &= ~TIMER_ENABLED;

    timer_count = 0;

    timer_inited = 0;
}

void
timer_init(void)
{
    timer_target = 0ULL;
    tsc          = 0;

    /* Initialise the CPU-independent timer */
    rivatimer_init();

    timer_inited = 1;
}

void
timer_add(pc_timer_t *timer, void (*callback)(void *priv), void *priv, int start_timer)
{
    memset(timer, 0, sizeof(pc_timer_t));

    timer->callback    = callback;
    timer->in_callback = 0;
    timer->priv        = priv;
    timer->flags       = 0;
    if (start_timer)
        timer_set_delay_u64(timer, 0);
}

/* The API for big timer periods starts here. */
void
timer_stop(pc_timer_t *timer)
{
    if (!timer_inited || (timer == NULL))
        return;

    timer->period = 0.0;
    if (timer->flags & TIMER_ENABLED)
        timer_disable(timer);
    timer->flags &= ~TIMER_SPLIT;
    timer->in_callback = 0;
}

static void
timer_do_period(pc_timer_t *timer, uint64_t period, int start)
{
    if (!timer_inited || (timer == NULL))
        return;

    if (start)
        timer_set_delay_u64(timer, period);
    else
        timer_advance_u64(timer, period);
}

static void
timer_advance_ex(pc_timer_t *timer, int start)
{
    if (!timer_inited || (timer == NULL))
        return;

    if (timer->period > MAX_USEC) {
        timer_do_period(timer, MAX_USEC64 * TIMER_USEC, start);
        timer->period -= MAX_USEC;
        timer->flags |= TIMER_SPLIT;
    } else {
        if (timer->period > 0.0)
            timer_do_period(timer, (uint64_t) (timer->period * ((double) TIMER_USEC)), start);
        else
            timer_disable(timer);
        timer->period = 0.0;
        timer->flags &= ~TIMER_SPLIT;
    }
}

static void
timer_on(pc_timer_t *timer, double period, int start)
{
    if (!timer_inited || (timer == NULL))
        return;

    timer->period = period;
    timer_advance_ex(timer, start);
}

void
timer_on_auto(pc_timer_t *timer, double period)
{
    if (!timer_inited || (timer == NULL))
        return;

    if (period > 0.0)
        /* If the timer is in the callback, signal that, so that timer_advance_u64()
           is used instead of timer_set_delay_u64(). */
        timer_on(timer, period, (timer->period <= 0.0) && !timer->in_callback);
    else
        timer_stop(timer);
}

void
timer_set_new_tsc(uint64_t new_tsc)
{
    uint32_t c;

    /* Run timers already expired. */
#ifdef USE_DYNAREC
    if (cpu_use_dynarec)
        update_tsc();
#endif

    /* Shifting every timestamp by the same delta preserves the heap order. */
    for (c = 0; c < timer_count; c++)
        timer_heap[c]->ts_integer = new_tsc + (int64_t) (timer_heap[c]->ts_integer - (uint64_t) tsc);

    tsc = new_tsc;

    timer_update_target();
}
