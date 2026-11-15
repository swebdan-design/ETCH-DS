/*
 * ETCH DS -- by Synth Centric
 * The opening screen, as on ETCH and ETCH II: the name, "by Synth Centric",
 * and PRESS START.  Audio is already running (silently); START -- or a tap,
 * though touch is never required -- returns, and the moment it was pressed
 * seeds the living song.  The wave on the bottom screen is a filtered saw
 * whose cutoff breathes, drawn as a single hairline.
 */
#include <stdio.h>
#include <string.h>
#include "gfx.h"
#include "app.h"
#include "dsp_tables.h"

#define WAVE_Y 64
#define WAVE_H 56

static void wordmark(uint16_t *fb, int y)
{
    int s = 4, w1 = (gfx_text_w(F_MAIN, "ETCH") + 1) * s, w2 = (gfx_text_w(F_MAIN, "DS") + 1) * s, gap = 10;
    int x = 128 - (w1 + gap + w2 - s) / 2;
    x = gfx_text_scaled(fb, x, y, F_MAIN, s, COL_TEXT, "ETCH");
    gfx_text_scaled(fb, x + gap - s, y, F_MAIN, s, COL_TEXT3, "DS");
}

static void draw_top(void)
{
    gfx_clear(fb_top, COL_BG);
    wordmark(fb_top, 52);
    gfx_text_c(fb_top, 128, 96, F_SMALL, COL_TEXT2, "A GENERATIVE ACID MACHINE");
    gfx_hline(fb_top, 112, 110, 32, COL_SURF3);
    gfx_text_c(fb_top, 128, 120, F_MAIN, COL_TEXT, "BY SYNTH CENTRIC");
    gfx_text_c(fb_top, 128, 180, F_SMALL, COL_TEXT3, "VERSION " ETCH_DS_VERSION "   CORROSION FROM ETCH 2.5");
}

/* one cycle of a band-limited saw through a resonant low-pass: a few
 * harmonics, the cutoff swinging slowly with t */
static void draw_wave(uint32_t t)
{
    static int16_t prev[244];
    int x, cut = 3 + ((sine_tab[(t * 3) & 1023] + 32768) >> 13);      /* 3..10 harmonics */
    gfx_rect(fb_bot, 6, WAVE_Y - WAVE_H / 2 - 2, 244, WAVE_H + 4, COL_BG);
    for (x = 0; x < 244; x++) {
        int k, v = 0;
        uint32_t ph = (uint32_t)(x * 2 * 1024 / 244 + t * 4);       /* two cycles */
        for (k = 1; k <= 10; k++) {
            int amp = 32767 / k;
            if (k > cut) amp >>= (k - cut) * 2;                      /* roll-off */
            if (k == cut) amp = amp * 5 / 2;                         /* resonance */
            v += (sine_tab[(ph * k) & 1023] * amp) >> 15;
        }
        prev[x] = (int16_t)(WAVE_Y - ((v * (WAVE_H / 2 - 2)) >> 16));
    }
    for (x = 1; x < 244; x++) gfx_line(fb_bot, 5 + x, prev[x - 1], 6 + x, prev[x], COL_TEXT);
}

void title_show(void)
{
    draw_top();
    gfx_clear(fb_bot, COL_BG);
    gfx_text_c(fb_bot, 128, 172, F_SMALL, COL_TEXT3, "HEADPHONES RECOMMENDED");
}

uint16_t title_run(void)
{
    uint32_t t = 0;
    uint16_t entropy = 0;
    for (;;) {
        uint32_t down;
        swiWaitForVBlank();
        scanKeys();
        down = keysDown();
        entropy = (uint16_t)(entropy * 31u + hw_noise() + down);
        if ((t & 1) == 0) draw_wave(t >> 1);
        if ((t & 31) == 0) {
            gfx_rect(fb_bot, 0, 132, 256, 10, COL_BG);
            if (t & 32) gfx_text_c(fb_bot, 128, 134, F_MAIN, COL_TEXT, "PRESS START");
        }
        if (down & (KEY_START | KEY_TOUCH)) break;
        t++;
    }
    while (keysHeld() & KEY_TOUCH) { swiWaitForVBlank(); scanKeys(); }   /* the tap is not a UI touch */
    return (uint16_t)(entropy ^ hw_noise());
}
