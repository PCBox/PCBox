/* Differential IFC, rectangle dependency ranges, and M2MF bulk reads. */
#include "../../src/video/nv/nv4/subsystems/nv4_pgraph_2d.c"

static uint32_t touch_lo, touch_hi, touch_count;
static uint8_t dma_source[16384], dma_dest[16384];
static uint32_t dma_test_limit, dma_reads;

uint32_t
nv4_dma_limit(nv4_t *nv4, uint32_t inst)
{
    return dma_test_limit;
}

void
nv4_3d_touch(nv4_t *nv4, uint32_t lo, uint32_t hi, int cause)
{
    touch_lo = lo;
    touch_hi = hi;
    touch_count++;
}
int
nv4_3d_fill(nv4_t *nv4, uint32_t base, uint32_t pitch, uint32_t lim, int bpp,
            int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint32_t val)
{
    return 0;
}

int
main(void)
{
    nv4_t    *card = calloc(1, sizeof(*card));
    monitor_t mon  = { 0 };
    uint8_t   vram[4096], reference[4096], dirty[1];
    uint32_t  rng            = 0x12345;
    int       accepted       = 0;
    const int formats[]      = { CF_Y8, CF_X24Y8, CF_A1R5G5B5, CF_X1R5G5B5, CF_R5G6B5, CF_A8R8G8B8, CF_X8R8G8B8 };
    card->vram_mask          = 4095;
    card->svga.vram          = vram;
    card->svga.changedvram   = dirty;
    card->svga.monitor       = &mon;
    mon.mon_changeframecount = 3;
    for (int i = 0; i < 20000; i++) {
        rng                = rng * 1664525u + 1013904223u;
        int           cfmt = formats[rng % 7];
        int           fmt  = 1 + ((rng >> 8) % 13);
        int           n    = cfmt == CF_Y8 || cfmt == CF_X24Y8 ? 4 : cfmt <= CF_R5G6B5 ? 2
                                                                                       : 1;
        int           bits = 32 / n;
        uint32_t      mask = n == 4 ? 255 : n == 2 ? 65535
                                                   : 0xffffffff;
        nv4_draw_t    d    = { 0 };
        nv4_gr_chan_t ch   = { 0 };
        d.nv4              = card;
        d.plain            = 1;
        d.dst.fmt          = fmt;
        d.dst.bpp          = nv4_surf_bpp(fmt);
        if (!d.dst.bpp)
            continue;
        d.dst.pitch = 64;
        d.dst.limit = 4095;
        d.cx0       = (rng >> 16) & 3;
        d.cy0       = 0;
        d.cx1       = 16;
        d.cy1       = 8;
        ch.img_kind = IMG_IFC;
        ch.in_w = ch.out_w = 16;
        ch.in_h = ch.out_h = 8;
        ch.pos             = (rng >> 20) % 128;
        ch.img_x           = (int) ((rng >> 12) & 7) - 3;
        ch.img_y           = (rng >> 15) & 1;
        memset(vram, 0xa5, sizeof(vram));
        memset(dirty, 0, sizeof(dirty));
        card->eng_bytes    = 0;
        nv4_gr_chan_t fast = ch;
        if (!nv4_img_plain_data(card, &d, &fast, cfmt, rng))
            continue;
        accepted++;
        memcpy(reference, vram, sizeof(vram));
        uint64_t cost    = card->eng_bytes;
        uint8_t  changed = dirty[0];
        memset(vram, 0xa5, sizeof(vram));
        memset(dirty, 0, sizeof(dirty));
        card->eng_bytes = 0;
        for (int k = 0; k < n && !nv4_img_done(&ch); k++)
            nv4_img_pixel(&d, &ch, nv4_color((rng >> (k * bits)) & mask, cfmt), 1);
        if (memcmp(reference, vram, sizeof(vram)) || ch.pos != fast.pos || cost != card->eng_bytes || changed != dirty[0]) {
            fprintf(stderr, "IFC mismatch iteration %d format %d destination %d\n", i, cfmt, fmt);
            return 1;
        }
    }
    /* A progress marker in a large window must touch only its own bytes.
       An overlapping marker must still trigger the dependency check. */
    for (int off = 0; off <= 2048; off += 2048) {
        nv4_draw_t d = { 0 };
        d.nv4 = card;
        d.dst.fmt = 13;
        d.dst.bpp = 4;
        d.dst.off = off;
        d.dst.pitch = 1280;
        d.dst.limit = 4095;
        d.cx1 = d.cy1 = 32768;
        d.plain = d.touch = 1;
        d.op = 3;
        d.hi = 4095;
        touch_count = 0;
        nv4_px_t px = { 0x12345678, 0x12345678 };
        nv4_fill(&d, 0, 0, 1, 1, px);
        if (touch_count != 1 || touch_lo != (uint32_t) off || touch_hi != (uint32_t) off + 3)
            abort();
    }
    /* Keep conservative synchronization when a pixel wraps VRAM. */
    {
        nv4_draw_t d = { 0 };
        d.nv4 = card;
        d.dst.fmt = 13;
        d.dst.bpp = 4;
        d.dst.off = 4094;
        d.dst.pitch = 1280;
        d.dst.limit = 8191;
        d.cx1 = d.cy1 = 32768;
        d.plain = d.touch = 1;
        d.op = 3;
        d.hi = 4095;
        touch_count = 0;
        nv4_px_t px = { 0x12345678, 0x12345678 };
        nv4_fill(&d, 0, 0, 1, 1, px);
        if (touch_count != 1 || touch_lo != 0 || touch_hi != 4095)
            abort();
    }
    /* Compare bulk M2MF input with byte reads, across pages, pitches,
       strides and a source limit partway through a row. */
    for (unsigned i = 0; i < sizeof(dma_source); i++)
        dma_source[i] = (i * 37 + (i >> 8)) & 255;
    for (int stride = 0; stride <= 3; stride++) {
        for (int short_input = 0; short_input < 2; short_input++) {
            nv4_gr_chan_t ch = { 0 };
            ch.m2mf_in = 4093;
            ch.m2mf_out = 7;
            ch.m2mf_pitch_in = 1100;
            ch.m2mf_pitch_out = 3100;
            ch.m2mf_len = 1000;
            ch.m2mf_count = 3;
            ch.m2mf_fmt = stride | (2 << 8);
            NV4_GR(card, NV4_PGRAPH_CTX_SWITCH3) = 1 | (2 << 16);
            dma_test_limit = short_input ? 5700 : sizeof(dma_source) - 1;
            memset(dma_dest, 0xa5, sizeof(dma_dest));
            dma_reads = 0;
            nv4_m2mf(card, &ch, 0);
            for (unsigned row = 0; row < 3; row++) {
                for (unsigned i = 0; i < 1000; i++) {
                    unsigned src = 4093 + row * 1100 + i * (stride ? stride : 1);
                    uint8_t want = src <= dma_test_limit ? dma_source[src] : 0;
                    if (dma_dest[7 + row * 3100 + i * 2] != want)
                        abort();
                    if (dma_dest[8 + row * 3100 + i * 2] != 0xa5)
                        abort();
                }
            }
            if (stride <= 1 && dma_reads > 3)
                abort();
        }
    }
    free(card);
    if (accepted < 1000)
        return 1;
    printf("nv4 2D tests passed (%d IFC words, rectangle ranges, M2MF strides/limits)\n", accepted);
    return 0;
}

void
nv4_gap(nv4_t *nv4, int cls, uint32_t mthd, const char *what)
{
    abort();
}

void
nv4_pgraph_set_grobj(nv4_t *nv4, int word, int shift, int bits, uint32_t val)
{
    abort();
}

uint32_t
nv4_ramin_read32(nv4_t *nv4, uint32_t off)
{
    abort();
}

int
nv4_3d_joins(nv4_t *nv4, uint32_t base, uint32_t pitch, uint32_t lo, uint32_t hi)
{
    abort();
}

int
nv4_3d_fill_rop(nv4_t *nv4, uint32_t base, uint32_t pitch, uint32_t lim, int bpp, int32_t x0, int32_t y0,
                int32_t x1, int32_t y1, const uint32_t rop[7])
{
    abort();
}

void
nv4_3d_sync(nv4_t *nv4, int cause)
{
    abort();
}

int
nv4_3d_copy(nv4_t *nv4, uint32_t src, uint32_t dst, uint32_t spitch, uint32_t dpitch, uint32_t len, uint32_t rows)
{
    abort();
}

int
nv4_dma_read32(nv4_t *nv4, uint32_t inst, uint32_t offset, uint32_t *val)
{
    abort();
}

int
nv4_dma_read(nv4_t *nv4, uint32_t inst, uint32_t offset, uint8_t *buf, uint32_t len)
{
    dma_reads++;
    for (uint32_t i = 0; i < len; i++) {
        if (offset + i > dma_test_limit)
            return NV4_DMA_FAULT;
        buf[i] = dma_source[offset + i];
    }
    return NV4_DMA_OK;
}

int
nv4_dma_write(nv4_t *nv4, uint32_t inst, uint32_t offset, const uint8_t *buf, uint32_t len)
{
    if ((uint64_t) offset + len > sizeof(dma_dest))
        abort();
    memcpy(dma_dest + offset, buf, len);
    return NV4_DMA_OK;
}

void
nv4_pgraph_notify_write(nv4_t *nv4, uint32_t index, uint32_t info32)
{
}

void
nv4_pmc_update_irq(nv4_t *nv4)
{
    abort();
}
