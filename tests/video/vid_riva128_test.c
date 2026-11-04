#include <assert.h>
#include "../../src/video/vid_riva128.c"

monitor_t monitors[MONITORS_NUM] = {{.mon_changeframecount = 1}};
int monitor_index_global;
static uint8_t host_memory[0x10000];
uint32_t *video_15to32;

void warning(const char *fmt, ...)
{
    (void)fmt;
    assert(!"unexpected renderer warning");
}

uint32_t svga_lookup_lut_ram(svga_t *svga, uint32_t val)
{
    (void)svga;
    return val;
}

uint32_t video_rop_gdi_ternary(uint32_t rop, uint32_t src, uint32_t dst, uint32_t pattern)
{
    (void)rop; (void)dst; (void)pattern;
    assert(!"2D rasterization is outside these tests");
    return src;
}

void dma_bm_write(uint32_t addr, const uint8_t *buffer, uint32_t length, int size)
{
    (void)size;
    assert(addr <= sizeof(host_memory) && length <= sizeof(host_memory) - addr);
    memcpy(host_memory + addr, buffer, length);
}

void pclog(const char *fmt, ...)
{
    (void)fmt;
}

void dma_bm_read(uint32_t addr, uint8_t *buffer, uint32_t transfer_length, int transfer_size)
{
    (void)transfer_size;
    assert(addr <= sizeof(host_memory) && transfer_length <= sizeof(host_memory) - addr);
    memcpy(buffer, host_memory + addr, transfer_length);
}

void pci_irq(uint8_t slot, uint8_t pci_int, int level, int set, uint8_t *irq_state)
{
    (void)slot;
    (void)pci_int;
    (void)level;
    (void)set;
    (void)irq_state;
}

static uint32_t bits(float f)
{
    uint32_t value;
    memcpy(&value, &f, sizeof(value));
    return value;
}

static void vertex(riva128_t *r, unsigned indices, float x, float y)
{
    uint32_t words[8] = {indices, 0xffff0000, bits(x), bits(y), bits(0.5f), bits(1.0f), 0, 0};
    for (unsigned i = 0; i < 8; i++)
        assert(riva128_d3d_method(r, 0x1000 + i * 4, words[i], 0x00108000, 0x100));
}

int main(void)
{
    riva128_t *r = calloc(1, sizeof(*r));
    assert(r);
    r->vram_size = 4 << 20;
    r->vram_mask = r->vram_size - 1;
    r->svga.vram = calloc(1, r->vram_size);
    r->svga.changedvram = calloc(1024, sizeof(*r->svga.changedvram));
    assert(r->svga.vram && r->svga.changedvram);
    r->pgraph.surf_pitch[0] = 64;
    r->pgraph.surf_config = 6;
    r->pgraph.clipw = r->pgraph.cliph = 32;
    assert(riva128_d3d_float(bits(1.0f)) == 1.0);
    assert((int16_t)riva128_d3d_fixed(bits(-2.5f), 4) == -40);
    assert(riva128_d3d_fixed(0x7f800000, 4) == 0x7fff);
    assert(riva128_d3d_compare(2, 1, 2));
    assert(!riva128_d3d_compare(2, 2, 2));
    assert(riva128_d3d_compare(4, 2, 2));
    assert(!riva128_d3d_write_enabled(2, 1, 0));
    assert(riva128_d3d_write_enabled(2, 1, 1));
    assert(riva128_d3d_wrap(-1, 8, 1) == 7);
    assert(riva128_d3d_wrap(8, 8, 2) == 7);
    assert(riva128_d3d_wrap(-1, 8, 3) == 0);
    assert(!riva128_d3d_method(r, 0x308, 0xff100000, 0, 0));
    riva128_ramin_write_l(0x1000, 0, r);
    riva128_ramin_write_l(0x1004, 0x3fffff, r);
    riva128_ramin_write_l(0x1008, 0, r);
    r->svga.vram[0x2000] = r->svga.vram[0x2001] = 0xff;
    assert(riva128_d3d_method(r, 0x304, 0x2000, 0, 0));
    assert(riva128_d3d_method(r, 0x308, 0x00100000, 0, 0));
    assert(riva128_d3d_method(r, 0x314, 0x04081100, 0, 0));
    assert(riva128_d3d_method(r, 0x318, 0x800, 0, 0));
    assert(riva128_d3d_texel(r, 0x1000, 0, 0) == 0xffffffff);
    vertex(r, 5, 2, 2);
    vertex(r, 9, 20, 2);
    assert(((uint16_t *)r->svga.vram)[5 * 32 + 5] == 0);
    vertex(r, 0x95d, 2, 20);
    assert(((uint16_t *)r->svga.vram)[5 * 32 + 5] == 0x7c00);
    assert(((uint16_t *)r->svga.vram)[25 * 32 + 25] == 0);
    assert(r->svga.changedvram[0] == changeframecount);
    assert((r->pgraph.d3d.valid & 0x2220) == 0x2220);
    uint32_t *reg = riva128_pgraph_d3d_register(r, 0x400644);
    assert(reg && *reg == 0x04081100);
    *reg = 0x01081100;
    assert(r->pgraph.d3d.config == 0x01081100);

    /* Reproduce nv3rm's DMA setup: GET includes ADJUST, PT_INST points at
       the first PTE, COUNT is bytes, and PIO PUSH_ACCESS remains disabled.
       The packet crosses two noncontiguous PCI pages. */
    memset(&r->pfifo, 0, sizeof(r->pfifo));
    riva128_ramin_write_l(0x1000, 4, r);
    riva128_ramin_write_l(0x1008, 0x3001, r);
    riva128_ramin_write_l(0x100c, 0x6001, r);
    ((uint32_t *)host_memory)[0x3ffc / 4] = (2u << 18) | (3u << 13) | 0x400;
    ((uint32_t *)host_memory)[0x6000 / 4] = 0x11223344;
    ((uint32_t *)host_memory)[0x6004 / 4] = 0x55667788;
    riva128_pfifo_write(0x3238, 0x1008, r);
    riva128_pfifo_write(0x3230, 0xffffffff, r);
    riva128_pfifo_write(0x3234, 1, r);
    riva128_pfifo_write(0x322c, 2, r);
    riva128_pfifo_write(0x3228, 0xff8, r);
    riva128_pfifo_write(0x3224, 12, r);
    riva128_pfifo_write(0x3218, 0, r);
    riva128_pfifo_write(0x3220, 1, r);
    assert(riva128_pfifo_read(0x3220, r) == 0x11);
    riva128_do_dma_pusher(r);
    assert(riva128_pfifo_read(0x3228, r) == 0x1004);
    assert(riva128_pfifo_read(0x3224, r) == 0);
    assert(riva128_pfifo_read(0x3220, r) == 0);
    assert(riva128_pfifo_gray2normal(r->pfifo.caches[1].put >> 2) == 2);
    assert(r->pfifo.cache1[0].method == 0x400);
    assert(r->pfifo.cache1[0].subchan == 3);
    assert(r->pfifo.cache1[0].param == 0x11223344);
    assert(r->pfifo.cache1[1].method == 0x404);
    assert(r->pfifo.cache1[1].param == 0x55667788);
    assert((riva128_pfifo_read(0x3218, r) & 0x1ffc) == 0x408);
    assert(!(riva128_pfifo_read(0x3218, r) & 0x1ffc0000));

    /* Backpressure and explicit disable preserve both transfer bytes and
       packet state; resuming cannot drop or duplicate a parameter. */
    memset(&r->pfifo, 0, sizeof(r->pfifo));
    riva128_ramin_write_l(0x1000, 0, r);
    ((uint32_t *)host_memory)[0x3000 / 4] = (32u << 18) | 0x400;
    for (unsigned i = 0; i < 32; i++)
        ((uint32_t *)host_memory)[0x3004 / 4 + i] = i;
    riva128_pfifo_write(0x3238, 0x1008, r);
    riva128_pfifo_write(0x3230, 0xffffffff, r);
    riva128_pfifo_write(0x322c, 2, r);
    riva128_pfifo_write(0x3224, 132, r);
    riva128_pfifo_write(0x3220, 1, r);
    riva128_do_dma_pusher(r);
    assert(riva128_pfifo_free(r) == 0);
    assert(riva128_pfifo_read(0x3228, r) == 128);
    assert(riva128_pfifo_read(0x3224, r) == 4);
    assert(riva128_pfifo_read(0x3218, r) == ((1u << 18) | 0x47c));
    riva128_pfifo_write(0x3220, 0, r);
    r->pfifo.caches[1].get = r->pfifo.caches[1].put;
    riva128_do_dma_pusher(r);
    assert(riva128_pfifo_read(0x3224, r) == 4);
    riva128_pfifo_write(0x3220, 1, r);
    riva128_do_dma_pusher(r);
    assert(riva128_pfifo_read(0x3224, r) == 0);
    assert(riva128_pfifo_read(0x3220, r) == 0);
    assert(r->pfifo.cache1[riva128_pfifo_normal2gray(31)].method == 0x47c);
    assert(r->pfifo.cache1[riva128_pfifo_normal2gray(31)].param == 31);

    /* nv3rm's leading padding is ignored before the first packet. */
    memset(&r->pfifo, 0, sizeof(r->pfifo));
    r->pfifo.caches[1].dma_target = 2;
    r->pfifo.caches[1].dma_tlb_pte = 0x3001;
    r->pfifo.caches[1].dma_ctrl = 1;
    r->pfifo.caches[1].dma_length = 16;
    ((uint32_t *)host_memory)[0x3000 / 4] = 0xffffffff;
    ((uint32_t *)host_memory)[0x3004 / 4] = 0;
    ((uint32_t *)host_memory)[0x3008 / 4] = (1u << 18) | 0x500;
    ((uint32_t *)host_memory)[0x300c / 4] = 0xa5a5a5a5;
    riva128_do_dma_pusher(r);
    assert(r->pfifo.intr == 0);
    assert(r->pfifo.cache1[0].method == 0x500);
    assert(r->pfifo.cache1[0].param == 0xa5a5a5a5);
    assert(r->pfifo.caches[1].dma_ctrl == 0);

    /* Malformed commands stop the pusher and report DMA_PUSHER. */
    memset(&r->pfifo, 0, sizeof(r->pfifo));
    r->pfifo.caches[1].dma_target = 2;
    r->pfifo.caches[1].dma_tlb_pte = 0x3001;
    r->pfifo.caches[1].dma_ctrl = 1;
    r->pfifo.caches[1].dma_length = 4;
    ((uint32_t *)host_memory)[0x3000 / 4] = 0x80000000;
    riva128_do_dma_pusher(r);
    assert(r->pfifo.intr & (1u << 12));
    assert(r->pfifo.caches[1].dma_state & (1u << 30));
    assert(r->pfifo.caches[1].dma_ctrl == 0);

    /* An invalid PTE must fault without consuming the inaccessible word. */
    memset(&r->pfifo, 0, sizeof(r->pfifo));
    r->pfifo.caches[1].dma_ctrl = 1;
    r->pfifo.caches[1].dma_length = 4;
    riva128_do_dma_pusher(r);
    assert(r->pfifo.intr & (1u << 16));
    assert(r->pfifo.caches[1].dma_addr == 0);
    assert(r->pfifo.caches[1].dma_length == 4);
    assert(r->pfifo.caches[1].dma_ctrl == 0);

    /* nv3rm.vxd reads a CACHE1 command slot as
       mmio[CACHE1_RAM + (GET >> 2) * 8], where GET is the raw (Gray-coded)
       pointer.  The MMIO RAM window must therefore be indexed by the raw
       slot number, so a write and a read of the same address agree. */
    memset(&r->pfifo, 0, sizeof(r->pfifo));
    for (unsigned slot = 0; slot < 32; slot++) {
        riva128_pfifo_write(0x3300 + slot * 8, (slot << 13) | 0x304, r);
        riva128_pfifo_write(0x3304 + slot * 8, 0xa0000000u | slot, r);
        assert(riva128_pfifo_read(0x3300 + slot * 8, r)
                == ((slot << 13) | 0x304));
        assert(riva128_pfifo_read(0x3304 + slot * 8, r) == (0xa0000000u | slot));
    }

    /* nv3rm.vxd saves and restores CACHE0 PUSH0 (0x3000) and PUSH1 (0x3004)
       around its software command submission, so both must read back. */
    riva128_pfifo_write(0x3000, 1, r);
    riva128_pfifo_write(0x3004, 0x42, r);
    assert(riva128_pfifo_read(0x3000, r) == 1);
    assert(riva128_pfifo_read(0x3004, r) == 0x42);

    /* The ROP register is part of the PGRAPH context the RM saves/restores. */
    riva128_pgraph_write(0x400624, 0xa5, r);
    assert(riva128_pgraph_read(0x400624, r) == 0xa5);

    /* A software surface method traps once, then holds the following
       format method until the RM acknowledges and re-enables PGRAPH. */
    memset(&r->pgraph, 0, sizeof(r->pgraph));
    memset(&r->pfifo, 0, sizeof(r->pfifo));
    riva128_ramin_write_l(0x1800, 0, r);
    r->pgraph.fifo_access = 1;
    r->pgraph.surf_config = 7;
    r->pfifo.caches[1].pull_ctrl = 1;
    r->pfifo.caches[1].ctx[0] = 0x00dc0180;
    r->pfifo.caches[1].put = 12; /* Gray pointer after two commands. */
    r->pfifo.cache1[0].method = 0x304;
    r->pfifo.cache1[0].param = 0xdd001010;
    r->pfifo.cache1[1].method = 0x300;
    r->pfifo.cache1[1].param = 0x01000000;
    riva128_do_cache1_puller(r);
    assert(r->pfifo.caches[1].get == 4);
    assert(r->pgraph.intr_0 & 1);
    assert(!r->pgraph.fifo_access);
    assert(r->pgraph.trapped_data == 0xdd001010);
    riva128_do_cache1_puller(r);
    assert(r->pfifo.caches[1].get == 4);
    assert(r->pgraph.surf_config == 7);
    riva128_pgraph_write(0x400100, 1, r);
    riva128_do_cache1_puller(r);
    assert(r->pfifo.caches[1].get == 4);
    riva128_pgraph_write(0x4006a4, 1, r);
    riva128_do_cache1_puller(r);
    assert(r->pfifo.caches[1].get == 12);
    assert(r->pgraph.surf_config == 6);

    /* The RM decodes the incoming channel and subchannel from the trap
       registers on a context-switch interrupt. Do not execute the method
       until it has acknowledged the switch. */
    memset(&r->pgraph, 0, sizeof(r->pgraph));
    r->pgraph.fifo_access = 1;
    r->pgraph.surf_config = 7;
    r->pgraph.trapped_addr = 0x001c0304; /* stale outgoing trap */
    assert(!riva128_pgraph_command_submit(0x300, 1, 3, 0x01000000,
                                         0x00dc0180, r));
    assert(r->pgraph.intr_0 == (1 << 4));
    assert(r->pgraph.trapped_addr == 0x011c6300);
    assert(r->pgraph.trapped_data == 0x01000000);
    assert(r->pgraph.trapped_instance == 0x180);
    assert(r->pgraph.surf_config == 7);
    assert(!riva128_pgraph_command_submit(0x300, 2, 0, 1,
                                         0x00dc0180, r));
    assert(r->pgraph.trapped_addr == 0x011c6300);
    riva128_pgraph_write(0x400100, 1 << 4, r);
    assert(riva128_pgraph_command_submit(0x300, 1, 3, 0x01000000,
                                        0x00dc0180, r));
    assert(r->pgraph.surf_config == 6);
    assert(!r->pgraph.intr_0);
    assert(riva128_pgraph_command_submit(0x304, 1, 3, 0xdd001010,
                                        0x00dc0180, r));
    assert(r->pgraph.trapped_addr == 0x011c6304);

    free(r->svga.changedvram);
    free(r->svga.vram);
    free(r);
    puts("NV3 source-level method and framebuffer tests passed");
    return 0;
}
