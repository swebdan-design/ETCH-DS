/*
 * ETCH DS -- by Synth Centric
 * Drawing primitives and text for the two screens (see gfx.h).
 *
 * VRAM: main engine (bottom screen, touch) uses bank A, sub engine (top) bank
 * C.  In each 128 KB bank: console map at 0, font tiles at 16 KB, the bitmap
 * at 32 KB (rows 0..191 fill the remaining 96 KB exactly).
 */
#include <stdarg.h>
#include <stdio.h>
#include "gfx.h"

uint16_t *fb_top, *fb_bot;
PrintConsole con_top, con_bot;

static const uint16_t text_rgb[16] = {
    RGB15(0, 0, 0),    RGB15(22, 8, 3),  RGB15(10, 26, 14), RGB15(28, 22, 6),
    RGB15(8, 14, 24),  RGB15(24, 8, 20), RGB15(8, 24, 24),  RGB15(21, 20, 18),
    RGB15(12, 11, 10), RGB15(31, 15, 4), RGB15(12, 31, 17), RGB15(31, 28, 9),
    RGB15(14, 20, 31), RGB15(31, 12, 28), RGB15(12, 31, 31), RGB15(29, 28, 25),
};

/* colour index 1 of the font is the stamped shadow, 2..15 the glyph */
static void tint(uint16_t *pal)
{
    int n, k;
    for (n = 0; n < 16; n++) {
        pal[n * 16 + 1] = RGB15(1, 1, 1);
        for (k = 2; k < 16; k++) pal[n * 16 + k] = text_rgb[n];
    }
    pal[0] = RGB15(2, 2, 3);
}

extern const uint8_t font_etch_tiles[];
static ConsoleFont etch_font = { font_etch_tiles, NULL, 0, 4, 32, 64 };

void gfx_init(void)
{
    int id;
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    lcdMainOnBottom();

    id = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 2, 0);
    bgSetPriority(id, 1);
    fb_bot = bgGetGfxPtr(id);
    id = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 2, 0);
    bgSetPriority(id, 1);
    fb_top = bgGetGfxPtr(id);

    consoleInit(&con_bot, 0, BgType_Text4bpp, BgSize_T_256x256, 0, 1, true, true);
    consoleInit(&con_top, 0, BgType_Text4bpp, BgSize_T_256x256, 0, 1, false, true);
    bgSetPriority(con_bot.bgId, 0);
    bgSetPriority(con_top.bgId, 0);
    consoleSetFont(&con_bot, &etch_font);
    consoleSetFont(&con_top, &etch_font);
    tint(BG_PALETTE);
    tint(BG_PALETTE_SUB);
    gfx_clear(fb_top, COL_BG);
    gfx_clear(fb_bot, COL_BG);
}

void gfx_clear(uint16_t *fb, uint16_t c)
{
    uint32_t i, v = c | ((uint32_t)c << 16), *p = (uint32_t *)fb;
    for (i = 0; i < 256 * 192 / 2; i++) p[i] = v;
}

void gfx_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    int i, j;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > 256) w = 256 - x;
    if (y + h > 192) h = 192 - y;
    for (j = 0; j < h; j++) {
        uint16_t *p = fb + (y + j) * 256 + x;
        for (i = 0; i < w; i++) p[i] = c;
    }
}

void gfx_hline(uint16_t *fb, int x, int y, int w, uint16_t c) { gfx_rect(fb, x, y, w, 1, c); }
void gfx_vline(uint16_t *fb, int x, int y, int h, uint16_t c) { gfx_rect(fb, x, y, 1, h, c); }

void gfx_frame(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    gfx_hline(fb, x, y, w, c);
    gfx_hline(fb, x, y + h - 1, w, c);
    gfx_vline(fb, x, y, h, c);
    gfx_vline(fb, x + w - 1, y, h, c);
}

static inline void plot(uint16_t *fb, int x, int y, uint16_t c)
{
    if ((unsigned)x < 256 && (unsigned)y < 192) fb[y * 256 + x] = c;
}

void gfx_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t c)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1, sx = x0 < x1 ? 1 : -1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0, sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        int e2;
        plot(fb, x0, y0, c);
        if (x0 == x1 && y0 == y1) break;
        e2 = 2 * err;
        if (e2 >= dy) { err += dy; x0 += sx; }
        if (e2 <= dx) { err += dx; y0 += sy; }
    }
}

void gfx_disc(uint16_t *fb, int cx, int cy, int r, uint16_t c)
{
    int x, y;
    for (y = -r; y <= r; y++)
        for (x = -r; x <= r; x++)
            if (x * x + y * y <= r * r + r) plot(fb, cx + x, cy + y, c);
}

void gfx_ring(uint16_t *fb, int cx, int cy, int r, uint16_t c)
{
    int x = r, y = 0, err = 1 - r;
    while (x >= y) {
        plot(fb, cx + x, cy + y, c); plot(fb, cx - x, cy + y, c);
        plot(fb, cx + x, cy - y, c); plot(fb, cx - x, cy - y, c);
        plot(fb, cx + y, cy + x, c); plot(fb, cx - y, cy + x, c);
        plot(fb, cx + y, cy - x, c); plot(fb, cx - y, cy - x, c);
        y++;
        if (err < 0) err += 2 * y + 1;
        else { x--; err += 2 * (y - x) + 1; }
    }
}

uint16_t gfx_mix(uint16_t a, uint16_t b, int t)
{
    int ra = a & 31, ga = (a >> 5) & 31, ba = (a >> 10) & 31;
    int rb = b & 31, gb = (b >> 5) & 31, bb = (b >> 10) & 31;
    return (uint16_t)(((ra + (((rb - ra) * t) >> 8)) | ((ga + (((gb - ga) * t) >> 8)) << 5) |
                       ((ba + (((bb - ba) * t) >> 8)) << 10)) | BIT(15));
}

void txt(PrintConsole *con, int row, int col, int color, const char *fmt, ...)
{
    va_list a;
    consoleSelect(con);
    consoleSetColor(con, (ConsoleColor)color);
    printf("\x1b[%d;%dH", row, col);
    va_start(a, fmt);
    vprintf(fmt, a);
    va_end(a);
}

void txt_clear(PrintConsole *con, int row, int col, int n)
{
    consoleSelect(con);
    printf("\x1b[%d;%dH%*s", row, col, n, "");
}

uint16_t gfx_shade(uint16_t c, int d)
{
    int r = (c & 31) + d, g = ((c >> 5) & 31) + d, b = ((c >> 10) & 31) + d;
    r = r < 0 ? 0 : r > 31 ? 31 : r;
    g = g < 0 ? 0 : g > 31 ? 31 : g;
    b = b < 0 ? 0 : b > 31 ? 31 : b;
    return (uint16_t)(r | (g << 5) | (b << 10) | BIT(15));
}

static uint32_t noise_state = 0x9E3779B9u;
static inline int rnd(void)
{
    noise_state ^= noise_state << 13; noise_state ^= noise_state >> 17; noise_state ^= noise_state << 5;
    return (int)(noise_state >> 24);
}

/* brushed steel: horizontal grain (slowly varying along a row, different on
 * every row), fine speckle, and the odd scratch */
void gfx_plate(uint16_t *fb, int x, int y, int w, int h, uint16_t base)
{
    int i, j;
    for (j = 0; j < h; j++) {
        int row = (rnd() & 3) - 1, grain = 0;
        uint16_t *p;
        if (y + j < 0 || y + j >= 192) continue;
        p = fb + (y + j) * 256;
        for (i = 0; i < w; i++) {
            int xx = x + i, d;
            if (xx < 0 || xx >= 256) continue;
            if ((i & 7) == 0) grain = (rnd() % 3) - 1;
            d = row + grain + ((rnd() & 15) == 0 ? 1 : 0);
            p[xx] = gfx_shade(base, d);
        }
    }
    for (i = 0; i < w * h / 900; i++) {                   /* scratches */
        int sx = x + rnd() * w / 256, sy = y + rnd() * h / 256, len = 6 + (rnd() & 15), k;
        for (k = 0; k < len; k++)
            if (sx + k < x + w && sy < y + h && sx + k < 256 && sy < 192)
                fb[sy * 256 + sx + k] = gfx_shade(fb[sy * 256 + sx + k], 2);
    }
}

void gfx_bevel(uint16_t *fb, int x, int y, int w, int h, int raised)
{
    int i;
    int hi = raised ? 5 : -4, lo = raised ? -5 : 3;
    for (i = 0; i < w; i++) {
        if (x + i < 0 || x + i >= 256) continue;
        if (y >= 0 && y < 192) fb[y * 256 + x + i] = gfx_shade(fb[y * 256 + x + i], hi);
        if (y + h - 1 >= 0 && y + h - 1 < 192) fb[(y + h - 1) * 256 + x + i] = gfx_shade(fb[(y + h - 1) * 256 + x + i], lo);
    }
    for (i = 1; i < h - 1; i++) {
        if (y + i < 0 || y + i >= 192) continue;
        if (x >= 0) fb[(y + i) * 256 + x] = gfx_shade(fb[(y + i) * 256 + x], hi);
        if (x + w - 1 < 256) fb[(y + i) * 256 + x + w - 1] = gfx_shade(fb[(y + i) * 256 + x + w - 1], lo);
    }
}

void gfx_well(uint16_t *fb, int x, int y, int w, int h, uint16_t fill)
{
    gfx_rect(fb, x + 1, y + 1, w - 2, h - 2, fill);
    gfx_hline(fb, x, y, w, C(1, 1, 1));
    gfx_vline(fb, x, y, h, C(1, 1, 1));
    gfx_hline(fb, x + 1, y + h - 1, w - 1, C(14, 14, 15));
    gfx_vline(fb, x + w - 1, y + 1, h - 1, C(14, 14, 15));
}

void gfx_button(uint16_t *fb, int x, int y, int w, int h, int pressed)
{
    gfx_rect(fb, x, y, w, h, C(1, 1, 1));                   /* the gap around it */
    gfx_plate(fb, x + 1, y + 1, w - 2, h - 2, pressed ? C(7, 6, 6) : C(11, 11, 12));
    gfx_bevel(fb, x + 1, y + 1, w - 2, h - 2, !pressed);
    if (!pressed) gfx_bevel(fb, x + 2, y + 2, w - 4, h - 4, 1);
}

void gfx_rivet(uint16_t *fb, int cx, int cy)
{
    gfx_disc(fb, cx + 1, cy + 1, 2, C(2, 2, 2));
    gfx_disc(fb, cx, cy, 2, C(13, 13, 14));
    plot(fb, cx - 1, cy - 1, C(24, 24, 25));
    plot(fb, cx, cy - 1, C(19, 19, 20));
    plot(fb, cx + 1, cy + 1, C(7, 7, 8));
}

void gfx_rivets(uint16_t *fb, int x, int y, int w, int h)
{
    gfx_rivet(fb, x + 4, y + 4);
    gfx_rivet(fb, x + w - 5, y + 4);
    gfx_rivet(fb, x + 4, y + h - 5);
    gfx_rivet(fb, x + w - 5, y + h - 5);
}

void gfx_hazard(uint16_t *fb, int x, int y, int w, int h)
{
    int i, j;
    for (j = 0; j < h; j++)
        for (i = 0; i < w; i++) {
            int on = (((x + i) + (y + j)) / 5) & 1;
            uint16_t c = on ? COL_HAZARD : C(2, 2, 2);
            if ((rnd() & 31) == 0) c = C(10, 7, 3);           /* worn paint */
            plot(fb, x + i, y + j, c);
        }
}

/* a rust bloom: denser in the middle, ragged at the edge */
void gfx_rust(uint16_t *fb, int cx, int cy, int r, uint32_t seed)
{
    int i, j;
    noise_state ^= seed * 2654435761u;
    for (j = -r; j <= r; j++)
        for (i = -r; i <= r; i++) {
            int d2 = i * i + j * j, x = cx + i, y = cy + j;
            if (d2 > r * r || (unsigned)x >= 256 || (unsigned)y >= 192) continue;
            if ((rnd() * r * r) >> 8 > r * r - d2 + (r * r >> 3)) continue;
            {
                int t = 60 + (rnd() & 127);
                uint16_t rc = (rnd() & 3) ? C(16, 7, 3) : C(22, 10, 3);
                fb[y * 256 + x] = gfx_mix(fb[y * 256 + x], rc, t);
            }
        }
}

void gfx_led(uint16_t *fb, int cx, int cy, int on, uint16_t col)
{
    if (on) {
        int i, j;
        for (j = -4; j <= 4; j++)                              /* glow */
            for (i = -4; i <= 4; i++) {
                int d2 = i * i + j * j, x = cx + i, y = cy + j;
                if (d2 > 16 || d2 < 4 || (unsigned)x >= 256 || (unsigned)y >= 192) continue;
                fb[y * 256 + x] = gfx_mix(fb[y * 256 + x], col, 120 - d2 * 6);
            }
        gfx_disc(fb, cx, cy, 2, col);
        plot(fb, cx - 1, cy - 1, gfx_shade(col, 8));
    } else {
        gfx_disc(fb, cx, cy, 2, gfx_mix(C(2, 2, 2), col, 60));
        plot(fb, cx - 1, cy - 1, gfx_mix(C(6, 6, 6), col, 60));
    }
}

/*  segments:  -a-
 *            f   b
 *             -g-
 *            e   c
 *             -d-          */
static const uint8_t seg_bits[22] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,   /* 0-9 */
    0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71,                           /* A-F */
    0x40, 0x00, 0x76, 0x38, 0x73, 0x3E                            /* - space H L P U */
};

static void seg(uint16_t *fb, int x, int y, int s, int k, uint16_t c)
{
    switch (k) {
    case 0: gfx_rect(fb, x + 2, y, s, 2, c); break;
    case 1: gfx_rect(fb, x + s + 2, y + 2, 2, s, c); break;
    case 2: gfx_rect(fb, x + s + 2, y + s + 3, 2, s, c); break;
    case 3: gfx_rect(fb, x + 2, y + 2 * s + 3, s, 2, c); break;
    case 4: gfx_rect(fb, x, y + s + 3, 2, s, c); break;
    case 5: gfx_rect(fb, x, y + 2, 2, s, c); break;
    default: gfx_rect(fb, x + 2, y + s + 2, s, 1, c); gfx_rect(fb, x + 3, y + s + 1, s - 2, 3, c); break;
    }
}

void gfx_seg7(uint16_t *fb, int x, int y, char ch, int s, uint16_t on, uint16_t ghost)
{
    int idx, k;
    uint8_t bits;
    if (ch >= '0' && ch <= '9') idx = ch - '0';
    else if (ch >= 'A' && ch <= 'F') idx = ch - 'A' + 10;
    else if (ch >= 'a' && ch <= 'f') idx = ch - 'a' + 10;
    else if (ch == '-') idx = 16;
    else if (ch == 'H') idx = 18;
    else if (ch == 'L') idx = 19;
    else if (ch == 'P') idx = 20;
    else if (ch == 'U') idx = 21;
    else idx = 17;
    bits = seg_bits[idx];
    for (k = 0; k < 7; k++) seg(fb, x, y, s, k, (bits >> k) & 1 ? on : ghost);
}

int gfx_seg7_str(uint16_t *fb, int x, int y, const char *str, int s, uint16_t on, uint16_t ghost)
{
    for (; *str; str++) {
        if (*str == '.') { gfx_rect(fb, x - 2, y + 2 * s + 3, 2, 2, on); continue; }
        gfx_seg7(fb, x, y, *str, s, on, ghost);
        x += s + 7;
    }
    return x;
}
