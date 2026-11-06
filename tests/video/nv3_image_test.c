/* Image uploads must preserve ordering and never arm direct writes over queued pixels. */
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <86box/86box.h>
#include <86box/device.h>
#include <86box/mem.h>
#include <86box/pci.h>
#include <86box/rom.h>
#include <86box/video.h>
#include <86box/nv/vid_nv.h>
#include <86box/nv/vid_nv3.h>
#define CHECK(x)                                            \
    do {                                                    \
        if (!(x)) {                                         \
            fprintf(stderr, "line %d: %s\n", __LINE__, #x); \
            exit(1);                                        \
        }                                                   \
    } while (0)
static nv3_t    card;
nv3_t          *nv3 = &card;
static uint8_t  vram[8192];
static uint8_t  changed[2];
monitor_t       monitors[MONITORS_NUM];
int             monitor_index_global;
static unsigned fills, touches;
static bool     queue;
static int32_t  last_y1;
bool
nv3_d3d_fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1, nv3_color_expanded_t c, nv3_grobj_t obj, bool own)
{
    fills++;
    return queue;
}
void
nv3_d3d_touch_rect(nv3_grobj_t obj, int32_t y0, int32_t y1, int32_t x1, int cause)
{
    touches++;
    last_y1 = y1;
}
int
main(void)
{
    card.nvbase.svga.vram        = vram;
    card.nvbase.svga.vram_mask   = sizeof(vram) - 1;
    card.nvbase.svga.changedvram = changed;
    card.pgraph.bpixel[0]        = bpixel_fmt_16bit;
    card.pgraph.bpitch[0]        = 16;
    card.pgraph.rop              = 0xCC;
    card.pgraph.image.size.x = card.pgraph.image.size_in.x = 4;
    card.pgraph.image.size.y = card.pgraph.image.size_in.y = 2;
    nv3_grobj_t obj                                        = { .grobj_0 = nv3_pgraph_pixel_format_r5g5b5 | (1u << NV3_PGRAPH_CTX_SWITCH_DST_BUFFER0_ENABLED) | (0x17u << NV3_PGRAPH_CTX_SWITCH_PATCH_CONFIG) };
    queue                                                  = true;
    nv3_render_image_fast_reset();
    nv3_render_blit_image(0x12345678, obj);
    CHECK(fills == 2 && touches == 0);
    CHECK(!nv3_image_fast.drew && nv3_image_fast.plain);
    CHECK(*(uint32_t *) vram == 0);
    queue = false;
    nv3_render_blit_image(0x11112222, obj);
    CHECK(touches >= 3 && last_y1 == 1);
    CHECK(nv3_image_fast.drew);
    CHECK(*(uint16_t *) (vram + 4) == 0x2222);
    CHECK(*(uint16_t *) (vram + 6) == 0x1111);
    /* Keyed pixels leave the framebuffer alone without draining preceding GPU work. */
    card.pgraph.image_pixel_count = 0;
    card.pgraph.chroma_key        = 0x40000000;
    obj.grobj_0 |= 1u << NV3_PGRAPH_CTX_SWITCH_CHROMA_KEY;
    nv3_render_image_fast_reset();
    fills = touches = 0;
    nv3_render_blit_image(0, obj);
    CHECK(nv3_image_fast.plain && nv3_image_fast.chroma);
    CHECK(fills == 0 && touches == 0 && !nv3_image_fast.drew);

    /* Even a first word clipped off the left must protect later direct-write rows. */
    card.pgraph.image_pixel_count = 0;
    card.pgraph.image.point.x     = (uint16_t) -2;
    obj.grobj_0 &= ~(1u << NV3_PGRAPH_CTX_SWITCH_CHROMA_KEY);
    nv3_render_image_fast_reset();
    fills = touches = 0;
    nv3_render_blit_image(0x12345678, obj);
    CHECK(touches > 0 && last_y1 == 1);
    CHECK(nv3_image_fast.drew);
    puts("NV3 image ordering tests passed");
    return 0;
}

void
nv3_d3d_sync(int cause)
{
    CHECK(0);
}
void
nv3_d3d_copy(const nv3_render_copy_t *c, int cause)
{
    CHECK(0);
}
void
nv3_pfifo_cache1_decode(uint32_t ptr, uint32_t *bin, uint32_t *mask, uint32_t *slot)
{
    CHECK(0);
}
