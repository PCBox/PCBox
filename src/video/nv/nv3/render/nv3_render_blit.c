/*
* 86Box    A hypervisor and IBM PC system emulator that specializes in
*          running old operating systems and software designed for IBM
*          PC systems and compatibles from 1981 through fairly recent
*          system designs based on the PCI bus.
*
*          This file is part of the 86Box distribution.
*
*          NV3 image transfers: image from CPU (0x11), bitmap from CPU
*          (0x12), stretched image from CPU (0x15) and screen to screen
*          blits (0x10).
*
* Authors: Connor Hyde, <mario64crashed@gmail.com>
 *          Marcos Alves, <xeon4d@gmail.com>
*
*          Copyright 2024-2026 Connor Hyde
 *          Copyright 2026 Marcos Alves
*/

#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>

/* Pixels the CPU sends per 32-bit method for a colour format, and the
   width of each */
static uint32_t
nv3_render_pixels_per_word(nv3_grobj_t grobj, uint32_t *bits)
{
    switch (grobj.grobj_0 & 0x07) {
        case nv3_pgraph_pixel_format_y8:
            *bits = 8;
            return 4;
        case nv3_pgraph_pixel_format_r5g5b5:
        case nv3_pgraph_pixel_format_y16:
            *bits = 16;
            return 2;
        default:
            *bits = 32;
            return 1;
    }
}

/* Image from CPU (class 0x11): SIZE_IN pixels per row are sent, the first
   SIZE_OUT of them are drawn (the rest is padding) */
void
nv3_render_ifc_start(void)
{
    nv3->pgraph.image_pixel_count = 0;
}

/* Programs that compose whole frames in system memory send them here a word at a time: Ultim@te
   Race Pro's exit credits are two 640x480 16-bit images per vertical blank, some 18 million writes
   a second. A word then costs the guest about 50 ns of emulated time, and the way through the MMIO
   arbiter, CACHE1, PGRAPH, the class and a setup and pixel pipeline per word took twice that.

   So the first word of a plain copy image takes that way and has the pipeline confirm the pixels
   are just stored (nv3_render_image_plain). The words after it are stored directly, leaving what
   that way leaves in the guest's view: CACHE1's entry and pointers, the trapped address and data,
   and the pixel count. Everything the first word relied on can only change through another MMIO
   write or method, and all of those end this: nv3_mmio_arbitrate_write and nv3_pgraph_submit. */
nv3_image_fast_t nv3_image_fast = { .key = NV3_IMAGE_FAST_OFF };

void
nv3_render_image_fast_reset(void)
{
    nv3_image_fast.key   = NV3_IMAGE_FAST_OFF;
    nv3_image_fast.drew  = false;
    nv3_image_fast.known = false;
}

/* Can this image's words be stored directly? Fills in what it takes. */
static bool
nv3_render_image_fast_prepare(const nv3_render_setup_t *st, nv3_grobj_t grobj, uint32_t bits, uint32_t count)
{
    nv3_image_fast_t  *f   = &nv3_image_fast;
    const nv3_image_t *img = &nv3->pgraph.image;
    int64_t            px  = (int16_t) img->point.x;
    int64_t            py  = (int16_t) img->point.y;
    int64_t            rx0, rx1, ry0, ry1;
    uint32_t           j = 0;

    /* one destination, whose every pixel is on its own bytes */
    if (!st->bufs || (st->bufs & (st->bufs - 1)))
        return false;
    while (!((st->bufs >> j) & 1))
        j++;
    if ((nv3->pgraph.boffset[j] | nv3->pgraph.bpitch[j]) & (st->cpp - 1))
        return false;
    if (!nv3_render_image_plain(st, grobj, bits, &f->vmask, &f->vor))
        return false;

    rx0 = MAX((int64_t) 0, (int64_t) st->cx0 - px);
    ry0 = MAX((int64_t) 0, (int64_t) st->cy0 - py);
    rx1 = MIN(MIN((int64_t) img->size.x, (int64_t) img->size_in.x), (int64_t) st->cx1 - px);
    ry1 = MIN(MIN((int64_t) img->size.y, (int64_t) img->size_in.y), (int64_t) st->cy1 - py);

    f->base = (int64_t) nv3->pgraph.boffset[j] + py * nv3->pgraph.bpitch[j] + px * st->cpp;
    /* nothing that is drawn may wrap around the end of VRAM */
    if (rx0 >= rx1 || ry0 >= ry1)
        rx0 = rx1 = ry0 = ry1 = 0;
    else if ((f->base + (ry1 - 1) * nv3->pgraph.bpitch[j] + rx1 * st->cpp) > ((int64_t) nv3->nvbase.svga.vram_mask + 1))
        return false;

    f->count = count;
    f->bits  = bits;
    f->pmask = (bits == 32) ? 0xFFFFFFFF : ((1u << bits) - 1);
    f->cpp   = st->cpp;
    f->pitch = nv3->pgraph.bpitch[j];
    f->total = (uint32_t) img->size_in.x * img->size_in.y;
    f->w     = img->size_in.x;
    f->rx0   = rx0;
    f->rx1   = rx1;
    f->ry0   = ry0;
    f->ry1   = ry1;
    return true;
}

/* A word was drawn by MMIO write addr: can the next ones skip the way there? */
void
nv3_render_image_fast_arm(uint32_t addr)
{
    nv3_image_fast_t         *f  = &nv3_image_fast;
    const nv3_pfifo_cache_t  *c1 = &nv3->pfifo.cache1_settings;
    uint32_t                  n  = nv3->pgraph.image_pixel_count;

    f->drew = false;
    if ((addr < NV3_USER_START) || (addr & 3) || ((addr & 0x1F80) != NV3_IMAGE_COLOR_START) || (c1->put_address != c1->get_address))
        return;

    f->rx   = n % f->w;
    f->ry   = n / f->w;
    f->row  = f->base + (int64_t) f->ry * f->pitch;
    f->ptr  = c1->put_address;
    f->trap = nv3->pgraph.trapped_address & ~0x1FFCu;
    nv3_pfifo_cache1_decode(f->ptr, &f->bin, &f->ptr_mask, &f->slot_mask);
    f->key = addr & NV3_IMAGE_FAST_ADDR_MASK;
}

/* The next word of the image, sent to NV_USER address addr. False: it has to go the long way. */
bool
nv3_render_image_word(uint32_t addr, uint32_t val)
{
    nv3_image_fast_t  *f      = &nv3_image_fast;
    nv3_pfifo_cache_t *c1     = &nv3->pfifo.cache1_settings;
    uint8_t           *vram   = nv3->nvbase.svga.vram;
    uint32_t           method = addr & 0x1FFC;
    uint32_t           n      = nv3->pgraph.image_pixel_count;
    uint32_t           rx     = f->rx;
    uint32_t           slot   = f->bin & f->slot_mask;

    if ((c1->put_address != f->ptr) || (c1->get_address != f->ptr) || (nv3->pfifo.runout_get != nv3->pfifo.runout_put))
        return false;

    /* nv3_pfifo_cache1_push and _pull: in and out of an empty CACHE1 */
    slot ^= slot >> 1;
    nv3->pfifo.cache1_entries[slot].subchannel = (addr >> NV3_OBJECT_SUBMIT_SUBCHANNEL) & (NV3_DMA_SUBCHANNELS_PER_CHANNEL - 1);
    nv3->pfifo.cache1_entries[slot].method     = method;
    nv3->pfifo.cache1_entries[slot].data       = val;
    f->bin = (f->bin + 1) & f->ptr_mask;
    f->ptr = (f->bin ^ (f->bin >> 1)) << 2;
    c1->put_address = c1->get_address = f->ptr;

    /* nv3_pgraph_submit */
    nv3->pgraph.trapped_address = f->trap | method;
    nv3->pgraph.trapped_data    = val;

    /* nv3_render_blit_image */
    for (uint32_t i = 0; i < f->count; i++) {
        if (n >= f->total)
            break;
        n++;

        if ((rx >= f->rx0) && (rx < f->rx1) && (f->ry >= f->ry0) && (f->ry < f->ry1)) {
            uint32_t a = (uint32_t) (f->row + (int64_t) rx * f->cpp);
            uint32_t v = (((val >> (i * f->bits)) & f->pmask) & f->vmask) | f->vor;
            bool     changed;

            switch (f->cpp) {
                case 1:
                    changed = vram[a] != (uint8_t) v;
                    vram[a] = v;
                    break;
                case 2:
                    changed = *(uint16_t *) &vram[a] != (uint16_t) v;
                    *(uint16_t *) &vram[a] = v;
                    break;
                default:
                    changed = *(uint32_t *) &vram[a] != v;
                    *(uint32_t *) &vram[a] = v;
                    break;
            }
            if (changed)
                nv3->nvbase.svga.changedvram[a >> 12] = changeframecount;
        }

        if (++rx == f->w) {
            rx = 0;
            f->ry++;
            f->row += f->pitch;
        }
    }
    f->rx = rx;
    nv3->pgraph.image_pixel_count = n;
    return true;
}

void
nv3_render_blit_image(uint32_t color, nv3_grobj_t grobj)
{
    nv3_image_t *img = &nv3->pgraph.image;
    uint32_t     bits;
    uint32_t     count = nv3_render_pixels_per_word(grobj, &bits);
    uint32_t     total = (uint32_t) img->size_in.x * img->size_in.y;
    nv3_render_setup_t st;

    /* this word may not have come from the MMIO write (the DMA pusher): the pixel count moves on */
    nv3_image_fast.key = NV3_IMAGE_FAST_OFF;

    if (!img->size_in.x)
        return;
    nv3_render_setup(&st, grobj, false);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t n = nv3->pgraph.image_pixel_count;
        if (n >= total)
            return;
        nv3->pgraph.image_pixel_count++;

        uint32_t rx = n % img->size_in.x;
        uint32_t ry = n / img->size_in.x;
        if (rx >= img->size.x || ry >= img->size.y)
            continue;

        uint32_t pixel = (bits == 32) ? color : ((color >> (i * bits)) & ((1u << bits) - 1));
        nv3_render_pixel_st(&st, (int16_t) img->point.x + (int32_t) rx, (int16_t) img->point.y + (int32_t) ry,
                            nv3_render_expand_color(pixel, grobj));
    }

    if (!nv3_image_fast.known) {
        nv3_image_fast.known = true;
        nv3_image_fast.plain = nv3_render_image_fast_prepare(&st, grobj, bits, count);
    }
    nv3_image_fast.drew = nv3_image_fast.plain;
}

/* Bitmap from CPU (class 0x12): 32 monochrome pixels per method, COLOR0 for
   0 bits and COLOR1 for 1 bits (a colour with zero alpha draws nothing).
   The data comes as SIZE_IN images side by side, as many as it takes to
   cover SIZE_OUT's width; once they are all sent, the next word starts
   over at POINT. The Windows 3.x driver draws text a glyph at a time this
   way: SIZE_IN 8x16 set once, then POINT and SIZE_OUT per glyph, a glyph
   wider than 8 pixels sent as two 8-pixel strips. The bits left in the
   word that ends the bitmap are padding. */
void
nv3_render_bitmap(uint32_t data, nv3_grobj_t grobj)
{
    nv3_bitmap_t *bmp    = &nv3->pgraph.bitmap;
    uint32_t      strip  = (uint32_t) bmp->size_in.x * bmp->size_in.y;
    uint32_t      strips = 1;
    uint32_t      mono   = nv3_render_expand_mono(data, grobj);

    if (!strip)
        return;
    if (bmp->size.x > bmp->size_in.x)
        strips = (bmp->size.x + bmp->size_in.x - 1) / bmp->size_in.x;

    uint32_t total = strip * strips;
    nv3_render_setup_t st;

    nv3_render_setup(&st, grobj, false);

    if (nv3->pgraph.image_pixel_count >= total)
        nv3->pgraph.image_pixel_count = 0;

    for (int i = 0; i < 32; i++) {
        uint32_t n = nv3->pgraph.image_pixel_count;
        if (n >= total)
            return;
        nv3->pgraph.image_pixel_count++;

        uint32_t rx = (n / strip) * bmp->size_in.x + (n % strip) % bmp->size_in.x;
        uint32_t ry = (n % strip) / bmp->size_in.x;
        if (rx >= bmp->size.x || ry >= bmp->size.y)
            continue;

        nv3_render_pixel_st(&st, (int16_t) bmp->point.x + (int32_t) rx, (int16_t) bmp->point.y + (int32_t) ry,
                            ((mono >> i) & 1) ? bmp->color_1 : bmp->color_0);
    }
}

/* Can this blit be a nv3_render_copy_t: a plain copy to surfaces that share no bytes with its
   source or (unless laid out alike) each other, all of it inside VRAM? */
static bool
nv3_render_blit_plan(const nv3_render_setup_t *st, uint32_t src_buffer, int32_t px0, int32_t py0, int32_t x0, int32_t y0, int32_t cw, int32_t ch,
                     nv3_render_copy_t *c)
{
    uint32_t cpp  = st->cpp;
    int64_t  size = (int64_t) nv3->nvbase.svga.vram_mask + 1;
    int64_t  slo, shi;
    int64_t  lo[NV3_PGRAPH_MAX_BUFFERS], hi[NV3_PGRAPH_MAX_BUFFERS];

    if ((px0 < 0) || (py0 < 0) || (x0 < 0) || (y0 < 0) || !st->bufs)
        return false;
    if ((nv3->pgraph.boffset[src_buffer] | nv3->pgraph.bpitch[src_buffer]) & (cpp - 1))
        return false;
    slo = (int64_t) nv3->pgraph.boffset[src_buffer] + (int64_t) py0 * nv3->pgraph.bpitch[src_buffer] + (int64_t) px0 * cpp;
    shi = slo + (int64_t) (ch - 1) * nv3->pgraph.bpitch[src_buffer] + (int64_t) cw * cpp;
    if (shi > size)
        return false;

    c->src       = (uint32_t) slo;
    c->src_pitch = nv3->pgraph.bpitch[src_buffer];
    c->ndst      = 0;
    c->row_bytes = (uint32_t) cw * cpp;
    c->rows      = (uint32_t) ch;
    c->cpp       = cpp;
    for (uint32_t j = 0; j < NV3_PGRAPH_MAX_BUFFERS; j++) {
        if (!((st->bufs >> j) & 1))
            continue;
        if ((nv3->pgraph.boffset[j] | nv3->pgraph.bpitch[j]) & (cpp - 1))
            return false;
        lo[j] = (int64_t) nv3->pgraph.boffset[j] + (int64_t) y0 * nv3->pgraph.bpitch[j] + (int64_t) x0 * cpp;
        hi[j] = lo[j] + (int64_t) (ch - 1) * nv3->pgraph.bpitch[j] + (int64_t) cw * cpp;
        if ((hi[j] > size) || ((lo[j] < shi) && (slo < hi[j])))
            return false;
        for (uint32_t k = 0; k < j; k++) {
            if (!((st->bufs >> k) & 1))
                continue;
            if ((nv3->pgraph.boffset[j] == nv3->pgraph.boffset[k]) && (nv3->pgraph.bpitch[j] == nv3->pgraph.bpitch[k]))
                continue;
            if ((lo[j] < hi[k]) && (lo[k] < hi[j]))
                return false;
        }
        c->dst[c->ndst]       = (uint32_t) lo[j];
        c->dst_pitch[c->ndst] = nv3->pgraph.bpitch[j];
        c->ndst++;
    }
    if (!nv3_render_copy_ok(st))
        return false;
    nv3_render_copy_bits(st, &c->vmask, &c->vor);
    return true;
}

/* Screen to screen blit (class 0x10): from the object's source surface to
   its destination surfaces. The source rectangle is read first, so
   overlapping copies come out as if done in the right direction. */
void
nv3_render_blit_screen2screen(nv3_grobj_t grobj)
{
    nv3_blit_t *blit       = &nv3->pgraph.blit;
    int32_t     w          = blit->size.x;
    int32_t     h          = blit->size.y;
    int32_t     sx         = (int16_t) blit->point_in.x;
    int32_t     sy         = (int16_t) blit->point_in.y;
    int32_t     dx         = (int16_t) blit->point_out.x;
    int32_t     dy         = (int16_t) blit->point_out.y;
    uint32_t    src_buffer = (grobj.grobj_0 >> NV3_PGRAPH_CTX_SWITCH_SRC_BUFFER) & 0x03;
    static uint32_t *src;
    static size_t    src_size;
    nv3_render_setup_t st;

    if (w <= 0 || h <= 0)
        return;
    nv3_render_setup(&st, grobj, false);

    /* Only the part that lands inside the clip is drawn, so only that part is read */
    int32_t x0 = MAX(dx, st.cx0);
    int32_t y0 = MAX(dy, st.cy0);
    int32_t x1 = (int32_t) MIN((int64_t) dx + w, (int64_t) st.cx1);
    int32_t y1 = (int32_t) MIN((int64_t) dy + h, (int64_t) st.cy1);
    int32_t cw = x1 - x0;
    int32_t ch = y1 - y0;
    uint32_t cpp = st.cpp;

    if (cw <= 0 || ch <= 0)
        return;

    nv3_render_copy_t plan;

    if (nv3_render_blit_plan(&st, src_buffer, sx + (x0 - dx), sy + (y0 - dy), x0, y0, cw, ch, &plan)) {
        nv3_d3d_copy(&plan, NV3_SYNC_CLASS + nv3_pgraph_class10_blit);
        return;
    }
    nv3_d3d_sync(NV3_SYNC_CLASS + nv3_pgraph_class10_blit);

    if (((size_t) cw * ch) > src_size) {
        uint32_t *n = realloc(src, (size_t) cw * ch * sizeof(uint32_t));

        if (!n)
            return;
        src      = n;
        src_size = (size_t) cw * ch;
    }

    for (int32_t y = 0; y < ch; y++) {
        int32_t   py  = sy + (y0 - dy) + y;
        int32_t   px0 = sx + (x0 - dx);
        uint32_t *row = &src[(size_t) y * cw];
        uint32_t  addr;

        if ((py >= 0) && (px0 >= 0) && nv3_render_rows_direct(&st, src_buffer, px0, px0 + cw, py, &addr)) {
            const uint8_t *p = &nv3->nvbase.svga.vram[addr];

            for (int32_t x = 0; x < cw; x++)
                row[x] = (cpp == 1) ? p[x] : ((cpp == 2) ? ((const uint16_t *) p)[x] : ((const uint32_t *) p)[x]);
            continue;
        }
        for (int32_t x = 0; x < cw; x++) {
            int32_t px = px0 + x;

            row[x] = (px < 0 || py < 0) ? 0 : nv3_render_read_surface(src_buffer, px, py, cpp);
        }
    }

    /* A plain copy stores, for each source pixel, a value that depends on that pixel alone. It
       goes a row and a surface at a time, not a pixel at a time across the surfaces, so the
       destination surfaces must not share bytes unless they are laid out alike. */
    bool    copy = nv3_render_copy_ok(&st);
    int64_t lo[NV3_PGRAPH_MAX_BUFFERS], hi[NV3_PGRAPH_MAX_BUFFERS];

    for (uint32_t j = 0; copy && (j < NV3_PGRAPH_MAX_BUFFERS); j++) {
        if (!((st.bufs >> j) & 1))
            continue;
        lo[j] = (int64_t) nv3->pgraph.boffset[j] + (int64_t) y0 * nv3->pgraph.bpitch[j] + (int64_t) x0 * cpp;
        hi[j] = (int64_t) nv3->pgraph.boffset[j] + (int64_t) (y1 - 1) * nv3->pgraph.bpitch[j] + (int64_t) x1 * cpp;
        if (hi[j] > (int64_t) nv3->nvbase.svga.vram_mask + 1)
            copy = false;
        for (uint32_t k = 0; copy && (k < j); k++) {
            if (!((st.bufs >> k) & 1))
                continue;
            if ((nv3->pgraph.boffset[j] == nv3->pgraph.boffset[k]) && (nv3->pgraph.bpitch[j] == nv3->pgraph.bpitch[k]))
                continue;
            if ((lo[j] < hi[k]) && (lo[k] < hi[j]))
                copy = false;
        }
    }
    if (copy) {
        for (int32_t y = 0; y < ch; y++) {
            const uint32_t *row = &src[(size_t) y * cw];

            for (uint32_t j = 0; j < NV3_PGRAPH_MAX_BUFFERS; j++) {
                uint32_t addr;

                if (!((st.bufs >> j) & 1))
                    continue;
                if (nv3_render_rows_direct(&st, j, x0, x1, y0 + y, &addr)) {
                    nv3_render_copy_row(&st, addr, row, cw);
                    continue;
                }
                nv3_render_setup_t one = st;

                one.bufs = 1 << j;
                for (int32_t x = 0; x < cw; x++)
                    nv3_render_pixel_st(&one, x0 + x, y0 + y, nv3_render_expand_surface(st.fmt, row[x]));
            }
        }
        return;
    }

    for (int32_t y = 0; y < ch; y++) {
        for (int32_t x = 0; x < cw; x++)
            nv3_render_pixel_st(&st, x0 + x, y0 + y, nv3_render_expand_surface(st.fmt, src[(size_t) y * cw + x]));
    }
}

/* Stretched image from CPU (class 0x15): SIZE_IN source pixels, each
   covering DX/DU by DY/DV destination pixels (12.20 fixed point) from POINT
   (12.4), clipped by the class's own clip rectangle */
void
nv3_render_sifc_start(void)
{
    nv3->pgraph.image_pixel_count = 0;
}

static int32_t
nv3_render_sifc_first_pixel(int64_t edge)
{
    /* first pixel whose centre (p + 0.5) is at or past edge (20-bit fraction) */
    return (int32_t) ((edge - (1 << 19) + (1 << 20) - 1) >> 20);
}

void
nv3_render_sifc(uint32_t color, nv3_grobj_t grobj)
{
    nv3_stretched_image_from_cpu_t *sifc = &nv3->pgraph.stretched_image_from_cpu;
    uint32_t                        bits;
    uint32_t                        count = nv3_render_pixels_per_word(grobj, &bits);
    uint32_t                        total = (uint32_t) sifc->size_in.x * sifc->size_in.y;
    int64_t                         x0    = (int64_t) (int16_t) (sifc->point12d4 & 0xFFFF) << 16;
    int64_t                         y0    = (int64_t) (int16_t) (sifc->point12d4 >> 16) << 16;
    int32_t                         cx0   = (int16_t) sifc->clip_0.x;
    int32_t                         cy0   = (int16_t) sifc->clip_0.y;
    int32_t                         cx1   = cx0 + sifc->clip_1.x;
    int32_t                         cy1   = cy0 + sifc->clip_1.y;
    nv3_render_setup_t              st;

    if (!sifc->size_in.x)
        return;
    nv3_render_setup(&st, grobj, true);

    for (uint32_t i = 0; i < count; i++) {
        uint32_t n = nv3->pgraph.image_pixel_count;
        if (n >= total)
            return;
        nv3->pgraph.image_pixel_count++;

        uint32_t u     = n % sifc->size_in.x;
        uint32_t v     = n / sifc->size_in.x;
        uint32_t pixel = (bits == 32) ? color : ((color >> (i * bits)) & ((1u << bits) - 1));

        int32_t xa = nv3_render_sifc_first_pixel(x0 + (int64_t) u * sifc->delta_dx_du);
        int32_t xb = nv3_render_sifc_first_pixel(x0 + (int64_t) (u + 1) * sifc->delta_dx_du);
        int32_t ya = nv3_render_sifc_first_pixel(y0 + (int64_t) v * sifc->delta_dy_dv);
        int32_t yb = nv3_render_sifc_first_pixel(y0 + (int64_t) (v + 1) * sifc->delta_dy_dv);

        nv3_color_expanded_t c = nv3_render_expand_color(pixel, grobj);
        for (int32_t y = MAX(ya, cy0); y < MIN(yb, cy1); y++) {
            for (int32_t x = MAX(xa, cx0); x < MIN(xb, cx1); x++)
                nv3_render_pixel_st(&st, x, y, c);
        }
    }
}
