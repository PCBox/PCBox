/*
 * 86Box    A hypervisor and IBM PC system emulator that specializes in
 *          running old operating systems and software designed for IBM
 *          PC systems and compatibles from 1981 through fairly recent
 *          system designs based on the PCI bus.
 *
 *          This file is part of the 86Box distribution.
 *
 *          NV3 headers for rendering 
 *
 * 
 * 
 * Authors: Connor Hyde, <mario64crashed@gmail.com> I need a better email address ;^)
 *          Marcos Alves, <xeon4d@gmail.com>
 *
 *          Copyright 2024-2026 Connor Hyde
 *          Copyright 2026 Marcos Alves
 */

#pragma once

/* Pixel pipeline */
nv3_color_expanded_t nv3_render_expand_color(uint32_t color, nv3_grobj_t grobj);            // A colour in the object's format, expanded to R10G10B10 + A8
nv3_color_expanded_t nv3_render_expand_surface(uint32_t fmt, uint32_t pixel);               // A surface pixel, expanded
uint32_t nv3_render_expand_mono(uint32_t mono, nv3_grobj_t grobj);                          // Mono data in the object's bit order (pixel n = bit n)
uint32_t nv3_render_to_chroma(nv3_color_expanded_t expanded);                               // A1R10G10B10 for the chroma key register
uint32_t nv3_render_get_palette_index(uint8_t index);                                       // A CLUT entry as RGB888 with 0xFF alpha
void     nv3_render_set_pattern_color(nv3_color_expanded_t pattern_colour, bool use_color1);
uint32_t nv3_render_surface_format(nv3_grobj_t grobj);                                      // BPIXEL of the first destination surface
uint32_t nv3_render_cpp(uint32_t fmt);
uint32_t nv3_render_read_surface(uint32_t buffer, int32_t x, int32_t y, uint32_t cpp);
bool     nv3_render_clip_pass(int32_t x, int32_t y, nv3_grobj_t grobj, bool own_clip);
uint8_t  nv3_render_translate_nvrop(nv3_grobj_t grobj, uint32_t rop);
void     nv3_render_pixel(int32_t x, int32_t y, nv3_color_expanded_t s, nv3_grobj_t grobj);
void     nv3_render_pixel_ex(int32_t x, int32_t y, nv3_color_expanded_t s, nv3_grobj_t grobj, bool own_clip);
void     nv3_render_write_pixel(nv3_coord_16_t position, uint32_t color, nv3_grobj_t grobj);
bool     nv3_render_fill_value(nv3_color_expanded_t s, nv3_grobj_t grobj, uint32_t *val);      // One value for every pixel of a fill?

/* What every pixel of a primitive is drawn with, worked out once per primitive: the surface
   format, the ROP after the patch configuration, the destination surfaces and the clip as one
   rectangle (the clip rectangles, when on, are still tested per pixel) */
typedef struct nv3_render_setup_s {
    uint32_t ctx;
    uint32_t fmt;
    uint32_t cpp;
    uint32_t op;        // patch configuration
    uint8_t  rop;       // translated ROP3
    bool     blend;     // op > 0x17: beta/alpha blend, no ROP
    bool     rop_noop;  // the "D" ROP with write-only ROPs on: pixels are left alone
    bool     chroma;
    uint32_t bufs;      // destination surfaces, bit n = surface n
    int32_t  cx0, cy0, cx1, cy1;
    uint32_t cliprects;
} nv3_render_setup_t;

/* A rectangle of VRAM copied to surfaces that share no bytes with it, every row in VRAM and in
   one piece (nv3_render_copy_run) */
typedef struct nv3_render_copy_t {
    uint32_t src, src_pitch;
    uint32_t dst[4], dst_pitch[4];
    uint32_t ndst;
    uint32_t row_bytes, rows, cpp;
    uint32_t vmask, vor;
} nv3_render_copy_t;

void     nv3_render_copy_run(const nv3_render_copy_t *c);

void     nv3_render_setup(nv3_render_setup_t *st, nv3_grobj_t grobj, bool own_clip);
void     nv3_render_pixel_st(const nv3_render_setup_t *st, int32_t x, int32_t y, nv3_color_expanded_t s);
void     nv3_render_span_st(const nv3_render_setup_t *st, int32_t y, int32_t x0, int32_t x1, nv3_color_expanded_t s);
bool     nv3_render_fill_value_st(const nv3_render_setup_t *st, nv3_color_expanded_t s, uint32_t *val);
bool     nv3_render_rows_direct(const nv3_render_setup_t *st, uint32_t j, int32_t x0, int32_t x1, int32_t y, uint32_t *addr);
void     nv3_render_store_row(uint32_t addr, int32_t n, uint32_t cpp, uint32_t val);
bool     nv3_render_copy_ok(const nv3_render_setup_t *st);
/* What a plain copy stores for a pixel p: (p & *vmask) | *vor, cut to the pixel's width */
void     nv3_render_copy_bits(const nv3_render_setup_t *st, uint32_t *vmask, uint32_t *vor);
bool     nv3_render_image_plain(const nv3_render_setup_t *st, nv3_grobj_t grobj, uint32_t bits, uint32_t *vmask, uint32_t *vor);
void     nv3_render_copy_row(const nv3_render_setup_t *st, uint32_t addr, const uint32_t *src, int32_t n);

/* Primitives */
void nv3_render_fill(int32_t x0, int32_t y0, int32_t x1, int32_t y1, nv3_color_expanded_t c, nv3_grobj_t grobj, bool own_clip);
void nv3_render_rect(nv3_coord_16_t position, nv3_coord_16_t size, uint32_t color, nv3_grobj_t grobj);        // Class 0x07 rectangle
void nv3_render_gdi_rect(nv3_coord_16_t position, nv3_coord_16_t size, uint32_t color, nv3_grobj_t grobj);    // GDI type A
void nv3_render_rect_clipped(nv3_clip_16_t clip, uint32_t color, nv3_grobj_t grobj);                           // GDI type B
void nv3_render_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, nv3_color_expanded_t c, nv3_grobj_t grobj, bool last_pixel);
void nv3_render_triangle(int32_t x0, int32_t y0, int32_t x1, int32_t y1, int32_t x2, int32_t y2, nv3_color_expanded_t c, nv3_grobj_t grobj);

/* Images */
void nv3_render_ifc_start(void);
void nv3_render_blit_image(uint32_t color, nv3_grobj_t grobj);

/* A plain copy image from CPU (class 0x11): once its first word has been drawn, the words that
   follow with nothing in between are stored directly from the MMIO write (nv3_render_blit.c).
   key: the NV_USER address of the image's colour methods, under NV3_IMAGE_FAST_ADDR_MASK. */
#define NV3_IMAGE_FAST_ADDR_MASK 0xFFFF83
#define NV3_IMAGE_FAST_OFF       0xFFFFFFFF

typedef struct nv3_image_fast_s {
    uint32_t key;
    bool     drew;  // a word went through nv3_render_blit_image in this MMIO write
    bool     known; // plain is up to date: nothing but the image's words came since it was worked out
    bool     plain;

    uint32_t count, bits, pmask; // pixels per word, their width and mask
    uint32_t vmask, vor;         // a pixel p is stored as (p & vmask) | vor
    uint32_t cpp, pitch;
    uint32_t total, w;           // pixels sent in all and per row
    uint32_t rx0, rx1, ry0, ry1; // the part of the image that is drawn
    int64_t  base;               // VRAM address of the image's first pixel, drawn or not

    uint32_t rx, ry; // position of the next pixel
    int64_t  row;    // VRAM address of the start of its row

    uint32_t ptr, bin, ptr_mask, slot_mask; // CACHE1's PUT and GET, and the pointer decoded
    uint32_t trap;                          // the trapped address but for the method
} nv3_image_fast_t;

extern nv3_image_fast_t nv3_image_fast;

void nv3_render_image_fast_reset(void);
void nv3_render_image_fast_arm(uint32_t addr);
bool nv3_render_image_word(uint32_t addr, uint32_t val);
void nv3_render_bitmap(uint32_t data, nv3_grobj_t grobj);
void nv3_render_blit_screen2screen(nv3_grobj_t grobj);
void nv3_render_sifc_start(void);
void nv3_render_sifc(uint32_t color, nv3_grobj_t grobj);

/* GDI */
void nv3_render_gdi_transparent_bitmap(bool clip, uint32_t color, uint32_t bitmap_data, nv3_grobj_t grobj);
void nv3_render_gdi_1bpp_bitmap(uint32_t color0, uint32_t color1, uint32_t bitmap_data, nv3_grobj_t grobj); /* GDI Type-E: Clipped 1bpp colour-expanded bitmap */

/* DMA */
bool nv3_perform_dma_m2mf(nv3_grobj_t grobj);
