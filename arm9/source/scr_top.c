/*
 * ETCH DS -- by Synth Centric
 * Top screen (always visible): the machine's face plate.
 *
 *   y 0-3      hazard band
 *   y 4-25     name plate: ETCH DS, the song, its seed code; tempo on an
 *              amber 7-segment display
 *   y 28-110   the pattern, in a recessed display: the bass line as pitch
 *              (1 px per semitone), accents white-hot, slides as ramps, ties
 *              as held bars; ETCH's four drum lanes as segment pads.  The
 *              playhead column glows; steps CORROSION just changed light up
 *              acid green and fade over about a second and a half.
 *   y 111-137  step LEDs (one per 16th) and the status line
 *   y 139-171  oscilloscope (amber phosphor)
 *   y 172-191  CPU meter: segment bar, worst-block tick, 7-segment percent,
 *              per-stage line
 * Everything static is painted once into topbg; dynamic parts redraw only
 * when what they show changes, restoring their patch of plate from topbg.
 */
#include <stdio.h>
#include <string.h>
#include "corrosion.h"
#include "gfx.h"
#include "app.h"
#include "audio.h"

#define COLX(s)  (8 + (s) * 15)
#define BASS_Y0  31
#define BASS_Y1  78          /* note 0 (C1) sits on this row */
#define LANE_Y0  81
#define LED_Y    116
#define SCOPE_Y  141
#define SCOPE_H  29

static uint16_t topbg[256 * 192];
static uint16_t glow[16];
static pattern_t snap;
static uint8_t snap_commits, snap_valid, snap_pat = 0xFF;
static uint32_t col_sig[16];
static uint16_t sbuf[256 * SCOPE_H];
static uint32_t frames;
static int last_bpm = -1, last_led = -1, last_play = -1, last_fill = -1, last_cpu = -1;

static int note_y(int n) { if (n > 47) n = 47; if (n < 0) n = 0; return BASS_Y1 - n; }

static void restore(int x, int y, int w, int h)
{
    int j;
    if (x < 0) { w += x; x = 0; }
    if (x + w > 256) w = 256 - x;
    for (j = 0; j < h; j++)
        if (y + j >= 0 && y + j < 192)
            memcpy(fb_top + (y + j) * 256 + x, topbg + (y + j) * 256 + x, (size_t)w * 2);
}

static uint8_t prev_sounding(const pattern_t *p, int s)
{
    int k;
    for (k = 1; k < 16; k++) {
        int i = (s - k) & 15;
        if ((p->flags[i] & STEP_TYPE_MASK) == STEP_NOTE) return p->note[i];
    }
    return p->note[s];
}

static void draw_column(const pattern_t *p, int s, int play, int g)
{
    int x = COLX(s), l;
    uint8_t f = p->flags[s], t = f & STEP_TYPE_MASK;
    uint16_t bg = (s & 3) == 0 ? C(2, 2, 2) : COL_LCD, grid;
    if (play) bg = C(7, 4, 1);
    if (g) bg = gfx_mix(bg, C(2, 12, 6), g);
    grid = gfx_mix(bg, COL_GHOST, 200);
    gfx_rect(fb_top, x, BASS_Y0, 14, BASS_Y1 - BASS_Y0 + 2, bg);
    gfx_hline(fb_top, x, note_y(12), 14, grid);
    gfx_hline(fb_top, x, note_y(24), 14, grid);
    gfx_hline(fb_top, x, note_y(36), 14, grid);
    if (t == STEP_NOTE) {
        int y = note_y(p->note[s]);
        uint16_t c = (f & STEP_ACCENT) ? C(31, 28, 18) : COL_AMBER;
        if (g) c = gfx_mix(c, COL_ACID, g);
        if (SH->proj.mutes & 1) c = C(9, 6, 3);
        gfx_vline(fb_top, x + 7, y + 2, BASS_Y1 - y, gfx_mix(bg, c, 70));
        gfx_rect(fb_top, x + 1, y - 2, 12, 5, gfx_mix(bg, c, 90));      /* halo */
        gfx_rect(fb_top, x + 1, y - 1, 12, 3, c);
        if (f & STEP_SLIDE) gfx_line(fb_top, x + 12, y, x + 15, note_y(p->note[(s + 1) & 15]), c);
    } else if (t == STEP_TIE) {
        int y = note_y(prev_sounding(p, s));
        gfx_rect(fb_top, x, y - 1, 15, 3, gfx_mix(COL_AMBER, bg, 120));
    }
    for (l = 0; l < NUM_LANES; l++) {
        int y = LANE_Y0 + l * 7;
        int on = (p->drum[l] >> s) & 1, muted = (SH->proj.mutes >> (l + 1)) & 1;
        uint16_t c = on ? (muted ? C(9, 6, 3) : l == 0 ? C(31, 12, 2) : COL_AMBER) : COL_GHOST;
        if (play && !on) c = C(10, 6, 2);
        if (g && on) c = gfx_mix(c, COL_ACID, g);
        gfx_rect(fb_top, x, y, 14, 7, COL_LCD);
        gfx_rect(fb_top, x + 1, y + 1, 12, 5, c);
    }
}

static void draw_pattern(void)
{
    const pattern_t *p = &SH->proj.pat[SH->cur];
    uint8_t c = SH->evo_commits, playing = SH->playing, step = SH->step;
    int s;
    if (!snap_valid || snap_pat != SH->cur) {
        memcpy(&snap, p, sizeof snap);
        snap_valid = 1; snap_pat = SH->cur; snap_commits = c;
    } else if (c != snap_commits) {                   /* what did CORROSION just change? */
        for (s = 0; s < 16; s++) {
            int l, diff = snap.note[s] != p->note[s] || snap.flags[s] != p->flags[s];
            for (l = 0; l < NUM_LANES; l++)
                if (((snap.drum[l] ^ p->drum[l]) >> s) & 1) diff = 1;
            if (diff) glow[s] = 256;
        }
        memcpy(&snap, p, sizeof snap);
        snap_commits = c;
    } else {
        memcpy(&snap, p, sizeof snap);                /* your own edits don't glow */
    }
    for (s = 0; s < 16; s++) {
        int play = playing && s == step, g = glow[s], l;
        uint32_t sig = (uint32_t)p->note[s] | ((uint32_t)(p->flags[s] & 15) << 6) |
                       ((uint32_t)play << 10) | ((uint32_t)(g >> 4) << 11) |
                       ((uint32_t)SH->proj.mutes << 16) | 0x80000000u;
        for (l = 0; l < NUM_LANES; l++) sig ^= ((uint32_t)((p->drum[l] >> s) & 1)) << (24 + l);
        if (p->flags[s] & STEP_SLIDE) sig ^= (uint32_t)p->note[(s + 1) & 15] << 20;
        if ((p->flags[s] & STEP_TYPE_MASK) == STEP_TIE) sig ^= (uint32_t)prev_sounding(p, s) * 2654435761u;
        if (sig != col_sig[s]) { draw_column(p, s, play, g); col_sig[s] = sig; }
        if (glow[s]) glow[s] = glow[s] > 3 ? (uint16_t)(glow[s] - 3) : 0;
    }
}

static void draw_leds(void)
{
    int s, led = SH->playing ? SH->step : -1;
    if (led == last_led) return;
    for (s = 0; s < 16; s++) {
        if (s != led && s != last_led && last_led != -2) continue;
        restore(COLX(s) + 2, LED_Y - 5, 11, 11);
        gfx_led(fb_top, COLX(s) + 7, LED_Y, s == led, (s & 3) == 0 ? C(31, 10, 2) : COL_AMBER);
    }
    last_led = led;
}

static void draw_scope(void)
{
    static int16_t smp[256];
    uint32_t n = audio_scope(smp, 240), i;
    int prev = SCOPE_H / 2;
    for (i = 0; i < 256 * SCOPE_H; i++) sbuf[i] = COL_LCD;
    for (i = 8; i < 248; i += 24)
        { int y; for (y = 0; y < SCOPE_H; y += 3) sbuf[y * 256 + i] = COL_GHOST; }
    for (i = 8; i < 248; i += 2) sbuf[(SCOPE_H / 2) * 256 + i] = COL_GHOST;
    for (i = 0; i < n; i++) {
        int x = (int)i + 8, y = SCOPE_H / 2 - ((smp[i] * (SCOPE_H / 2 - 2)) >> 15), a, b;
        if (y < 1) y = 1;
        if (y > SCOPE_H - 2) y = SCOPE_H - 2;
        a = prev < y ? prev : y;
        b = prev < y ? y : prev;
        for (; a <= b; a++) {
            sbuf[a * 256 + x] = COL_AMBER;
            sbuf[(a - 1) * 256 + x] = gfx_mix(sbuf[(a - 1) * 256 + x], COL_AMBER, 90);
            sbuf[(a + 1) * 256 + x] = gfx_mix(sbuf[(a + 1) * 256 + x], COL_AMBER, 90);
        }
        prev = y;
    }
    for (i = 0; i < SCOPE_H; i++) memcpy(fb_top + (SCOPE_Y + i) * 256 + 4, sbuf + i * 256 + 4, 248 * 2);
}

static void draw_bpm(void)
{
    char b[4];
    int bpm = SH->proj.synth.bpm;
    if (bpm == last_bpm) return;
    sprintf(b, "%3d", bpm);
    gfx_rect(fb_top, 205, 8, 42, 14, COL_LCD);
    gfx_seg7_str(fb_top, 206, 9, b, 4, COL_AMBER, COL_GHOST);
    last_bpm = bpm;
}

static void pct(char *o, uint16_t v) { sprintf(o, "%d.%d", v / 100, (v % 100) / 10); }

static void draw_status(void)
{
    const project_t *pr = &SH->proj;
    const pattern_t *p = &pr->pat[SH->cur];
    uint8_t bars = bars_to_evolution(SH), play = SH->playing, fill = SH->fill;
    char code[13];

    corr_code_encode(&pr->gen, code);
    code[12] = 0;
    txt(&con_top, 1, 2, T_HOT, "ETCH DS");
    txt(&con_top, 1, 10, T_BRIGHT, "%-10.10s", pr->name);
    txt(&con_top, 2, 2, T_SULFUR, "%.4s-%.4s-%.4s", code, code + 4, code + 8);

    if (play != last_play) txt_clear(&con_top, 16, 0, 26);
    if (!play) {
        txt(&con_top, 16, 1, (frames & 32) ? T_SULFUR : T_DIM, "    -- PRESS START --     ");
    } else {
        txt(&con_top, 16, 1, T_ACID, "%-7.7s", corr_mode_names[pr->gen.mode % EVO_COUNT]);
        txt(&con_top, 16, 9, T_TEXT, "P%02d G%03d", SH->cur, p->gen);
        if (bars) txt(&con_top, 16, 19, T_TEXT, "EVO %d ", bars);
        else txt(&con_top, 16, 19, T_DIM, "EVO - ");
    }
    if (play != last_play || fill != last_fill) {
        restore(204, 124, 50, 12);
        gfx_led(fb_top, 210, 131, play, COL_ACID);
        gfx_led(fb_top, 236, 131, fill, COL_AMBER);
        last_play = play; last_fill = fill;
    }
    txt(&con_top, 16, 27, play ? T_ACID : T_DIM, "R");
    txt(&con_top, 16, 30, fill ? T_HOT : T_DIM, "F");
}

static void draw_cpu(void)
{
    char a[8], b[8], c[8], d[8];
    int i, segs = (astats.avg_total * 24 + 5000) / 10000, maxs = (astats.max_total_pct * 24 + 5000) / 10000;
    int sig = astats.avg_total * 3 + astats.max_total_pct + (int)astats.windows * 7;
    if (sig == last_cpu) return;
    last_cpu = sig;
    for (i = 0; i < 24; i++) {
        int x = 34 + i * 6;
        uint16_t on = i >= 17 ? C(31, 6, 2) : i >= 12 ? C(31, 22, 3) : COL_AMBER;
        gfx_rect(fb_top, x, 177, 5, 6, i < segs ? on : (i == maxs - 1 ? gfx_mix(COL_GHOST, on, 140) : COL_GHOST));
    }
    sprintf(a, "%4d", astats.avg_total / 10);              /* tenths of a percent */
    a[4] = a[3]; a[3] = '.'; a[5] = 0;
    gfx_rect(fb_top, 186, 175, 50, 12, COL_LCD);
    gfx_seg7_str(fb_top, 188, 176, a[0] == ' ' && a[1] == ' ' ? a + 1 : a, 3, COL_AMBER, COL_GHOST);
    pct(a, astats.avg_pct[ST_ACID]);
    pct(b, astats.avg_pct[ST_DRUMS]);
    pct(c, (uint16_t)(astats.avg_pct[ST_DRIVE] + astats.avg_pct[ST_CRUSH] + astats.avg_pct[ST_DELAY] +
                      astats.avg_pct[ST_COMB] + astats.avg_pct[ST_LIMIT] + astats.avg_pct[ST_OUT]));
    pct(d, (uint16_t)((SH->arm7_ticks_max * 10000u) >> 11));
    txt(&con_top, 23, 1, T_DIM, "AC %-4s DR %-4s FX %-4s A7 %-3s", a, b, c, d);   /* <= col 30: no scroll */
}

void top_init(void)
{
    int s;
    /* the face plate */
    gfx_plate(fb_top, 0, 0, 256, 192, COL_STEEL_D);
    gfx_hazard(fb_top, 0, 0, 256, 4);
    gfx_plate(fb_top, 2, 5, 252, 21, COL_STEEL);
    gfx_bevel(fb_top, 2, 5, 252, 21, 1);
    gfx_rivet(fb_top, 7, 15);
    gfx_rivet(fb_top, 248, 15);
    gfx_well(fb_top, 203, 7, 46, 17, COL_LCD);
    gfx_rust(fb_top, 150, 22, 6, 1);
    gfx_rust(fb_top, 30, 6, 4, 2);

    gfx_well(fb_top, 5, 28, 246, 83, COL_LCD);            /* pattern display */
    gfx_plate(fb_top, 2, 112, 252, 25, COL_STEEL);
    gfx_bevel(fb_top, 2, 112, 252, 25, 1);
    gfx_rust(fb_top, 240, 135, 5, 3);
    gfx_well(fb_top, 3, SCOPE_Y - 2, 250, SCOPE_H + 4, COL_LCD);
    gfx_plate(fb_top, 2, 173, 252, 19, COL_STEEL);
    gfx_bevel(fb_top, 2, 173, 252, 19, 1);
    gfx_well(fb_top, 32, 175, 148, 10, COL_LCD);
    gfx_well(fb_top, 184, 174, 54, 15, COL_LCD);
    gfx_rivet(fb_top, 245, 181);
    gfx_rust(fb_top, 12, 189, 5, 4);
    gfx_rust(fb_top, 120, 3, 3, 5);
    for (s = 0; s < 16; s++) gfx_led(fb_top, COLX(s) + 7, LED_Y, 0, COL_AMBER);
    memcpy(topbg, fb_top, sizeof topbg);

    txt(&con_top, 2, 21, T_DIM, "BPM");
    txt(&con_top, 22, 1, T_DIM, "CPU");
    txt(&con_top, 22, 30, T_DIM, "%%");
    memset(col_sig, 0, sizeof col_sig);
    snap_valid = 0;
    last_bpm = last_led = last_play = last_fill = last_cpu = -1;
    last_led = -2;
}

void top_frame(void)
{
    frames++;
    draw_pattern();
    draw_leds();
    if (frames & 1) draw_scope();
    else {
        draw_status();
        draw_bpm();
        if ((frames & 15) == 0) draw_cpu();
    }
}
