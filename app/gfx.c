/* SPDX-License-Identifier: MPL-2.0
 * Copyright (C) 2026 Kyle McCann */
/* GNOMNOM's renderer: two 120x120 indexed layers (the background, the field) doubled to the 240x240 panel through
 * a per-frame palette LUT, in ten 240x24 strips over two buffers so one fills while the DMA sends the other. The
 * background goes through the post pass (wave, zoom, bend, mirror, melt); the field is composited over it crisp.
 * Primitives, sprites and two bitmap faces (3x5 in a 4x6 cell, 5x7 digits in a 6x8 cell) draw into the target layer. */

#define GX_WRAP (GFX_W << 8)             /* the source row's width in 8.8: the affine wraps here */

static uint8_t gx_layer[2][GFX_W * GFX_H] PLAT_POOL;
static uint16_t gx_strip[2][GFX_SCREEN * GFX_STRIP_ROWS] PLAT_POOL;
static uint8_t gx_pal[256 * 3];
static uint8_t gx_dim[256];
static uint16_t gx_lut[256];
static uint8_t *gx_tgt = gx_layer[GFX_FIELD];
static uint8_t gx_pal_loaded;

static const int16_t gx_sin_tab[256] = {
       0,    6,   13,   19,   25,   31,   38,   44,   50,   56,   62,   68,   74,   80,   86,   92,
      98,  104,  109,  115,  121,  126,  132,  137,  142,  147,  152,  157,  162,  167,  172,  177,
     181,  185,  190,  194,  198,  202,  206,  209,  213,  216,  220,  223,  226,  229,  231,  234,
     237,  239,  241,  243,  245,  247,  248,  250,  251,  252,  253,  254,  255,  255,  256,  256,
     256,  256,  256,  255,  255,  254,  253,  252,  251,  250,  248,  247,  245,  243,  241,  239,
     237,  234,  231,  229,  226,  223,  220,  216,  213,  209,  206,  202,  198,  194,  190,  185,
     181,  177,  172,  167,  162,  157,  152,  147,  142,  137,  132,  126,  121,  115,  109,  104,
      98,   92,   86,   80,   74,   68,   62,   56,   50,   44,   38,   31,   25,   19,   13,    6,
       0,   -6,  -13,  -19,  -25,  -31,  -38,  -44,  -50,  -56,  -62,  -68,  -74,  -80,  -86,  -92,
     -98, -104, -109, -115, -121, -126, -132, -137, -142, -147, -152, -157, -162, -167, -172, -177,
    -181, -185, -190, -194, -198, -202, -206, -209, -213, -216, -220, -223, -226, -229, -231, -234,
    -237, -239, -241, -243, -245, -247, -248, -250, -251, -252, -253, -254, -255, -255, -256, -256,
    -256, -256, -256, -255, -255, -254, -253, -252, -251, -250, -248, -247, -245, -243, -241, -239,
    -237, -234, -231, -229, -226, -223, -220, -216, -213, -209, -206, -202, -198, -194, -190, -185,
    -181, -177, -172, -167, -162, -157, -152, -147, -142, -137, -132, -126, -121, -115, -109, -104,
     -98,  -92,  -86,  -80,  -74,  -68,  -62,  -56,  -50,  -44,  -38,  -31,  -25,  -19,  -13,   -6,
};

/* ASCII 32..95, a glyph an octal literal: five 3-bit rows, the top row in the highest digit (7 = ###, 5 = #.#) */
static const uint16_t gx_font4[64] = {
    000000, 022202, 055000, 057575, 036236, 051245, 025253, 022000,   /*  !"#$%&' */
    024442, 021112, 052725, 002720, 000024, 000700, 000002, 011244,   /* ()*+,-./ */
    025552, 026227, 071747, 071717, 055711, 074717, 074757, 071122,   /* 01234567 */
    075757, 075717, 002020, 002024, 012421, 007070, 042124, 071302,   /* 89:;<=>? */
    075747, 025755, 065656, 034443, 065556, 074747, 074744, 034553,   /* @ABCDEFG */
    055755, 072227, 011152, 055655, 044447, 057755, 065555, 075557,   /* HIJKLMNO */
    065644, 075571, 065655, 034216, 072222, 055557, 055552, 055575,   /* PQRSTUVW */
    055255, 055222, 071247, 064446, 044211, 031113, 025000, 000007,   /* XYZ[\]^_ */
};
static const uint16_t gx_font4_mul = 005250;      /* 'x' stays a multiplication sign: the HUD's "x2" */
/* 0..9 - : as 5-bit rows, bit 4 the left column */
static const uint8_t gx_font6[12][7] = {
    {0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E},
    {0x04, 0x0C, 0x04, 0x04, 0x04, 0x04, 0x0E},
    {0x0E, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1F},
    {0x1E, 0x01, 0x01, 0x0E, 0x01, 0x01, 0x1E},
    {0x02, 0x06, 0x0A, 0x12, 0x1F, 0x02, 0x02},
    {0x1F, 0x10, 0x10, 0x1E, 0x01, 0x01, 0x1E},
    {0x0E, 0x10, 0x10, 0x1E, 0x11, 0x11, 0x0E},
    {0x1F, 0x01, 0x02, 0x04, 0x04, 0x04, 0x04},
    {0x0E, 0x11, 0x11, 0x0E, 0x11, 0x11, 0x0E},
    {0x0E, 0x11, 0x11, 0x0F, 0x01, 0x01, 0x0E},
    {0x00, 0x00, 0x00, 0x1F, 0x00, 0x00, 0x00},
    {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00},
};

int32_t gfx_sin(uint32_t turn_q8) { return gx_sin_tab[turn_q8 & 255u]; }

void gfx_init(void)
{
    uint32_t i;
    memset(gx_layer, 0, sizeof gx_layer);
    gx_tgt = gx_layer[GFX_FIELD];
    if (gx_pal_loaded)
        return;
    for (i = 0; i < 256u; i++) {
        gx_pal[i * 3u] = gx_pal[i * 3u + 1u] = gx_pal[i * 3u + 2u] = (uint8_t)i;
        gx_dim[i] = (uint8_t)(i >> 1);
    }
}

void gfx_palette(const uint8_t *rgb, const uint8_t *dim)
{
    memcpy(gx_pal, rgb, sizeof gx_pal);
    memcpy(gx_dim, dim, sizeof gx_dim);
    gx_pal_loaded = 1;
}

void gfx_pal_set(uint32_t i, uint32_t r, uint32_t g, uint32_t b)
{
    i &= 255u;
    gx_pal[i * 3u] = (uint8_t)r;
    gx_pal[i * 3u + 1u] = (uint8_t)g;
    gx_pal[i * 3u + 2u] = (uint8_t)b;
}

void gfx_target(int layer) { gx_tgt = gx_layer[layer == GFX_BG ? 0 : 1]; }
uint8_t *gfx_buf(int layer) { return gx_layer[layer == GFX_BG ? 0 : 1]; }
void gfx_clear(uint8_t c) { memset(gx_tgt, c, GFX_W * GFX_H); }

void gfx_pixel(int32_t x, int32_t y, uint8_t c)
{
    if ((uint32_t)x < (uint32_t)GFX_W && (uint32_t)y < (uint32_t)GFX_H)
        gx_tgt[y * GFX_W + x] = c;
}

void gfx_hline(int32_t x, int32_t y, int32_t w, uint8_t c)
{
    int32_t x1 = x + w;
    if ((uint32_t)y >= (uint32_t)GFX_H)
        return;
    if (x < 0)
        x = 0;
    if (x1 > GFX_W)
        x1 = GFX_W;
    if (x1 > x)
        memset(gx_tgt + y * GFX_W + x, c, (uint32_t)(x1 - x));
}

void gfx_vline(int32_t x, int32_t y, int32_t h, uint8_t c)
{
    int32_t y1 = y + h;
    uint8_t *p;
    if ((uint32_t)x >= (uint32_t)GFX_W)
        return;
    if (y < 0)
        y = 0;
    if (y1 > GFX_H)
        y1 = GFX_H;
    for (p = gx_tgt + y * GFX_W + x; y < y1; y++, p += GFX_W)
        *p = c;
}

void gfx_rect(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c)
{
    int32_t x1 = x + w, y1 = y + h;
    if (x < 0)
        x = 0;
    if (y < 0)
        y = 0;
    if (x1 > GFX_W)
        x1 = GFX_W;
    if (y1 > GFX_H)
        y1 = GFX_H;
    if (x1 <= x)
        return;
    for (; y < y1; y++)
        memset(gx_tgt + y * GFX_W + x, c, (uint32_t)(x1 - x));
}

void gfx_frame(int32_t x, int32_t y, int32_t w, int32_t h, uint8_t c)
{
    if (w <= 0 || h <= 0)
        return;
    gfx_hline(x, y, w, c);
    gfx_hline(x, y + h - 1, w, c);
    gfx_vline(x, y + 1, h - 2, c);
    gfx_vline(x + w - 1, y + 1, h - 2, c);
}

void gfx_line(int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t c)
{
    int32_t dx, dy, sx, sy, err, guard = 4 * (GFX_W + GFX_H);
    x0 = clamp(x0, -2 * GFX_W, 3 * GFX_W);   /* far ends pulled in: the walk stays short and clipping stays cheap */
    x1 = clamp(x1, -2 * GFX_W, 3 * GFX_W);
    y0 = clamp(y0, -2 * GFX_H, 3 * GFX_H);
    y1 = clamp(y1, -2 * GFX_H, 3 * GFX_H);
    dx = x1 > x0 ? x1 - x0 : x0 - x1;
    dy = y1 > y0 ? y0 - y1 : y1 - y0;
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;
    err = dx + dy;
    while (guard--) {
        int32_t e2 = 2 * err;
        gfx_pixel(x0, y0, c);
        if (x0 == x1 && y0 == y1)
            break;
        if (e2 >= dy) {
            err += dy;
            x0 += sx;
        }
        if (e2 <= dx) {
            err += dx;
            y0 += sy;
        }
    }
}

void gfx_circle(int32_t cx, int32_t cy, int32_t r, uint8_t c)
{
    int32_t x = r, y = 0, err = 1 - r;
    if (r < 0 || r > 2 * GFX_W)
        return;
    while (x >= y) {
        gfx_pixel(cx + x, cy + y, c);
        gfx_pixel(cx - x, cy + y, c);
        gfx_pixel(cx + x, cy - y, c);
        gfx_pixel(cx - x, cy - y, c);
        gfx_pixel(cx + y, cy + x, c);
        gfx_pixel(cx - y, cy + x, c);
        gfx_pixel(cx + y, cy - x, c);
        gfx_pixel(cx - y, cy - x, c);
        y++;
        if (err < 0) {
            err += 2 * y + 1;
        } else {
            x--;
            err += 2 * (y - x) + 1;
        }
    }
}

void gfx_sprite(const gn_sprite_t *s, int32_t x, int32_t y, uint32_t flags)
{
    int32_t w = s->w, h = s->h, sx0 = 0, sy0 = 0, sx1 = w, sy1 = h, sx, sy;
    if (x < 0)
        sx0 = -x;
    if (y < 0)
        sy0 = -y;
    if (x + w > GFX_W)
        sx1 = GFX_W - x;
    if (y + h > GFX_H)
        sy1 = GFX_H - y;
    for (sy = sy0; sy < sy1; sy++) {
        const uint8_t *row = s->px + sy * w;
        uint8_t *dst = gx_tgt + (y + sy) * GFX_W + x + sx0;
        for (sx = sx0; sx < sx1; sx++, dst++) {
            uint8_t c = row[(flags & GFX_FLIP_X) ? w - 1 - sx : sx];
            if (!c || ((flags & GFX_GHOST) && ((x + sx + y + sy) & 1)))
                continue;
            *dst = (flags & GFX_DIM) ? gx_dim[c] : c;
        }
    }
}

static uint32_t gx_glyph_row(uint32_t ch, int32_t r, uint32_t flags)
{
    if (flags & GFX_BIG) {
        uint32_t i;
        if (ch >= '0' && ch <= '9')
            i = ch - '0';
        else if (ch == '-')
            i = 10;
        else if (ch == ':')
            i = 11;
        else
            return 0;
        return gx_font6[i][r];
    }
    if (ch == 'x')
        return (gx_font4_mul >> (3 * (4 - r))) & 7u;
    if (ch >= 'a' && ch <= 'z')
        ch -= 32u;
    if (ch < 32u || ch > 95u)
        ch = '?';
    return (gx_font4[ch - 32u] >> (3 * (4 - r))) & 7u;
}

int32_t gfx_text(int32_t x, int32_t y, const char *s, uint8_t c, uint32_t flags)
{
    int32_t sc = (flags & GFX_X2) ? 2 : 1, gw = (flags & GFX_BIG) ? 5 : 3, gh = (flags & GFX_BIG) ? 7 : 5;
    int32_t adv = (gw + 1) * sc, r, col;
    if (y >= GFX_H || y + gh * sc <= 0)
        return x + gfx_text_w(s, flags);
    for (; *s; s++, x += adv) {
        if (x >= GFX_W || x + gw * sc <= 0)
            continue;
        for (r = 0; r < gh; r++) {
            uint32_t bits = gx_glyph_row((uint8_t)*s, r, flags);
            for (col = 0; col < gw; col++) {
                if (!(bits & (1u << (gw - 1 - col))))
                    continue;
                gfx_pixel(x + col * sc, y + r * sc, c);
                if (sc == 2) {
                    gfx_pixel(x + col * 2 + 1, y + r * 2, c);
                    gfx_pixel(x + col * 2, y + r * 2 + 1, c);
                    gfx_pixel(x + col * 2 + 1, y + r * 2 + 1, c);
                }
            }
        }
    }
    return x;
}

int32_t gfx_text_w(const char *s, uint32_t flags)
{
    int32_t n = 0;
    while (*s++)
        n++;
    return n * ((flags & GFX_BIG) ? 6 : 4) * ((flags & GFX_X2) ? 2 : 1);
}

static void gx_lut_build(const gfx_post_t *p)
{
    uint32_t i;
    for (i = 0; i < 256u; i++) {
        uint32_t j = i;
        int32_t r, g, b, a;
        if (i >= GN_PAL_SKY && i < GN_PAL_GROUND)
            j = GN_PAL_SKY + ((i + p->cycle_a) & 31u);
        else if (i >= GN_PAL_GROUND && i < GN_PAL_GNOME)
            j = GN_PAL_GROUND + ((i + p->cycle_b) & 31u);
        else if (i >= GN_PAL_FX && i < GN_PAL_PROPS)
            j = GN_PAL_FX + ((i + p->cycle_fx) & 31u);
        r = gx_pal[j * 3u];
        g = gx_pal[j * 3u + 1u];
        b = gx_pal[j * 3u + 2u];
        if (p->strobe && i >= GN_PAL_SKY && i < GN_PAL_GROUND)
            r = g = b = 255;
        a = p->tint_amt;
        if (i < GN_PAL_SKY)
            a >>= 1;
        if (a) {
            r += (p->tint_r - r) * a / 255;
            g += (p->tint_g - g) * a / 255;
            b += (p->tint_b - b) * a / 255;
        }
        if (p->invert) {
            r = 255 - r;
            g = 255 - g;
            b = 255 - b;
        }
        gx_lut[i] = swap16(RGB(r, g, b));
    }
}

/* one screen row: the background through the affine (its row mirrored about the mouth line under mirror 2, its
 * left half copied over the right under any mirror), then the field over it from row y / 2 */
static void gx_row(uint16_t *dst, int32_t y, const gfx_post_t *p)
{
    const uint16_t *lut = gx_lut;
    const uint8_t *src, *fld;
    int32_t ys = y, sy, scale = 128, off = 0, sxq, x, n, fo, lo, hi;
    if (p->mirror > 1 && y >= 2 * GFX_MOUTH)
        ys = 4 * GFX_MOUTH - 1 - y;
    sy = ys >> 1;
    if (p->melt)
        sy += p->melt * gfx_sin((uint32_t)(ys * 3 + (p->melt_phase >> 8))) / 256;
    sy = clamp(sy, 0, GFX_H - 1);
    src = gx_layer[GFX_BG] + sy * GFX_W;
    if (p->zoom)
        scale += p->zoom * gfx_sin((uint32_t)((p->zoom_phase >> 8) + ys)) / 512;
    if (p->wave_amp)
        off += p->wave_amp * gfx_sin((uint32_t)(ys * p->wave_freq + (p->wave_phase >> 8)));
    if (p->bend) {
        int32_t d = ys - 2 * GFX_MOUTH;
        off += p->bend * d * d / 16;
    }
    sxq = ((GFX_W / 2) << 8) - (GFX_SCREEN / 2) * scale + off;
    sxq %= GX_WRAP;
    if (sxq < 0)
        sxq += GX_WRAP;
    n = p->mirror ? GFX_W : (int32_t)GFX_SCREEN;
    for (x = 0; x < n; x++) {
        dst[x] = lut[src[sxq >> 8]];
        sxq += scale;
        if (sxq >= GX_WRAP)
            sxq -= GX_WRAP;
    }
    if (p->mirror)
        for (x = 0; x < GFX_W; x++)
            dst[GFX_W + x] = dst[GFX_W - 1 - x];
    fld = gx_layer[GFX_FIELD] + (y >> 1) * GFX_W;
    fo = 0;
    if (p->field_wave)
        fo = p->field_wave * gfx_sin((uint32_t)(y * 3 + (p->wave_phase >> 8))) / 256;
    lo = fo < 0 ? -fo : 0;
    hi = fo > 0 ? GFX_W - fo : GFX_W;
    fld += lo + fo;
    dst += 2 * lo;
    for (x = lo; x < hi; x++, dst += 2, fld++) {
        uint32_t c = *fld;
        if (c)
            dst[0] = dst[1] = lut[c];
    }
}

void gfx_present(const gfx_post_t *post)
{
    gfx_post_t p = *post;
    uint32_t k, r;
    if (p.wave_amp > 24)
        p.wave_amp = 24;
    if (p.zoom > 64)
        p.zoom = 64;
    if (p.melt > 16)
        p.melt = 16;
    if (p.field_wave > 3)
        p.field_wave = 3;
    if (p.mirror > 2)
        p.mirror = 2;
    p.bend = (int8_t)clamp(p.bend, -32, 32);
    gx_lut_build(&p);
    for (k = 0; k < GFX_SCREEN / GFX_STRIP_ROWS; k++) {
        uint16_t *buf = gx_strip[k & 1u];
        for (r = 0; r < GFX_STRIP_ROWS; r++)
            gx_row(buf + r * GFX_SCREEN, (int32_t)(k * GFX_STRIP_ROWS + r), &p);
        lcd_blit(0, k * GFX_STRIP_ROWS, GFX_SCREEN, GFX_STRIP_ROWS, buf);
    }
}
