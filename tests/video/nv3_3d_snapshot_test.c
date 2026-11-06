/* Verify sealed texture state reuse against both cached and uncached staging. */
#include "../../src/video/nv/nv3/render/nv3_render_d3d_gpu.c"

#define CHECK(cond) do { if (!(cond)) { fprintf(stderr, "line %d: %s\n", __LINE__, #cond); exit(1); } } while (0)

nv3_t *nv3;
int nv3_gpu_inflight;
static uint8_t texels[4096];
void pclog(const char *fmt, ...) {}

const uint8_t *nv3_d3d_tex_ptr(const nv3_d3d_dstate_t *s, uint32_t byte, uint32_t len, uint8_t *tmp)
{
    CHECK(byte + len <= sizeof(texels));
    return texels + byte;
}

int main(void)
{
    nv3_gpu_t *g = calloc(1, sizeof(*g));
    nv3_d3d_batch_t *b = calloc(1, sizeof(*b));
    uint8_t stage[4096];
    b->states[0].textured = 1;
    b->states[0].tex_size = sizeof(texels);
    b->states[0].tex_pages[0] = 0x100000;
    for (unsigned i = 0; i < sizeof(texels); i++)
        texels[i] = i * 37;
    memcpy(stage, texels, sizeof(stage));
    g->vk.b[NV_GPU_B_TSTAGE].map = stage;
    g->ncache = 1;
    g->cache[0].base = 0;
    for (int cached = 0; cached <= 1; cached++) {
        g->vk.b[NV_GPU_B_TSTAGE].cached = cached;
        g->cache[0].hash = gpu_tex_hash(g, &b->states[0]);
        CHECK(nv3_gpu_state_reusable(g, b, 0));
        texels[4095] ^= 1;
        CHECK(!nv3_gpu_state_reusable(g, b, 0));
        texels[4095] ^= 1;
        CHECK(nv3_gpu_state_reusable(g, b, 0));
        texels[0] ^= 1;
        CHECK(!nv3_gpu_state_reusable(g, b, 0));
        texels[0] ^= 1;
        CHECK(g->nflush_tex == 0 && g->ntcopy == 0);
    }
    b->states[0].tex_size = 0;
    CHECK(nv3_gpu_state_reusable(g, b, 0));
    free(b);
    free(g);
    puts("NV3 sealed texture snapshot tests passed");
    return 0;
}

monitor_t monitors[MONITORS_NUM];
int monitor_index_global;
char usr_path[1024];
void nv3_render_copy_run(const nv3_render_copy_t *c) { abort(); }
void fatal(const char *fmt, ...) { abort(); }
FILE *plat_fopen(const char *path, const char *mode) { abort(); }
void path_append_filename(char *dest, const char *s1, const char *s2) { abort(); }
