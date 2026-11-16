/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA GeForce 256 (NV10) -- the Celsius class (0x56), the
 *          chip's 3D engine with transform and lighting.
 *
 *          Methods load three kinds of state, as the hardware keeps it:
 *
 *          - the bundle registers (0x400e00-0x400ea8) and the transform
 *            unit's modes (0x400f00-0x400f4c), in the PGRAPH register
 *            file: textures, register combiners, raster and blend state;
 *          - the transform pipe's memory, reached by the resman through
 *            PIPE_ADDRESS/PIPE_DATA: the current vertex attributes (the
 *            VAB, 0x4400), the matrices and other 4-vectors (0x6400), the
 *            lighting 3-vectors (0x6800) and scalars (0x6c00-0x7800);
 *          - per channel, what the hardware holds in its vertex fetch and
 *            primitive assembly: the vertex arrays, the primitive begun,
 *            the vertices a strip or fan still needs.
 *
 *          A vertex goes through the transform unit as envytools models it
 *          (nvhw/pgraph_celsius_xfrm.c): either bypassed (pre-transformed
 *          vertices, scaled and offset by the second model view matrix) or
 *          transformed by the composite matrix, its texture coordinates
 *          generated and transformed, lit by up to eight lights and given
 *          a fog value. Window coordinates span -2048..2048 with OpenGL
 *          pixel centres; the D3D driver offsets its viewport by half a
 *          pixel. Primitives are assembled, clipped against w (and a guard
 *          band), culled, offset, and handed to the TNT's batch
 *          (nv4_pgraph_3d.c) as screen-space triangles under a draw state
 *          built from the bundle, which the CPU span shader then shades
 *          through the register combiners.
 *
 *          Method semantics follow envytools' hardware tests (hwtest
 *          pgraph_class_celsius.cc; MIT licensed, by the envytools
 *          contributors) and the public nv32.h class definitions; the
 *          specular, spotlight and fog parameters as NVIDIA's NV10 D3D
 *          driver computes them (nvCelsiusState.cpp).
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *          Bachimus, <mszoopers@protonmail.com>
 *
 *          Copyright 2026 Marcos Alves
 *          Copyright 2026 Bachimus
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/nv/vid_nv4.h>

#include "nv4_pgraph_3d.h"

#define GR(a) NV4_GR(nv4, a)

/* The bundle */
#define NV10_TEX_OFFSET(i)   (0x400e00 + (i) * 4)
#define NV10_TEX_PALETTE(i)  (0x400e08 + (i) * 4)
#define NV10_TEX_FORMAT(i)   (0x400e10 + (i) * 4)
#define NV10_TEX_CONTROL0(i) (0x400e18 + (i) * 4)
#define NV10_TEX_CONTROL1(i) (0x400e20 + (i) * 4)
#define NV10_TEX_CONTROL2(i) (0x400e28 + (i) * 4)
#define NV10_TEX_RECT(i)     (0x400e30 + (i) * 4)
#define NV10_TEX_FILTER(i)   (0x400e38 + (i) * 4)
#define NV10_RC_IN_ALPHA(i)  (0x400e40 + (i) * 4)
#define NV10_RC_IN_COLOR(i)  (0x400e48 + (i) * 4)
#define NV10_RC_FACTOR(i)    (0x400e50 + (i) * 4)
#define NV10_RC_OUT_ALPHA(i) (0x400e58 + (i) * 4)
#define NV10_RC_OUT_COLOR(i) (0x400e60 + (i) * 4)
#define NV10_RC_FINAL0       0x400e68
#define NV10_RC_FINAL1       0x400e6c
#define NV10_CONFIG_A        0x400e70
#define NV10_STENCIL_A       0x400e74
#define NV10_STENCIL_B       0x400e78
#define NV10_CONFIG_B        0x400e7c
#define NV10_BLEND           0x400e80
#define NV10_BLEND_COLOR     0x400e84
#define NV10_RASTER          0x400e88
#define NV10_FOG_COLOR       0x400e8c
#define NV10_POFS_FACTOR     0x400e90
#define NV10_POFS_UNITS      0x400e94
#define NV10_DEPTH_NEAR      0x400e98
#define NV10_DEPTH_FAR       0x400e9c
#define NV10_TEX_CKEY(i)     (0x400ea0 + (i) * 4)
#define NV10_POINT_SIZE      0x400ea8
/* The transform unit */
#define NV10_WCLIP_H(i)      (0x400f00 + (i) * 4)
#define NV10_WCLIP_V(i)      (0x400f20 + (i) * 4)
#define NV10_XF_A            0x400f40
#define NV10_XF_B            0x400f44
#define NV10_CONFIG_C        0x400f48
#define NV10_CELSIUS_DMA     0x400f4c /* vertex 15:0, state 31:16 */
#define NV10_PIPE_ADDRESS    0x400f50
/* The 3D surface clip: x and y */
#define NV10_UCLIP3D_MIN     0x400550
#define NV10_UCLIP3D_MAX     0x400558

/* XF_A */
#define XFA_BYPASS   (1u << 28)
#define XFA_WEIGHT   (1u << 27)
/* XF_B */
#define XFB_LIGHTING (1u << 29)
#define XFB_NORMALIZE (1u << 30)
#define XFB_LOCALEYE (1u << 28)

/* The transform pipe's memory, in words */
#define PIPE_VAB  (0x4400 / 4) /* 7 attributes of 4 */
#define PIPE_XFRM (0x6400 / 4) /* 4-vectors, a slot every 16 bytes */
#define PIPE_LV   (0x6800 / 4) /* 3-vectors */
#define PIPE_SA   (0x6c00 / 4) /* scalars */
#define PIPE_SB   (0x7000 / 4)
#define PIPE_SC   (0x7400 / 4)
#define PIPE_SD   (0x7800 / 4)

/* VAB attributes: 0 position, 1 diffuse, 2 specular (its alpha the fog
   coordinate, 11), 3 and 4 the texture coordinates, 5 the normal, 6 the
   skinning weight (24). The vertex arrays add 7, fog. */
#define VAB_WORDS 28

/* Transform slots */
#define XF_MV0   0x00
#define XF_IMV0  0x04
#define XF_PROJ  0x08 /* the composite matrix without skinning */
#define XF_MV1   0x0c /* bypass: 0xc scale, 0xd or 0xe offset */
#define XF_IMV1  0x10
#define XF_TG0   0x14 /* texgen planes, then the texture matrix, per unit */
#define XF_TX0   0x18
#define XF_LPOS  0x24
#define XF_SPOT  0x2c
#define XF_EYE   0x34
#define XF_FOGP  0x38
#define XF_VIEWP 0x39

/* A transformed vertex. Bypassed, X Y Z are window coordinates and W
   the reciprocal w; transformed, X Y Z W are clip coordinates. Texture
   coordinates are not yet divided by w; colours are 0-1. */
enum {
    V_X,
    V_Y,
    V_Z,
    V_W,
    V_T0,
    V_T1  = V_T0 + 4,
    V_C0  = V_T1 + 4,
    V_C1  = V_C0 + 4,
    V_FOG = V_C1 + 4,
    V_N
};

typedef struct {
    float f[V_N];
} nv10_vtx_t;

/* A vertex on screen: x, y, then the batch's interpolants */
enum {
    R_X,
    R_Y,
    R_A,
    R_AX = R_A + AT_COUNT,
    R_N  = R_AX + AX_COUNT
};

typedef struct {
    float f[R_N];
} nv10_rv_t;

#define NV10_MAXV 16

/* The guard band rasterized (the batch takes coordinates below 4096),
   and the smallest w kept by clipping */
#define NV10_GB0   (-64.0f)
#define NV10_GB1   (4032.0f)
#define NV10_W_EPS (1.0e-5f)

typedef struct {
    uint32_t   va_off[8], va_fmt[8]; /* vertex arrays */
    int        prim;                 /* BEGIN_END: 0 none, 1-10 */
    int        n;                    /* vertices of the primitive so far */
    nv10_vtx_t first, last[3];       /* last[2] the latest */
    uint32_t   inl[32];              /* an inline vertex being received */
    int        ninl;
} nv10_chan_t;

typedef struct nv10_celsius_s {
    nv10_chan_t ch[NV4_MAX_CHANNELS];
} nv10_celsius_t;

/* ------------------------------------------------------------------ */
/* Helpers.                                                            */
/* ------------------------------------------------------------------ */
static inline float
u2f(uint32_t v)
{
    union {
        uint32_t u;
        float    f;
    } c = { v };

    return c.f;
}

static inline uint32_t
f2u(float f)
{
    union {
        float    f;
        uint32_t u;
    } c = { f };

    return c.u;
}

static inline float
pipe_f(const nv4_t *nv4, int word)
{
    return u2f(nv4->pgraph_pipe[word & 0x1fff]);
}

static inline void
xf_row(const nv4_t *nv4, int slot, float r[4])
{
    for (int k = 0; k < 4; k++)
        r[k] = pipe_f(nv4, PIPE_XFRM + slot * 4 + k);
}

static inline void
lv_get(const nv4_t *nv4, int slot, float r[3])
{
    for (int k = 0; k < 3; k++)
        r[k] = pipe_f(nv4, PIPE_LV + slot * 4 + k);
}

static inline float
ls_get(const nv4_t *nv4, int base, int i)
{
    return pipe_f(nv4, base + i * 4);
}

static inline float
dot3(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2];
}

static inline float
dot4(const float *a, const float *b)
{
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3];
}

/* d[r] = a . row r of the matrix at <slot> */
static void
nv10_mmul(const nv4_t *nv4, float d[4], const float a[4], int slot)
{
    float row[4], r[4];

    for (int i = 0; i < 4; i++) {
        xf_row(nv4, slot + i, row);
        r[i] = dot4(a, row);
    }
    memcpy(d, r, sizeof(r));
}

static void
nv10_mmul3(const nv4_t *nv4, float d[4], const float a[4], int slot)
{
    float row[4], r[4];

    for (int i = 0; i < 4; i++) {
        xf_row(nv4, slot + i, row);
        r[i] = dot3(a, row);
    }
    memcpy(d, r, sizeof(r));
}

/* Normalizes a's xyz; returns its length */
static float
nv10_norm3(float a[3])
{
    float d = dot3(a, a);
    float l;

    if (!(d > 0.0f))
        return 0.0f;
    l = sqrtf(d);
    a[0] /= l;
    a[1] /= l;
    a[2] /= l;
    return l;
}

static inline float
clamp01(float v)
{
    return (v > 0.0f) ? ((v < 1.0f) ? v : 1.0f) : 0.0f;
}

/* Texture formats (nv32.h NV056_SET_TEXTURE_FORMAT_COLOR): bytes a texel */
static int
nv10_tex_bpp(int cfmt)
{
    switch (cfmt) {
        case 0x00:
        case 0x01:
        case 0x08:
        case 0x09:
        case 0x0a:
        case 0x0b:
        case 0x13:
        case 0x14:
            return 1;
        case 0x06:
        case 0x07:
        case 0x12:
            return 4;
        default:
            return 2;
    }
}

static int
nv10_bpp(int sfmt)
{
    if ((sfmt >= 2) && (sfmt <= 6))
        return 2;
    if ((sfmt >= 7) && (sfmt <= 0xd))
        return 4;
    return (sfmt == 1) ? 1 : 0;
}

/* ------------------------------------------------------------------ */
/* Registers.                                                          */
/* ------------------------------------------------------------------ */
static uint32_t
nv10_reg_mask(uint32_t addr)
{
    if ((addr >= 0x400f00) && (addr < 0x400f40))
        return 0x0fff0fff;
    switch (addr) {
        case 0x400e08:
        case 0x400e0c:
            return 0xffffffc1;
        case 0x400e10:
        case 0x400e14:
            return 0xffffffd6;
        case 0x400e18:
        case 0x400e1c:
            return 0x7fffffff;
        case 0x400e20:
        case 0x400e24:
            return 0xffff0000;
        case 0x400e30:
        case 0x400e34:
            return 0x07ff07ff;
        case 0x400e38:
        case 0x400e3c:
            return 0x77001fff;
        case 0x400e58:
        case 0x400e5c:
            return 0x0003cfff;
        case 0x400e60:
            return 0x0003ffff;
        case 0x400e64:
            return 0x3803ffff;
        case NV10_RC_FINAL0:
            return 0x3f3f3f3f;
        case NV10_RC_FINAL1:
            return 0x3f3f3fe0;
        case NV10_CONFIG_A:
            return 0x3fcf5fff;
        case NV10_STENCIL_A:
            return 0xfffffff1;
        case NV10_STENCIL_B:
            return 0x00000fff;
        case NV10_CONFIG_B:
            return 0x00003ff5;
        case NV10_BLEND:
            return 0x00000fff;
        case NV10_RASTER:
            return 0xfcffffcf;
        case NV10_POINT_SIZE:
            return 0x000001ff;
        case NV10_XF_A:
            return 0x3bffffff;
        case NV10_CONFIG_C:
            return 0x17ff0117;
        default:
            return 0xffffffff;
    }
}

void
nv10_celsius_reg_write(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    GR(addr) = val & nv10_reg_mask(addr);
    nv4->celsius_si = -1;
}

static void
nv10_set(nv4_t *nv4, uint32_t addr, uint32_t val)
{
    nv10_celsius_reg_write(nv4, addr, val);
}

/* Bits <shift>+<bits>-1:<shift> of register <addr> */
static void
nv10_insrt(nv4_t *nv4, uint32_t addr, int shift, int bits, uint32_t val)
{
    uint32_t m = ((bits >= 32) ? 0xffffffffu : ((1u << bits) - 1)) << shift;

    nv10_set(nv4, addr, (GR(addr) & ~m) | ((val << shift) & m));
}

uint32_t
nv10_celsius_pipe_read(nv4_t *nv4)
{
    uint32_t a = GR(NV10_PIPE_ADDRESS) & 0x7ffc;
    uint32_t v = nv4->pgraph_pipe[a >> 2];

    GR(NV10_PIPE_ADDRESS) = (a + 4) & 0x7ffc;
    return v;
}

void
nv10_celsius_pipe_write(nv4_t *nv4, uint32_t val)
{
    uint32_t a = GR(NV10_PIPE_ADDRESS) & 0x7ffc;

    nv4->pgraph_pipe[a >> 2] = val;
    GR(NV10_PIPE_ADDRESS)    = (a + 4) & 0x7ffc;
}

/* ------------------------------------------------------------------ */
/* The draw state.                                                     */
/* ------------------------------------------------------------------ */
/* TEX_FORMAT keeps SetTextureFormat's two-valued fields (1 or 2) as one
   bit each, the upper (envytools: val & 0xffffffd6): bit 1 set is
   context DMA B, clear DMA A; bit 4 ORIGIN_ZOH and bit 6 ORIGIN_FOH
   CORNER. */
static void
nv10_tex_setup(nv4_t *nv4, nv4_3d_tex_t *t, int n)
{
    uint32_t fmt  = GR(NV10_TEX_FORMAT(n));
    uint32_t c0   = GR(NV10_TEX_CONTROL0(n));
    uint32_t filt = GR(NV10_TEX_FILTER(n));
    int      cf   = (fmt >> 7) & 0x1f;
    uint32_t lo   = 0;
    int      maxl;

    memset(t, 0, sizeof(*t));
    if (!(c0 & (1u << 30)) || (cf == 0x0d) || (cf > 0x18))
        return;
    t->cfmt   = cf;
    t->dma    = (fmt & 2) ? (GR(NV4_PGRAPH_CTX_SWITCH3) >> 16) : (GR(NV4_PGRAPH_CTX_SWITCH3) & 0xffff);
    t->offset = GR(NV10_TEX_OFFSET(n));
    t->dxt    = (cf == 0x0c) ? 1 : ((cf == 0x0e) ? 3 : ((cf == 0x0f) ? 5 : 0));
    t->linear = (cf >= 0x10);
    t->bpp    = nv10_tex_bpp(cf);
    t->wlog   = (fmt >> 16) & 0xf;
    t->hlog   = (fmt >> 20) & 0xf;
    if (t->wlog > 11)
        t->wlog = 11;
    if (t->hlog > 11)
        t->hlog = 11;
    t->levels = (fmt >> 12) & 0xf;
    maxl      = ((t->wlog > t->hlog) ? t->wlog : t->hlog) + 1;
    if (t->levels > maxl)
        t->levels = maxl;
    if (t->levels < 1)
        t->levels = 1;
    if (t->linear) {
        /* IMAGE formats: one level of any size, rows of the pitch */
        t->w      = (GR(NV10_TEX_RECT(n)) >> 16) & 0x7ff;
        t->h      = GR(NV10_TEX_RECT(n)) & 0x7ff;
        t->pitch  = GR(NV10_TEX_CONTROL1(n)) >> 16;
        t->levels = 1;
        if (!t->w || !t->h)
            return;
        t->size = t->pitch * (uint32_t) t->h;
    } else {
        for (int l = 0; l < t->levels; l++) {
            int wl = (t->wlog - l > 0) ? t->wlog - l : 0;
            int hl = (t->hlog - l > 0) ? t->hlog - l : 0;

            t->level_off[l] = lo;
            if (t->dxt) {
                uint32_t bw = ((1u << wl) + 3) >> 2;
                uint32_t bh = ((1u << hl) + 3) >> 2;

                lo += bw * bh * ((t->dxt == 1) ? 8 : 16);
            } else
                lo += (uint32_t) t->bpp << (wl + hl);
        }
        t->size = lo;
        if (fmt & 4) {
            /* six faces in D3D's order, each padded */
            uint32_t al = (t->dxt || ((cf >= 0x08) && (cf <= 0x0b))) ? 32 : 128;

            t->cube = 1;
            t->face = (lo + al - 1) & ~(al - 1);
            t->size = t->face * 6;
        }
    }
    t->wrap_u   = (fmt >> 24) & 7;
    t->wrap_v   = (fmt >> 28) & 7;
    t->center   = (fmt >> 6) & 1;
    t->min      = (filt >> 24) & 7;
    t->mag      = (filt >> 28) & 7;
    /* bias: 5.8 signed; the clamps: 4.8 */
    t->lod_bias = (float) ((int32_t) (filt << 19) >> 19) / 256.0f;
    t->lod_min  = (float) ((c0 >> 18) & 0xfff) / 256.0f;
    t->lod_max  = (float) ((c0 >> 6) & 0xfff) / 256.0f;
    t->ckey_op  = c0 & 3;
    t->akill    = (c0 >> 2) & 1;
    if ((cf >= 0x08) && (cf <= 0x0b)) {
        uint32_t pal = GR(NV10_TEX_PALETTE(n));

        t->pal     = 1;
        t->pal_dma = (pal & 1) ? (GR(NV4_PGRAPH_CTX_SWITCH3) >> 16) : (GR(NV4_PGRAPH_CTX_SWITCH3) & 0xffff);
        t->pal_off = pal & 0xffffffc0;
    }
    t->on = (t->dma != 0);
}

/* The surface, clip and per-pixel state for Celsius triangles; 0 with
   no colour surface to draw into */
static int
nv10_make_state(nv4_t *nv4, nv4_3d_state_t *s)
{
    uint32_t cfga = GR(NV10_CONFIG_A);
    uint32_t sta  = GR(NV10_STENCIL_A);
    uint32_t stb  = GR(NV10_STENCIL_B);
    uint32_t bl   = GR(NV10_BLEND);
    uint32_t lim;
    int      zfmt;
    int32_t  x0, x1, y0, y1;

    memset(s, 0, sizeof(*s));
    s->celsius   = 1;
    s->dx6       = 1;
    s->swz       = (GR(NV4_PGRAPH_SURFACE) & 3) == 2;
    s->swz_wl    = (GR(NV4_PGRAPH_BSWIZZLE2) >> 16) & 0xf;
    s->swz_hl    = (GR(NV4_PGRAPH_BSWIZZLE2) >> 24) & 0xf;
    s->col_fmt   = (GR(NV4_PGRAPH_BPIXEL) >> 8) & 0xf;
    s->bpp       = nv10_bpp(s->col_fmt);
    s->col_base  = GR(NV4_PGRAPH_BBASE + NV4_SURF_COLOR * 4) + GR(NV4_PGRAPH_BOFFSET + NV4_SURF_COLOR * 4);
    s->col_pitch = GR(NV4_PGRAPH_BPITCH + NV4_SURF_COLOR * 4) & 0xffff;
    lim          = GR(NV4_PGRAPH_BLIMIT + NV4_SURF_COLOR * 4);
    if ((lim & 0x80000000) || !s->bpp)
        return 0;
    s->col_lim = GR(NV4_PGRAPH_BBASE + NV4_SURF_COLOR * 4) + (lim & 0x7fffffff);
    zfmt       = (GR(NV4_PGRAPH_BPIXEL) >> 12) & 0xf;
    s->z_bpp   = (zfmt == 1) ? 2 : 4;
    s->z_base  = GR(NV4_PGRAPH_BBASE + NV4_SURF_ZETA * 4) + GR(NV4_PGRAPH_BOFFSET + NV4_SURF_ZETA * 4);
    s->z_pitch = GR(NV4_PGRAPH_BPITCH + NV4_SURF_ZETA * 4) & 0xffff;
    lim        = GR(NV4_PGRAPH_BLIMIT + NV4_SURF_ZETA * 4);
    s->z_on    = !(lim & 0x80000000) && zfmt && (s->z_pitch || s->swz);
    s->z_lim   = GR(NV4_PGRAPH_BBASE + NV4_SURF_ZETA * 4) + (lim & 0x7fffffff);

    /* the surface clip (never set: the whole surface) */
    x0 = GR(NV10_UCLIP3D_MIN) & 0xffff;
    y0 = GR(NV10_UCLIP3D_MIN + 4) & 0xffff;
    x1 = (int32_t) (GR(NV10_UCLIP3D_MAX) << 14) >> 14;
    y1 = (int32_t) (GR(NV10_UCLIP3D_MAX + 4) << 14) >> 14;
    if (!x0 && !x1 && !y0 && !y1)
        x1 = y1 = 4096;
    /* the window clip rectangles, inclusive, in window coordinates; an
       inactive one is all zero (its single pixel is off any surface).
       Exclusive rectangles are not modeled. */
    if (!(GR(NV10_CONFIG_C) & 0x10)) {
        int32_t wx0 = 4096, wy0 = 4096, wx1 = -1, wy1 = -1;
        int     any = 0;

        for (int i = 0; i < 8; i++) {
            uint32_t h = GR(NV10_WCLIP_H(i));
            uint32_t v = GR(NV10_WCLIP_V(i));

            if (!h && !v)
                continue;
            any = 1;
            if ((int32_t) ((h & 0xfff) ^ 0x800) < wx0)
                wx0 = (h & 0xfff) ^ 0x800;
            if ((int32_t) (((h >> 16) & 0xfff) ^ 0x800) > wx1)
                wx1 = ((h >> 16) & 0xfff) ^ 0x800;
            if ((int32_t) ((v & 0xfff) ^ 0x800) < wy0)
                wy0 = (v & 0xfff) ^ 0x800;
            if ((int32_t) (((v >> 16) & 0xfff) ^ 0x800) > wy1)
                wy1 = ((v >> 16) & 0xfff) ^ 0x800;
        }
        if (any) {
            if (wx0 > x0)
                x0 = wx0;
            if (wy0 > y0)
                y0 = wy0;
            if (wx1 + 1 < x1)
                x1 = wx1 + 1;
            if (wy1 + 1 < y1)
                y1 = wy1 + 1;
        }
    }
    if (s->swz) {
        if (x1 > (1 << s->swz_wl))
            x1 = 1 << s->swz_wl;
        if (y1 > (1 << s->swz_hl))
            y1 = 1 << s->swz_hl;
    } else if (s->col_pitch && (x1 > (int32_t) (s->col_pitch / s->bpp)))
        x1 = s->col_pitch / s->bpp;
    s->cx0 = (x0 < 0) ? 0 : x0;
    s->cy0 = (y0 < 0) ? 0 : y0;
    s->cx1 = (x1 > 4096) ? 4096 : x1;
    s->cy1 = (y1 > 4096) ? 4096 : y1;
    if ((s->cx1 <= s->cx0) || (s->cy1 <= s->cy0))
        return 0;
    s->origin = 0.5f;

    nv10_tex_setup(nv4, &s->tex[0], 0);
    nv10_tex_setup(nv4, &s->tex[1], 1);
    s->ckey[0] = GR(NV10_TEX_CKEY(0));
    s->ckey[1] = GR(NV10_TEX_CKEY(1));
    for (int i = 0; i < 2; i++) {
        s->rc_ia[i] = GR(NV10_RC_IN_ALPHA(i));
        s->rc_ic[i] = GR(NV10_RC_IN_COLOR(i));
        s->rc_oa[i] = GR(NV10_RC_OUT_ALPHA(i));
        s->rc_oc[i] = GR(NV10_RC_OUT_COLOR(i));
        s->rc_k[i]  = GR(NV10_RC_FACTOR(i));
    }
    s->rc_f0 = GR(NV10_RC_FINAL0);
    s->rc_f1 = GR(NV10_RC_FINAL1);
    s->rc_n  = (s->rc_oc[1] >> 28) & 3;
    if (s->rc_n < 1)
        s->rc_n = 1;
    if (s->rc_n > 2)
        s->rc_n = 2;

    s->persp     = 1;
    s->fog_color = GR(NV10_FOG_COLOR);
    s->fogmode   = GR(NV10_CONFIG_C) & 7;
    s->ablend    = (bl >> 3) & 1;
    s->beq       = bl & 7;
    s->sblend    = (bl >> 4) & 0xf;
    s->dblend    = (bl >> 8) & 0xf;
    s->bcolor    = GR(NV10_BLEND_COLOR);
    /* comparisons are OpenGL's (& 0xf: 0 never - 7 always), one below
       the TNT's */
    s->atest  = (cfga >> 12) & 1;
    s->afunc  = ((cfga >> 8) & 0xf) + 1;
    s->aref   = cfga & 0xff;
    s->zen    = ((cfga >> 14) & 1) && s->z_on;
    s->zfunc  = ((cfga >> 16) & 0xf) + 1;
    s->zwrite = ((cfga >> 14) & 1) && ((cfga >> 24) & 1) && s->z_on;
    s->wbuf   = (cfga >> 23) & 1;
    s->dither = (cfga >> 22) & 1;
    s->wr_a   = (cfga >> 26) & 1;
    s->wr_r   = (cfga >> 27) & 1;
    s->wr_g   = (cfga >> 28) & 1;
    s->wr_b   = (cfga >> 29) & 1;
    s->sten      = (sta & 1) && s->z_on && (s->z_bpp == 4);
    s->swrite    = (cfga >> 25) & 1;
    s->sfunc     = ((sta >> 4) & 0xf) + 1;
    s->sref      = (sta >> 8) & 0xff;
    s->smask_r   = (sta >> 16) & 0xff;
    s->smask_w   = (sta >> 24) & 0xff;
    s->sop_fail  = stb & 0xf;
    s->sop_zfail = (stb >> 4) & 0xf;
    s->sop_zpass = (stb >> 8) & 0xf;
    s->zclip0    = u2f(GR(NV10_DEPTH_NEAR));
    s->zclip1    = u2f(GR(NV10_DEPTH_FAR));
    if (!(s->zclip1 > s->zclip0)) {
        s->zclip0 = -1e30f;
        s->zclip1 = 1e30f;
    }
    /* a depth surface neither tested nor written is left alone */
    if (!s->zwrite && !s->sten && (!s->zen || (s->zfunc == 8))) {
        s->z_on = 0;
        s->zen  = 0;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Transform and lighting.                                             */
/* ------------------------------------------------------------------ */
/* The lighting unit (envytools' pgraph_celsius_lt_full): primary and
   secondary colours from the lights, the material and the vertex
   colours. Specular highlights and spotlight falloff are the rational
   approximations of a power the driver programs, (x + L) / (M x + N). */
static void
nv10_light(nv4_t *nv4, uint32_t xa, uint32_t xb, const float *icol0, const float *icol1, const float nrm[3],
           const float ev[3], const float epos3[3], float col0[3], float col1[3])
{
    int   att_mode  = (xa >> 18) & 1;
    int   spec_in   = (xa >> 19) & 1;
    int   spec_out  = (xa >> 20) & 1;
    int   lm_e      = (xa >> 21) & 1;
    int   lm_a      = (xa >> 22) & 1;
    int   lm_d      = (xa >> 23) & 1;
    int   lm_s      = (xa >> 24) & 1;
    int   eye_local = !!(xb & XFB_LOCALEYE);
    float amb[3], emi[3], e[3];

    lv_get(nv4, 0x29, amb);
    lv_get(nv4, 0x2a, emi);
    for (int k = 0; k < 3; k++)
        col0[k] = lm_a ? (icol0[k] * amb[k] + emi[k]) : (amb[k] + (lm_e ? icol0[k] : 0.0f));
    lv_get(nv4, 0x2c, col1);
    if (eye_local)
        memcpy(e, ev, sizeof(e));
    else
        lv_get(nv4, 0x28, e);

    for (int i = 0; i < 8; i++) {
        int   mode = (xa >> (i * 2)) & 3;
        int   zero = 0;
        float l[3], ca, cd, cs, t, b;
        float lc[3];

        if (!mode)
            continue;
        if (mode == 1) {
            lv_get(nv4, i * 5 + 4, l);
            ca = 1.0f;
        } else {
            float p[4], k[3], d, d2, att, dr;

            xf_row(nv4, XF_LPOS + i, p);
            l[0] = p[0] - epos3[0];
            l[1] = p[1] - epos3[1];
            l[2] = p[2] - epos3[2];
            d2   = dot3(l, l);
            d    = nv10_norm3(l);
            lv_get(nv4, i * 5 + 3, k);
            att = k[0] + k[1] * d + k[2] * d2;
            ca  = att_mode ? att : ((att != 0.0f) ? 1.0f / att : 0.0f);
            /* beyond the light's range */
            dr = d * ls_get(nv4, PIPE_SA, 1) + ls_get(nv4, PIPE_SB, 3 + i);
            if (dr < 0.0f)
                zero = 1;
            if (mode == 3) {
                float sd[4], x;

                xf_row(nv4, XF_SPOT + i, sd);
                x = dot3(l, sd) + sd[3];
                if (x <= 0.0f)
                    zero = 1;
                else {
                    float sl = ls_get(nv4, PIPE_SB, 11 + i);
                    float sm = ls_get(nv4, PIPE_SC, 4 + i);
                    float sn = ls_get(nv4, PIPE_SD, 3 + i);
                    float den, f;

                    if (x > 1.0f)
                        x = 1.0f;
                    den = sm * x + sn;
                    f   = (den != 0.0f) ? (x + sl) / den : 1.0f;
                    ca *= clamp01(f);
                }
            }
            if (zero)
                ca = 0.0f;
        }

        cd = dot3(nrm, l);
        if (cd < 0.0f)
            zero = 1;
        if (zero)
            cd = 0.0f;
        cd *= ca;

        if (!eye_local && (mode == 1)) {
            float hv[3];
            float s;

            lv_get(nv4, i * 5 + 3, hv);
            s = dot3(nrm, hv);
            t = s + ls_get(nv4, PIPE_SB, 1);
            b = s * ls_get(nv4, PIPE_SC, 1) + ls_get(nv4, PIPE_SD, 2);
        } else {
            float h[3] = { e[0] + l[0], e[1] + l[1], e[2] + l[2] };
            float hd   = dot3(h, h);
            float s    = dot3(nrm, h);
            float ss   = s * s;

            if (s < 0.0f)
                zero = 1;
            t = ss + hd * ls_get(nv4, PIPE_SA, 2);
            b = ss * ls_get(nv4, PIPE_SC, 2) + hd * ls_get(nv4, PIPE_SC, 3);
        }
        if ((t < 0.0f) || (b == 0.0f))
            zero = 1;
        cs = zero ? 0.0f : (t / b) * ca;

        lv_get(nv4, i * 5 + 0, lc);
        for (int k = 0; k < 3; k++)
            col0[k] += ca * lc[k] * (lm_a ? icol0[k] : 1.0f);
        lv_get(nv4, i * 5 + 1, lc);
        for (int k = 0; k < 3; k++)
            col0[k] += cd * lc[k] * (lm_d ? icol0[k] : 1.0f);
        lv_get(nv4, i * 5 + 2, lc);
        for (int k = 0; k < 3; k++) {
            float v = cs * lc[k];

            if (lm_s)
                v *= spec_in ? icol1[k] : icol0[k];
            if (spec_out)
                col1[k] += v;
            else
                col0[k] += v;
        }
    }
}

/* One vertex through the transform unit, from the attributes in <vab> */
static void
nv10_xform(nv4_t *nv4, const float *vab, nv10_vtx_t *o)
{
    uint32_t xa  = GR(NV10_XF_A);
    uint32_t xb  = GR(NV10_XF_B);
    float   *f   = o->f;
    int      fgm = (xa >> 16) & 3;
    float    lv[3];

    memset(o, 0, sizeof(*o));
    if (xa & XFA_BYPASS) {
        float sc[4], of[4], in, af;

        xf_row(nv4, XF_MV1, sc);
        xf_row(nv4, (xa & (1u << 29)) ? 0x0e : 0x0d, of);
        for (int k = 0; k < 3; k++)
            f[V_X + k] = vab[k] * sc[k] + of[k];
        f[V_W] = vab[3] * sc[3];
        memcpy(&f[V_T0], &vab[12], 8 * sizeof(float));
        memcpy(&f[V_C0], &vab[4], 7 * sizeof(float));
        f[V_C1 + 3] = 0.0f;
        /* fog: from the fog coordinate, z, or w */
        in = (fgm == 0) ? vab[11] : ((fgm == 1) ? vab[2] : ((f[V_W] != 0.0f) ? 1.0f / f[V_W] : 0.0f));
        lv_get(nv4, (fgm == 0) ? 0x28 : 0x29, lv);
        af = lv[0] + lv[1] + lv[2];
        lv_get(nv4, 0x2b, lv);
        f[V_FOG] = lv[0] * in + af;
        return;
    }

    {
        static const float k2[4]    = { 2.0f, 2.0f, 2.0f, 2.0f }; /* the pipe's constants (0x35-0x37) */
        static const float zdir[4]  = { 0.0f, 0.0f, 1.0f, 0.0f };
        static const float half[4]  = { 0.5f, 0.5f, 0.0f, 0.0f };
        const float       *ipos     = &vab[0];
        int                weight   = !!(xa & XFA_WEIGHT);
        float              w        = vab[24];
        float              iw       = 1.0f - w;
        float              epos[4], cpos[4], nrm[4], epos3[4], uev[4], ev[4], ed, dist;
        float              rp[4], rm[4], sm[4];
        float              e0[4];

        /* position: eye space, then clip space */
        nv10_mmul(nv4, e0, ipos, XF_MV0);
        if (weight) {
            float e1[4];

            nv10_mmul(nv4, e1, ipos, XF_MV1);
            for (int k = 0; k < 3; k++)
                epos[k] = e0[k] * w + e1[k] * iw;
            epos[3] = e0[3];
            nv10_mmul(nv4, cpos, epos, XF_PROJ);
        } else {
            memcpy(epos, e0, sizeof(epos));
            nv10_mmul(nv4, cpos, ipos, XF_PROJ);
        }
        memcpy(&f[V_X], cpos, sizeof(cpos));

        /* normal */
        {
            float n0[4], in4[4] = { vab[20], vab[21], vab[22], 0.0f };

            nv10_mmul3(nv4, n0, in4, XF_IMV0);
            if (weight) {
                float n1[4];

                nv10_mmul3(nv4, n1, in4, XF_IMV1);
                for (int k = 0; k < 3; k++)
                    nrm[k] = n0[k] * w + n1[k] * iw;
            } else
                memcpy(nrm, n0, sizeof(nrm));
            nrm[3] = 0.0f;
            if (xb & XFB_NORMALIZE)
                nv10_norm3(nrm);
        }

        /* the eye vector, its distance */
        {
            float rwe = (epos[3] != 0.0f) ? 1.0f / epos[3] : 0.0f;
            float eye[4];

            for (int k = 0; k < 4; k++)
                epos3[k] = epos[k] * rwe;
            xf_row(nv4, XF_EYE, eye);
            for (int k = 0; k < 3; k++)
                uev[k] = eye[k] - epos3[k];
            ed = dot3(uev, uev);
            memcpy(ev, uev, sizeof(ev));
            dist  = nv10_norm3(ev);
            ev[3] = dist;
        }

        /* reflection and sphere maps */
        {
            float d = dot3(ev, nrm) * k2[0];
            float l;

            for (int k = 0; k < 3; k++) {
                rp[k] = nrm[k] * d + zdir[k] - ev[k];
                rm[k] = rp[k] - zdir[k];
            }
            rp[3] = rm[3] = 0.0f;
            l = sqrtf(dot3(rp, rp));
            for (int k = 0; k < 4; k++)
                sm[k] = ((l > 0.0f) ? rp[k] / l : 0.0f) * half[k] + half[k];
        }

        /* texture coordinates: generated, then the texture matrix (the
           NV10 forms q from the transformed s) */
        for (int i = 0; i < 2; i++) {
            uint32_t tc = (xb >> (i * 14)) & 0x3fff;
            float    p[4], m[4];

            memcpy(p, &vab[12 + i * 4], sizeof(p));
            if (tc & 1) {
                for (int j = 0; j < 4; j++) {
                    int   mode = (j == 3) ? ((tc >> 12) & 3) : ((tc >> (3 + j * 3)) & 7);
                    float pl[4];

                    switch (mode) {
                        case 1:
                            xf_row(nv4, XF_TG0 + i * 8 + j, pl);
                            p[j] = dot4(epos, pl);
                            break;
                        case 2:
                            xf_row(nv4, XF_TG0 + i * 8 + j, pl);
                            p[j] = dot4(ipos, pl);
                            break;
                        case 3:
                            if (j < 2)
                                p[j] = sm[j];
                            break;
                        case 4:
                            p[j] = nrm[j];
                            break;
                        case 5:
                            p[j] = rm[j];
                            break;
                        default:
                            break;
                    }
                }
                if (tc & 2) {
                    float row[4], fq[4];

                    nv10_mmul(nv4, m, p, XF_TX0 + i * 8);
                    memcpy(fq, p, sizeof(fq));
                    fq[0] = m[0];
                    xf_row(nv4, XF_TX0 + i * 8 + 3, row);
                    m[3] = dot4(fq, row);
                    memcpy(p, m, sizeof(p));
                }
            }
            memcpy(&f[V_T0 + i * 4], p, sizeof(p));
        }

        /* fog */
        {
            float fv[3], k[3];

            if (fgm == 0) {
                fv[0] = zdir[2];
                fv[1] = vab[11];
                fv[2] = vab[11] * vab[11];
            } else if (fgm == 1) {
                fv[0] = 1.0f;
                fv[1] = dist;
                fv[2] = ed;
            } else {
                float pl[4];

                xf_row(nv4, XF_FOGP, pl);
                fv[0] = zdir[2];
                fv[1] = dot4(epos3, pl);
                if (fgm == 3)
                    fv[1] = fabsf(fv[1]);
                fv[2] = fv[1] * fv[1];
            }
            lv_get(nv4, 0x2b, k);
            f[V_FOG] = dot3(k, fv);
        }

        /* colours */
        if (xb & XFB_LIGHTING) {
            nv10_light(nv4, xa, xb, &vab[4], &vab[8], nrm, ev, epos3, &f[V_C0], &f[V_C1]);
            f[V_C0 + 3] = ((xa >> 23) & 1) ? vab[7] : ls_get(nv4, PIPE_SD, 11);
        } else {
            memcpy(&f[V_C0], &vab[4], 4 * sizeof(float));
            if (((xa >> 19) & 1) && ((xa >> 20) & 1))
                memcpy(&f[V_C1], &vab[8], 3 * sizeof(float));
            else
                lv_get(nv4, 0x2c, &f[V_C1]);
        }
        f[V_C1 + 3] = 0.0f;
    }
}

/* ------------------------------------------------------------------ */
/* Rasterization.                                                      */
/* ------------------------------------------------------------------ */
/* To the screen: x, y with the pixel grid at 0,0 and the interpolants */
static void
nv10_project(nv4_t *nv4, const nv10_vtx_t *v, nv10_rv_t *r)
{
    const float *f   = v->f;
    uint32_t     xb  = GR(NV10_XF_B);
    uint32_t     cb  = GR(NV10_CONFIG_B);
    float       *a   = &r->f[R_A];
    float       *ax  = &r->f[R_AX];
    float        rw, sx, sy, sz;

    if (GR(NV10_XF_A) & XFA_BYPASS) {
        sx = f[V_X];
        sy = f[V_Y];
        sz = f[V_Z];
        rw = f[V_W];
    } else {
        float vp[4];

        xf_row(nv4, XF_VIEWP, vp);
        rw = 1.0f / f[V_W];
        sx = f[V_X] * rw + vp[0];
        sy = f[V_Y] * rw + vp[1];
        sz = f[V_Z] * rw + vp[2];
    }
    r->f[R_X] = sx + 2048.0f;
    r->f[R_Y] = sy + 2048.0f;
    a[AT_Z]   = sz;
    /* the transform unit divides texture coordinates by w where told
       (W_DIVIDE); the rasterizer interpolates them and q linearly */
    for (int n = 0; n < 2; n++) {
        float d = ((xb >> (n * 14 + 2)) & 1) ? rw : 1.0f;

        a[AT_U0 + n * 2]   = f[V_T0 + n * 4 + 0] * d;
        a[AT_V0 + n * 2]   = f[V_T0 + n * 4 + 1] * d;
        ax[AX_R0 + n * 2] = f[V_T0 + n * 4 + 2] * d;
        ax[AX_Q0 + n * 2] = f[V_T0 + n * 4 + 3] * d;
    }
    /* TEXTUREPERSPECTIVE off forces w to 1; w-buffering takes 1/w from a
       texture unit's q (WBUFFER_SELECT) */
    a[AT_Q] = (cb & 0x40) ? rw : 1.0f;
    if (GR(NV10_CONFIG_A) & (1u << 23))
        a[AT_Q] = ax[AX_Q0 + ((cb >> 10) & 1) * 2];
    a[AT_R] = clamp01(f[V_C0 + 0]) * 255.0f;
    a[AT_G] = clamp01(f[V_C0 + 1]) * 255.0f;
    a[AT_B] = clamp01(f[V_C0 + 2]) * 255.0f;
    a[AT_A] = clamp01(f[V_C0 + 3]) * 255.0f;
    if (cb & 0x20) {
        a[AT_SR] = clamp01(f[V_C1 + 0]) * 255.0f;
        a[AT_SG] = clamp01(f[V_C1 + 1]) * 255.0f;
        a[AT_SB] = clamp01(f[V_C1 + 2]) * 255.0f;
    } else
        a[AT_SR] = a[AT_SG] = a[AT_SB] = 0.0f;
    a[AT_FOG]  = ((f[V_FOG] > -16.0f) ? ((f[V_FOG] < 16.0f) ? f[V_FOG] : 16.0f) : -16.0f) * 255.0f;
    ax[AX_SA] = 0.0f;
}

/* Clips the convex polygon in[0..n) (records of <len> floats, <size>
   bytes apart) to c . (f0, f1, f2, f3, 1) >= 0 */
static int
nv10_clip(const void *in, int n, size_t size, int len, void *out, const float c[5])
{
    int m = 0;

    for (int i = 0; i < n; i++) {
        const float *a  = (const float *) ((const char *) in + (size_t) i * size);
        const float *b  = (const float *) ((const char *) in + (size_t) ((i + 1) % n) * size);
        float        da = c[0] * a[0] + c[1] * a[1] + c[2] * a[2] + c[3] * a[3] + c[4];
        float        db = c[0] * b[0] + c[1] * b[1] + c[2] * b[2] + c[3] * b[3] + c[4];

        if ((da >= 0.0f) && (m < NV10_MAXV))
            memcpy((char *) out + (size_t) m++ * size, a, size);
        if (((da >= 0.0f) != (db >= 0.0f)) && (m < NV10_MAXV)) {
            float  t = da / (da - db);
            float *o = (float *) ((char *) out + (size_t) m++ * size);

            for (int k = 0; k < len; k++)
                o[k] = a[k] + (b[k] - a[k]) * t;
        }
    }
    return m;
}

/* The planes a transformed primitive is clipped to: w above zero and
   the guard band (window x = X / W + viewport x, so x >= g is
   X + (viewport x + 2048 - g) W >= 0). <np> planes. */
static int
nv10_planes(nv4_t *nv4, float pl[5][5])
{
    float vp[4];

    xf_row(nv4, XF_VIEWP, vp);
    memset(pl, 0, sizeof(float) * 25);
    pl[0][3] = 1.0f;
    pl[0][4] = -NV10_W_EPS;
    pl[1][0] = 1.0f;
    pl[1][3] = vp[0] + 2048.0f - NV10_GB0;
    pl[2][0] = -1.0f;
    pl[2][3] = NV10_GB1 - vp[0] - 2048.0f;
    pl[3][1] = 1.0f;
    pl[3][3] = vp[1] + 2048.0f - NV10_GB0;
    pl[4][1] = -1.0f;
    pl[4][3] = NV10_GB1 - vp[1] - 2048.0f;
    return 5;
}

static void
nv10_submit(nv4_t *nv4, const nv10_rv_t *p0, const nv10_rv_t *p1, const nv10_rv_t *p2)
{
    const nv10_rv_t *p[3] = { p0, p1, p2 };
    float            xy[3][2], a[3][AT_COUNT], ax[3][AX_COUNT];

    if (!nv4_3d_room(nv4))
        return;
    if (nv4->celsius_si < 0) {
        nv4_3d_state_t st;

        if (!nv10_make_state(nv4, &st))
            return;
        nv4->celsius_si = nv4_3d_place(nv4, &st);
        if (nv4->celsius_si < 0)
            return;
    }
    for (int i = 0; i < 3; i++) {
        xy[i][0] = p[i]->f[R_X];
        xy[i][1] = p[i]->f[R_Y];
        memcpy(a[i], &p[i]->f[R_A], sizeof(a[i]));
        memcpy(ax[i], &p[i]->f[R_AX], sizeof(ax[i]));
    }
    nv4_3d_raster(nv4, nv4->celsius_si, xy, a, ax);
}

/* A convex polygon on screen, clipped to the guard band where it leaves it */
static void
nv10_fill(nv4_t *nv4, const nv10_rv_t *in, int n)
{
    static const float gb[4][5] = {
        { 1.0f, 0.0f, 0.0f, 0.0f, -NV10_GB0 },
        { -1.0f, 0.0f, 0.0f, 0.0f, NV10_GB1 },
        { 0.0f, 1.0f, 0.0f, 0.0f, -NV10_GB0 },
        { 0.0f, -1.0f, 0.0f, 0.0f, NV10_GB1 }
    };
    nv10_rv_t        buf[2][NV10_MAXV];
    const nv10_rv_t *v   = in;
    int              out = 0;

    for (int i = 0; i < n; i++)
        if (!(in[i].f[R_X] >= NV10_GB0) || !(in[i].f[R_X] <= NV10_GB1) || !(in[i].f[R_Y] >= NV10_GB0)
            || !(in[i].f[R_Y] <= NV10_GB1))
            out = 1;
    if (out) {
        int cur = 0;

        memcpy(buf[0], in, (size_t) n * sizeof(nv10_rv_t));
        for (int p = 0; (p < 4) && (n >= 3); p++) {
            n   = nv10_clip(buf[cur], n, sizeof(nv10_rv_t), R_N, buf[cur ^ 1], gb[p]);
            cur ^= 1;
        }
        v = buf[cur];
    }
    for (int i = 1; i + 1 < n; i++)
        nv10_submit(nv4, &v[0], &v[i], &v[i + 1]);
}

/* Widths in eighths of a pixel */
static float
nv10_width(uint32_t v)
{
    float w = (float) v / 8.0f;

    return (w < 1.0f) ? 1.0f : w;
}

static void
nv10_quad_at(nv4_t *nv4, const nv10_rv_t *c, float size)
{
    nv10_rv_t q[4];
    float     h = size * 0.5f;

    for (int i = 0; i < 4; i++)
        q[i] = *c;
    q[0].f[R_X] -= h;
    q[0].f[R_Y] -= h;
    q[1].f[R_X] += h;
    q[1].f[R_Y] -= h;
    q[2].f[R_X] += h;
    q[2].f[R_Y] += h;
    q[3].f[R_X] -= h;
    q[3].f[R_Y] += h;
    nv10_fill(nv4, q, 4);
}

/* A line of <width> pixels, widened across its minor axis */
static void
nv10_line_rv(nv4_t *nv4, const nv10_rv_t *a, const nv10_rv_t *b, float width)
{
    nv10_rv_t q[4] = { *a, *b, *b, *a };
    float     dx   = b->f[R_X] - a->f[R_X];
    float     dy   = b->f[R_Y] - a->f[R_Y];
    float     ox = 0.0f, oy = 0.0f;

    if ((dx == 0.0f) && (dy == 0.0f))
        return;
    if (fabsf(dx) >= fabsf(dy))
        oy = width * 0.5f;
    else
        ox = width * 0.5f;
    q[0].f[R_X] -= ox;
    q[0].f[R_Y] -= oy;
    q[1].f[R_X] -= ox;
    q[1].f[R_Y] -= oy;
    q[2].f[R_X] += ox;
    q[2].f[R_Y] += oy;
    q[3].f[R_X] += ox;
    q[3].f[R_Y] += oy;
    nv10_fill(nv4, q, 4);
}

static void
nv10_flat(nv10_vtx_t *v, const nv10_vtx_t *pv)
{
    memcpy(&v->f[V_C0], &pv->f[V_C0], 8 * sizeof(float));
}

static void
nv10_tri(nv4_t *nv4, const nv10_vtx_t *v0, const nv10_vtx_t *v1, const nv10_vtx_t *v2, const nv10_vtx_t *pv)
{
    uint32_t   ras = GR(NV10_RASTER);
    nv10_vtx_t p[2][NV10_MAXV];
    nv10_rv_t  r[NV10_MAXV];
    int        n   = 3;
    int        cur = 0;
    float      area = 0.0f;
    int        front, mode;

    p[0][0] = *v0;
    p[0][1] = *v1;
    p[0][2] = *v2;
    if (!(GR(NV10_CONFIG_B) & 0x80))
        for (int i = 0; i < 3; i++)
            nv10_flat(&p[0][i], pv);
    if (!(GR(NV10_XF_A) & XFA_BYPASS)) {
        float pl[5][5];
        int   np = nv10_planes(nv4, pl);

        for (int i = 0; (i < np) && (n >= 3); i++) {
            n   = nv10_clip(p[cur], n, sizeof(nv10_vtx_t), V_N, p[cur ^ 1], pl[i]);
            cur ^= 1;
        }
        if (n < 3)
            return;
    }
    for (int i = 0; i < n; i++)
        nv10_project(nv4, &p[cur][i], &r[i]);
    for (int i = 0; i < n; i++) {
        const nv10_rv_t *a = &r[i];
        const nv10_rv_t *b = &r[(i + 1) % n];

        area += a->f[R_X] * b->f[R_Y] - b->f[R_X] * a->f[R_Y];
    }
    if (!(area != 0.0f))
        return;
    /* y grows down: a positive area runs clockwise; FRONT_FACE set makes
       counterclockwise the front */
    front = (ras & (1u << 23)) ? (area < 0.0f) : (area > 0.0f);
    if (ras & (1u << 28)) {
        int cf = (ras >> 21) & 3;

        if ((cf == 3) || ((cf == 1) && front) || ((cf == 2) && !front))
            return;
    }
    mode = front ? (ras & 3) : ((ras >> 2) & 3);
    if ((ras >> ((mode == 1) ? 6 : ((mode == 2) ? 7 : 8))) & 1) {
        /* polygon offset: factor x the depth slope + units */
        float x10 = r[1].f[R_X] - r[0].f[R_X], y10 = r[1].f[R_Y] - r[0].f[R_Y];
        float x20 = r[2].f[R_X] - r[0].f[R_X], y20 = r[2].f[R_Y] - r[0].f[R_Y];
        float z10 = r[1].f[R_A + AT_Z] - r[0].f[R_A + AT_Z], z20 = r[2].f[R_A + AT_Z] - r[0].f[R_A + AT_Z];
        float det = x10 * y20 - x20 * y10;
        float m   = 0.0f;
        float off;

        if (det != 0.0f) {
            float dzdx = fabsf((z10 * y20 - z20 * y10) / det);
            float dzdy = fabsf((z20 * x10 - z10 * x20) / det);

            m = (dzdx > dzdy) ? dzdx : dzdy;
        }
        off = u2f(GR(NV10_POFS_FACTOR)) * m + u2f(GR(NV10_POFS_UNITS));
        for (int i = 0; i < n; i++)
            r[i].f[R_A + AT_Z] += off;
    }
    if (mode == 1) {
        float sz = nv10_width(GR(NV10_POINT_SIZE));

        for (int i = 0; i < n; i++)
            nv10_quad_at(nv4, &r[i], sz);
    } else if (mode == 2) {
        float w = nv10_width((ras >> 12) & 0x1ff);

        for (int i = 0; i < n; i++)
            nv10_line_rv(nv4, &r[i], &r[(i + 1) % n], w);
    } else
        nv10_fill(nv4, r, n);
}

static void
nv10_lerp_vtx(nv10_vtx_t *o, const nv10_vtx_t *a, const nv10_vtx_t *b, float t)
{
    for (int k = 0; k < V_N; k++)
        o->f[k] = a->f[k] + (b->f[k] - a->f[k]) * t;
}

static void
nv10_line(nv4_t *nv4, const nv10_vtx_t *v0, const nv10_vtx_t *v1, const nv10_vtx_t *pv)
{
    nv10_vtx_t a = *v0, b = *v1;
    nv10_rv_t  ra, rb;
    uint32_t   ras = GR(NV10_RASTER);

    if (!(GR(NV10_CONFIG_B) & 0x80)) {
        nv10_flat(&a, pv);
        nv10_flat(&b, pv);
    }
    if (!(GR(NV10_XF_A) & XFA_BYPASS)) {
        float      pl[5][5];
        int        np = nv10_planes(nv4, pl);
        float      t0 = 0.0f, t1 = 1.0f;
        nv10_vtx_t c;

        for (int i = 0; i < np; i++) {
            float da = dot4(pl[i], a.f) + pl[i][4];
            float db = dot4(pl[i], b.f) + pl[i][4];

            if ((da < 0.0f) && (db < 0.0f))
                return;
            if (da < 0.0f) {
                float t = da / (da - db);

                if (t > t0)
                    t0 = t;
            } else if (db < 0.0f) {
                float t = da / (da - db);

                if (t < t1)
                    t1 = t;
            }
        }
        if (t0 >= t1)
            return;
        c = a;
        nv10_lerp_vtx(&a, &c, &b, t0);
        nv10_lerp_vtx(&b, &c, &b, t1);
    }
    nv10_project(nv4, &a, &ra);
    nv10_project(nv4, &b, &rb);
    if (ras & (1u << 7)) {
        float off = u2f(GR(NV10_POFS_UNITS));

        ra.f[R_A + AT_Z] += off;
        rb.f[R_A + AT_Z] += off;
    }
    nv10_line_rv(nv4, &ra, &rb, nv10_width((ras >> 12) & 0x1ff));
}

static void
nv10_point(nv4_t *nv4, const nv10_vtx_t *v)
{
    nv10_rv_t r;

    if (!(GR(NV10_XF_A) & XFA_BYPASS)) {
        float pl[5][5];
        int   np = nv10_planes(nv4, pl);

        for (int i = 0; i < np; i++)
            if (dot4(pl[i], v->f) + pl[i][4] < 0.0f)
                return;
    }
    nv10_project(nv4, v, &r);
    if (GR(NV10_RASTER) & (1u << 6))
        r.f[R_A + AT_Z] += u2f(GR(NV10_POFS_UNITS));
    nv10_quad_at(nv4, &r, nv10_width(GR(NV10_POINT_SIZE)));
}

/* ------------------------------------------------------------------ */
/* Primitive assembly.                                                 */
/* ------------------------------------------------------------------ */
/* The vertex whose colours a flat primitive takes: OpenGL's last, or
   with FLATSHADE_FIRST Direct3D's first */
static const nv10_vtx_t *
nv10_provoking(nv4_t *nv4, const nv10_vtx_t *first, const nv10_vtx_t *last)
{
    return (GR(NV10_CONFIG_B) & 1) ? first : last;
}

static void
nv10_emit(nv4_t *nv4, nv10_chan_t *ch, const nv10_vtx_t *v)
{
    nv10_vtx_t *l = ch->last;
    int         n = ch->n;

    switch (ch->prim) {
        case 1: /* points */
            nv10_point(nv4, v);
            break;
        case 2: /* lines */
            if (n & 1)
                nv10_line(nv4, &l[2], v, nv10_provoking(nv4, &l[2], v));
            break;
        case 3: /* line loop */
        case 4: /* line strip */
            if (n)
                nv10_line(nv4, &l[2], v, nv10_provoking(nv4, &l[2], v));
            break;
        case 5: /* triangles */
            if ((n % 3) == 2)
                nv10_tri(nv4, &l[1], &l[2], v, nv10_provoking(nv4, &l[1], v));
            break;
        case 6: /* triangle strip: odd triangles swap to keep the winding */
            if (n >= 2) {
                if (n & 1)
                    nv10_tri(nv4, &l[2], &l[1], v, nv10_provoking(nv4, &l[1], v));
                else
                    nv10_tri(nv4, &l[1], &l[2], v, nv10_provoking(nv4, &l[1], v));
            }
            break;
        case 7: /* triangle fan: Direct3D's flat colour is the second vertex's */
            if (n >= 2)
                nv10_tri(nv4, &ch->first, &l[2], v, nv10_provoking(nv4, &l[2], v));
            break;
        case 8: /* quads */
            if ((n & 3) == 3) {
                const nv10_vtx_t *pv = nv10_provoking(nv4, &l[0], v);

                nv10_tri(nv4, &l[0], &l[1], &l[2], pv);
                nv10_tri(nv4, &l[0], &l[2], v, pv);
            }
            break;
        case 9: /* quad strip */
            if ((n >= 3) && (n & 1)) {
                const nv10_vtx_t *pv = nv10_provoking(nv4, &l[0], v);

                nv10_tri(nv4, &l[0], &l[1], v, pv);
                nv10_tri(nv4, &l[0], v, &l[2], pv);
            }
            break;
        case 10: /* polygon: the first vertex's colour */
            if (n >= 2)
                nv10_tri(nv4, &ch->first, &l[2], v, &ch->first);
            break;
        default:
            return;
    }
    if (!n)
        ch->first = *v;
    l[0] = l[1];
    l[1] = l[2];
    l[2] = *v;
    ch->n++;
}

static void
nv10_vertex(nv4_t *nv4, nv10_chan_t *ch, const float *vab)
{
    nv10_vtx_t v;

    if (!ch->prim)
        return;
    nv10_xform(nv4, vab, &v);
    nv10_emit(nv4, ch, &v);
}

static void
nv10_vab_load(nv4_t *nv4, float *vab)
{
    for (int i = 0; i < VAB_WORDS; i++)
        vab[i] = pipe_f(nv4, PIPE_VAB + i);
}

static inline void
vab_set(nv4_t *nv4, int i, float v)
{
    nv4->pgraph_pipe[PIPE_VAB + i] = f2u(v);
}

/* ------------------------------------------------------------------ */
/* Vertex arrays.                                                      */
/* ------------------------------------------------------------------ */
/* The bytes one element of array <a> takes */
static int
nv10_elem_bytes(uint32_t fmt)
{
    int type = fmt & 0xf;
    int size = (fmt >> 4) & 0xf;

    if (!size)
        return 0;
    if (size > 4)
        size = 4;
    if ((type == 0) || (type == 4))
        return 4;
    return (type == 1) ? size * 2 : size * 4;
}

/* One element of array <a> into the attributes */
static void
nv10_decode(int a, uint32_t fmt, const uint8_t *p, float *vab)
{
    int   type = fmt & 0xf;
    int   size = (fmt >> 4) & 0xf;
    float v[4] = { 0.0f, 0.0f, 0.0f, 1.0f };
    int   cnt;

    if (size > 4)
        size = 4;
    for (int k = 0; k < size; k++) {
        switch (type) {
            case 0: /* D3DCOLOR: B, G, R, A in memory */
                v[k] = (float) p[(k < 3) ? (2 - k) : 3] / 255.0f;
                break;
            case 4:
                v[k] = (float) p[k] / 255.0f;
                break;
            case 1:
                {
                    int16_t s = (int16_t) (p[k * 2] | (p[k * 2 + 1] << 8));

                    v[k] = (a == 5) ? ((2.0f * (float) s + 1.0f) / 65535.0f) : (float) s;
                }
                break;
            default:
                v[k] = u2f(p[k * 4] | (p[k * 4 + 1] << 8) | (p[k * 4 + 2] << 16) | ((uint32_t) p[k * 4 + 3] << 24));
                break;
        }
    }
    if (a == 7) {
        vab[11] = v[0];
        return;
    }
    if (a == 6) {
        vab[24] = v[0];
        return;
    }
    /* a specular array without alpha leaves the fog coordinate */
    cnt = ((a == 2) && (size < 4)) ? size : ((a == 5) ? 3 : 4);
    for (int k = 0; k < cnt; k++)
        vab[a * 4 + k] = v[k];
}

static void
nv10_fetch(nv4_t *nv4, nv10_chan_t *ch, uint32_t idx)
{
    uint32_t inst = GR(NV10_CELSIUS_DMA) & 0xffff;
    uint32_t adj;
    float    vab[VAB_WORDS];

    if (!inst)
        return;
    /* offsets count from the DMA object's page */
    adj = nv4_ramin_read32(nv4, inst << 4) >> 20;
    nv10_vab_load(nv4, vab);
    for (int a = 0; a < 8; a++) {
        uint32_t fmt = ch->va_fmt[a];
        int      nb  = nv10_elem_bytes(fmt);
        uint32_t off;
        uint8_t  buf[16];

        if (!nb)
            continue;
        off = ch->va_off[a] + idx * ((fmt >> 8) & 0xffff);
        if ((off < adj) || (nv4_dma_read(nv4, inst, off - adj, buf, nb) != NV4_DMA_OK))
            continue;
        nv10_decode(a, fmt, buf, vab);
    }
    nv10_vertex(nv4, ch, vab);
}

/* Words of an inline vertex: the enabled arrays, from fog down to the
   position */
static int
nv10_inline_words(const nv10_chan_t *ch)
{
    int n = 0;

    for (int a = 0; a < 8; a++)
        n += (nv10_elem_bytes(ch->va_fmt[a]) + 3) >> 2;
    return n;
}

static void
nv10_inline(nv4_t *nv4, nv10_chan_t *ch, uint32_t data)
{
    int need = nv10_inline_words(ch);

    if (!need || (need > (int) (sizeof(ch->inl) / sizeof(ch->inl[0]))))
        return;
    ch->inl[ch->ninl++] = data;
    if (ch->ninl < need)
        return;
    ch->ninl = 0;
    {
        float   vab[VAB_WORDS];
        uint8_t bytes[sizeof(ch->inl)];
        int     w = 0;

        for (int i = 0; i < need; i++) {
            bytes[i * 4 + 0] = ch->inl[i];
            bytes[i * 4 + 1] = ch->inl[i] >> 8;
            bytes[i * 4 + 2] = ch->inl[i] >> 16;
            bytes[i * 4 + 3] = ch->inl[i] >> 24;
        }
        nv10_vab_load(nv4, vab);
        for (int a = 7; a >= 0; a--) {
            int nb = nv10_elem_bytes(ch->va_fmt[a]);

            if (!nb)
                continue;
            nv10_decode(a, ch->va_fmt[a], &bytes[w * 4], vab);
            w += (nb + 3) >> 2;
        }
        nv10_vertex(nv4, ch, vab);
    }
}

/* ------------------------------------------------------------------ */
/* Methods.                                                            */
/* ------------------------------------------------------------------ */
static void
pipe_set(nv4_t *nv4, int word, uint32_t v)
{
    nv4->pgraph_pipe[word & 0x1fff] = v;
}

/* The immediate vertex attributes (0xc00-0xcfc) */
static int
nv10_attr_method(nv4_t *nv4, nv10_chan_t *ch, uint32_t mthd, uint32_t data)
{
    int     i  = (mthd >> 2) & 3;
    int16_t s0 = (int16_t) data, s1 = (int16_t) (data >> 16);

    /* floats; writing the first two components of a colour or texture
       coordinate resets the last two to 0, 1 */
    if (mthd < 0xc10) { /* VERTEX3F */
        if (mthd > 0xc08)
            return NV4_GR_OK;
        pipe_set(nv4, PIPE_VAB + (mthd - 0xc00) / 4, data);
        if (mthd == 0xc08) {
            float vab[VAB_WORDS];

            vab_set(nv4, 3, 1.0f);
            nv10_vab_load(nv4, vab);
            nv10_vertex(nv4, ch, vab);
        }
        return NV4_GR_OK;
    }
    if (mthd < 0xc18) { /* VERTEX3S */
        if (mthd == 0xc10) {
            vab_set(nv4, 0, (float) s0);
            vab_set(nv4, 1, (float) s1);
        } else {
            float vab[VAB_WORDS];

            vab_set(nv4, 2, (float) s0);
            vab_set(nv4, 3, 1.0f);
            nv10_vab_load(nv4, vab);
            nv10_vertex(nv4, ch, vab);
        }
        return NV4_GR_OK;
    }
    if (mthd < 0xc28) { /* VERTEX4F */
        pipe_set(nv4, PIPE_VAB + (mthd - 0xc18) / 4, data);
        if (mthd == 0xc24) {
            float vab[VAB_WORDS];

            nv10_vab_load(nv4, vab);
            nv10_vertex(nv4, ch, vab);
        }
        return NV4_GR_OK;
    }
    if (mthd < 0xc30) { /* VERTEX4S */
        vab_set(nv4, (mthd == 0xc28) ? 0 : 2, (float) s0);
        vab_set(nv4, (mthd == 0xc28) ? 1 : 3, (float) s1);
        if (mthd == 0xc2c) {
            float vab[VAB_WORDS];

            nv10_vab_load(nv4, vab);
            nv10_vertex(nv4, ch, vab);
        }
        return NV4_GR_OK;
    }
    if (mthd < 0xc3c) { /* NORMAL3F */
        pipe_set(nv4, PIPE_VAB + 20 + (mthd - 0xc30) / 4, data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0xc40) && (mthd < 0xc48)) { /* NORMAL3S */
        vab_set(nv4, (mthd == 0xc40) ? 20 : 22, (2.0f * (float) s0 + 1.0f) / 65535.0f);
        if (mthd == 0xc40)
            vab_set(nv4, 21, (2.0f * (float) s1 + 1.0f) / 65535.0f);
        return NV4_GR_OK;
    }
    if ((mthd >= 0xc50) && (mthd < 0xc6c)) { /* DIFFUSE4F, DIFFUSE3F */
        int k = (mthd < 0xc60) ? (mthd - 0xc50) / 4 : (mthd - 0xc60) / 4;

        if (k >= ((mthd < 0xc60) ? 4 : 3))
            return NV4_GR_OK;
        if (k < 2) {
            vab_set(nv4, 6, 0.0f);
            vab_set(nv4, 7, 1.0f);
        }
        pipe_set(nv4, PIPE_VAB + 4 + k, data);
        return NV4_GR_OK;
    }
    if ((mthd == 0xc6c) || (mthd == 0xc8c)) { /* DIFFUSE4UB, SPECULAR4UB: R first */
        for (int k = 0; k < 4; k++)
            vab_set(nv4, ((mthd == 0xc6c) ? 4 : 8) + k, (float) ((data >> (k * 8)) & 0xff) / 255.0f);
        return NV4_GR_OK;
    }
    if ((mthd >= 0xc70) && (mthd < 0xc8c)) { /* SPECULAR4F, SPECULAR3F */
        int k = (mthd < 0xc80) ? (mthd - 0xc70) / 4 : (mthd - 0xc80) / 4;

        if (k < ((mthd < 0xc80) ? 4 : 3))
            pipe_set(nv4, PIPE_VAB + 8 + k, data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0xc90) && (mthd < 0xce0)) { /* texture coordinates */
        int      u   = (mthd >= 0xcb8);
        uint32_t m   = mthd - (u ? 0xcb8 : 0xc90);
        int      b   = 12 + u * 4;

        if (m < 0x08) { /* 2F */
            vab_set(nv4, b + 2, 0.0f);
            vab_set(nv4, b + 3, 1.0f);
            pipe_set(nv4, PIPE_VAB + b + m / 4, data);
        } else if (m < 0x10) { /* 2S */
            if (m == 0x08) {
                vab_set(nv4, b + 0, (float) s0);
                vab_set(nv4, b + 1, (float) s1);
                vab_set(nv4, b + 2, 0.0f);
                vab_set(nv4, b + 3, 1.0f);
            }
        } else if (m < 0x20) { /* 4F */
            if (m < 0x18) {
                vab_set(nv4, b + 2, 0.0f);
                vab_set(nv4, b + 3, 1.0f);
            }
            pipe_set(nv4, PIPE_VAB + b + (m - 0x10) / 4, data);
        } else if (m < 0x28) { /* 4S */
            if (m == 0x20) {
                vab_set(nv4, b + 0, (float) s0);
                vab_set(nv4, b + 1, (float) s1);
                vab_set(nv4, b + 2, 0.0f);
                vab_set(nv4, b + 3, 1.0f);
            } else {
                vab_set(nv4, b + 2, (float) s0);
                vab_set(nv4, b + 3, (float) s1);
            }
        }
        return NV4_GR_OK;
    }
    switch (mthd) {
        case 0xce0: /* FOG1F */
            pipe_set(nv4, PIPE_VAB + 11, data);
            break;
        case 0xce4: /* WEIGHT1F */
            pipe_set(nv4, PIPE_VAB + 24, data);
            break;
        default: /* edge flag, XF NOP and SYNC */
            break;
    }
    (void) i;
    return NV4_GR_OK;
}

static void
nv10_begin_end(nv4_t *nv4, nv10_chan_t *ch, uint32_t data)
{
    if (!data) {
        /* closing a line loop */
        if ((ch->prim == 3) && (ch->n >= 2)) {
            nv10_vtx_t last = ch->last[2];

            nv10_line(nv4, &last, &ch->first, nv10_provoking(nv4, &last, &ch->first));
        }
        ch->prim = 0;
        GR(NV10_CONFIG_C) &= ~0x100u;
    } else {
        ch->prim = (data <= 10) ? (int) data : 0;
        GR(NV10_CONFIG_C) |= 0x100u;
    }
    ch->n    = 0;
    ch->ninl = 0;
}

/* The lights (0x800-0xbfc) */
static void
nv10_light_method(nv4_t *nv4, uint32_t mthd, uint32_t data)
{
    int      i = (mthd - 0x800) >> 7;
    uint32_t o = mthd & 0x7f;

    if (o < 0x24)
        pipe_set(nv4, PIPE_LV + (i * 5 + o / 12) * 4 + (o % 12) / 4, data);
    else if (o == 0x24)
        pipe_set(nv4, PIPE_SB + (3 + i) * 4, data);
    else if (o < 0x40)
        pipe_set(nv4, PIPE_LV + (i * 5 + 3 + (o - 0x28) / 12) * 4 + ((o - 0x28) % 12) / 4, data);
    else if (o == 0x40)
        pipe_set(nv4, PIPE_SB + (11 + i) * 4, data);
    else if (o == 0x44)
        pipe_set(nv4, PIPE_SC + (4 + i) * 4, data);
    else if (o == 0x48)
        pipe_set(nv4, PIPE_SD + (3 + i) * 4, data);
    else if (o < 0x5c)
        pipe_set(nv4, PIPE_XFRM + (XF_SPOT + i) * 4 + (o - 0x4c) / 4, data);
    else if (o < 0x68)
        pipe_set(nv4, PIPE_XFRM + (XF_LPOS + i) * 4 + (o - 0x5c) / 4, data);
    else if (o < 0x74)
        pipe_set(nv4, PIPE_LV + (i * 5 + 3) * 4 + (o - 0x68) / 4, data);
}

/* Methods that only load the transform pipe; -1 for any other */
static int
nv10_pipe_method(nv4_t *nv4, uint32_t mthd, uint32_t data)
{
    static const int mtx[7] = { 0, 3, 1, 4, 2, 6, 8 }; /* MV0, MV1, IMV0, IMV1, composite, TX0, TX1 */

    if ((mthd >= 0x800) && (mthd < 0xc00)) {
        nv10_light_method(nv4, mthd, data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x400) && (mthd < 0x5c0)) {
        int idx = (mthd >> 2) & 0xf;

        pipe_set(nv4, PIPE_XFRM + (mtx[(mthd - 0x400) >> 6] * 4 + (idx >> 2)) * 4 + (idx & 3), data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x600) && (mthd < 0x680)) { /* texgen planes */
        pipe_set(nv4, PIPE_XFRM + (XF_TG0 + ((mthd - 0x600) >> 6) * 8 + ((mthd >> 4) & 3)) * 4 + ((mthd >> 2) & 3), data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x680) && (mthd < 0x68c)) { /* fog coefficients */
        pipe_set(nv4, PIPE_LV + 0x2b * 4 + (mthd - 0x680) / 4, data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x68c) && (mthd < 0x69c)) {
        pipe_set(nv4, PIPE_XFRM + XF_FOGP * 4 + (mthd - 0x68c) / 4, data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x6c4) && (mthd < 0x6d0)) { /* scene ambient */
        pipe_set(nv4, PIPE_LV + 0x29 * 4 + (mthd - 0x6c4) / 4, data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x6e8) && (mthd < 0x6f8)) {
        pipe_set(nv4, PIPE_XFRM + XF_VIEWP * 4 + (mthd - 0x6e8) / 4, data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x6f8) && (mthd < 0x710)) { /* point parameters */
        pipe_set(nv4, PIPE_LV + ((mthd < 0x704) ? 0x2d : 0x2e) * 4 + ((mthd - 0x6f8) % 12) / 4, data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x718) && (mthd < 0x728)) {
        pipe_set(nv4, PIPE_XFRM + XF_EYE * 4 + (mthd - 0x718) / 4, data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x3a8) && (mthd < 0x3b4)) { /* material emission */
        pipe_set(nv4, PIPE_LV + 0x2a * 4 + (mthd - 0x3a8) / 4, data);
        return NV4_GR_OK;
    }
    switch (mthd) {
        case 0x6a0: /* specular parameters */
            pipe_set(nv4, PIPE_SB + 1 * 4, data);
            return NV4_GR_OK;
        case 0x6a4:
            pipe_set(nv4, PIPE_SC + 1 * 4, data);
            return NV4_GR_OK;
        case 0x6a8:
            pipe_set(nv4, PIPE_SD + 2 * 4, data);
            return NV4_GR_OK;
        case 0x6ac:
            pipe_set(nv4, PIPE_SA + 2 * 4, data);
            return NV4_GR_OK;
        case 0x6b0:
            pipe_set(nv4, PIPE_SC + 2 * 4, data);
            return NV4_GR_OK;
        case 0x6b4:
            pipe_set(nv4, PIPE_SC + 3 * 4, data);
            return NV4_GR_OK;
        case 0x710:
            pipe_set(nv4, PIPE_SB + 2 * 4, data);
            return NV4_GR_OK;
        case 0x714:
            pipe_set(nv4, PIPE_SD + 1 * 4, data);
            return NV4_GR_OK;
        case 0x3b4: /* material alpha */
            pipe_set(nv4, PIPE_SD + 11 * 4, data);
            return NV4_GR_OK;
        default:
            return -1;
    }
}

/* OpenGL enums the methods take, to the bundle's encodings */
static int
nv10_blend_factor_rv(uint32_t v)
{
    switch (v) {
        case 0:
        case 1:
            return (int) v;
        case 0x8001:
        case 0x8002:
        case 0x8003:
        case 0x8004:
            return 0xc + (int) (v - 0x8001);
        default:
            return ((v >= 0x300) && (v <= 0x308)) ? (int) (v - 0x300) + 2 : -1;
    }
}

static int
nv10_stencil_op_rv(uint32_t v)
{
    switch (v) {
        case 0:
            return 2;
        case 0x150a:
            return 6;
        case 0x1e00:
            return 1;
        case 0x1e01:
            return 3;
        case 0x1e02:
            return 4;
        case 0x1e03:
            return 5;
        case 0x8507:
            return 7;
        case 0x8508:
            return 8;
        default:
            return -1;
    }
}

static int
nv10_texgen_rv(uint32_t v, int which)
{
    switch (v) {
        case 0:
            return 0;
        case 0x2400:
            return 1;
        case 0x2401:
            return 2;
        case 0x2402:
            return (which < 2) ? 3 : -1;
        case 0x8511:
            return (which < 3) ? 4 : -1;
        case 0x8512:
            return (which < 3) ? 5 : -1;
        case 0x855f:
            return (which < 3) ? 6 : -1;
        default:
            return -1;
    }
}

int
nv10_celsius_method(nv4_t *nv4, int chid, int cls, uint32_t mthd, uint32_t data)
{
    nv10_chan_t *ch;
    int          r;

    if (!nv4->celsius)
        return NV4_GR_OK;
    ch = &nv4->celsius->ch[chid & (NV4_MAX_CHANNELS - 1)];

    /* vertices first, the busiest */
    if (mthd >= 0x1800) {
        nv10_inline(nv4, ch, data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x1600) && (mthd < 0x1660))
        return NV4_GR_MISSING; /* the resman's: DEBUG_INIT, MATERIAL_EMISSION_SW */
    if ((mthd >= 0x1400) && (mthd < 0x1600)) {
        uint32_t start = data & 0xffffff;
        uint32_t count = (data >> 24) + 1;

        for (uint32_t i = 0; i < count; i++)
            nv10_fetch(nv4, ch, start + i);
        return NV4_GR_OK;
    }
    if ((mthd >= 0x1100) && (mthd < 0x1200)) {
        nv10_fetch(nv4, ch, data);
        return NV4_GR_OK;
    }
    if ((mthd >= 0xe00) && (mthd < 0x1000)) {
        nv10_fetch(nv4, ch, data & 0xffff);
        nv10_fetch(nv4, ch, data >> 16);
        return NV4_GR_OK;
    }
    if ((mthd == 0xdfc) || (mthd == 0x10fc) || (mthd == 0x13fc) || (mthd == 0x17fc)) {
        nv10_begin_end(nv4, ch, data & 0xf);
        return NV4_GR_OK;
    }
    if ((mthd >= 0xc00) && (mthd < 0xd00))
        return nv10_attr_method(nv4, ch, mthd, data);
    if ((mthd >= 0xd00) && (mthd < 0xd40)) {
        int a = (mthd - 0xd00) >> 3;

        if (mthd & 4)
            ch->va_fmt[a] = data;
        else
            ch->va_off[a] = data & 0x0ffffffc;
        ch->ninl = 0;
        return NV4_GR_OK;
    }
    r = nv10_pipe_method(nv4, mthd, data);
    if (r >= 0)
        return r;
    if ((mthd >= 0x72c) && (mthd < 0x738))
        return NV4_GR_MISSING; /* EYE_DIRECTION_SW: the resman loads the pipe */

    /* everything below changes the draw state */
    nv4->celsius_si = -1;
    if ((mthd >= 0x218) && (mthd < 0x258)) {
        int      i = (mthd >> 2) & 1;
        uint32_t m = (mthd - 0x218) & ~4u;

        switch (m) {
            case 0x00:
                nv10_set(nv4, NV10_TEX_OFFSET(i), data);
                break;
            case 0x08:
                {
                    /* MIPMAP_LEVELS: no more than the largest side has */
                    int mips = (data >> 12) & 0xf;
                    int su   = (data >> 16) & 0xf;
                    int sv   = (data >> 20) & 0xf;

                    if ((mips > su) && (mips > sv))
                        mips = ((su > sv) ? su : sv) + 1;
                    nv10_set(nv4, NV10_TEX_FORMAT(i), (data & ~0xf000u) | ((uint32_t) mips << 12));
                }
                break;
            case 0x10:
                nv10_set(nv4, NV10_TEX_CONTROL0(i), data);
                nv10_insrt(nv4, NV10_XF_B, i ? 14 : 0, 1, (data >> 30) & 1);
                break;
            case 0x18:
                nv10_set(nv4, NV10_TEX_CONTROL1(i), data);
                break;
            case 0x20:
                nv10_set(nv4, NV10_TEX_CONTROL2(i), data);
                break;
            case 0x28:
                nv10_set(nv4, NV10_TEX_RECT(i), data);
                break;
            case 0x30:
                nv10_set(nv4, NV10_TEX_FILTER(i), data);
                break;
            case 0x38:
                nv10_set(nv4, NV10_TEX_PALETTE(i), data);
                break;
            default:
                break;
        }
        return NV4_GR_OK;
    }
    if ((mthd >= 0x260) && (mthd < 0x280)) {
        int i = (mthd >> 2) & 1;

        switch ((mthd - 0x260) >> 3) {
            case 0:
                nv10_set(nv4, NV10_RC_IN_ALPHA(i), data);
                break;
            case 1:
                nv10_set(nv4, NV10_RC_IN_COLOR(i), data);
                break;
            case 2:
                nv10_set(nv4, NV10_RC_FACTOR(i), data);
                break;
            default:
                nv10_set(nv4, NV10_RC_OUT_ALPHA(i), data);
                break;
        }
        return NV4_GR_OK;
    }
    if ((mthd >= 0x2c0) && (mthd < 0x300)) {
        /* a window clip rectangle, and the ones after it */
        int v = (mthd >= 0x2e0);

        for (int i = (mthd >> 2) & 7; i < 8; i++)
            GR(v ? NV10_WCLIP_V(i) : NV10_WCLIP_H(i)) = data & 0x0fff0fff;
        return NV4_GR_OK;
    }
    if ((mthd >= 0x3c0) && (mthd < 0x3e0)) {
        int which = (mthd >> 2) & 3;
        int idx   = (mthd >> 4) & 1;
        int rv    = nv10_texgen_rv(data, which);

        if (rv >= 0)
            nv10_insrt(nv4, NV10_XF_B, 3 + idx * 14 + which * 3, (which == 3) ? 2 : 3, rv);
        return NV4_GR_OK;
    }

    switch (mthd) {
        case 0x10c: /* GET_STATE */
            return NV4_GR_MISSING;
        case 0x110: /* WAIT_FOR_IDLE */
        case 0x140: /* PM_TRIGGER */
        case 0x728: /* FLUSH */
            return NV4_GR_OK;

        case 0x184: /* SetContextDmaA, B: the textures' */
            nv4_pgraph_set_grobj(nv4, 2, 0, 16, data);
            return NV4_GR_OK;
        case 0x188:
            nv4_pgraph_set_grobj(nv4, 2, 16, 16, data);
            return NV4_GR_OK;
        case 0x18c: /* SetContextDmaVertex */
            GR(NV10_CELSIUS_DMA) = (GR(NV10_CELSIUS_DMA) & 0xffff0000) | (data & 0xffff);
            return NV4_GR_OK;
        case 0x190: /* SetContextDmaState */
            GR(NV10_CELSIUS_DMA) = (GR(NV10_CELSIUS_DMA) & 0xffff) | (data << 16);
            return NV4_GR_OK;
        case 0x194:
            nv4_pgraph_dma_surf(nv4, NV4_SURF_COLOR, data & 0xffff);
            return NV4_GR_OK;
        case 0x198:
            nv4_pgraph_dma_surf(nv4, NV4_SURF_ZETA, data & 0xffff);
            return NV4_GR_OK;

        case 0x200: /* SURFACE_CLIP_HORIZONTAL, VERTICAL: x | width << 16 */
        case 0x204:
            {
                int      hv  = (mthd >> 2) & 1;
                uint32_t min = data & 0xffff;

                GR(NV10_UCLIP3D_MIN + hv * 4) = min;
                GR(NV10_UCLIP3D_MAX + hv * 4) = (min + (data >> 16)) & 0x3ffff;
            }
            return NV4_GR_OK;
        case 0x208: /* SURFACE_FORMAT */
            {
                static const int fmts[11] = { 0, 0x2, 0x3, 0x5, 0x7, 0xb, 0x9, 0xa, 0xc, 0x1, 0x6 };
                int              sfmt = data & 0xf;
                int              fmt  = (sfmt < 11) ? fmts[sfmt] : 0;
                int              zfmt = (data & 0x10) ? 1 : 2;

                /* Z16 behind a 16-bit (or narrower) colour buffer */
                if ((sfmt == 1) || (sfmt == 2) || (sfmt == 3) || (sfmt == 9) || (sfmt == 0xa))
                    zfmt = 1;
                if (!fmt)
                    zfmt = 0;
                GR(NV4_PGRAPH_BPIXEL)    = (GR(NV4_PGRAPH_BPIXEL) & ~0xff00u) | ((uint32_t) fmt << 8) | ((uint32_t) zfmt << 12);
                GR(NV4_PGRAPH_SURFACE)   = (GR(NV4_PGRAPH_SURFACE) & ~3u) | ((data >> 8) & 3);
                GR(NV4_PGRAPH_BSWIZZLE2) = data & 0x0f0f0000;
            }
            return NV4_GR_OK;
        case 0x20c: /* SURFACE_PITCH: colour, zeta */
            GR(NV4_PGRAPH_BPITCH + NV4_SURF_COLOR * 4) = data & 0xfff0;
            GR(NV4_PGRAPH_BPITCH + NV4_SURF_ZETA * 4)  = (data >> 16) & 0xfff0;
            return NV4_GR_OK;
        case 0x210:
            GR(NV4_PGRAPH_BOFFSET + NV4_SURF_COLOR * 4) = data & 0x07ffffff;
            return NV4_GR_OK;
        case 0x214:
            GR(NV4_PGRAPH_BOFFSET + NV4_SURF_ZETA * 4) = data & 0x07ffffff;
            return NV4_GR_OK;

        case 0x280: /* COMBINER0/1_COLOR_OCW */
        case 0x284:
            nv10_set(nv4, NV10_RC_OUT_COLOR((mthd >> 2) & 1), data);
            return NV4_GR_OK;
        case 0x288:
            nv10_set(nv4, NV10_RC_FINAL0, data);
            return NV4_GR_OK;
        case 0x28c:
            nv10_set(nv4, NV10_RC_FINAL1, data);
            return NV4_GR_OK;

        case 0x290: /* CONTROL0 */
            nv10_insrt(nv4, NV10_CONFIG_A, 25, 1, data & 1);         /* stencil write */
            nv10_insrt(nv4, NV10_CONFIG_A, 23, 1, (data >> 16) & 1); /* w-buffering */
            nv10_insrt(nv4, NV10_CONFIG_B, 2, 1, (data >> 24) & 1);  /* premultiplied alpha */
            nv10_insrt(nv4, NV10_CONFIG_B, 6, 1, (data >> 20) & 1);  /* texture perspective */
            nv10_insrt(nv4, NV10_CONFIG_B, 10, 4, (data >> 8) & 0xf); /* w-buffer source */
            nv10_insrt(nv4, NV10_RASTER, 29, 1, (data >> 12) & 1);   /* float depth */
            return NV4_GR_OK;
        case 0x294: /* LIGHT_CONTROL */
            nv10_insrt(nv4, NV10_XF_A, 18, 1, (data >> 2) & 1);
            nv10_insrt(nv4, NV10_XF_A, 19, 1, data & 1);
            nv10_insrt(nv4, NV10_XF_A, 20, 1, (data >> 1) & 1);
            nv10_insrt(nv4, NV10_XF_B, 28, 1, (data >> 16) & 1);
            return NV4_GR_OK;
        case 0x298: /* COLOR_MATERIAL */
            if (data <= 0xf)
                nv10_insrt(nv4, NV10_XF_A, 21, 4, data);
            return NV4_GR_OK;
        case 0x29c: /* FOG_MODE */
            {
                int rv = -1;

                switch (data) {
                    case 0x2601:
                        rv = 0;
                        break;
                    case 0x800:
                        rv = 1;
                        break;
                    case 0x801:
                        rv = 3;
                        break;
                    case 0x802:
                        rv = 5;
                        break;
                    case 0x803:
                        rv = 7;
                        break;
                    default:
                        break;
                }
                if (rv >= 0)
                    nv10_insrt(nv4, NV10_CONFIG_C, 0, 3, rv);
            }
            return NV4_GR_OK;
        case 0x2a0: /* FOG_GEN_MODE */
            if (data <= 3)
                nv10_insrt(nv4, NV10_XF_A, 16, 2, data);
            return NV4_GR_OK;
        case 0x2a4: /* FOG_ENABLE */
            nv10_insrt(nv4, NV10_CONFIG_B, 8, 1, data & 1);
            nv10_insrt(nv4, NV10_XF_B, 31, 1, data & 1);
            return NV4_GR_OK;
        case 0x2a8: /* FOG_COLOR: red first, kept as ARGB */
            nv10_set(nv4, NV10_FOG_COLOR, (data & 0xff00ff00) | ((data & 0xff) << 16) | ((data >> 16) & 0xff));
            return NV4_GR_OK;
        case 0x2ac:
        case 0x2b0:
            nv10_set(nv4, NV10_TEX_CKEY((mthd - 0x2ac) >> 2), data);
            return NV4_GR_OK;
        case 0x2b4: /* WINDOW_CLIP_TYPE */
            nv10_insrt(nv4, NV10_CONFIG_C, 4, 1, data & 1);
            return NV4_GR_OK;

        case 0x300: /* ALPHA_TEST_ENABLE */
            nv10_insrt(nv4, NV10_CONFIG_A, 12, 1, data & 1);
            return NV4_GR_OK;
        case 0x304: /* BLEND_ENABLE */
            nv10_insrt(nv4, NV10_BLEND, 3, 1, data & 1);
            return NV4_GR_OK;
        case 0x308: /* CULL_FACE_ENABLE */
            nv10_insrt(nv4, NV10_RASTER, 28, 1, data & 1);
            return NV4_GR_OK;
        case 0x30c: /* DEPTH_TEST_ENABLE */
            nv10_insrt(nv4, NV10_CONFIG_A, 14, 1, data & 1);
            return NV4_GR_OK;
        case 0x310: /* DITHER_ENABLE */
            nv10_insrt(nv4, NV10_CONFIG_A, 22, 1, data & 1);
            return NV4_GR_OK;
        case 0x314: /* LIGHTING_ENABLE */
            nv10_insrt(nv4, NV10_XF_B, 29, 1, data & 1);
            return NV4_GR_OK;
        case 0x318: /* POINT_PARAMS_ENABLE */
            nv10_insrt(nv4, NV10_CONFIG_B, 9, 1, data & 1);
            nv10_insrt(nv4, NV10_XF_A, 25, 1, data & 1);
            return NV4_GR_OK;
        case 0x31c: /* POINT, LINE, POLYGON_SMOOTH_ENABLE */
        case 0x320:
        case 0x324:
            nv10_insrt(nv4, NV10_RASTER, 9 + (mthd - 0x31c) / 4, 1, data & 1);
            return NV4_GR_OK;
        case 0x328: /* SKIN_ENABLE */
            nv10_insrt(nv4, NV10_XF_A, 27, 1, data & 1);
            return NV4_GR_OK;
        case 0x32c: /* STENCIL_TEST_ENABLE */
            nv10_insrt(nv4, NV10_STENCIL_A, 0, 1, data & 1);
            return NV4_GR_OK;
        case 0x330: /* POLY_OFFSET_POINT, LINE, FILL_ENABLE */
        case 0x334:
        case 0x338:
            nv10_insrt(nv4, NV10_RASTER, 6 + (mthd - 0x330) / 4, 1, data & 1);
            return NV4_GR_OK;
        case 0x33c: /* ALPHA_FUNC */
            if ((data >= 0x200) && (data < 0x208))
                nv10_insrt(nv4, NV10_CONFIG_A, 8, 4, data & 0xf);
            return NV4_GR_OK;
        case 0x340: /* ALPHA_REF */
            nv10_insrt(nv4, NV10_CONFIG_A, 0, 8, data & 0xff);
            return NV4_GR_OK;
        case 0x344: /* BLEND_FUNC_SFACTOR, DFACTOR */
        case 0x348:
            {
                int rv = nv10_blend_factor_rv(data);

                if (rv >= 0)
                    nv10_insrt(nv4, NV10_BLEND, (mthd == 0x344) ? 4 : 8, 4, rv);
            }
            return NV4_GR_OK;
        case 0x34c:
            nv10_set(nv4, NV10_BLEND_COLOR, data);
            return NV4_GR_OK;
        case 0x350: /* BLEND_EQUATION */
            {
                int rv = -1;

                switch (data) {
                    case 0x8006:
                        rv = 2;
                        break;
                    case 0x8007:
                        rv = 3;
                        break;
                    case 0x8008:
                        rv = 4;
                        break;
                    case 0x800a:
                        rv = 0;
                        break;
                    case 0x800b:
                        rv = 1;
                        break;
                    default:
                        break;
                }
                if (rv >= 0)
                    nv10_insrt(nv4, NV10_BLEND, 0, 3, rv);
            }
            return NV4_GR_OK;
        case 0x354: /* DEPTH_FUNC */
            if ((data >= 0x200) && (data < 0x208))
                nv10_insrt(nv4, NV10_CONFIG_A, 16, 4, data & 0xf);
            return NV4_GR_OK;
        case 0x358: /* COLOR_MASK: B G R A bytes */
            nv10_insrt(nv4, NV10_CONFIG_A, 29, 1, data & 1);
            nv10_insrt(nv4, NV10_CONFIG_A, 28, 1, (data >> 8) & 1);
            nv10_insrt(nv4, NV10_CONFIG_A, 27, 1, (data >> 16) & 1);
            nv10_insrt(nv4, NV10_CONFIG_A, 26, 1, (data >> 24) & 1);
            return NV4_GR_OK;
        case 0x35c: /* DEPTH_MASK */
            nv10_insrt(nv4, NV10_CONFIG_A, 24, 1, data & 1);
            return NV4_GR_OK;
        case 0x360: /* STENCIL_MASK */
            nv10_insrt(nv4, NV10_STENCIL_A, 24, 8, data);
            return NV4_GR_OK;
        case 0x364: /* STENCIL_FUNC */
            if ((data >= 0x200) && (data < 0x208))
                nv10_insrt(nv4, NV10_STENCIL_A, 4, 4, data & 0xf);
            return NV4_GR_OK;
        case 0x368: /* STENCIL_FUNC_REF, MASK */
            nv10_insrt(nv4, NV10_STENCIL_A, 8, 8, data);
            return NV4_GR_OK;
        case 0x36c:
            nv10_insrt(nv4, NV10_STENCIL_A, 16, 8, data);
            return NV4_GR_OK;
        case 0x370: /* STENCIL_OP_FAIL, ZFAIL, ZPASS */
        case 0x374:
        case 0x378:
            {
                int rv = nv10_stencil_op_rv(data);

                if (rv >= 0)
                    nv10_insrt(nv4, NV10_STENCIL_B, (mthd - 0x370), 4, rv);
            }
            return NV4_GR_OK;
        case 0x37c: /* SHADE_MODE */
            if ((data == 0x1d00) || (data == 0x1d01))
                nv10_insrt(nv4, NV10_CONFIG_B, 7, 1, data & 1);
            return NV4_GR_OK;
        case 0x380: /* LINE_WIDTH */
            nv10_insrt(nv4, NV10_RASTER, 12, 9, data & 0xff);
            return NV4_GR_OK;
        case 0x384:
            nv10_set(nv4, NV10_POFS_FACTOR, data);
            return NV4_GR_OK;
        case 0x388:
            nv10_set(nv4, NV10_POFS_UNITS, data);
            return NV4_GR_OK;
        case 0x38c: /* FRONT, BACK_POLYGON_MODE */
        case 0x390:
            {
                int rv = (data == 0x1b00) ? 1 : ((data == 0x1b01) ? 2 : ((data == 0x1b02) ? 0 : -1));

                if (rv >= 0)
                    nv10_insrt(nv4, NV10_RASTER, (mthd == 0x38c) ? 0 : 2, 2, rv);
            }
            return NV4_GR_OK;
        case 0x394:
            nv10_set(nv4, NV10_DEPTH_NEAR, data);
            return NV4_GR_OK;
        case 0x398:
            nv10_set(nv4, NV10_DEPTH_FAR, data);
            return NV4_GR_OK;
        case 0x39c: /* CULL_FACE */
            {
                int rv = (data == 0x404) ? 1 : ((data == 0x405) ? 2 : ((data == 0x408) ? 3 : -1));

                if (rv >= 0)
                    nv10_insrt(nv4, NV10_RASTER, 21, 2, rv);
            }
            return NV4_GR_OK;
        case 0x3a0: /* FRONT_FACE */
            if ((data == 0x900) || (data == 0x901))
                nv10_insrt(nv4, NV10_RASTER, 23, 1, data & 1);
            return NV4_GR_OK;
        case 0x3a4: /* NORMALIZE_ENABLE */
            nv10_insrt(nv4, NV10_XF_B, 30, 1, data & 1);
            return NV4_GR_OK;
        case 0x3b8: /* SPECULAR_ENABLE */
            nv10_insrt(nv4, NV10_CONFIG_B, 5, 1, data & 1);
            return NV4_GR_OK;
        case 0x3bc: /* LIGHT_ENABLE_MASK */
            nv10_insrt(nv4, NV10_XF_A, 0, 16, data);
            return NV4_GR_OK;
        case 0x3e0: /* TEXTURE_MATRIX0, 1_ENABLE */
        case 0x3e4:
            nv10_insrt(nv4, NV10_XF_B, 1 + ((mthd - 0x3e0) >> 2) * 14, 1, data & 1);
            return NV4_GR_OK;
        case 0x3e8: /* TLMODE: passthrough, W_DIVIDE_0, W_DIVIDE_1 */
            nv10_insrt(nv4, NV10_XF_A, 28, 1, data & 1);
            nv10_insrt(nv4, NV10_XF_B, 2, 1, (data >> 1) & 1);
            nv10_insrt(nv4, NV10_XF_B, 16, 1, (data >> 2) & 1);
            return NV4_GR_OK;
        case 0x3ec: /* POINT_SIZE */
            nv10_set(nv4, NV10_POINT_SIZE, data & 0x1ff);
            return NV4_GR_OK;
        case 0x3f0:
            nv10_insrt(nv4, NV10_RASTER, 26, 2, data);
            return NV4_GR_OK;
        case 0x3f4: /* FLAT_SHADE_OP: first vertex */
            nv10_insrt(nv4, NV10_CONFIG_B, 0, 1, data & 1);
            return NV4_GR_OK;
        case 0xd40: /* COLOR_LOGIC_OP: not modeled */
        case 0xd44:
            return NV4_GR_OK;

        default:
            nv4_gap(nv4, cls, mthd, "Celsius method");
            return NV4_GR_OK;
    }
}

/* ------------------------------------------------------------------ */
/* Setup.                                                              */
/* ------------------------------------------------------------------ */
void
nv10_celsius_reset(nv4_t *nv4)
{
    static const float vab[VAB_WORDS] = {
        0.0f, 0.0f, 0.0f, 1.0f, /* position */
        1.0f, 1.0f, 1.0f, 1.0f, /* diffuse */
        0.0f, 0.0f, 0.0f, 0.0f, /* specular, fog */
        0.0f, 0.0f, 0.0f, 1.0f, /* texture coordinates */
        0.0f, 0.0f, 0.0f, 1.0f,
        0.0f, 0.0f, 1.0f, 0.0f, /* normal */
        1.0f, 0.0f, 0.0f, 0.0f  /* weight */
    };

    if (nv4->celsius)
        memset(nv4->celsius, 0, sizeof(*nv4->celsius));
    memset(nv4->pgraph_pipe, 0, sizeof(nv4->pgraph_pipe));
    for (int i = 0; i < VAB_WORDS; i++)
        nv4->pgraph_pipe[PIPE_VAB + i] = f2u(vab[i]);
    nv4->celsius_si = -1;
}

void
nv10_celsius_init(nv4_t *nv4)
{
    nv4->celsius = calloc(1, sizeof(*nv4->celsius));
    nv10_celsius_reset(nv4);
}

void
nv10_celsius_close(nv4_t *nv4)
{
    free(nv4->celsius);
    nv4->celsius = NULL;
}
