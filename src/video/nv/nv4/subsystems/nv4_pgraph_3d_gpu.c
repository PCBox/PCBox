/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NVIDIA RIVA TNT / TNT2 -- GPU lane for the Direct3D classes.
 *
 *          Optional, off by default: the "Draw 3D on the host GPU"
 *          option, or NV4_GPU=1 in the environment. A flushed batch
 *          (nv4_pgraph_3d.h) is shaded by a Vulkan compute kernel
 *          (nv4_pgraph_3d_gpu.comp) instead of the CPU: setup stays on
 *          the CPU, the kernel takes the same draw states, triangles
 *          and spans, one invocation per pixel walking its row's spans
 *          in submission order, and writes the bytes the CPU span
 *          shader would.
 *
 *          A flush submits and returns (two batches can be in flight):
 *          the color and depth rows the batch touches are copied into a
 *          device-local mirror of VRAM (but not those a batch in flight
 *          owns: the mirror's are newer),
 *          the batch's textures are gathered through their DMA objects
 *          (VRAM or system memory alike) into a texture arena, the
 *          records go to device memory, the kernel runs and the rows go
 *          back to a staging buffer. The rows reach VRAM when the next
 *          flush or a sync point (nv4_3d_sync) waits for the batch, so
 *          the GPU works while the guest goes on. A batch the lane
 *          cannot take (rows past the end of VRAM, textures beyond the
 *          arena) is left to the CPU.
 *
 *          Vulkan is opened at run time, so a host without it only
 *          logs that the lane is off. NV4_3D_VERIFY=1 compares every
 *          batch with the CPU's result (nv4_pgraph_3d.c).
 *
 * Authors: Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2026 Marcos Alves
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <wchar.h>
#include <86box/86box.h>
#include <86box/plat_unused.h>
#include <86box/device.h>
#include <86box/dma.h>
#include <86box/mem.h>
#include <86box/agpgart.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/timer.h>
#include <86box/thread.h>
#include <86box/video.h>
#include <86box/vid_svga.h>
#include <86box/plat_unused.h>
#include <86box/nv/vid_nv4.h>
#include "nv4_pgraph_3d.h"

#ifndef NV4_GPU_HAVE_VULKAN

struct nv4_gpu_s *
nv4_gpu_init(UNUSED(nv4_t *nv4))
{
    pclog("NV4 GPU: built without Vulkan headers, 3D stays on the CPU\n");
    return NULL;
}

int
nv4_gpu_shade(UNUSED(struct nv4_gpu_s *g), UNUSED(nv4_t *nv4), UNUSED(const nv4_3d_batch_t *b))
{
    return 0;
}

void
nv4_gpu_close(UNUSED(struct nv4_gpu_s *g))
{
}

void
nv4_gpu_wait(UNUSED(struct nv4_gpu_s *g), UNUSED(nv4_t *nv4))
{
}

int
nv4_gpu_pending_overlaps(UNUSED(struct nv4_gpu_s *g), UNUSED(uint32_t lo), UNUSED(uint32_t hi))
{
    return 0;
}

void
nv4_gpu_counts(UNUSED(struct nv4_gpu_s *g), uint64_t *batches, uint64_t *fallbacks)
{
    *batches   = 0;
    *fallbacks = 0;
}

#else

#    include <86box/nv/nv_gpu_vk.h>
#    include <86box/nv/nv4_3d_gpu_spv.h>

#    define GPU_TEX_ARENA (32u << 20)

/* Records as the kernel reads them (std430, every field 4 bytes). */
typedef struct {
    int32_t  on, fmt, bpp, wlog, hlog, levels, wrap_u, wrap_v, min, mag;
    float    lod_bias;
    int32_t  center, ckey;
    uint32_t base; /* level 0 in the texture arena */
    uint32_t level_off[12];
} nv4_gpu_tex_t;

typedef struct {
    uint32_t      col_base, z_base, col_pitch, z_pitch, col_lim, z_lim;
    int32_t       col_fmt, bpp, z_on, dx6;
    float         origin;
    uint32_t      color_key;
    int32_t       texblend;
    uint32_t      comb_a0, comb_a1, comb_c0, comb_c1, factor;
    int32_t       flat, persp, spec, fog, ablend, sblend, dblend;
    uint32_t      fog_color;
    int32_t       atest, afunc, aref, zen, zfunc, zwrite, zfloat, dither;
    int32_t       wr_a, wr_r, wr_g, wr_b;
    int32_t       sten, sfunc, sref, smask_r, smask_w, sop_fail, sop_zfail, sop_zpass, swrite;
    int32_t       fill;
    nv4_gpu_tex_t tex[2];
} nv4_gpu_state_t;

typedef struct {
    int32_t y, x0, x1, tri;
} nv4_gpu_span_t;

typedef struct {
    uint32_t first, count;
} nv4_gpu_row_t;

_Static_assert(sizeof(nv4_gpu_tex_t) == 26 * 4, "nv4_gpu_tex_t layout");
_Static_assert(sizeof(nv4_gpu_state_t) == 100 * 4, "nv4_gpu_state_t layout");
_Static_assert(sizeof(nv4_3d_tri_t) == (3 + 3 * AT_COUNT) * 4, "nv4_3d_tri_t layout");
_Static_assert(AT_COUNT == 14, "the kernel interpolates 14 attributes");

/* Two batches can be in flight (NV_GPU_SLOTS): each has its own records,
   command buffer and fence (nv_gpu_vk.h). The rows a batch in flight owns
   are newer in the GPU's VRAM mirror than in VRAM, so a later batch does
   not upload them; batches complete (rows copied back) oldest first. */
typedef struct {
    nv_gpu_vk_slot_t *vk; /* qpool: GPU timestamps: start, rows in, kernel, rows out */
    int               pending;
    uint64_t          seq;
    VkBufferCopy      pdown[B3D_REGIONS]; /* its rows, to copy back */
    int               pndown;
    int               p_spans, p_chunks;
    uint32_t          p_rows;
    uint64_t          p_px;
} gpu_slot_t;

/* A texture in the arena: where its bytes came from, a hash of them,
   and where they are */
typedef struct {
    uint32_t dma, offset, size, base;
    uint64_t hash;
} gpu_texent_t;

#    define GPU_TEX_CACHE 1024

typedef struct nv4_gpu_s {
    nv_gpu_vk_t           vk;
    gpu_slot_t            slot[NV_GPU_SLOTS];
    int                   next; /* the slot the next batch takes */
    uint64_t              seq;

    uint32_t      vram_size;
    /* The texture arena keeps textures across batches: each batch hashes
       its textures' bytes where they are, and copies only the ones that
       are new or changed. */
    gpu_texent_t  cache[GPU_TEX_CACHE];
    int           ncache;
    uint32_t      tex_used;           /* arena bytes taken */
    int           flush_tex[B3D_STATES * 2]; /* cache entries this batch checked */
    int           nflush_tex;
    VkBufferCopy  tcopy[B3D_STATES * 2];     /* arena regions to upload */
    int           ntcopy;
    uint64_t      hash_bytes;
    uint32_t      row_count[4096];
    uint32_t      row_first[4096];
    uint64_t      batches, fallbacks;
    uint64_t      us_gather, us_copy, us_wait, tex_bytes; /* where the time goes */
    uint64_t      row_bytes;
    uint64_t      ts_ns[3];  /* uploads, kernel, download */
    int           prof_left; /* flushes left to log one by one (NV4_GPU_PROFILE=n) */
} nv4_gpu_t;

struct nv4_gpu_s *
nv4_gpu_init(nv4_t *nv4)
{
    nv4_gpu_t             *g = calloc(1, sizeof(nv4_gpu_t));
    const nv_gpu_vk_desc_t d = {
        .tag       = "NV4 GPU",
        .app_name  = "86Box RIVA TNT",
        .vram_size = nv4->vram_size,
        .tex_arena = GPU_TEX_ARENA,
        .rec_size  = { sizeof(nv4_gpu_state_t) * B3D_STATES, sizeof(nv4_3d_tri_t) * B3D_TRIS,
                       sizeof(nv4_gpu_span_t) * B3D_SPANS, sizeof(nv4_gpu_row_t) * 4096 },
        .spv       = (const uint32_t *) nv4_gpu_spv,
        .spv_size  = sizeof(nv4_gpu_spv),
        .queries   = 4
    };

    g->vram_size = nv4->vram_size;
    if (getenv("NV4_GPU_PROFILE"))
        g->prof_left = atoi(getenv("NV4_GPU_PROFILE"));
    if (!nv_gpu_vk_open(&g->vk, &d)) {
        free(g);
        return NULL;
    }
    for (int i = 0; i < NV_GPU_SLOTS; i++)
        g->slot[i].vk = &g->vk.slot[i];
    return g;
}

void
nv4_gpu_counts(struct nv4_gpu_s *g, uint64_t *batches, uint64_t *fallbacks)
{
    *batches   = g->batches;
    *fallbacks = g->fallbacks;
    pclog("nv4 3D: GPU textures %llu us (%llu KB hashed, %llu KB copied), row copies %llu us, waiting %llu us\n",
          (unsigned long long) g->us_gather, (unsigned long long) (g->hash_bytes >> 10),
          (unsigned long long) (g->tex_bytes >> 10),
          (unsigned long long) g->us_copy, (unsigned long long) g->us_wait);
    pclog("nv4 3D: GPU rows %llu KB in and out; on the GPU: uploads %llu us, kernel %llu us, download %llu us\n",
          (unsigned long long) (g->row_bytes >> 10), (unsigned long long) (g->ts_ns[0] / 1000),
          (unsigned long long) (g->ts_ns[1] / 1000), (unsigned long long) (g->ts_ns[2] / 1000));
    g->us_gather = g->us_copy = g->us_wait = g->tex_bytes = g->row_bytes = g->hash_bytes = 0;
    g->ts_ns[0] = g->ts_ns[1] = g->ts_ns[2] = 0;
}

void
nv4_gpu_close(struct nv4_gpu_s *g)
{
    if (!g)
        return;
    for (int i = 0; i < NV_GPU_SLOTS; i++)
        if (g->slot[i].pending)
            g->vk.vkWaitForFences(g->vk.dev, 1, &g->vk.slot[i].fence, VK_TRUE, UINT64_MAX);
    pclog("NV4 GPU: %llu batches on the GPU, %llu left to the CPU\n", (unsigned long long) g->batches,
          (unsigned long long) g->fallbacks);
    nv_gpu_vk_close(&g->vk);
    free(g);
}

/* <len> bytes of a texture at DMA offset <off> (within one page of the
   window: nv4_dma_page_left) as
   the CPU span shader reads them: a pointer into VRAM or system memory,
   or <tmp> filled. */
static const uint8_t *
gpu_tex_bytes(nv4_t *nv4, const nv4_3d_tex_t *t, uint32_t off, uint32_t len, uint8_t *tmp)
{
    uint32_t addr;
    int      target;

    if (nv4_dma_translate(nv4, t->dma, off, 0, &addr, &target) != NV4_DMA_OK) {
        memset(tmp, 0, len);
        return tmp;
    }
    if (!target) {
        uint32_t va = addr & nv4->vram_mask;

        if (va + len <= nv4->vram_size)
            return nv4->svga.vram + va;
        for (uint32_t i = 0; i < len; i++)
            tmp[i] = nv4->svga.vram[(va + i) & nv4->vram_mask];
        return tmp;
    }
    {
        uint32_t pa = addr;
        uint32_t ra;

        /* through the AGP aperture to the page behind it (len stays
           within one page) */
        if (((pa & 0xfff) + len) > 0x1000) {
            dma_bm_read(pa, tmp, len, 1);
            return tmp;
        }
        if (agpgart_resolve(pa, &ra))
            pa = ra;
        /* system memory above 1 MB is one flat array */
        if ((pa >= 0x100000) && ((uint64_t) pa + len <= (uint64_t) mem_size * 1024))
            return ram + pa;
        dma_bm_read(pa, tmp, len, 1);
        return tmp;
    }
}

static void gpu_complete_upto(nv4_gpu_t *g, nv4_t *nv4, uint64_t seq);
int         nv4_gpu_pending_overlaps(struct nv4_gpu_s *g, uint32_t lo, uint32_t hi);

/* A texture drawn into by a batch still in flight (render to texture)
   reads that batch's pixels: it completes first. */
static void
gpu_tex_settle(nv4_gpu_t *g, nv4_t *nv4, const nv4_3d_tex_t *t)
{
    for (uint32_t done = 0; done < t->size;) {
        uint32_t off   = t->offset + done;
        uint32_t chunk = nv4_dma_page_left(nv4, t->dma, off);
        uint32_t addr;
        int      target;

        if (chunk > t->size - done)
            chunk = t->size - done;
        if ((nv4_dma_translate(nv4, t->dma, off, 0, &addr, &target) == NV4_DMA_OK) && !target) {
            uint32_t va = addr & nv4->vram_mask;

            if (nv4_gpu_pending_overlaps(g, va, va + chunk - 1)) {
                gpu_complete_upto(g, nv4, UINT64_MAX);
                return;
            }
        }
        done += chunk;
    }
}

/* A 64-bit hash of a texture's bytes, read where they are. */
static uint64_t
gpu_tex_hash(nv4_gpu_t *g, nv4_t *nv4, const nv4_3d_tex_t *t)
{
    uint8_t  tmp[0x1000];
    uint64_t h = 0x9e3779b97f4a7c15ull ^ t->size;

    gpu_tex_settle(g, nv4, t);

    for (uint32_t done = 0; done < t->size;) {
        uint32_t       off   = t->offset + done;
        uint32_t       chunk = nv4_dma_page_left(nv4, t->dma, off);
        const uint8_t *p;
        uint32_t       i = 0;

        if (chunk > t->size - done)
            chunk = t->size - done;
        p = gpu_tex_bytes(nv4, t, off, chunk, tmp);
        for (; i + 8 <= chunk; i += 8) {
            uint64_t w;

            memcpy(&w, p + i, 8);
            h = (h ^ w) * 0x100000001b3ull;
            h ^= h >> 29;
        }
        for (; i < chunk; i++)
            h = (h ^ p[i]) * 0x100000001b3ull;
        done += chunk;
    }
    g->hash_bytes += t->size;
    return h;
}

/* Copy a texture's bytes into the arena's staging at <base>, to upload.
   A batch in flight may still read the arena and its staging: they
   finish first. */
static void
gpu_tex_copy(nv4_gpu_t *g, nv4_t *nv4, const nv4_3d_tex_t *t, uint32_t base)
{
    uint8_t *dst = (uint8_t *) g->vk.b[NV_GPU_B_TSTAGE].map + base;

    gpu_complete_upto(g, nv4, UINT64_MAX);

    for (uint32_t done = 0; done < t->size;) {
        uint32_t off   = t->offset + done;
        uint32_t chunk = nv4_dma_page_left(nv4, t->dma, off);

        if (chunk > t->size - done)
            chunk = t->size - done;
        {
            const uint8_t *src = gpu_tex_bytes(nv4, t, off, chunk, dst + done);

            if (src != dst + done)
                memcpy(dst + done, src, chunk);
        }
        done += chunk;
    }
    g->tcopy[g->ntcopy].srcOffset = base;
    g->tcopy[g->ntcopy].dstOffset = base;
    g->tcopy[g->ntcopy].size      = t->size;
    g->ntcopy++;
    g->tex_bytes += t->size;
}

/* A texture's place in the arena for this batch: from the cache when
   its bytes have not changed, else copied in. UINT32_MAX when the arena
   is full. */
static uint32_t
gpu_gather_tex(nv4_gpu_t *g, nv4_t *nv4, const nv4_3d_tex_t *t)
{
    uint64_t      h;
    gpu_texent_t *e = NULL;

    for (int i = 0; i < g->nflush_tex; i++) {
        gpu_texent_t *c = &g->cache[g->flush_tex[i]];

        if ((c->dma == t->dma) && (c->offset == t->offset) && (c->size == t->size))
            return c->base;
    }
    if (!t->size)
        return UINT32_MAX;
    h = gpu_tex_hash(g, nv4, t);
    for (int i = 0; i < g->ncache; i++)
        if ((g->cache[i].dma == t->dma) && (g->cache[i].offset == t->offset) && (g->cache[i].size == t->size)) {
            e = &g->cache[i];
            break;
        }
    if (!e) {
        if ((g->ncache >= GPU_TEX_CACHE) || (t->size > GPU_TEX_ARENA - g->tex_used))
            return UINT32_MAX;
        e         = &g->cache[g->ncache++];
        e->dma    = t->dma;
        e->offset = t->offset;
        e->size   = t->size;
        e->base   = g->tex_used;
        e->hash   = ~h; /* not there yet */
        g->tex_used = (e->base + t->size + 3) & ~3u;
    }
    if (e->hash != h) {
        gpu_tex_copy(g, nv4, t, e->base);
        e->hash = h;
    }
    g->flush_tex[g->nflush_tex++] = (int) (e - g->cache);
    return e->base;
}

static void
gpu_pack_tex(nv4_gpu_tex_t *o, const nv4_3d_tex_t *t, uint32_t base)
{
    o->on       = t->on;
    o->fmt      = t->fmt;
    o->bpp      = t->bpp;
    o->wlog     = t->wlog;
    o->hlog     = t->hlog;
    o->levels   = t->levels;
    o->wrap_u   = t->wrap_u;
    o->wrap_v   = t->wrap_v;
    o->min      = t->min;
    o->mag      = t->mag;
    o->lod_bias = t->lod_bias;
    o->center   = t->center;
    o->ckey     = t->ckey;
    o->base     = base;
    for (int l = 0; l < 12; l++)
        o->level_off[l] = t->level_off[l];
}

static void
gpu_pack_state(nv4_gpu_state_t *o, const nv4_3d_state_t *s)
{
    memset(o, 0, sizeof(*o));
    o->col_base  = s->col_base;
    o->z_base    = s->z_base;
    o->col_pitch = s->col_pitch;
    o->z_pitch   = s->z_pitch;
    o->col_lim   = s->col_lim;
    o->z_lim     = s->z_lim;
    o->col_fmt   = s->col_fmt;
    o->bpp       = s->bpp;
    o->z_on      = s->z_on;
    o->dx6       = s->dx6;
    o->origin    = s->origin;
    o->color_key = s->color_key;
    o->texblend  = s->texblend;
    o->comb_a0   = s->comb_a[0];
    o->comb_a1   = s->comb_a[1];
    o->comb_c0   = s->comb_c[0];
    o->comb_c1   = s->comb_c[1];
    o->factor    = s->factor;
    o->flat      = s->flat;
    o->persp     = s->persp;
    o->spec      = s->spec;
    o->fog       = s->fog;
    o->ablend    = s->ablend;
    o->sblend    = s->sblend;
    o->dblend    = s->dblend;
    o->fog_color = s->fog_color;
    o->atest     = s->atest;
    o->afunc     = s->afunc;
    o->aref      = s->aref;
    o->zen       = s->zen;
    o->zfunc     = s->zfunc;
    o->zwrite    = s->zwrite;
    o->zfloat    = s->zfloat;
    o->dither    = s->dither;
    o->wr_a      = s->wr_a;
    o->wr_r      = s->wr_r;
    o->wr_g      = s->wr_g;
    o->wr_b      = s->wr_b;
    o->sten      = s->sten;
    o->sfunc     = s->sfunc;
    o->sref      = s->sref;
    o->smask_r   = s->smask_r;
    o->smask_w   = s->smask_w;
    o->sop_fail  = s->sop_fail;
    o->sop_zfail = s->sop_zfail;
    o->sop_zpass = s->sop_zpass;
    o->swrite    = s->swrite;
    o->fill      = s->fill;
}

/* Wait for one batch in flight and copy its rows back into VRAM. */
static void
gpu_complete(nv4_gpu_t *g, nv4_t *nv4, gpu_slot_t *sl)
{
    uint8_t *stage = (uint8_t *) g->vk.b[NV_GPU_B_VSTAGE].map;
    uint64_t t0, t1;

    t0 = nv_gpu_now_us();
    g->vk.vkWaitForFences(g->vk.dev, 1, &sl->vk->fence, VK_TRUE, UINT64_MAX);
    t1 = nv_gpu_now_us();
    g->us_wait += t1 - t0;
    if (sl->vk->qpool) {
        uint64_t ts[4];

        if (g->vk.vkGetQueryPoolResults(g->vk.dev, sl->vk->qpool, 0, 4, sizeof(ts), ts, sizeof(uint64_t), VK_QUERY_RESULT_64_BIT)
            == VK_SUCCESS) {
            for (int i = 0; i < 3; i++)
                g->ts_ns[i] += (uint64_t) ((double) (ts[i + 1] - ts[i]) * g->vk.ts_period);
            if (g->prof_left) {
                g->prof_left--;
                pclog("nv4 GPU prof: %d spans %llu px rows %u x%d: up %.0f kernel %.0f down %.0f us\n", sl->p_spans,
                      (unsigned long long) sl->p_px, sl->p_rows, sl->p_chunks, (ts[1] - ts[0]) * g->vk.ts_period / 1000.0,
                      (ts[2] - ts[1]) * g->vk.ts_period / 1000.0, (ts[3] - ts[2]) * g->vk.ts_period / 1000.0);
            }
        }
    }
    for (int i = 0; i < sl->pndown; i++) {
        memcpy(nv4->svga.vram + sl->pdown[i].srcOffset, stage + sl->pdown[i].srcOffset, sl->pdown[i].size);
        g->row_bytes += sl->pdown[i].size;
    }
    for (int i = 0; i < sl->pndown; i++)
        for (uint32_t a = sl->pdown[i].srcOffset & ~0xfffu; a < sl->pdown[i].srcOffset + sl->pdown[i].size; a += 0x1000)
            nv4->svga.changedvram[a >> 12] = nv4->svga.monitor->mon_changeframecount;
    g->us_copy += nv_gpu_now_us() - t1;
    sl->pending = 0;
}

/* Complete the batches in flight up to number <seq>, oldest first (a
   later batch's rows are newer). */
static void
gpu_complete_upto(nv4_gpu_t *g, nv4_t *nv4, uint64_t seq)
{
    for (;;) {
        gpu_slot_t *old = NULL;

        for (int i = 0; i < NV_GPU_SLOTS; i++)
            if (g->slot[i].pending && (g->slot[i].seq <= seq) && (!old || (g->slot[i].seq < old->seq)))
                old = &g->slot[i];
        if (!old)
            return;
        gpu_complete(g, nv4, old);
    }
}

void
nv4_gpu_wait(struct nv4_gpu_s *g, nv4_t *nv4)
{
    gpu_complete_upto(g, nv4, UINT64_MAX);
}

int
nv4_gpu_pending_overlaps(struct nv4_gpu_s *g, uint32_t lo, uint32_t hi)
{
    for (int s = 0; s < NV_GPU_SLOTS; s++) {
        const gpu_slot_t *sl = &g->slot[s];

        for (int i = 0; sl->pending && (i < sl->pndown); i++)
            if ((sl->pdown[i].srcOffset <= hi) && (lo < sl->pdown[i].srcOffset + sl->pdown[i].size))
                return 1;
    }
    return 0;
}

/* Upload the bytes lo..hi of VRAM to the GPU's mirror, except those a
   batch in flight owns (the mirror's are newer). */
static void
gpu_upload_range(nv4_gpu_t *g, nv4_t *nv4, uint32_t lo, uint32_t hi, VkBufferCopy *up, int *nup, int max)
{
    uint8_t *stage = (uint8_t *) g->vk.b[NV_GPU_B_VSTAGE].map;

    for (int s = 0; s < NV_GPU_SLOTS; s++) {
        const gpu_slot_t *sl = &g->slot[s];

        for (int i = 0; sl->pending && (i < sl->pndown); i++) {
            uint32_t plo = (uint32_t) sl->pdown[i].srcOffset;
            uint32_t phi = plo + (uint32_t) sl->pdown[i].size - 1;

            if ((plo > hi) || (phi < lo))
                continue;
            /* the parts either side */
            if (plo > lo)
                gpu_upload_range(g, nv4, lo, plo - 1, up, nup, max);
            if (phi < hi)
                gpu_upload_range(g, nv4, phi + 1, hi, up, nup, max);
            return;
        }
    }
    /* at most 1 + 8 pieces per region (two batches in flight, four
       regions each), so <max> (64) is never reached */
    if (*nup >= max)
        return;
    memcpy(stage + lo, nv4->svga.vram + lo, hi - lo + 1);
    up[*nup].srcOffset = up[*nup].dstOffset = lo;
    up[*nup].size                           = hi - lo + 1;
    (*nup)++;
}

int
nv4_gpu_shade(struct nv4_gpu_s *g, nv4_t *nv4, const nv4_3d_batch_t *b)
{
    gpu_slot_t      *sl = &g->slot[g->next];
    nv4_gpu_state_t *gs;
    nv4_gpu_span_t  *gsp;
    nv4_gpu_row_t   *gr;
    VkCommandBuffer  cb;
    VkBufferCopy     up[64], down[B3D_REGIONS];
    int              nup = 0, ndown = 0;
    uint32_t         nrows = 0;
    int              xmax  = 0;
    uint32_t         pc[2];
    uint64_t         t0, t1, t2;

    /* this slot's last batch (and any older one) first */
    if (sl->pending)
        gpu_complete_upto(g, nv4, sl->seq);
    gs  = (nv4_gpu_state_t *) sl->vk->rech[NV_GPU_R_STATES].map;
    gsp = (nv4_gpu_span_t *) sl->vk->rech[NV_GPU_R_SPANS].map;
    gr  = (nv4_gpu_row_t *) sl->vk->rech[NV_GPU_R_ROWS].map;
    cb  = sl->vk->cb;
    if (!b->nregions)
        goto fallback;
    for (int r = 0; r < b->nregions; r++)
        if ((b->regions[r].wlo <= b->regions[r].whi) && (b->regions[r].whi >= g->vram_size))
            goto fallback;

    /* draw states and their textures; a full arena is emptied once and
       the batch's textures gathered again */
    t0 = nv_gpu_now_us();
    for (int pass = 0;; pass++) {
        int full = 0;

        g->nflush_tex = 0;
        g->ntcopy     = 0;
        for (int i = 0; (i < b->nstates) && !full; i++) {
            const nv4_3d_state_t *s = &b->states[i];

            gpu_pack_state(&gs[i], s);
            for (int n = 0; n < 2; n++) {
                uint32_t base = 0;

                if (s->tex[n].on && ((base = gpu_gather_tex(g, nv4, &s->tex[n])) == UINT32_MAX)) {
                    full = 1;
                    break;
                }
                gpu_pack_tex(&gs[i].tex[n], &s->tex[n], base);
            }
        }
        if (!full)
            break;
        g->ncache   = 0;
        g->tex_used = 0;
        if (pass)
            goto fallback;
    }
    t1 = nv_gpu_now_us();
    g->us_gather += t1 - t0;
    memcpy(sl->vk->rech[NV_GPU_R_TRIS].map, b->tris, sizeof(nv4_3d_tri_t) * b->ntris);

    /* spans grouped by row, each row in submission order */
    memset(g->row_count, 0, sizeof(g->row_count));
    for (int i = 0; i < b->nspans; i++)
        g->row_count[b->spans[i].y & 0xfff]++;
    {
        uint32_t at = 0;

        for (int y = 0; y < 4096; y++) {
            g->row_first[y] = at;
            if (g->row_count[y]) {
                gr[nrows].first = at;
                gr[nrows].count = g->row_count[y];
                nrows++;
            }
            at += g->row_count[y];
        }
    }
    for (int i = 0; i < b->nspans; i++) {
        const nv4_3d_span_t *sp = &b->spans[i];
        nv4_gpu_span_t      *o  = &gsp[g->row_first[sp->y & 0xfff]++];

        o->y   = sp->y;
        o->x0  = sp->x0;
        o->x1  = sp->x1;
        o->tri = sp->tri;
        if (sp->x1 > xmax)
            xmax = sp->x1;
    }
    if (!nrows)
        goto fallback;

    /* rows in */
    for (int r = 0; r < b->nregions; r++) {
        const nv4_3d_region_t *rg = &b->regions[r];

        if (rg->wlo > rg->whi)
            continue;
        gpu_upload_range(g, nv4, rg->wlo, rg->whi, up, &nup, 64);
        down[ndown].srcOffset = down[ndown].dstOffset = rg->wlo;
        down[ndown].size                              = rg->whi - rg->wlo + 1;
        ndown++;
    }
    if (!ndown)
        goto fallback;

    t2 = nv_gpu_now_us();
    g->us_copy += t2 - t1;
    {
        VkCommandBufferBeginInfo bi = { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
        VkSubmitInfo             si = { VK_STRUCTURE_TYPE_SUBMIT_INFO };

        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        g->vk.vkResetCommandBuffer(cb, 0);
        g->vk.vkBeginCommandBuffer(cb, &bi);
        if (sl->vk->qpool) {
            g->vk.vkCmdResetQueryPool(cb, sl->vk->qpool, 0, 4);
            g->vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, sl->vk->qpool, 0);
        }
        if (nup)
            g->vk.vkCmdCopyBuffer(cb, g->vk.b[NV_GPU_B_VSTAGE].buf, g->vk.b[NV_GPU_B_VRAM].buf, nup, up);
        {
            /* the records, read many times per pixel, to device memory */
            VkBufferCopy rc[4] = {
                { 0, 0, sizeof(nv4_gpu_state_t) * b->nstates },
                { 0, 0, sizeof(nv4_3d_tri_t) * b->ntris },
                { 0, 0, sizeof(nv4_gpu_span_t) * b->nspans },
                { 0, 0, sizeof(nv4_gpu_row_t) * nrows }
            };

            for (int i = 0; i < 4; i++)
                if (rc[i].size)
                    g->vk.vkCmdCopyBuffer(cb, sl->vk->rech[i].buf, sl->vk->rec[i].buf, 1, &rc[i]);
        }
        if (g->ntcopy)
            g->vk.vkCmdCopyBuffer(cb, g->vk.b[NV_GPU_B_TSTAGE].buf, g->vk.b[NV_GPU_B_TEX].buf, g->ntcopy, g->tcopy);
        nv_gpu_vk_barrier(&g->vk, cb, VK_PIPELINE_STAGE_TRANSFER_BIT | VK_PIPELINE_STAGE_HOST_BIT,
                    VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_HOST_WRITE_BIT, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                    VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT);
        if (sl->vk->qpool)
            g->vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, sl->vk->qpool, 1);
        g->vk.vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->vk.pipe);
        g->vk.vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_COMPUTE, g->vk.pl, 0, 1, &sl->vk->dset, 0, NULL);
        pc[0] = nrows;
        pc[1] = nv4->vram_mask;
        g->vk.vkCmdPushConstants(cb, g->vk.pl, VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(pc), pc);
        /* a workgroup per row and 64 pixels */
        g->vk.vkCmdDispatch(cb, nrows, (xmax + 63) / 64, 1);
        nv_gpu_vk_barrier(&g->vk, cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_ACCESS_SHADER_WRITE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
                    VK_ACCESS_TRANSFER_READ_BIT);
        if (sl->vk->qpool)
            g->vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, sl->vk->qpool, 2);
        g->vk.vkCmdCopyBuffer(cb, g->vk.b[NV_GPU_B_VRAM].buf, g->vk.b[NV_GPU_B_VSTAGE].buf, ndown, down);
        if (sl->vk->qpool)
            g->vk.vkCmdWriteTimestamp(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, sl->vk->qpool, 3);
        nv_gpu_vk_barrier(&g->vk, cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_HOST_BIT,
                    VK_ACCESS_HOST_READ_BIT);
        g->vk.vkEndCommandBuffer(cb);
        si.commandBufferCount = 1;
        si.pCommandBuffers    = &cb;
        g->vk.vkResetFences(g->vk.dev, 1, &sl->vk->fence);
        if (g->vk.vkQueueSubmit(g->vk.queue, 1, &si, sl->vk->fence) != VK_SUCCESS)
            goto fallback;
    }

    /* in flight: the rows come back when a later batch needs the slot
       or a sync point waits */
    sl->pending  = 1;
    sl->seq      = ++g->seq;
    sl->pndown   = ndown;
    for (int i = 0; i < ndown; i++)
        sl->pdown[i] = down[i];
    sl->p_spans  = b->nspans;
    sl->p_rows   = nrows;
    sl->p_chunks = (xmax + 63) / 64;
    sl->p_px     = 0;
    if (g->prof_left)
        for (int i = 0; i < b->nspans; i++)
            sl->p_px += b->spans[i].x1 - b->spans[i].x0;
    g->next = (g->next + 1) % NV_GPU_SLOTS;
    g->batches++;
    return 1;

fallback:
    g->fallbacks++;
    return 0;
}

#endif
