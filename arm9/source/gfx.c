/*
 * ETCH DS -- by Synth Centric
 * Drawing primitives and proportional text (see gfx.h).
 * VRAM: main engine (bottom, touch) bank A, sub engine (top) bank C; one
 * 256x192 16-bit bitmap each.
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "dsp_tables.h"

uint16_t *fb_top, *fb_bot;

extern const uint8_t font_small_w[64], font_small_rows[];
extern const uint8_t font_main_w[64], font_main_rows[];

void gfx_init(void)
{
    int id;
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_5_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    lcdMainOnBottom();
    id = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    fb_bot = bgGetGfxPtr(id);
    id = bgInitSub(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
    fb_top = bgGetGfxPtr(id);
    BG_PALETTE[0] = COL_BG;
    BG_PALETTE_SUB[0] = COL_BG;
    gfx_clear(fb_top, COL_BG);
    gfx_clear(fb_bot, COL_BG);
}

void gfx_clear(uint16_t *fb, uint16_t c)
{
    uint32_t i, v = c | ((uint32_t)c << 16), *p = (uint32_t *)fb;
    for (i = 0; i < 256 * 192 / 2; i++) p[i] = v;
}

static inline void plot(uint16_t *fb, int x, int y, uint16_t c)
{
    if ((unsigned)x < 256 && (unsigned)y < 192) fb[y * 256 + x] = c;
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

void gfx_rrect(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    if (w < 5 || h < 5) { gfx_rect(fb, x, y, w, h, c); return; }
    gfx_rect(fb, x + 2, y, w - 4, 1, c);
    gfx_rect(fb, x + 1, y + 1, w - 2, 1, c);
    gfx_rect(fb, x, y + 2, w, h - 4, c);
    gfx_rect(fb, x + 1, y + h - 2, w - 2, 1, c);
    gfx_rect(fb, x + 2, y + h - 1, w - 4, 1, c);
}

void gfx_rframe(uint16_t *fb, int x, int y, int w, int h, uint16_t c)
{
    gfx_hline(fb, x + 2, y, w - 4, c);
    gfx_hline(fb, x + 2, y + h - 1, w - 4, c);
    gfx_vline(fb, x, y + 2, h - 4, c);
    gfx_vline(fb, x + w - 1, y + 2, h - 4, c);
    plot(fb, x + 1, y + 1, c); plot(fb, x + w - 2, y + 1, c);
    plot(fb, x + 1, y + h - 2, c); plot(fb, x + w - 2, y + h - 2, c);
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

void gfx_arc(uint16_t *fb, int cx, int cy, int r, int a0, int a1, int thick, uint16_t c)
{
    int a, step = r > 10 ? 3 : 5;
    for (a = a0; a <= a1; a += step) {
        int i = a & 1023;
        int s = sine_tab[i], co = sine_tab[(i + 256) & 1023];
        plot(fb, cx + ((s * r + (1 << 14)) >> 15), cy - ((co * r + (1 << 14)) >> 15), c);
        if (thick) plot(fb, cx + ((s * (r - 1) + (1 << 14)) >> 15), cy - ((co * (r - 1) + (1 << 14)) >> 15), c);
    }
}

uint16_t gfx_mix(uint16_t a, uint16_t b, int t)
{
    int ra = a & 31, ga = (a >> 5) & 31, ba = (a >> 10) & 31;
    int rb = b & 31, gb = (b >> 5) & 31, bb = (b >> 10) & 31;
    if (t < 0) t = 0;
    if (t > 256) t = 256;
    return (uint16_t)(((ra + (((rb - ra) * t) >> 8)) | ((ga + (((gb - ga) * t) >> 8)) << 5) |
                       ((ba + (((bb - ba) * t) >> 8)) << 10)) | BIT(15));
}

void gfx_led(uint16_t *fb, int cx, int cy, int on, uint16_t col)
{
    if (on) {
        int i, j;
        for (j = -3; j <= 3; j++)
            for (i = -3; i <= 3; i++) {
                int d2 = i * i + j * j, x = cx + i, y = cy + j;
                if (d2 > 9 || d2 < 3 || (unsigned)x >= 256 || (unsigned)y >= 192) continue;
                fb[y * 256 + x] = gfx_mix(fb[y * 256 + x], col, 90 - d2 * 8);
            }
        gfx_disc(fb, cx, cy, 1, col);
    } else {
        gfx_disc(fb, cx, cy, 1, gfx_mix(COL_SURF2, col, 40));
    }
}

/*  segments:  -a-  f b  -g-  e c  -d-   */
static const uint8_t seg_bits[18] = {
    0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F,
    0x77, 0x7C, 0x39, 0x5E, 0x79, 0x71, 0x40, 0x00
};

void gfx_seg7(uint16_t *fb, int x, int y, char ch, int s, uint16_t on, uint16_t ghost)
{
    int idx, k;
    uint8_t bits;
    if (ch >= '0' && ch <= '9') idx = ch - '0';
    else if (ch >= 'A' && ch <= 'F') idx = ch - 'A' + 10;
    else if (ch == '-') idx = 16;
    else idx = 17;
    bits = seg_bits[idx];
    for (k = 0; k < 7; k++) {
        uint16_t c = (bits >> k) & 1 ? on : ghost;
        switch (k) {
        case 0: gfx_rect(fb, x + 2, y, s, 2, c); break;
        case 1: gfx_rect(fb, x + s + 2, y + 2, 2, s, c); break;
        case 2: gfx_rect(fb, x + s + 2, y + s + 3, 2, s, c); break;
        case 3: gfx_rect(fb, x + 2, y + 2 * s + 3, s, 2, c); break;
        case 4: gfx_rect(fb, x, y + s + 3, 2, s, c); break;
        case 5: gfx_rect(fb, x, y + 2, 2, s, c); break;
        default: gfx_rect(fb, x + 2, y + s + 2, s, 1, c); break;
        }
    }
}

/* ---- text ------------------------------------------------------------------ */
static inline int glyph(int font, char ch, const uint8_t **rows)
{
    int i;
    if (ch >= 'a' && ch <= 'z') ch = (char)(ch - 32);
    if (ch < 32 || ch > 95) ch = '?';
    i = ch - 32;
    if (font == F_SMALL) { *rows = font_small_rows + i * 5; return font_small_w[i]; }
    *rows = font_main_rows + i * 7;
    return font_main_w[i];
}

int gfx_text_w(int font, const char *s)
{
    int w = 0;
    const uint8_t *r;
    for (; *s; s++) w += glyph(font, *s, &r) + 1;
    return w ? w - 1 : 0;
}

int gfx_text(uint16_t *fb, int x, int y, int font, uint16_t c, const char *s)
{
    int h = FONT_H(font);
    for (; *s; s++) {
        const uint8_t *rows;
        int w = glyph(font, *s, &rows), i, j;
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                if (rows[j] & (0x80 >> i)) plot(fb, x + i, y + j, c);
        x += w + 1;
    }
    return x;
}

void gfx_text_c(uint16_t *fb, int cx, int y, int font, uint16_t c, const char *s)
{
    gfx_text(fb, cx - gfx_text_w(font, s) / 2, y, font, c, s);
}

void gfx_text_r(uint16_t *fb, int rx, int y, int font, uint16_t c, const char *s)
{
    gfx_text(fb, rx - gfx_text_w(font, s), y, font, c, s);
}

int gfx_textf(uint16_t *fb, int x, int y, int font, uint16_t c, const char *fmt, ...)
{
    char b[64];
    va_list a;
    va_start(a, fmt);
    vsnprintf(b, sizeof b, fmt, a);
    va_end(a);
    return gfx_text(fb, x, y, font, c, b);
}

int gfx_text_scaled(uint16_t *fb, int x, int y, int font, int scale, uint16_t c, const char *s)
{
    int h = FONT_H(font);
    for (; *s; s++) {
        const uint8_t *rows;
        int w = glyph(font, *s, &rows), i, j;
        for (j = 0; j < h; j++)
            for (i = 0; i < w; i++)
                if (rows[j] & (0x80 >> i)) gfx_rect(fb, x + i * scale, y + j * scale, scale, scale, c);
        x += (w + 1) * scale;
    }
    return x;
}

int gfx_wrap(uint16_t *fb, int x, int y, int w, int font, int line_h, uint16_t c, const char *s)
{
    char line[64], word[40];
    int ln = 0;
    line[0] = 0;
    while (*s) {
        int n = 0;
        while (*s == ' ') s++;
        if (*s == '\n') {                         /* paragraph break */
            if (ln) { gfx_text(fb, x, y, font, c, line); y += line_h; line[0] = 0; ln = 0; }
            y += line_h / 2;
            s++;
            continue;
        }
        while (*s && *s != ' ' && *s != '\n' && n < 39) word[n++] = *s++;
        word[n] = 0;
        if (!n) continue;
        if (ln) {
            char test[104];
            snprintf(test, sizeof test, "%s %s", line, word);
            if (gfx_text_w(font, test) > w) {
                gfx_text(fb, x, y, font, c, line);
                y += line_h;
                strcpy(line, word);
            } else strcpy(line, test);
        } else strcpy(line, word);
        ln = (int)strlen(line);
    }
    if (ln) { gfx_text(fb, x, y, font, c, line); y += line_h; }
    return y;
}
