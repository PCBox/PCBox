/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          Tests of the TNT and GeForce GPU lane (nv4_pgraph_3d_gpu.c) against
 *          the CPU: every batch is shaded by the lane and again by the
 *          CPU (NV4_3D_VERIFY), and no byte may differ -- a batch of
 *          states running different kernel variants, ROP fills joining
 *          a batch, and a swizzled target (rows that alias: one
 *          workgroup in order). Skipped without a Vulkan device.
 *
 *          The 3D code is included as C, as in nv4_3d_swizzle_test.c,
 *          whose stand-ins for the rest of the card this repeats; the
 *          lane and nv_gpu_vk.c are linked as they are.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../src/video/nv/nv4/subsystems/nv4_pgraph_3d.c"

#define VRAM_SIZE (1 << 20)
#define COL_BASE  0x10000
#define Z_BASE    0x20000

#define CHECK(cond)                                                    \
    do {                                                               \
        if (!(cond)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #cond); \
            failures++;                                                \
        }                                                              \
    } while (0)

static int failures;

/* What the 3D code reaches outside itself: nothing here textures,
   threads or uses the GPU lane. */
uint64_t tsc;
double   cpuclock = 1.0;
uint32_t mem_size;
uint8_t *ram;

/* the lane's and the verifier's lines, for a failure's story */
void
pclog(const char *fmt, ...)
{
    va_list ap;

    if (!getenv("NV4_TEST_LOG") && !strstr(fmt, "verify: flush"))
        return;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

void
fatal(const char *fmt, ...)
{
    va_list ap;

    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
    exit(2);
}

/* ROP3 from its truth table */
static uint32_t
nv4_rop3_ref(uint8_t rop, uint32_t p, uint32_t s, uint32_t d)
{
    uint32_t r = 0;

    for (int b = 0; b < 32; b++) {
        int i = (((p >> b) & 1) << 2) | (((s >> b) & 1) << 1) | ((d >> b) & 1);

        r |= (uint32_t) ((rop >> i) & 1) << b;
    }
    return r;
}

int
agpgart_resolve(uint32_t addr, uint32_t *ram_addr)
{
    (void) addr;
    (void) ram_addr;
    return 0;
}

uint8_t
mem_readb_phys(uint32_t addr)
{
    return addr < mem_size * 1024u ? ram[addr] : 0;
}

uint16_t
mem_readw_phys(uint32_t addr)
{
    (void) addr;
    return 0;
}

uint32_t
mem_readl_phys(uint32_t addr)
{
    (void) addr;
    return 0;
}

int
nv4_dma_translate(nv4_t *nv4, uint32_t inst, uint32_t offset, int write, uint32_t *addr, int *target)
{
    (void) write;
    if ((inst != 1 && inst != 2) || offset > nv4->vram_mask)
        return !NV4_DMA_OK;
    *target = inst == 2 ? 2 : 0; /* PCI memory or VRAM */
    *addr   = offset + (inst == 2 ? VRAM_SIZE : 0);
    return NV4_DMA_OK;
}

int
nv4_dma_read(nv4_t *card, uint32_t inst, uint32_t offset, uint8_t *buf, uint32_t len)
{
    uint32_t addr;
    int target;
    if ((uint64_t) offset + len > card->vram_size || nv4_dma_translate(card, inst, offset, 0, &addr, &target) != NV4_DMA_OK)
        return NV4_DMA_FAULT;
    memcpy(buf, (target ? ram : card->svga.vram) + addr, len);
    return NV4_DMA_OK;
}

uint32_t
nv4_dma_page_left(nv4_t *nv4, uint32_t inst, uint32_t offset)
{
    (void) nv4;
    (void) inst;
    (void) offset;
    return 0x1000 - (offset & 0xfff);
}

void
nv4_pgraph_2d_flush_run(nv4_t *nv4)
{
    (void) nv4;
}

void
nv4_pgraph_set_grobj(nv4_t *nv4, int word, int shift, int bits, uint32_t val)
{
    (void) nv4;
    (void) word;
    (void) shift;
    (void) bits;
    (void) val;
}

uint32_t
nv4_ramin_read32(nv4_t *nv4, uint32_t off)
{
    (void) nv4;
    (void) off;
    return 0;
}

void
dma_bm_read(uint32_t phys_addr, uint8_t *data, uint32_t total_size, int transfer_size)
{
    (void) transfer_size;
    for (uint32_t i = 0; i < total_size; i++)
        data[i] = mem_readb_phys(phys_addr + i);
}

/* nv_gpu_vk.c's telemetry file (NV_GPU_TELEMETRY, never set here) */
char usr_path[1024];

FILE *
plat_fopen(const char *path, const char *mode)
{
    return fopen(path, mode);
}

void
path_append_filename(char *dest, const char *s1, const char *s2)
{
    sprintf(dest, "%s/%s", s1, s2);
}

/* the lane's variant compiler runs at once: every variant is ready */
thread_t *
thread_create_named(void (*thread_func)(void *param), void *param, const char *name)
{
    (void) name;
    thread_func(param);
    return NULL;
}

int
thread_wait(thread_t *arg)
{
    (void) arg;
    return 0;
}

event_t *
thread_create_event(void)
{
    return NULL;
}

void
thread_set_event(event_t *arg)
{
    (void) arg;
}

void
thread_reset_event(event_t *arg)
{
    (void) arg;
}

int
thread_wait_event(event_t *arg, int timeout)
{
    (void) arg;
    (void) timeout;
    return 0;
}

void
thread_destroy_event(event_t *arg)
{
    (void) arg;
}

/* ------------------------------------------------------------------ */
/* ------------------------------------------------------------------ */
static nv4_t    *nv4;
static monitor_t mon;

/* the GPU lane is there (a Vulkan device); else every test is skipped */
static int
card_new(nv4_chip_t chip)
{
    nv4                      = calloc(1, sizeof(nv4_t));
    mem_size                 = 2 * VRAM_SIZE / 1024;
    ram                      = calloc(1, mem_size * 1024u);
    nv4->chip                = chip;
    nv4->vram_size           = VRAM_SIZE;
    nv4->vram_mask           = VRAM_SIZE - 1;
    nv4->svga.vram           = calloc(1, VRAM_SIZE + 4096); /* as svga_init: the lane moves it */
    nv4->svga.changedvram    = calloc(1, VRAM_SIZE >> 12);
    nv4->svga.monitor        = &mon;
    nv4->render_threads      = 1;
    nv4->gpu_raster          = 1;
    mon.mon_changeframecount = 2;
    nv4_3d_init(nv4);
    return nv4->batch3d->gpu != NULL;
}

static void
card_free(void)
{
    nv4_3d_close(nv4);
    free(nv4->svga.vram);
    free(nv4->svga.changedvram);
    free(nv4);
    free(ram);
}

/* After a flush: every flush verified and none differed; the GPU took
   <batches> batches more than before and left none to the CPU */
static uint64_t seen_batches;

static void
check_gpu(uint64_t batches, const char *what)
{
    uint64_t n, f;

    nv4_gpu_counts(nv4->batch3d->gpu, &n, &f);
    if (nv4->batch3d->vbad || f || (n - seen_batches != batches)) {
        fprintf(stderr, "  %s: %u of %u flushes differ, GPU took %llu (want %llu), left %llu\n", what, nv4->batch3d->vbad,
                nv4->batch3d->vflushes, (unsigned long long) (n - seen_batches), (unsigned long long) batches,
                (unsigned long long) f);
        failures++;
    }
    seen_batches = n;
}

/* The 3D surfaces as 0x53 leaves them: color A8R8G8B8 at COL_BASE,
   depth at Z_BASE (64 KB limits each); swizzled 2^wl by 2^hl, or rows
   of <pitch> (64 pixels by 32 rows). The user clip is the surface. */
static void
surfaces(int swz, int wl, int hl, uint32_t pitch, int zeta)
{
    NV4_GR(nv4, NV4_PGRAPH_SURFACE)         = swz ? 2 : 1;
    NV4_GR(nv4, NV4_PGRAPH_BSWIZZLE2)       = ((uint32_t) hl << 24) | ((uint32_t) wl << 16);
    NV4_GR(nv4, NV4_PGRAPH_BPIXEL)          = 0xc << 8;
    NV4_GR(nv4, NV4_PGRAPH_BBASE + 2 * 4)   = COL_BASE;
    NV4_GR(nv4, NV4_PGRAPH_BOFFSET + 2 * 4) = 0;
    NV4_GR(nv4, NV4_PGRAPH_BPITCH + 2 * 4)  = pitch;
    NV4_GR(nv4, NV4_PGRAPH_BLIMIT + 2 * 4)  = 0xffff;
    NV4_GR(nv4, NV4_PGRAPH_BBASE + 3 * 4)   = Z_BASE;
    NV4_GR(nv4, NV4_PGRAPH_BOFFSET + 3 * 4) = 0;
    NV4_GR(nv4, NV4_PGRAPH_BPITCH + 3 * 4)  = swz ? 0 : pitch;
    NV4_GR(nv4, NV4_PGRAPH_BLIMIT + 3 * 4)  = zeta ? 0xffff : 0x80000000;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MIN)       = 0;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MIN + 4)   = 0;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MAX)       = swz ? (1u << wl) : 64;
    NV4_GR(nv4, NV4_PGRAPH_UCLIP_MAX + 4)   = swz ? (1u << hl) : 32;
    nv4_3d_flush(nv4);
}

static uint32_t
fbits(float f)
{
    uint32_t u;

    memcpy(&u, &f, 4);
    return u;
}

static void
mthd(uint32_t m, uint32_t d)
{
    CHECK(nv4_pgraph_3d_method(nv4, 0, 0x54, m, d) == NV4_GR_OK);
}

/* DX5 state, untextured: depth test LESS with writes when <zbuf>,
   alpha blending (source alpha, inverse source alpha) when <blend> */
static void
dx5_state(int zbuf, int blend)
{
    mthd(0x308, 0);
    mthd(0x310, blend ? ((1u << 20) | (5u << 24) | (6u << 28)) : 0);
    mthd(0x314, zbuf ? ((0x3fu << 24) | (2u << 16) | (1u << 14)) : 0);
}

static void
vertex(int i, float x, float y, float z, uint32_t color)
{
    uint32_t m = 0x400 + i * 0x20;

    mthd(m + 0x00, fbits(x));
    mthd(m + 0x04, fbits(y));
    mthd(m + 0x08, fbits(z));
    mthd(m + 0x0c, fbits(1.0f));
    mthd(m + 0x10, color);
    mthd(m + 0x14, 0);
}

/* A triangle, colors per vertex */
static void
tri(float x0, float y0, float x1, float y1, float x2, float y2, float z, uint32_t c0, uint32_t c1, uint32_t c2)
{
    vertex(0, x0, y0, z, c0);
    vertex(1, x1, y1, z + 0.1f, c1);
    vertex(2, x2, y2, z - 0.1f, c2);
    mthd(0x600, 0x000210);
}

/* A scene starts on known bytes, with nothing in the batch */
static void
fill_vram(void)
{
    nv4_3d_flush(nv4);
    nv4_gpu_wait(nv4->batch3d->gpu, nv4);
    for (uint32_t a = 0; a < 0x40000; a++)
        nv4->svga.vram[a] = (uint8_t) ((a * 0x9e3779b1u) >> 24);
}

/* Overlapping triangles under four states (depth, blending, both,
   neither): runs of different kernel variants in one batch */
static void
test_variants(void)
{
    fill_vram();
    surfaces(0, 0, 0, 256, 1);
    for (int i = 0; i < 6; i++) {
        float o = (float) i * 3.0f;

        dx5_state(1, 0);
        tri(1 + o, 1, 60, 5 + o, 10, 30, 0.3f + 0.05f * i, 0xff102030, 0xff405060, 0xff708090);
        dx5_state(0, 1);
        tri(2, 2 + o, 63, 10, 20 + o, 31, 0.5f, 0x80ff0000, 0x4000ff00, 0xc00000ff);
        dx5_state(1, 1);
        tri(0, 31, 40 + o, 0, 63, 20, 0.4f, 0x60abcdef, 0xa0123456, 0x20fedcba);
        dx5_state(0, 0);
        tri(5, 5, 30, 8 + o, 8, 25, 0.2f, 0xff00ffff, 0xffff00ff, 0xffffff00);
    }
    nv4_3d_flush(nv4);
    check_gpu(1, "variants");
}

/* ROP fills joining a batch that draws into their rows, under ROPs
   with and without the destination, a mono pattern and a transparent
   color: the GPU's (d & A) | (~d & B) */
static void
test_ropfill(void)
{
    static const uint8_t rops[] = { 0x55, 0x5a, 0x66, 0xa0, 0x96, 0xf0, 0x1d };

    fill_vram();
    surfaces(0, 0, 0, 256, 0);
    dx5_state(0, 0);
    tri(0, 0, 64, 0, 0, 32, 0.5f, 0xff112233, 0xff445566, 0xff778899);
    for (unsigned i = 0; i < sizeof(rops); i++) {
        uint32_t r[7];
        uint32_t s = 0x13579bdf * (i + 1), p0 = 0x00ff00ff ^ i, p1 = 0xff00ff00 ^ (i << 8);

        r[0] = nv4_rop3_ref(rops[i], p0, s, 0xffffffffu);
        r[1] = nv4_rop3_ref(rops[i], p0, s, 0);
        r[2] = nv4_rop3_ref(rops[i], p1, s, 0xffffffffu);
        r[3] = nv4_rop3_ref(rops[i], p1, s, 0);
        r[4] = 0x8142a5c3 ^ i;
        r[5] = 0x3cf00f96;
        r[6] = NV4_ROPFILL_PAT | (i % 3) | ((i & 1) ? NV4_ROPFILL_SKIP0 : 0);
        CHECK(nv4_3d_fill_rop(nv4, COL_BASE, 256, COL_BASE + 0xffff, 4, (int) i, (int) i * 2, 40 + (int) i, 20 + (int) i,
                              r));
    }
    nv4_3d_flush(nv4);
    check_gpu(1, "ROP fills");
}

/* A swizzled target with depth: rows alias, so the GPU takes the batch
   as one workgroup in order */
static void
test_serial(void)
{
    fill_vram();
    surfaces(1, 5, 4, 0, 1);
    for (int i = 0; i < 5; i++) {
        dx5_state(1, i & 1);
        tri(0, (float) i, 32, 3.0f * i, 4.0f * i, 16, 0.2f + 0.1f * i, 0xa0ff8000, 0x6000ff80, 0xff8000ff);
    }
    nv4_3d_flush(nv4);
    check_gpu(1, "swizzled target");
}

/* Screamer 4x4 draws with adjacent depth/color surfaces. These batches
   must use the parallel kernel. */
static void
test_adjacent_depth(void)
{
    fill_vram();
    surfaces(0, 0, 0, 256, 1);
    NV4_GR(nv4, NV4_PGRAPH_BBASE + 3 * 4) = COL_BASE - 32 * 256;
    dx5_state(1, 1);
    tri(0, 0, 64, 4, 20, 32, 0.3f, 0x80102030, 0x80405060, 0x80708090);
    CHECK(!nv4->batch3d->serial);
    CHECK(nv4->batch3d->nregions == 2);
    nv4_3d_flush(nv4);
    check_gpu(1, "adjacent depth");
}

static void
test_deferred_gpu_copy(void)
{
    fill_vram();
    surfaces(0, 0, 0, 256, 0);
    dx5_state(0, 0);
    nv4->batch3d->verify = 0; /* leave the submission in flight */
    tri(0, 0, 64, 0, 0, 32, 0.5f, 0xff112233, 0xff445566, 0xff778899);
    memset(nv4->svga.vram + 0x40000, 0, 8192);
    CHECK(nv4_3d_copy(nv4, COL_BASE, 0x40000, 256, 256, 256, 32));
    CHECK(nv4->batch3d->copy.pending);
    nv4_3d_vblank(nv4, 0x40000, 0x41fff);
    CHECK(!nv4->batch3d->copy.pending);
    CHECK(!memcmp(nv4->svga.vram + COL_BASE, nv4->svga.vram + 0x40000, 8192));
    check_gpu(1, "deferred copy");
    nv4->batch3d->verify = 1;
}

/* Reuse staged texture bytes, then change them between batches. The
   verifier compares every textured batch with the CPU renderer. */
static void
test_texture_cache(void)
{
    fill_vram();
    surfaces(0, 0, 0, 256, 0);
    NV4_GR(nv4, NV4_PGRAPH_CTX_SWITCH3) = 1;
    for (int pass = 0; pass < 4; pass++) {
        if (pass == 0 || pass == 2)
            memset(nv4->svga.vram + 0x30000, pass ? 0x69 : 0x35, 4096);
        dx5_state(0, 0);
        mthd(0x304, 0x30000);
        mthd(0x308, (7u << 8) | (1u << 12) | (5u << 16) | (5u << 20));
        mthd(0x310, 7);
        tri(0, 0, 64, 0, 0, 32, 0.5f, 0xffffffff, 0xffffffff, 0xffffffff);
        nv4_3d_flush(nv4);
        check_gpu(1, "texture cache");
    }
    NV4_GR(nv4, NV4_PGRAPH_CTX_SWITCH3) = 0;
}

/* Use the screen-space submission entry points that follow hardware T&L.
   Each flush compares the Vulkan result with the serial rasterizer and
   requires a completed GPU batch. */
static void
celsius_tri(nv4_3d_state_t *s)
{
    const float xy[3][2] = { { 0, 0 }, { 32, 0 }, { 0, 24 } };
    float a[3][AT_COUNT] = { { 0 } }, ax[3][AX_COUNT] = { { 0 } };
    int si = nv4_3d_place(nv4, s);

    for (int i = 0; i < 3; i++) {
        a[i][AT_Q] = 0.5f + i;
        a[i][AT_Z] = 8100 + i * 400;
        a[i][AT_R] = 100 + i * 71;
        a[i][AT_G] = 210 - i * 67;
        a[i][AT_B] = 34 + i * 86;
        a[i][AT_A] = 200;
        a[i][AT_FOG] = 180 + i * 30;
        a[i][AT_U0] = xy[i][0] * 0.35f - 1;
        a[i][AT_V0] = xy[i][1] * 0.55f - 2;
        a[i][AT_U1] = -a[i][AT_V0];
        a[i][AT_V1] = a[i][AT_U0];
        ax[i][AX_R0] = 3 - i;
        ax[i][AX_R1] = -1 - i;
        ax[i][AX_Q0] = 1 + i * 0.25f;
        ax[i][AX_Q1] = 1 + i * 0.125f;
        ax[i][AX_SA] = 43 + i * 60;
    }
    nv4_3d_raster(nv4, si, xy, a, ax);
}

static void
test_celsius(void)
{
    nv4_3d_state_t s = { .celsius = 1, .col_base = COL_BASE, .z_base = Z_BASE,
                         .col_pitch = 256, .z_pitch = 128, .col_lim = COL_BASE + 0xffff,
                         .z_lim = Z_BASE + 0xffff, .col_fmt = 12, .bpp = 4, .z_bpp = 2,
                         .cx1 = 64, .cy1 = 32, .z_on = 1, .zen = 1, .zfunc = 8,
                         .zwrite = 1, .zclip1 = 65535, .wr_r = 1, .wr_g = 1, .wr_b = 1,
                         .wr_a = 1, .rc_n = 1, .rc_f0 = 4, .rc_f1 = 0x1500 };

    fill_vram();
    celsius_tri(&s);
    nv4_3d_flush(nv4);
    check_gpu(1, "Celsius vertex color, specular alpha and Z16");
    CHECK(nv4->svga.vram[COL_BASE + 2] == 100);
    s.col_fmt = 5;
    s.bpp = 2;
    s.z_bpp = 4;
    s.z_pitch = 256;
    s.dither = 1;
    s.sten = s.swrite = 1;
    s.sfunc = 8;
    s.smask_w = 255;
    s.sop_zpass = 4;
    s.ablend = 1;
    s.bcolor = 0x9234b7c8;
    s.rc_f0 = 0x03200405; /* fog factor/color via the final combiner below */
    s.rc_f1 = 0x1300;
    for (int i = 0; i < 16; i++) {
        s.sblend = i;
        s.dblend = 15 - i;
        s.beq = i % 5;
        s.fogmode = i % 4;
        s.wbuf = i & 1;
        celsius_tri(&s);
        nv4_3d_flush(nv4);
        check_gpu(1, "Celsius blend equation, fog and Z24S8");
    }
    s.ablend = 0;
    s.wr_g = 0;
    if (nv4->chip == NV4_CHIP_NV15) {
        for (int op = 0; op < 16; op++) {
            s.logic_op = 16 | op;
            celsius_tri(&s);
            nv4_3d_flush(nv4);
            check_gpu(1, "NV15 masked logic operation");
        }
        s.logic_op = 0;
    }
    s.wr_g = 1;
    s.tex[0] = (nv4_3d_tex_t) {
        .on = 1, .dma = 1, .offset = 0x30000, .wlog = 3, .hlog = 3, .levels = 1,
        .size = 512, .wrap_u = 1, .wrap_v = 2, .min = 2, .mag = 2, .center = 1, .lod_max = 3
    };
    s.rc_f0 = 8; /* texture 0 RGB and alpha */
    s.rc_f1 = 0x1800;
    memcpy(ram + VRAM_SIZE + 0x31000, nv4->svga.vram + 0x45000, 4096);
    for (int f = 0; f <= 0x18; f++) {
        nv4_3d_tex_t *t = &s.tex[0];
        if (f == 0x0d)
            continue;
        t->cfmt = f;
        t->pal = f >= 8 && f <= 11;
        t->pal_dma = 2;
        t->pal_off = 0x31000;
        t->linear = f >= 0x10;
        t->w = 7;
        t->h = 5;
        t->pitch = 32;
        t->bpp = (t->pal || f < 2 || f == 0x13 || f == 0x14) ? 1 :
                 ((f == 6 || f == 7 || f == 0x12) ? 4 : 2);
        t->dxt = f == 12 ? 1 : (f == 14 ? 3 : (f == 15 ? 5 : 0));
        celsius_tri(&s);
        nv4_3d_flush(nv4);
        check_gpu(1, "Celsius texture format");
    }

    /* One texel on a 32-bit surface gives exact readbacks for every
       key operation, including keeping the old pixel on discard. */
    s.bpp = 4;
    s.col_fmt = 12;
    s.dither = 0;
    s.tex[0].cfmt = 0x12;
    s.tex[0].bpp = 4;
    s.tex[0].w = s.tex[0].h = 1;
    s.tex[0].wrap_u = s.tex[0].wrap_v = 3;
    s.ckey[0] = 0x123456;
    for (int op = 0; op < 4; op++) {
        uint32_t texel = 0xde123456u, old = 0xcafebabeu, pixel;
        memcpy(nv4->svga.vram + s.tex[0].offset, &texel, sizeof(texel));
        memcpy(nv4->svga.vram + COL_BASE, &old, sizeof(old));
        s.tex[0].ckey_op = op;
        celsius_tri(&s);
        nv4_3d_flush(nv4);
        check_gpu(1, "Celsius texture color key");
        memcpy(&pixel, nv4->svga.vram + COL_BASE, sizeof(pixel));
        CHECK(pixel == (op == 0 ? texel : op == 1 ? s.ckey[0] : op == 2 ? 0 : old));
    }
    s.tex[0].ckey_op = 0;
    s.tex[0].cfmt = 6;
    s.tex[0].bpp = 4;
    s.tex[0].linear = 0;
    s.tex[0].levels = 3;
    s.tex[0].level_off[1] = 256;
    s.tex[0].level_off[2] = 320;
    s.tex[0].cube = 1;
    s.tex[0].face = 384;
    s.tex[0].size = 6 * 384;
    s.tex[1] = s.tex[0];
    s.tex[1].cube = 0;
    s.tex[1].dma = 2;
    s.tex[1].offset += 0x1000;
    s.rc_ic[0] = 0x08090420; /* texture0 * texture1, primary * one */
    s.rc_ia[0] = 0x18191420;
    s.rc_oa[0] = s.rc_oc[0] = 0x0c00;
    s.rc_f0 = 12;
    s.rc_f1 = 0x1c00;
    s.swz = 1;
    s.swz_wl = 6;
    s.swz_hl = 5;
    for (int mode = 1; mode <= 6; mode++) {
        s.tex[0].min = mode;
        s.tex[1].min = 7 - mode;
        s.tex[1].wrap_u = mode;
        s.tex[1].akill = 1;
        celsius_tri(&s);
        nv4_3d_flush(nv4);
        check_gpu(1, "Celsius dual-texture mip, cube and swizzled target");
    }
    s.rc_n = 2;
    s.rc_ic[1] = 0x4c292185; /* signed inputs, both dot products and mux */
    s.rc_ia[1] = 0x9c153c14;
    s.rc_oc[1] = 0x08037ccd;
    s.rc_oa[1] = 0x24c0c;
    s.rc_k[0] = 0x3070e4c2;
    s.rc_f0 = 0x010f0e00;
    s.rc_f1 = 0x0c041c80;
    celsius_tri(&s);
    nv4_3d_flush(nv4);
    check_gpu(1, "Celsius two combiner stages and final products");

    /* The CPU must sample a palette that the same draw can write. */
    s.swz = 0;
    s.tex[0].cube = 0;
    s.tex[0].pal = 1;
    s.tex[0].cfmt = 11;
    s.tex[0].bpp = 1;
    s.tex[0].pal_dma = 1;
    s.tex[0].pal_off = COL_BASE;
    celsius_tri(&s);
    CHECK(nv4->batch3d->serial);
    nv4_3d_flush(nv4);
    uint64_t batches, fallbacks;
    nv4_gpu_counts(nv4->batch3d->gpu, &batches, &fallbacks);
    CHECK(batches == seen_batches && fallbacks == 1 && !nv4->batch3d->vbad);
}

int
main(void)
{
    putenv("NV4_3D_VERIFY=1");
    putenv("NV4_GPU_RUN_MIN=1"); /* a run per state change: several variants in a batch */
    if (!card_new(NV4_CHIP_TNT)) {
        printf("no Vulkan device: GPU tests skipped\n");
        card_free();
        return 0;
    }
    test_variants();
    test_ropfill();
    test_serial();
    test_adjacent_depth();
    test_texture_cache();
    test_deferred_gpu_copy();
    card_free();
    seen_batches = 0;
    if (!card_new(NV4_CHIP_NV10))
        return 1;
    test_celsius();
    card_free();
    seen_batches = 0;
    if (!card_new(NV4_CHIP_NV15))
        return 1;
    test_celsius();
    card_free();
    if (failures)
        fprintf(stderr, "%d failure(s)\n", failures);
    else
        printf("nv4 3D GPU tests passed\n");
    return failures != 0;
}
