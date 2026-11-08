/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- the Direct3D classes' batch: draw
 *          states, triangles and spans, as the CPU span shader
 *          (nv4_pgraph_3d.c) and the GPU lane (nv4_pgraph_3d_gpu.c)
 *          consume them. The GeForce 256's Celsius class
 *          (nv10_celsius.c) transforms and lights its vertices itself and
 *          hands the batch screen-space triangles under states of its
 *          own (celsius set: register combiners, the NV10 texture
 *          formats, blend equations); the CPU shades those, the GPU lane
 *          declines a batch holding any.
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#ifndef NV4_PGRAPH_3D_H
#define NV4_PGRAPH_3D_H

#include <stddef.h>

struct nv4_gpu_s;

#define B3D_STATES 64
#define B3D_TRIS   16384
#define B3D_SPANS  262144

/* Attributes interpolated across a triangle */
enum {
    AT_Z,
    AT_Q,       /* rhw, or 1 without perspective */
    AT_U0,      /* u * q */
    AT_V0,
    AT_U1,
    AT_V1,
    AT_R,
    AT_G,
    AT_B,
    AT_A,
    AT_SR,
    AT_SG,
    AT_SB,
    AT_FOG,
    AT_COUNT
};

typedef struct {
    int      on;
    uint32_t dma;      /* DMA object instance */
    uint32_t offset;   /* level 0 */
    int      fmt;      /* texel format 1-7 */
    int      bpp;
    int      wlog, hlog;
    int      levels;
    uint32_t level_off[12];
    int      wrap_u, wrap_v;
    int      min, mag;
    float    lod_bias;
    int      center;   /* texel centers at +0.5 */
    int      ckey;
    uint32_t size;     /* bytes of all levels */
    /* Celsius (NV10) texture units */
    int      cfmt;     /* the NV10 format, 0-0x18 (fmt is then 0) */
    int      dxt;      /* 1, 3 or 5: 4x4 blocks, stored block-linear */
    int      linear;   /* IMAGE formats: rows of <pitch> bytes */
    uint32_t pitch;
    int      w, h;     /* IMAGE formats: any size */
    int      cube;     /* six faces of <face> bytes */
    uint32_t face;
    int      pal;      /* paletted: the palette's DMA object and offset */
    uint32_t pal_dma, pal_off;
    int      ckey_op;  /* COLOR_KEY_OPERATION: 1 alpha 0, 2 all 0, 3 kill */
    int      akill;    /* texels of alpha 0 kill the pixel */
    float    lod_min, lod_max;
} nv4_3d_tex_t;

typedef struct {
    int      dx6;
    uint32_t col_base, z_base;     /* absolute VRAM byte addresses */
    uint32_t col_pitch, z_pitch;
    uint32_t col_lim, z_lim;       /* last valid byte, absolute */
    int      col_fmt, bpp;
    int      swz, swz_wl, swz_hl;  /* swizzled target of 2^wl by 2^hl
                                      pixels: no pitch, x and y bits
                                      interleave (depth too) */
    int      z_on;                 /* a depth surface exists */
    int32_t  cx0, cy0, cx1, cy1;
    float    origin;               /* 0: D3D pixel centers, 0.5: corner */
    nv4_3d_tex_t tex[2];
    uint32_t color_key;
    /* DX5 */
    int      texblend;
    /* DX6 */
    uint32_t comb_a[2], comb_c[2], factor;
    /* both */
    int      flat, persp, spec, fog, ablend, sblend, dblend;
    uint32_t fog_color;
    int      atest, afunc, aref;
    int      zen, zfunc, zwrite, zfloat;
    int      cull, dither;
    int      wr_a, wr_r, wr_g, wr_b;
    int      sten, sfunc, sref, smask_r, smask_w, sop_fail, sop_zfail, sop_zpass, swrite;
    /* Celsius: the register combiners (two general stages and the
       final one), their constants, the blend equation and constant
       colour, the texture colour keys, the depth buffer's own depth,
       w-buffering, the depth clip range, the fog colour and function */
    int      celsius;
    uint32_t rc_ia[2], rc_ic[2], rc_oa[2], rc_oc[2], rc_k[2], rc_f0, rc_f1;
    int      rc_n;
    int      beq;
    uint32_t bcolor;
    uint32_t ckey[2];
    int      z_bpp;
    int      wbuf;
    float    zclip0, zclip1;
    int      fogmode;
    /* 2D rectangle fills queued with the 3D work: col_* is their
       destination, each fill's triangle record carries its packed pixel
       in the bits of c[0] */
    int      fill;
    int      rc, rz; /* its color and depth regions in the batch, -1: none */
} nv4_3d_state_t;

typedef struct {
    int   state;
    float x0, y0;            /* vertex 0, pixels */
    float c[AT_COUNT], dx[AT_COUNT], dy[AT_COUNT];
} nv4_3d_tri_t;

/* The Celsius class's interpolants past AT_COUNT, kept apart so that
   nv4_3d_tri_t stays as the GPU kernel reads it: the r and q texture
   coordinates of both units (already divided by w where the transform
   unit does it), and the specular alpha the combiners can read */
enum {
    AX_R0,
    AX_Q0,
    AX_R1,
    AX_Q1,
    AX_SA,
    AX_COUNT
};

typedef struct {
    float c[AX_COUNT], dx[AX_COUNT], dy[AX_COUNT];
} nv4_3d_tri_ext_t;

typedef struct {
    int16_t  y, x0, x1;      /* x1 exclusive */
    uint16_t tri;
} nv4_3d_span_t;

/* A 2D fill's triangle (state fill): c[NV4_FILL_KIND] says what it does,
   c[0] the value to store, or the words of nv4_3d_fill_rop */
#define NV4_FILL_KIND  13
#define NV4_FILL_STORE 0
#define NV4_FILL_ROP   1

#define B3D_MAX_WORKERS 16
#define B3D_REGIONS     4

/* A row layout in the batch: row y at base + y * pitch. A batch can hold
   several when their bytes do not meet (a back buffer and its depth
   buffer), since rows are then still each on their own bytes. */
typedef struct {
    uint32_t base, pitch;
    uint32_t lo, hi;   /* the bytes its states can reach */
    uint32_t wlo, whi; /* the bytes touched so far; wlo > whi: none */
} nv4_3d_region_t;

typedef struct {
    nv4_t    *nv4;
    int       id;
    thread_t *thread;
    event_t  *wake; /* a batch is ready, or the pool is closing */
    event_t  *done; /* this worker finished the batch */
} nv4_3d_worker_t;

/* A batch the worker threads shade: its arrays and spans, and, while it
   is in flight (shaded as emulation goes on), the VRAM bytes it reads or
   writes, which nothing else may touch until it is done */
typedef struct {
    const nv4_3d_state_t   *states;
    const nv4_3d_tri_t     *tris;
    const nv4_3d_tri_ext_t *ext;
    const nv4_3d_span_t    *spans;
    int                   nspans;
    int                   busy;
    int                   nranges;
    uint32_t              lo[B3D_REGIONS + 1], hi[B3D_REGIONS + 1];
} nv4_3d_fly_t;

typedef struct nv4_3d_batch_s {
    /* the arrays being filled: one of the two sets, the other in flight */
    nv4_3d_state_t   *states;
    nv4_3d_tri_t     *tris;
    nv4_3d_tri_ext_t *ext;
    nv4_3d_span_t    *spans;
    int               nstates, ntris, nspans;
    int               ncelsius; /* Celsius states in the batch: the GPU declines it */
    nv4_3d_state_t   *set_states[2];
    nv4_3d_tri_t     *set_tris[2];
    nv4_3d_tri_ext_t *set_ext[2];
    nv4_3d_span_t    *set_spans[2];
    int             set;

    /* The row layouts the batch draws into (threads and GPU only), with
       the bytes each has touched (depth also when only read) */
    nv4_3d_region_t regions[B3D_REGIONS];
    int             nregions;
    uint32_t        tex_lo, tex_hi; /* VRAM its textures read; lo > hi: none */
    int             bus_tex; /* a texture is (or may be) outside VRAM */
    int             serial; /* rows alias: shade on one thread */

    /* The worker threads shade a batch while emulation goes on (no GPU,
       no verify or recording); the batch they are given */
    /* A disjoint row copy following submitted 3D work. Its source and
       destination remain owned until it runs, before another submission. */
    struct {
        uint32_t src, dst, spitch, dpitch, len, rows, shi, dhi;
        int      pending;
    } copy;
    int          async;
    nv4_3d_fly_t fly;

    /* 3D seen lately (vertical blank count of the last triangle): large
       2D fills then join the batch instead of waiting for it */
    int      had3d;
    uint32_t last3d;

    int             nthreads;
    int             run;
    nv4_3d_worker_t workers[B3D_MAX_WORKERS];

    /* Rows are shaded apart (threads or the GPU): the batch must keep
       every row on its own bytes */
    int               parallel;
    struct nv4_gpu_s *gpu;

    uint64_t st_flushes, st_spans, st_us; /* for nv4_3d_stats */
    uint64_t st_nosurf;                   /* triangles dropped: no valid colour surface */
    uint64_t st_serial, st_serial_spans;  /* flushes shaded serially (rows alias), and their spans */
    uint64_t st_sync_us[NV4_SYNC_CAUSES]; /* time waiting for the GPU, by cause */
    uint32_t st_sync_n[NV4_SYNC_CAUSES];

    /* NV4_3D_VERIFY: 1 compares the batch's regions, with all of VRAM
       every NV4_VERIFY_FULL_EVERY flushes (and for rows that alias);
       "full" compares all of VRAM every time */
    int      verify, verify_full;
    uint8_t *vsnap, *vres;
    uint32_t vflushes, vbad, vfull;

    /* NV4_3D_RECORD=<dir>[,every[,max]]: flushed batches to files, for
       tests/video/nv4_3d_replay_test.c (nv4_3d_record) */
    char     rec_dir[512];
    uint32_t rec_every, rec_max, rec_seen, rec_done;
} nv4_3d_batch_t;

#define NV4_VERIFY_FULL_EVERY 64

/* A recorded batch (NV4_3D_RECORD), little-endian:
     "NV4B", version, sizeof nv4_3d_state_t / nv4_3d_tri_t /
     nv4_3d_span_t / nv4_3d_region_t, vram_size, vram_mask, nstates,
     ntris, nspans, nregions, serial, parallel, tex_lo, tex_hi (u32
     each), the four arrays, then chunks to the end of the file: a u32
     kind, then
       NV4_REC_VRAM: addr, len, the bytes -- VRAM before the batch
       NV4_REC_TEX:  dma, offset, left, len, addr, target, the bytes -- a
                     piece of a texture's DMA window as nv4_dma_translate
                     gave it (left: nv4_dma_page_left there; len: the
                     bytes kept, to the texture's end), system memory at
                     its address behind the AGP aperture */
#define NV4_REC_MAGIC   0x4234564eu /* "NV4B" */
#define NV4_REC_VERSION 1
#define NV4_REC_VRAM    0
#define NV4_REC_TEX     1

/* The GPU lane (nv4_pgraph_3d_gpu.c). nv4_gpu_shade runs a whole batch
   and returns 1, or returns 0 and leaves VRAM untouched when it cannot
   take it; the caller then shades it on the CPU. */
extern struct nv4_gpu_s *nv4_gpu_init(nv4_t *nv4);
extern int               nv4_gpu_shade(struct nv4_gpu_s *g, nv4_t *nv4, const nv4_3d_batch_t *b);
extern void              nv4_gpu_close(struct nv4_gpu_s *g);
/* The batch in flight: wait for it and copy its rows into VRAM; does
   it touch bytes lo..hi? */
extern void              nv4_gpu_wait(struct nv4_gpu_s *g, nv4_t *nv4);
extern int               nv4_gpu_pending_overlaps(struct nv4_gpu_s *g, uint32_t lo, uint32_t hi);
extern void              nv4_gpu_counts(struct nv4_gpu_s *g, uint64_t *batches, uint64_t *fallbacks);

/* nv4_pgraph_3d.c, for the Celsius class: put a draw state into the
   batch (its index, or -1 with no surface to draw into), then draw a
   triangle under it from screen-space vertices (x, y with the pixel
   grid's origin at 0,0) and their interpolants. The caller culls and
   picks the flat-shaded colours; a full batch is flushed first, which
   resets nv4->celsius_si. */
extern int  nv4_3d_place(nv4_t *nv4, nv4_3d_state_t *s);
extern int  nv4_3d_room(nv4_t *nv4);
extern void nv4_3d_raster(nv4_t *nv4, int si, const float xy[3][2], const float a[3][AT_COUNT],
                          const float ax[3][AX_COUNT]);

#endif
