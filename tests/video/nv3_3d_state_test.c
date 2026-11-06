/* State reuse across texture snapshots, and bounded texture page mapping. */
#include "../../src/video/nv/nv3/render/nv3_render_d3d.c"
#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "line %d: %s\n", __LINE__, #cond); exit(1); } } while (0)

static nv3_t card;
nv3_t *nv3 = &card;
static int reusable, resolves;
static uint32_t mapped_pages, adjust;

int nv3_gpu_state_reusable(struct nv3_gpu_s *g, const nv3_d3d_batch_t *b, int state)
{
    return reusable;
}

uint32_t nv3_dma_map_pages(uint32_t inst, uint32_t off, uint32_t *addr, uint32_t pages, uint32_t *lin)
{
    mapped_pages = pages;
    *lin = off + adjust;
    for (uint32_t i = 0; i < pages; i++)
        addr[i] = 0x100000 + i * 4096;
    return NV3_DMA_TARGET_NODE_AGP;
}

int agpgart_resolve(uint32_t addr, uint32_t *ram_addr)
{
    resolves++;
    *ram_addr = addr + 0x100000;
    return 1;
}

int main(void)
{
    nv3_d3d_batch_t *b = calloc(1, sizeof(*b));
    nv3_d3d_dstate_t s = {0};
    b->nstates = 2;
    b->states[1].config = 1;
    CHECK(nv3_d3d_find_state(b, &s) == 0); /* A, B, A */
    s.textured = 1;
    b->states[0] = s;
    b->nsealed = 2;
    CHECK(nv3_d3d_find_state(b, &s) == -1);
    b->gpu = (struct nv3_gpu_s *) b;
    reusable = 1;
    CHECK(nv3_d3d_find_state(b, &s) == 0);
    reusable = 0; /* the driver replaced the system-memory texture */
    CHECK(nv3_d3d_find_state(b, &s) == -1);
    b->nsealed = 0;
    CHECK(nv3_d3d_find_state(b, &s) == 0);
    b->nsealed = 2;
    s.tex_vram = b->states[0].tex_vram = 1;
    CHECK(nv3_d3d_find_state(b, &s) == 0);
    s.config = 7;
    CHECK(nv3_d3d_find_state(b, &s) == -1);
    s.config = 0;
    s.tex_size = 32;
    s.tex_pages[0] = 0x1000;
    b->states[0] = s;
    b->parallel = 1;
    b->nregions = 1;
    card.nvbase.svga.vram_mask = 0xfffff;
    b->regions[0].wlo = 0x1000;
    b->regions[0].whi = 0x101f;
    CHECK(nv3_d3d_find_state(b, &s) == -1); /* intervening render to texture */
    b->regions[0].wlo = 0x2000;
    b->regions[0].whi = 0x201f;
    CHECK(nv3_d3d_find_state(b, &s) == 0);
    free(b);

    const uint32_t sizes[] = {32, 4096, 4097, 8192, 262144};
    const uint32_t offsets[] = {0, 1, 4095};
    for (unsigned i = 0; i < sizeof(sizes)/sizeof(sizes[0]); i++)
        for (unsigned j = 0; j < sizeof(offsets)/sizeof(offsets[0]); j++) {
            memset(&card, 0xa5, sizeof(card));
            card.pgraph.prim.d3d.tex_mapped = false;
            card.pgraph.prim.d3d.tex_offset = 0;
            adjust = offsets[j];
            resolves = 0;
            nv3_d3d_map_texture(sizes[i]);
            uint32_t pages = MIN(D3_TEX_PAGES, (sizes[i] + adjust + 4095) >> 12);
            CHECK(mapped_pages == pages && resolves == pages);
            for (uint32_t p = 0; p < D3_TEX_PAGES; p++) {
                CHECK(card.pgraph.prim.d3d.tex_pages[p] == (p < pages ? 0x100000 + p * 4096 : 0));
                CHECK(card.pgraph.prim.d3d.tex_rpages[p] == (p < pages ? 0x200000 + p * 4096 : 0));
            }
            nv3_d3d_map_texture(sizes[i]);
            CHECK(resolves == pages);
        }
    puts("NV3 state reuse and texture mapping tests passed");
    return 0;
}

/* Unused emulator services fail loudly if a focused test reaches them. */
void pclog(const char *fmt, ...) {}
void nv_warning(const char *fmt, ...) { abort(); }
void nv3_pgraph_interrupt_invalid(uint32_t num) { abort(); }
void nv3_render_copy_run(const nv3_render_copy_t *c) { abort(); }
uint32_t nv3_render_surface_format(nv3_grobj_t g) { abort(); }
uint32_t nv3_render_cpp(uint32_t f) { abort(); }
bool nv3_render_fill_value(nv3_color_expanded_t c, nv3_grobj_t g, uint32_t *v) { abort(); }
nv3_color_expanded_t nv3_render_expand_color(uint32_t c, nv3_grobj_t g) { abort(); }
struct nv3_gpu_s *nv3_gpu_init(void) { abort(); }
void nv3_gpu_wait(struct nv3_gpu_s *g) { abort(); }
void nv3_gpu_close(struct nv3_gpu_s *g) { abort(); }
int nv3_gpu_shade(struct nv3_gpu_s *g, const nv3_d3d_batch_t *b) { abort(); }
int nv3_gpu_seal(struct nv3_gpu_s *g, nv3_d3d_batch_t *b) { abort(); }
int nv3_gpu_pending_overlaps(struct nv3_gpu_s *g, uint32_t lo, uint32_t hi) { abort(); }
int nv3_gpu_copy_later(struct nv3_gpu_s *g, const nv3_render_copy_t *c) { abort(); }
void nv3_gpu_counts(struct nv3_gpu_s *g, uint64_t *b, uint64_t *f) { abort(); }
thread_t *thread_create_named(void (*fn)(void *), void *p, const char *name) { abort(); }
int thread_wait(thread_t *t) { abort(); }
event_t *thread_create_event(void) { abort(); }
void thread_set_event(event_t *e) { abort(); }
void thread_reset_event(event_t *e) { abort(); }
int thread_wait_event(event_t *e, int t) { abort(); }
void thread_destroy_event(event_t *e) { abort(); }
uint8_t mem_readb_phys(uint32_t a) { abort(); }
uint32_t mem_size;
uint8_t *ram;
monitor_t monitors[MONITORS_NUM];
int monitor_index_global;
