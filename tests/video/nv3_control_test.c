/* NV3 interrupt delivery, PIO runout, and GDI B/C regression tests.
 * The real control and primitive sources run against a PCI line and pixel sink.
 * No guest, Vulkan device or ROM is required. */
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

#define CHECK(c)                                                    \
    do {                                                            \
        if (!(c)) {                                                 \
            fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #c); \
            exit(1);                                                \
        }                                                           \
    } while (0)
static nv3_t    card;
nv3_t          *nv3 = &card;
static uint8_t  vram[1 << 20];
static uint16_t pixels[16][128];
const char     *nv3_class_names[32];
void
nv_log(const char *fmt, ...)
{
}
void
nv_log_verbose_only(const char *fmt, ...)
{
}
void
nv_warning(const char *fmt, ...)
{
}
void
pci_irq(uint8_t slot, uint8_t pin, int level, int set, uint8_t *state)
{
    *state = set;
}
void
timer_on_auto(pc_timer_t *timer, double period)
{
    CHECK(0);
}
void
nv3_d3d_sync(int cause)
{
}
void
nv3_d3d_touch_rect(nv3_grobj_t obj, int32_t y0, int32_t y1, int32_t x1, int cause)
{
}
bool
nv3_d3d_fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1, nv3_color_expanded_t c, nv3_grobj_t obj, bool own)
{
    return false;
}
nv3_color_expanded_t
nv3_render_expand_color(uint32_t c, nv3_grobj_t obj)
{
    return (nv3_color_expanded_t) { .r = c };
}
uint32_t
nv3_render_expand_mono(uint32_t data, nv3_grobj_t obj)
{
    return data;
}
void
nv3_render_pixel_ex(int32_t x, int32_t y, nv3_color_expanded_t c, nv3_grobj_t obj, bool own)
{
    CHECK(own);
    CHECK(x >= 0 && x < 128 && y >= 0 && y < 16);
    pixels[y][x] = c.r;
}
/* Other classes are outside this fixture. Unexpected dispatch fails the test. */
#define OTHER_METHOD(name) \
    void name(uint32_t p, uint32_t m, nv3_ramin_context_t c, nv3_grobj_t g) { CHECK(0); }
OTHER_METHOD(nv3_generic_method)
OTHER_METHOD(nv3_notify_if_needed)
OTHER_METHOD(nv3_class_001_method)
OTHER_METHOD(nv3_class_002_method)
OTHER_METHOD(nv3_class_003_method)
OTHER_METHOD(nv3_class_004_method)
OTHER_METHOD(nv3_class_005_method)
OTHER_METHOD(nv3_class_006_method)
OTHER_METHOD(nv3_class_007_method)
OTHER_METHOD(nv3_class_008_method)
OTHER_METHOD(nv3_class_009_method)
OTHER_METHOD(nv3_class_00a_method)
OTHER_METHOD(nv3_class_00b_method)
OTHER_METHOD(nv3_class_00d_method)
OTHER_METHOD(nv3_class_00e_method)
OTHER_METHOD(nv3_class_010_method)
OTHER_METHOD(nv3_class_011_method)
OTHER_METHOD(nv3_class_012_method)
OTHER_METHOD(nv3_class_014_method)
OTHER_METHOD(nv3_class_015_method)
OTHER_METHOD(nv3_class_017_method)
OTHER_METHOD(nv3_class_018_method)
OTHER_METHOD(nv3_class_01c_method)

void
dma_bm_read(uint32_t addr, uint8_t *data, uint32_t size, int transfer)
{
    CHECK(0);
}
void
nv3_d3d_vblank(uint32_t lo, uint32_t hi)
{
    CHECK(0);
}
void
nv3_pramdac_checksum_frame(svga_t *svga)
{
    CHECK(0);
}
void
nv3_pvideo_vblank(void)
{
    CHECK(0);
}
void
nv3_render_pixel(int32_t x, int32_t y, nv3_color_expanded_t c, nv3_grobj_t obj)
{
    CHECK(0);
}

static void
reset(void)
{
    memset(&card, 0, sizeof(card));
    memset(vram, 0, sizeof(vram));
    memset(pixels, 0, sizeof(pixels));
    card.nvbase.svga.vram      = vram;
    card.nvbase.svga.vram_max  = sizeof(vram);
    card.nvbase.svga.vram_mask = sizeof(vram) - 1;
    card.pmc.enable            = 1u << NV3_PMC_ENABLE_PGRAPH;
    card.pfifo.ramro_location  = 0x2000;
    nv3_pmc_init();
}
static void
interrupts(void)
{
    reset();
    CHECK(nv3_pmc_read(NV3_PMC_INTR_EN) == 0);
    CHECK(nv3_pmc_read(NV3_PMC_INTR_READ) == 1);
    card.pme.intr = NV3_PME_INTR_NOTIFY;
    nv3_pme_write(NV3_PME_INTR_EN, ~0u);
    CHECK(nv3_pme_read(NV3_PME_INTR_EN) == 0x11);
    CHECK(nv3_pmc_read(NV3_PMC_INTR) == (1u << 4));
    CHECK(!card.nvbase.pci_irq_state); /* status survives global masking */
    nv3_pmc_write(NV3_PMC_INTR_EN, 1);
    CHECK(card.nvbase.pci_irq_state);
    CHECK(nv3_pmc_read(NV3_PMC_INTR_READ) == 0);
    card.nvbase.pci_irq_state = 0; /* motherboard route may be disconnected */
    CHECK(nv3_pmc_read(NV3_PMC_INTR_READ) == 0);
    nv3_pme_write(NV3_PME_INTR_EN, 0);
    CHECK(!card.nvbase.pci_irq_state);
    CHECK(card.pme.intr == 1);
    nv3_pme_write(NV3_PME_INTR_EN, 1);
    CHECK(card.nvbase.pci_irq_state);
    nv3_pme_write(NV3_PME_INTR, 1);
    CHECK(!card.nvbase.pci_irq_state);
    card.pme.intr = NV3_PME_INTR_VMI;
    nv3_pme_write(NV3_PME_INTR_EN, 0x11);
    CHECK(nv3_pmc_read(NV3_PMC_INTR) == 0); /* VMI unimplemented on NV3 */

    for (unsigned enables = 0; enables < 4; enables++) {
        for (unsigned sources = 0; sources < 4; sources++) {
            card.pfifo.intr = card.pfifo.intr_en = sources & 1;
            nv3_pmc_write(NV3_PMC_INTR_EN, enables);
            nv3_pmc_write(NV3_PMC_INTR, (sources & 2) ? 0xFFFFFFFF : 0x7FFFFFFF);
            CHECK(card.nvbase.pci_irq_state == !!(enables & sources));
            CHECK(nv3_pmc_read(NV3_PMC_INTR) == ((sources & 1 ? 0x100u : 0) | (sources & 2 ? 0x80000000u : 0)));
        }
    }
    card.pfifo.intr = 0;
    nv3_pmc_write(NV3_PMC_INTR, 0);
    nv3_pmc_write(NV3_PMC_INTR_EN, 1);
    for (unsigned bit = 0; bit <= 16; bit += 4) {
        card.pgraph.intr_dma = 1u << bit;
        nv3_pgraph_write(NV3_PGRAPH_DMA_INTR_EN_0, 1u << bit);
        CHECK(nv3_pgraph_read(NV3_PGRAPH_DMA_INTR_0) == (1u << bit));
        CHECK(nv3_pmc_read(NV3_PMC_INTR) == (1u << 13));
        CHECK(card.nvbase.pci_irq_state);
        nv3_pgraph_write(NV3_PGRAPH_DMA_INTR_EN_0, 0);
        CHECK(!card.nvbase.pci_irq_state);
        nv3_pgraph_write(NV3_PGRAPH_DMA_INTR_EN_0, ~0u);
        CHECK(nv3_pgraph_read(NV3_PGRAPH_DMA_INTR_EN_0) == 0x11111);
        CHECK(card.nvbase.pci_irq_state);
        nv3_pgraph_write(NV3_PGRAPH_DMA_INTR_0, 1u << bit);
        CHECK(!card.nvbase.pci_irq_state);
    }
}
static void
runout(void)
{
    reset();
    card.pfifo.intr_en = 0x110;
    nv3_pmc_write(NV3_PMC_INTR_EN, 1);
    uint32_t addr = NV3_USER_START | (127u << 16) | (7u << 13) | 0x400;
    CHECK(nv3_user_read((addr & ~0x1FFFu) | 0x10) == 124);
    CHECK(nv3_user_read((addr & ~0x1FFFu) | 0x14) == 0);
    CHECK(card.pfifo.runout_put == 0);
    CHECK(nv3_user_read(addr) == 0);
    CHECK(card.pfifo.runout_put == 8);
    CHECK(nv3_ramin_read32(0x2000, NULL) == ((addr & 0x7FFFFC) | (1u << 23)));
    CHECK(card.nvbase.pci_irq_state);
    card.pfifo.runout_get = card.pfifo.runout_put;
    nv3_pfifo_cache1_push((addr & ~0x1FFFu) | 0x10, 0xABCDEF);
    CHECK(nv3_ramin_read32(0x2008, NULL) == ((addr & ~0x1FFFu & 0x7FFFFC) | 0x10));
    CHECK(nv3_ramin_read32(0x200C, NULL) == 0xABCDEF);
    /* Disabled cache plus reserved offset must produce reason 5, never 1|5. */
    nv3_pfifo_cache1_push((addr & ~0x1FFFu) | 0x20, 0x1234);
    CHECK((nv3_ramin_read32(0x2010, NULL) >> 28) == 5);
    /* Otherwise valid methods distinguish unavailable, runout, and full caches. */
    reset();
    nv3_pfifo_cache1_push(NV3_USER_START | 0x400, 1);
    CHECK((nv3_ramin_read32(0x2000, NULL) >> 28) == 1);
    card.pfifo.cache1_settings.push0 = 1;
    nv3_pfifo_cache1_push(NV3_USER_START | 0x400, 2);
    CHECK((nv3_ramin_read32(0x2008, NULL) >> 28) == 2);
    card.pfifo.runout_get = card.pfifo.runout_put;
    for (unsigned i = 0; i < 31; i++)
        nv3_pfifo_cache1_push(NV3_USER_START | 0x400, i);
    CHECK(nv3_pfifo_cache1_num_free_spaces() == 0);
    nv3_pfifo_cache1_push(NV3_USER_START | 0x400, 3);
    CHECK((nv3_ramin_read32(0x2010, NULL) >> 28) == 3);
    /* A missing object belongs to the puller: CACHE_ERROR, not another RAMRO entry. */
    uint32_t put                     = card.pfifo.runout_put;
    card.pfifo.ramht_size            = 0x1000;
    card.pfifo.cache1_settings.pull0 = 1;
    CHECK(!nv3_ramin_find_object(0xDEADBEEF, 1, 0, 0));
    CHECK(card.pfifo.runout_put == put);
    CHECK(card.pfifo.intr & 1);
    CHECK(!(card.pfifo.cache1_settings.pull0 & 1));
    /* Every ring size stops one entry short, preserving unread data. */
    for (unsigned large = 0; large < 2; large++) {
        card.pfifo.ramro_config = large << NV3_PFIFO_CONFIG_RAMRO_SIZE;
        card.pfifo.runout_get = card.pfifo.runout_put = 0;
        card.pfifo.intr                               = 0;
        unsigned entries                              = large ? 1024 : 64;
        for (unsigned i = 0; i < entries - 1; i++)
            nv3_pfifo_runout(addr, i, false, nv3_runout_reason_no_cache_available);
        CHECK(card.pfifo.runout_put == (entries - 1) * 8);
        nv3_pfifo_runout(addr, 0xDEADBEEF, false, nv3_runout_reason_no_cache_available);
        CHECK(card.pfifo.runout_put == (entries - 1) * 8);
        CHECK(card.pfifo.intr & (1u << NV3_PFIFO_INTR_RUNOUT_OVERFLOW));
        CHECK(nv3_ramin_read32(0x2004, NULL) == 0);
        /* Make room, then wrap PUT while GET stays nonzero. */
        card.pfifo.runout_get = 8;
        nv3_pfifo_runout(addr, 0xCAFE, false, nv3_runout_reason_no_cache_available);
        CHECK(card.pfifo.runout_put == 0);
        CHECK(nv3_ramin_read32(0x2000 + (entries - 1) * 8 + 4, NULL) == 0xCAFE);
    }
}
static void
gdi(uint32_t method, uint32_t param)
{
    nv3_class_00c_method(param, method, (nv3_ramin_context_t) { 0 }, (nv3_grobj_t) { 0 });
}
static void
text(void)
{
    reset();
    /* Type B: intersect signed bounds, excluding right/bottom. Last array slot. */
    gdi(NV3_W95TXT_B_CLIP_TOPLEFT, 0x00010001);
    gdi(NV3_W95TXT_B_CLIP_BOTTOMRIGHT, 0x00040005);
    gdi(NV3_W95TXT_B_COLOR, 9);
    gdi(0x9F8, 0xFFFFFFFF);
    gdi(0x9FC, 0x00030004);
    for (unsigned y = 0; y < 6; y++)
        for (unsigned x = 0; x < 8; x++)
            CHECK(pixels[y][x] == ((x >= 1 && x < 4 && y >= 1 && y < 3) ? 9 : 0));
    memset(pixels, 0, sizeof(pixels));
    /* Type C: consecutive words cross an unpadded 35-pixel row. */
    gdi(NV3_W95TXT_C_CLIP_TOPLEFT, 0);
    gdi(NV3_W95TXT_C_CLIP_BOTTOMRIGHT, 0x00040060);
    gdi(NV3_W95TXT_C_CLIP_COLOR, 7);
    gdi(NV3_W95TXT_C_CLIP_SIZE, 0x00020023);
    gdi(NV3_W95TXT_C_CLIP_POSITION, 0x00010002);
    gdi(0xC00, 0x80000001);
    gdi(0xC04, 0x00000009);
    CHECK(card.pgraph.win95_gdi_text.bitmap_c[0] == 0x80000001);
    CHECK(card.pgraph.win95_gdi_text.bitmap_c[1] == 9);
    CHECK(pixels[1][2] == 7 && pixels[1][33] == 7 && pixels[1][34] == 7 && pixels[2][2] == 7);
    CHECK(pixels[1][3] == 0 && pixels[2][3] == 0);
    gdi(0xDFC, 0xFFFFFFFF);
    CHECK(card.pgraph.win95_gdi_text.bitmap_c[127] == 0xFFFFFFFF);
    CHECK(card.pgraph.win95_gdi_text_bit_count == 70);
    CHECK(pixels[2][36] == 7 && pixels[2][37] == 0);
}
int
main(void)
{
    interrupts();
    runout();
    text();
    puts("NV3 control and GDI tests passed");
    return 0;
}
