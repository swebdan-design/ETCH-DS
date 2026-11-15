/*
 * ETCH DS -- by Synth Centric
 * Top screen (always visible).  Layout, in pixels (6 px margins):
 *
 *   y   6-12   ETCH DS, song name, seed code
 *   y  22-103  the pattern: bass line as pitch (1 px per semitone, C1 at the
 *              bottom), ETCH's four drum lanes; playhead column lit; steps
 *              CORROSION just changed glow green and fade (~1.5 s)
 *   y 106-107  step strip (one segment per 16th)
 *   y 112-131  MODE / PATTERN / GEN / NEXT EVO / TEMPO
 *   y 136-165  oscilloscope
 *   y 170-186  DSP load: bar, total, per stage
 * Each element redraws only when what it shows changes.
 */
#include <stdio.h>
#include <string.h>
#include "corrosion.h"
#include "gfx.h"
#include "app.h"
#include "audio.h"

#define COLX(s)  (8 + (s) * 15)
#define BASS_Y0  25
#define BASS_Y1  72          /* note 0 (C1) sits on this row */
#define LANE_Y0  76
#define STRIP_Y  106
#define STAT_Y   112
#define SCOPE_Y  136
#define SCOPE_H  30
#define CPU_Y    170

static uint16_t glow[16];
static pattern_t snap;
static uint8_t snap_commits, snap_valid, snap_pat = 0xFF;
static uint32_t col_sig[16], stat_sig = 0xFFFFFFFF, head_sig = 0xFFFFFFFF;
static uint16_t sbuf[244 * SCOPE_H];
static uint32_t frames;
static int last_strip = -2, last_cpu = -1;

static int note_y(int n) { if (n > 47) n = 47; if (n < 0) n = 0; return BASS_Y1 - n; }

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
    uint16_t bg = play ? COL_PLAY : (s & 3) == 0 ? COL_BEAT : COL_SURF, grid;
    if (g) bg = gfx_mix(bg, COL_GLOW, g);
    grid = gfx_mix(bg, COL_BORDER, 110);
    gfx_rect(fb_top, x, BASS_Y0, 14, BASS_Y1 - BASS_Y0 + 2, bg);
    gfx_hline(fb_top, x, note_y(12), 14, grid);
    gfx_hline(fb_top, x, note_y(24), 14, grid);
    gfx_hline(fb_top, x, note_y(36), 14, grid);
    if (t == STEP_NOTE) {
        int y = note_y(p->note[s]);
        uint16_t c = (f & STEP_ACCENT) ? COL_HOT : COL_NOTE;
        if (g) c = gfx_mix(c, COL_EVOLVE, g);
        if (SH->proj.mutes & 1) c = COL_TEXT3;
        gfx_vline(fb_top, x + 7, y + 2, BASS_Y1 - y, gfx_mix(bg, c, 60));
        gfx_rrect(fb_top, x + 1, y - 1, 12, 3, c);
        if (f & STEP_SLIDE) gfx_line(fb_top, x + 12, y, x + 15, note_y(p->note[(s + 1) & 15]), c);
    } else if (t == STEP_TIE) {
        gfx_rect(fb_top, x, note_y(prev_sounding(p, s)) - 1, 15, 3, gfx_mix(COL_NOTE, bg, 140));
    }
    for (l = 0; l < NUM_LANES; l++) {
        int y = LANE_Y0 + l * 7;
        int on = (p->drum[l] >> s) & 1, muted = (SH->proj.mutes >> (l + 1)) & 1;
        uint16_t c = on ? (muted ? COL_TEXT3 : l == 0 ? COL_HOT : COL_NOTE) : (play ? COL_SURF3 : COL_SURF2);
        if (g && on) c = gfx_mix(c, COL_EVOLVE, g);
        gfx_rect(fb_top, x, y, 14, 7, COL_SURF);
        gfx_rect(fb_top, x + 1, y, 12, 5, c);
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
    {   /* step strip */
        int cur = playing ? step : -1;
        if (cur != last_strip) {
            for (s = 0; s < 16; s++)
                gfx_rect(fb_top, COLX(s) + 1, STRIP_Y, 12, 2, s == cur ? COL_TEXT : (s & 3) == 0 ? COL_SURF3 : COL_SURF2);
            last_strip = cur;
        }
    }
}

static void draw_header(void)
{
    const project_t *pr = &SH->proj;
    char code[13], b[20];
    uint32_t sig = 0;
    int i, x;
    corr_code_encode(&pr->gen, code);
    code[12] = 0;
    for (i = 0; i < 12; i++) sig = sig * 31 + (uint8_t)code[i];
    for (i = 0; i < PROJECT_NAME_LEN; i++) sig = sig * 31 + (uint8_t)pr->name[i];
    if (sig == head_sig) return;
    head_sig = sig;
    gfx_rect(fb_top, 0, 0, 256, 20, COL_BG);
    x = gfx_text(fb_top, 6, 7, F_MAIN, COL_TEXT, "ETCH");
    x = gfx_text(fb_top, x + 4, 7, F_MAIN, COL_TEXT3, "DS");
    memcpy(b, pr->name, PROJECT_NAME_LEN);
    b[PROJECT_NAME_LEN] = 0;
    for (i = PROJECT_NAME_LEN - 1; i >= 0 && b[i] == ' '; i--) b[i] = 0;
    gfx_vline(fb_top, x + 6, 7, 7, COL_BORDER);
    gfx_text(fb_top, x + 12, 7, F_MAIN, COL_TEXT2, b);
    sprintf(b, "%.4s-%.4s-%.4s", code, code + 4, code + 8);
    gfx_text_r(fb_top, 250, 7, F_MAIN, COL_TEXT2, b);
}

static void stat(int i, const char *cap, const char *val, uint16_t vc)
{
    int x = 6 + i * 49;
    if (i) gfx_vline(fb_top, x - 2, STAT_Y + 2, 16, COL_SURF2);
    gfx_text(fb_top, x + 5, STAT_Y + 3, F_SMALL, COL_TEXT2, cap);
    gfx_text(fb_top, x + 5, STAT_Y + 10, F_MAIN, vc, val);
}

static void draw_stats(void)
{
    const project_t *pr = &SH->proj;
    uint8_t bars = bars_to_evolution(SH), play = SH->playing;
    uint32_t sig = (uint32_t)pr->gen.mode | ((uint32_t)SH->cur << 2) | ((uint32_t)pr->pat[SH->cur].gen << 8) |
                   ((uint32_t)bars << 16) | ((uint32_t)play << 20) | ((uint32_t)pr->synth.bpm << 21) |
                   ((uint32_t)(SH->fill != 0) << 29) | (play ? 0 : ((frames >> 5) & 1) << 30);
    char b[12];
    if (sig == stat_sig) return;
    stat_sig = sig;
    gfx_rect(fb_top, 0, STAT_Y, 256, 20, COL_BG);
    if (!play) {
        gfx_text_c(fb_top, 128, STAT_Y + 7, F_MAIN, (frames >> 5) & 1 ? COL_TEXT : COL_TEXT3, "PRESS START");
        return;
    }
    stat(0, "MODE", corr_mode_names[pr->gen.mode % EVO_COUNT], COL_TEXT);
    sprintf(b, "%02d", SH->cur);
    stat(1, SH->fill ? "PATTERN  FILL" : "PATTERN", b, SH->fill ? COL_ACCENT : COL_TEXT);
    sprintf(b, "%03d", pr->pat[SH->cur].gen);
    stat(2, "GENERATION", b, COL_TEXT);
    if (bars) sprintf(b, "%d BAR%s", bars, bars == 1 ? "" : "S");
    else strcpy(b, "-");
    stat(3, "NEXT EVO", b, COL_TEXT);
    sprintf(b, "%d", pr->synth.bpm);
    stat(4, "TEMPO", b, COL_TEXT);
}

static void draw_scope(void)
{
    static int16_t smp[244];
    uint32_t n = audio_scope(smp, 236), i;
    int prev = SCOPE_H / 2;
    for (i = 0; i < 244 * SCOPE_H; i++) sbuf[i] = COL_SURF;
    for (i = 4; i < 240; i += 2) sbuf[(SCOPE_H / 2) * 244 + i] = COL_SURF2;
    for (i = 0; i < n; i++) {
        int x = (int)i + 4, y = SCOPE_H / 2 - ((smp[i] * (SCOPE_H / 2 - 3)) >> 15), a, b;
        if (y < 2) y = 2;
        if (y > SCOPE_H - 3) y = SCOPE_H - 3;
        a = prev < y ? prev : y;
        b = prev < y ? y : prev;
        for (; a <= b; a++) {
            sbuf[a * 244 + x] = COL_TEXT;
            sbuf[(a + 1) * 244 + x] = gfx_mix(sbuf[(a + 1) * 244 + x], COL_TEXT, 50);
        }
        prev = y;
    }
    /* rounded corners */
    sbuf[0] = sbuf[1] = sbuf[244] = sbuf[242] = sbuf[243] = sbuf[244 + 243] = COL_BG;
    sbuf[(SCOPE_H - 1) * 244] = sbuf[(SCOPE_H - 1) * 244 + 1] = sbuf[(SCOPE_H - 2) * 244] = COL_BG;
    sbuf[(SCOPE_H - 1) * 244 + 243] = sbuf[(SCOPE_H - 1) * 244 + 242] = sbuf[(SCOPE_H - 2) * 244 + 243] = COL_BG;
    for (i = 0; i < SCOPE_H; i++) memcpy(fb_top + (SCOPE_Y + i) * 256 + 6, sbuf + i * 244, 244 * 2);
}

static void pctf(char *o, uint16_t v) { sprintf(o, "%d.%d", v / 100, (v % 100) / 10); }

static void draw_cpu(void)
{
    char a[8], b[8], c[8], d[8], line[64];
    int w, m, sig = astats.avg_total * 3 + astats.max_total_pct + (int)astats.windows * 7;
    if (sig == last_cpu) return;
    last_cpu = sig;
    gfx_rect(fb_top, 0, CPU_Y, 256, 22, COL_BG);
    gfx_text(fb_top, 6, CPU_Y + 2, F_SMALL, COL_TEXT2, "DSP");
    gfx_rrect(fb_top, 24, CPU_Y + 2, 180, 5, COL_SURF2);
    w = (astats.avg_total * 180) / 10000;
    m = (astats.max_total_pct * 180) / 10000;
    if (w > 180) w = 180;
    if (w > 2) gfx_rrect(fb_top, 24, CPU_Y + 2, w, 5, astats.avg_total > 7000 ? COL_TEXT : COL_NOTE);
    if (m > 1 && m < 180) gfx_vline(fb_top, 24 + m, CPU_Y + 1, 7, COL_TEXT);
    pctf(a, astats.avg_total);
    sprintf(line, "%s%%", a);
    gfx_text_r(fb_top, 250, CPU_Y + 1, F_MAIN, COL_TEXT, line);
    pctf(a, astats.avg_pct[ST_ACID]);
    pctf(b, astats.avg_pct[ST_DRUMS]);
    pctf(c, (uint16_t)(astats.avg_pct[ST_DRIVE] + astats.avg_pct[ST_CRUSH] + astats.avg_pct[ST_DELAY] +
                       astats.avg_pct[ST_COMB] + astats.avg_pct[ST_LIMIT] + astats.avg_pct[ST_OUT]));
    pctf(d, (uint16_t)((SH->arm7_ticks_max * 10000u) >> 11));
    sprintf(line, "ACID %s   DRUMS %s   FX %s   ARM7 %s", a, b, c, d);
    gfx_text(fb_top, 6, CPU_Y + 12, F_SMALL, COL_TEXT3, line);
    if (SH->dropouts) {
        sprintf(line, "DROPOUTS %lu", (unsigned long)SH->dropouts);
        gfx_text_r(fb_top, 250, CPU_Y + 12, F_SMALL, COL_RED, line);
    }
}

void top_init(void)
{
    gfx_clear(fb_top, COL_BG);
    gfx_rrect(fb_top, 6, 22, 244, 82, COL_SURF);
    memset(col_sig, 0, sizeof col_sig);
    snap_valid = 0;
    stat_sig = head_sig = 0xFFFFFFFF;
    last_strip = -2;
    last_cpu = -1;
}

void top_frame(void)
{
    frames++;
    draw_pattern();
    if (frames & 1) draw_scope();
    else {
        draw_header();
        draw_stats();
        if ((frames & 15) == 0) draw_cpu();
    }
}
