/*
 * ETCH DS -- by Synth Centric
 * Bottom screen: tabs LIVE / XY / PANEL / GEN.
 *
 * Touch is for performing, buttons are for editing, and every tab speaks
 * ETCH's grammar: the D-pad moves a cursor, A places or toggles, A + D-pad
 * changes a value, B + A deletes.  Global: START play/stop, SELECT + LEFT /
 * RIGHT changes tab (or tap the tab bar), SELECT + A evolves at the next bar,
 * SELECT + B undoes the last generation, hold R to latch a fill, hold L while
 * dragging to make the move temporary.  Touch is single-point everywhere and
 * never needed for sound.
 *
 *   LIVE   the pattern: drag a bass step to set its pitch (tap it to rest),
 *          tap the A/S strip under it to cycle accent / slide, tap or swipe
 *          drum cells, tap a row name to mute; pads FILL, REGEN, EVOLVE, UNDO
 *   XY     X = cutoff, Y = resonance, with a fading trail
 *   PANEL  knobs for ACID, tempo and the FX (patch cables come later)
 *   GEN    CORROSION's settings, the seed-code pad, GEN ALL / BASS / DRUMS,
 *          the mutation dial
 */
#include <stdio.h>
#include <string.h>
#include "corrosion.h"
#include "gfx.h"
#include "app.h"
#include "audio.h"
#include "dsp_tables.h"

enum { TAB_LIVE, TAB_XY, TAB_PANEL, TAB_GEN, TAB_COUNT };
static const char * const tab_names[TAB_COUNT] = { "LIVE", "XY", "PANEL", "GEN" };
static uint8_t tab = TAB_LIVE, need_full = 1;
uint8_t ui_stress;
static uint32_t frame;

/* touch state */
static uint8_t t_active, t_on_tabs, t_temp, t_moved;
static int t_x, t_y, t_sx, t_sy;

static const char * const notes12[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };

/* the current tab's static art, for redrawing parts of it */
static uint16_t tabbg[256 * 192];
static void snapshot_bot(void) { memcpy(tabbg, fb_bot, sizeof tabbg); }
static void restore_bot(int x, int y, int w, int h)
{
    int j;
    if (x < 0) { w += x; x = 0; }
    if (x + w > 256) w = 256 - x;
    for (j = 0; j < h; j++)
        if (y + j >= 0 && y + j < 192)
            memcpy(fb_bot + (y + j) * 256 + x, tabbg + (y + j) * 256 + x, (size_t)w * 2);
}

/* ============================== tab bar =================================== */
static void draw_tabs(void)
{
    int i;
    for (i = 0; i < TAB_COUNT; i++) {
        int x = i * 64, len = (int)strlen(tab_names[i]), act = i == tab;
        gfx_button(fb_bot, x + 1, 1, 62, 20, act);
        gfx_led(fb_bot, x + 10, 11, act, COL_AMBER);
        txt(&con_bot, 1, i * 8 + 4 - len / 2 + 1, act ? T_HOT : T_BRIGHT, "%s", tab_names[i]);
    }
    gfx_hazard(fb_bot, 0, 22, 256, 3);
}

/* ================================ LIVE ==================================== */
#define LX0     32
#define LCW     14
#define BASS_T  26
#define BASS_B  73          /* note 0 (C1) sits on this row */
#define STRIP_Y 74
#define LANE_Y(l) (80 + (l) * 16)
#define PAD_Y   148

static int8_t lv_col;               /* -1 = row label (mute) */
static uint8_t lv_row;              /* 0 bass, 1-4 lanes, 5 pads */
static uint8_t last_note = 21;      /* A2: what A writes on an empty step (ETCH) */
static uint32_t lv_sig[5][16], lv_label_sig;
static uint8_t pad_flash[4];
static int8_t t_step, t_lane, t_paint;
static uint8_t t_kind;              /* 1 bass, 2 strip, 3 lane, 4 pad, 5 label */
static uint8_t t_start_note, t_start_flags;

static const char * const lane_names[4] = { "KICK", "SNR", "HAT", "OPEN" };
static const char * const pad_names[4] = { "FILL", "REGEN", "EVOLVE", "UNDO" };

static int bass_y(int n) { if (n > 47) n = 47; if (n < 0) n = 0; return BASS_B - n; }

static void live_bass_cell(const pattern_t *p, int s, int play, int cur)
{
    int x = LX0 + s * LCW;
    uint8_t f = p->flags[s], t = f & STEP_TYPE_MASK;
    uint16_t bg = play ? C(7, 4, 1) : ((s & 3) == 0 ? C(3, 3, 3) : COL_LCD), grid = gfx_mix(bg, COL_GHOST, 200);
    uint16_t c = (f & STEP_ACCENT) ? C(31, 28, 18) : COL_AMBER;
    if (SH->proj.mutes & 1) c = C(9, 6, 3);
    gfx_rect(fb_bot, x, BASS_T, LCW - 1, BASS_B - BASS_T + 2, bg);
    gfx_hline(fb_bot, x, bass_y(12), LCW - 1, grid);
    gfx_hline(fb_bot, x, bass_y(24), LCW - 1, grid);
    gfx_hline(fb_bot, x, bass_y(36), LCW - 1, grid);
    if (t == STEP_NOTE) {
        int y = bass_y(p->note[s]);
        gfx_vline(fb_bot, x + 6, y + 2, BASS_B - y, gfx_mix(bg, c, 70));
        gfx_rect(fb_bot, x + 1, y - 2, LCW - 3, 5, gfx_mix(bg, c, 90));
        gfx_rect(fb_bot, x + 1, y - 1, LCW - 3, 3, c);
        if (f & STEP_SLIDE) gfx_line(fb_bot, x + LCW - 3, y, x + LCW - 1, bass_y(p->note[(s + 1) & 15]), c);
    } else if (t == STEP_TIE) {
        int k, n = p->note[s];
        for (k = 1; k < 16; k++)
            if ((p->flags[(s - k) & 15] & STEP_TYPE_MASK) == STEP_NOTE) { n = p->note[(s - k) & 15]; break; }
        gfx_rect(fb_bot, x, bass_y(n) - 1, LCW, 3, gfx_mix(COL_AMBER, bg, 120));
    }
    /* A/S strip: accent = hot block, slide = ramp */
    gfx_rect(fb_bot, x, STRIP_Y, LCW - 1, 5, C(2, 2, 2));
    if (t == STEP_NOTE && (f & STEP_ACCENT)) gfx_rect(fb_bot, x + 1, STRIP_Y + 1, 5, 3, C(31, 28, 18));
    if (t == STEP_NOTE && (f & STEP_SLIDE)) gfx_line(fb_bot, x + 7, STRIP_Y + 3, x + 11, STRIP_Y + 1, COL_AMBER);
    if (cur) gfx_frame(fb_bot, x, BASS_T, LCW - 1, STRIP_Y + 5 - BASS_T, COL_SULFUR);
}

static void live_lane_cell(const pattern_t *p, int l, int s, int play, int cur)
{
    int x = LX0 + s * LCW, y = LANE_Y(l), on = (p->drum[l] >> s) & 1, muted = (SH->proj.mutes >> (l + 1)) & 1;
    uint16_t lit = muted ? C(9, 6, 3) : l == 0 ? C(31, 12, 2) : COL_AMBER;
    gfx_rect(fb_bot, x, y, LCW - 1, 14, play ? C(7, 4, 1) : COL_LCD);
    if (on) {                                          /* lit segment pad */
        gfx_rect(fb_bot, x + 1, y + 1, LCW - 3, 12, gfx_mix(COL_LCD, lit, 110));
        gfx_rect(fb_bot, x + 2, y + 2, LCW - 5, 10, lit);
        gfx_hline(fb_bot, x + 3, y + 3, LCW - 7, gfx_shade(lit, 6));
    } else {                                           /* dark rubber */
        gfx_rect(fb_bot, x + 2, y + 2, LCW - 5, 10, play ? C(10, 6, 2) : C(4, 3, 3));
        gfx_hline(fb_bot, x + 2, y + 2, LCW - 5, C(7, 6, 6));
    }
    if (cur) gfx_frame(fb_bot, x, y, LCW - 1, 14, COL_SULFUR);
}

static void live_pad(int i, int lit, int cur, int pressed)
{
    int x = i * 64 + 2;
    restore_bot(x, PAD_Y, 60, 40);
    gfx_button(fb_bot, x, PAD_Y, 60, 40, pressed);
    if (i == 1 || i == 2) gfx_hazard(fb_bot, x + 4, PAD_Y + 4 + pressed, 52, 4);   /* these rewrite music */
    gfx_led(fb_bot, x + 30, PAD_Y + 31, lit, i == 1 || i == 2 ? C(31, 8, 2) : COL_AMBER);
    if (cur) gfx_frame(fb_bot, x - 1, PAD_Y - 1, 62, 42, COL_SULFUR);
}

static void live_draw_static(void)
{
    int i;
    gfx_well(fb_bot, LX0 - 1, BASS_T - 1, 256 - LX0 + 1, STRIP_Y + 6 - BASS_T + 1, COL_LCD);
    gfx_well(fb_bot, LX0 - 1, LANE_Y(0) - 1, 256 - LX0 + 1, 4 * 16 + 1, COL_LCD);
    snapshot_bot();
    for (i = 0; i < 4; i++) {
        live_pad(i, 0, 0, 0);
        txt(&con_bot, 20, i * 8 + 4 - (int)strlen(pad_names[i]) / 2, T_BRIGHT, "%s", pad_names[i]);
    }
    txt(&con_bot, 9, 0, T_DIM, "A/S");
    memset(lv_sig, 0, sizeof lv_sig);
    lv_label_sig = 0xFFFFFFFF;
}

static void live_frame(void)
{
    const pattern_t *p = &SH->proj.pat[SH->cur];
    int s, l, step = SH->step, playing = SH->playing;
    uint32_t lsig = SH->proj.mutes | ((uint32_t)(lv_col < 0) << 8) | ((uint32_t)lv_row << 9);
    if (lsig != lv_label_sig) {                    /* row names: dim when muted */
        int sel = lv_col < 0;
        txt(&con_bot, 5, 0, (SH->proj.mutes & 1) ? T_DIM : (sel && lv_row == 0 ? T_SULFUR : T_HOT), "BASS");
        for (l = 0; l < 4; l++)
                txt(&con_bot, 10 + 2 * l, 0, ((SH->proj.mutes >> (l + 1)) & 1) ? T_DIM :
                (sel && lv_row == l + 1 ? T_SULFUR : T_BRIGHT), "%-4s", lane_names[l]);
        lv_label_sig = lsig;
    }
    for (s = 0; s < 16; s++) {
        int play = playing && s == step;
        int cur0 = lv_row == 0 && lv_col == s;
        uint32_t sig = 0x80000000u | p->note[s] | ((uint32_t)(p->flags[s] & 15) << 6) |
                       ((uint32_t)play << 10) | ((uint32_t)cur0 << 11) | ((uint32_t)(SH->proj.mutes & 1) << 12);
        if (p->flags[s] & STEP_SLIDE) sig ^= (uint32_t)p->note[(s + 1) & 15] << 16;
        if ((p->flags[s] & STEP_TYPE_MASK) == STEP_TIE) sig ^= (uint32_t)p->note[(s + 15) & 15] << 22 | (frame & ~63u);
        if (sig != lv_sig[0][s]) { live_bass_cell(p, s, play, cur0); lv_sig[0][s] = sig; }
        for (l = 0; l < 4; l++) {
            int cur = lv_row == l + 1 && lv_col == s;
            uint32_t ls = 0x80000000u | ((p->drum[l] >> s) & 1) | ((uint32_t)play << 1) | ((uint32_t)cur << 2) |
                          ((uint32_t)((SH->proj.mutes >> (l + 1)) & 1) << 3);
            if (ls != lv_sig[l + 1][s]) { live_lane_cell(p, l, s, play, cur); lv_sig[l + 1][s] = ls; }
        }
    }
    for (l = 0; l < 4; l++) {                       /* pads: flash + cursor */
        static uint8_t pad_state[4] = { 255, 255, 255, 255 };
        uint8_t st = (uint8_t)((pad_flash[l] ? 1 : 0) | ((lv_row == 5 && lv_col == l) ? 2 : 0) |
                               (l == 0 && SH->fill ? 4 : 0));
        if (st != pad_state[l]) {
            live_pad(l, (st & 5) != 0, (st & 2) != 0, st & 1);
            txt(&con_bot, 20, l * 8 + 4 - (int)strlen(pad_names[l]) / 2, (st & 5) ? T_HOT : T_BRIGHT, "%s", pad_names[l]);
            pad_state[l] = st;
        }
        if (pad_flash[l]) pad_flash[l]--;
    }
}

static void pad_action(int i)
{
    pad_flash[i] = 8;
    switch (i) {
    case 0: app_fill(); break;
    case 1: app_regen(GEN_ALL); break;
    case 2: app_evolve(); break;
    default: app_undo(); break;
    }
}

static uint8_t snap_note(int n)
{
    if (n < 0) n = 0;
    if (n > NOTE_MAX) n = NOTE_MAX;
    return corr_snap((uint8_t)n, &SH->proj.gen);
}

static void live_touch(int phase)          /* 0 down, 1 drag, 2 up */
{
    const pattern_t *p = &SH->proj.pat[SH->cur];
    int s = (t_x - LX0) / LCW;
    if (phase == 0) {
        t_kind = 0;
        if (t_y >= PAD_Y) { int i = t_x / 64; t_kind = 4; lv_row = 5; lv_col = (int8_t)i; pad_action(i); return; }
        if (t_x < LX0) {
            int bit = t_y < 80 ? 0 : (t_y - 80) / 16 + 1;
            if (bit <= 4) { app_toggle_mute((uint8_t)bit); t_kind = 5; }
            return;
        }
        if (s > 15) s = 15;
        t_step = (int8_t)s;
        if (t_y >= BASS_T && t_y < STRIP_Y) {
            uint8_t f = p->flags[s];
            t_kind = 1; lv_row = 0; lv_col = (int8_t)s;
            t_start_note = p->note[s];
            t_start_flags = f;
            if ((f & STEP_TYPE_MASK) != STEP_NOTE) {
                uint8_t n = snap_note(BASS_B - t_y);
                last_note = n;
                app_set_bass((uint8_t)s, n, (uint8_t)(STEP_NOTE | (f & STEP_LOCK)));
                t_moved = 1;
            }
        } else if (t_y >= STRIP_Y && t_y < 80) {
            uint8_t f = p->flags[s];
            if ((f & STEP_TYPE_MASK) == STEP_NOTE) {        /* none -> A -> S -> A+S */
                uint8_t as = (uint8_t)((f & STEP_ACCENT ? 1 : 0) | (f & STEP_SLIDE ? 2 : 0));
                as = (uint8_t)((as + 1) & 3);
                f = (uint8_t)((f & ~(STEP_ACCENT | STEP_SLIDE)) | (as & 1 ? STEP_ACCENT : 0) | (as & 2 ? STEP_SLIDE : 0));
                app_set_bass((uint8_t)s, p->note[s], f);
            }
            t_kind = 2;
        } else if (t_y >= 80 && t_y < 144) {
            int l = (t_y - 80) / 16;
            t_kind = 3; t_lane = (int8_t)l; lv_row = (uint8_t)(l + 1); lv_col = (int8_t)s;
            t_paint = (int8_t)!((p->drum[l] >> s) & 1);
            app_toggle_drum((uint8_t)l, (uint8_t)s);
        }
    } else if (phase == 1) {
        if (t_kind == 1) {
            int y = t_y < BASS_T ? BASS_T : t_y > BASS_B ? BASS_B : t_y;
            uint8_t n = snap_note(BASS_B - y);
            if (n != p->note[t_step] && (p->flags[t_step] & STEP_TYPE_MASK) == STEP_NOTE &&
                (t_moved || (t_y - t_sy > 3 || t_sy - t_y > 3))) {
                app_set_bass((uint8_t)t_step, n, p->flags[t_step]);
                last_note = n;
                t_moved = 1;
            }
        } else if (t_kind == 3 && s >= 0 && s <= 15 && s != t_step) {
            t_step = (int8_t)s;                               /* swipe paints */
            if ((int)((p->drum[t_lane] >> s) & 1) != t_paint) app_toggle_drum((uint8_t)t_lane, (uint8_t)s);
        }
    } else if (t_kind == 1 && !t_moved && (t_start_flags & STEP_TYPE_MASK) == STEP_NOTE) {
        int ny = bass_y(t_start_note);
        if (t_sy - ny <= 5 && ny - t_sy <= 5)                  /* tapped the note: rest */
            app_set_bass((uint8_t)t_step, t_start_note, (uint8_t)(t_start_flags & STEP_LOCK));
    }
}

static void live_keys(uint32_t down, uint32_t held, uint32_t rep)
{
    const pattern_t *p = &SH->proj.pat[SH->cur];
    if (held & KEY_A) {
        if (lv_row == 0 && lv_col >= 0) {
            uint8_t f = p->flags[lv_col], n = p->note[lv_col];
            if ((f & STEP_TYPE_MASK) == STEP_NOTE) {
                if (rep & KEY_UP) { n = corr_scale_step(n, 1, &SH->proj.gen); app_set_bass((uint8_t)lv_col, n, f); last_note = n; }
                if (rep & KEY_DOWN) { n = corr_scale_step(n, -1, &SH->proj.gen); app_set_bass((uint8_t)lv_col, n, f); last_note = n; }
                if (down & KEY_RIGHT) app_set_bass((uint8_t)lv_col, n, f ^ STEP_ACCENT);
                if (down & KEY_LEFT) app_set_bass((uint8_t)lv_col, n, f ^ STEP_SLIDE);
            }
        }
        if (down & KEY_A) {                                    /* A places or toggles */
            if (held & KEY_B) {                                /* B + A deletes       */
                if (lv_row == 0 && lv_col >= 0) app_set_bass((uint8_t)lv_col, p->note[lv_col], p->flags[lv_col] & STEP_LOCK);
                else if (lv_row >= 1 && lv_row <= 4 && lv_col >= 0 && ((p->drum[lv_row - 1] >> lv_col) & 1))
                    app_toggle_drum((uint8_t)(lv_row - 1), (uint8_t)lv_col);
            } else if (lv_row == 5) pad_action(lv_col < 0 ? 0 : lv_col > 3 ? 3 : lv_col);
            else if (lv_col < 0) app_toggle_mute(lv_row);
            else if (lv_row == 0) {
                if ((p->flags[lv_col] & STEP_TYPE_MASK) != STEP_NOTE)
                    app_set_bass((uint8_t)lv_col, last_note, (uint8_t)(STEP_NOTE | (p->flags[lv_col] & STEP_LOCK)));
            } else app_toggle_drum((uint8_t)(lv_row - 1), (uint8_t)lv_col);
        }
        return;
    }
    if ((rep & KEY_UP) && lv_row > 0) lv_row--;
    if ((rep & KEY_DOWN) && lv_row < 5) lv_row++;
    if (lv_row == 5) {
        if (lv_col < 0) lv_col = 0;
        if (lv_col > 3) lv_col = 3;
        if ((rep & KEY_LEFT) && lv_col > 0) lv_col--;
        if ((rep & KEY_RIGHT) && lv_col < 3) lv_col++;
    } else {
        if ((rep & KEY_LEFT) && lv_col > -1) lv_col--;
        if ((rep & KEY_RIGHT) && lv_col < 15) lv_col++;
    }
}

/* ================================= XY ===================================== */
#define XY_X0 16
#define XY_X1 239
#define XY_Y0 30
#define XY_Y1 170
static int16_t trail_x[24], trail_y[24];
static uint8_t trail_age[24], trail_head, xy_dirty;
static uint8_t xy_saved_cut, xy_saved_res;
static int xy_last_cut = -1, xy_last_res = -1;

static void xy_draw_static(void)
{
    int x, y;
    gfx_well(fb_bot, XY_X0 - 1, XY_Y0 - 1, XY_X1 - XY_X0 + 3, XY_Y1 - XY_Y0 + 3, COL_LCD);
    for (y = XY_Y0; y <= XY_Y1; y++)                 /* phosphor graticule */
        for (x = XY_X0; x <= XY_X1; x++) {
            int gx = (x - XY_X0) % 28 == 0, gy = (y - XY_Y0) % 20 == 0;
            if ((gx && (y & 1)) || (gy && (x & 1))) fb_bot[y * 256 + x] = C(5, 3, 1);
            if (gx && gy) fb_bot[y * 256 + x] = C(10, 6, 2);
        }
    gfx_well(fb_bot, 72, 176, 34, 14, COL_LCD);
    gfx_well(fb_bot, 166, 176, 34, 14, COL_LCD);
    snapshot_bot();
    txt(&con_bot, 23, 2, T_BRIGHT, "CUTOFF");
    txt(&con_bot, 23, 15, T_BRIGHT, "RES");
    txt(&con_bot, 23, 27, T_DIM, "L:TMP");
    xy_dirty = 1;
    xy_last_cut = -1;
}

static void xy_frame(void)
{
    int i, x, y;
    int any = xy_dirty;
    char b[4];
    for (i = 0; i < 24; i++) if (trail_age[i]) { trail_age[i]--; any = 1; }
    if (acid_p.cutoff != xy_last_cut || acid_p.res != xy_last_res) any = 1;
    if (!any) return;
    xy_dirty = 0;
    restore_bot(XY_X0, XY_Y0, XY_X1 - XY_X0 + 1, XY_Y1 - XY_Y0 + 1);
    for (i = 0; i < 24; i++)
        if (trail_age[i]) gfx_disc(fb_bot, trail_x[i], trail_y[i], 1 + trail_age[i] / 20,
                                   gfx_mix(COL_LCD, COL_AMBER, trail_age[i] * 3));
    x = XY_X0 + (acid_p.cutoff * (XY_X1 - XY_X0)) / 127;
    y = XY_Y1 - (acid_p.res * (XY_Y1 - XY_Y0)) / 127;
    for (i = XY_X0; i <= XY_X1; i += 2) fb_bot[y * 256 + i] = C(16, 9, 2);
    for (i = XY_Y0; i <= XY_Y1; i += 2) fb_bot[i * 256 + x] = C(16, 9, 2);
    gfx_ring(fb_bot, x, y, 7, gfx_mix(COL_LCD, COL_AMBER, 120));
    gfx_disc(fb_bot, x, y, 3, t_active && t_temp ? COL_SULFUR : C(31, 26, 14));
    sprintf(b, "%03d", acid_p.cutoff);
    gfx_seg7_str(fb_bot, 75, 178, b, 3, COL_AMBER, COL_GHOST);
    sprintf(b, "%03d", acid_p.res);
    gfx_seg7_str(fb_bot, 169, 178, b, 3, COL_AMBER, COL_GHOST);
    xy_last_cut = acid_p.cutoff;
    xy_last_res = acid_p.res;
}

static void xy_touch(int phase)
{
    if (phase == 0) { xy_saved_cut = acid_p.cutoff; xy_saved_res = acid_p.res; }
    if (phase < 2) {
        int cut = ((t_x - XY_X0) * 127) / (XY_X1 - XY_X0);
        int res = ((XY_Y1 - t_y) * 127) / (XY_Y1 - XY_Y0);
        acid_p.cutoff = (uint8_t)(cut < 0 ? 0 : cut > 127 ? 127 : cut);
        acid_p.res = (uint8_t)(res < 0 ? 0 : res > 127 ? 127 : res);
        trail_x[trail_head] = (int16_t)(t_x < XY_X0 + 3 ? XY_X0 + 3 : t_x > XY_X1 - 3 ? XY_X1 - 3 : t_x);
        trail_y[trail_head] = (int16_t)(t_y < XY_Y0 + 3 ? XY_Y0 + 3 : t_y > XY_Y1 - 3 ? XY_Y1 - 3 : t_y);
        trail_age[trail_head] = 60;
        trail_head = (uint8_t)((trail_head + 1) % 24);
    } else if (t_temp) {
        acid_p.cutoff = xy_saved_cut;                  /* L: the move was temporary */
        acid_p.res = xy_saved_res;
    }
    xy_dirty = 1;
}

static void xy_keys(uint32_t down, uint32_t held, uint32_t rep)
{
    int c = acid_p.cutoff, r = acid_p.res;
    (void)down;
    if (!(held & KEY_A)) return;
    if (rep & KEY_RIGHT) c += 2;
    if (rep & KEY_LEFT) c -= 2;
    if (rep & KEY_UP) r += 2;
    if (rep & KEY_DOWN) r -= 2;
    acid_p.cutoff = (uint8_t)(c < 0 ? 0 : c > 127 ? 127 : c);
    acid_p.res = (uint8_t)(r < 0 ? 0 : r > 127 ? 127 : r);
}

/* ================================ PANEL =================================== */
enum { K_WAVE, K_CUT, K_RES, K_ENV, K_DEC, K_ACC, K_SLIDE, K_DRIVE, K_DAMT, K_FILT,
       K_LEVEL, K_BPM, K_FXDRV, K_DELAY, K_COMB, K_TEST, K_COUNT };
static const char * const knob_names[K_COUNT] = {
    "WAVE", "CUTOFF", "RES", "ENV MOD", "DECAY", "ACCENT", "SLIDE", "DRIVE",
    "DRV AMT", "FILTER", "LEVEL", "BPM", "FX DRV", "DELAY", "COMB", "CPU TEST"
};
static const int16_t knob_lo[K_COUNT] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 60, 0, 0, 0, 0 };
static const int16_t knob_hi[K_COUNT] = { 3, 127, 127, 127, 127, 127, 127, 3, 127, 1, 127, 220, 127, 127, 127, 1 };
static uint8_t knob_cur, knob_drag;
static int knob_start;
static uint32_t knob_sig[K_COUNT];

static int knob_is_enum(int k) { return k == K_WAVE || k == K_DRIVE || k == K_FILT || k == K_TEST; }

static int knob_get(int k)
{
    switch (k) {
    case K_WAVE: return acid_p.wave;      case K_CUT: return acid_p.cutoff;
    case K_RES: return acid_p.res;        case K_ENV: return acid_p.envmod;
    case K_DEC: return acid_p.decay;      case K_ACC: return acid_p.accent;
    case K_SLIDE: return acid_p.slide;    case K_DRIVE: return acid_p.drive;
    case K_DAMT: return acid_p.drive_amt; case K_FILT: return acid_p.keytrack;
    case K_LEVEL: return acid_p.level;    case K_BPM: return SH->proj.synth.bpm;
    case K_FXDRV: return fx_p.drive;      case K_DELAY: return fx_p.dly_mix;
    case K_COMB: return fx_p.comb_mix;    default: return ui_stress;
    }
}

static void knob_set(int k, int v)
{
    if (v < knob_lo[k]) v = knob_lo[k];
    if (v > knob_hi[k]) v = knob_hi[k];
    switch (k) {
    case K_WAVE: acid_p.wave = (uint8_t)v; break;      case K_CUT: acid_p.cutoff = (uint8_t)v; break;
    case K_RES: acid_p.res = (uint8_t)v; break;        case K_ENV: acid_p.envmod = (uint8_t)v; break;
    case K_DEC: acid_p.decay = (uint8_t)v; break;      case K_ACC: acid_p.accent = (uint8_t)v; break;
    case K_SLIDE: acid_p.slide = (uint8_t)v; break;    case K_DRIVE: acid_p.drive = (uint8_t)v; break;
    case K_DAMT: acid_p.drive_amt = (uint8_t)v; break; case K_FILT: acid_p.keytrack = (uint8_t)v; break;
    case K_LEVEL: acid_p.level = (uint8_t)v; break;
    case K_BPM: SH->proj.synth.bpm = (uint8_t)v; fx_p.bpm = (uint8_t)v; break;
    case K_FXDRV: fx_p.drive = (uint8_t)v; break;      case K_DELAY: fx_p.dly_mix = (uint8_t)v; break;
    case K_COMB: fx_p.comb_mix = (uint8_t)v; break;
    default:
        if ((uint8_t)v != ui_stress) {
            ui_stress = (uint8_t)v;
            app_send(CMD(CMD_STRESS, v));
            fx_p.crush_hold = v ? 2 : 1;               /* every FX stage working */
            fx_p.crush_bits = v ? 12 : 16;
        }
        break;
    }
}

static void knob_value(int k, char *o)
{
    static const char * const waves[4] = { "SAW", "SQUARE", "HOLLOW", "BITE" };
    static const char * const drives[4] = { "OFF", "SOFT", "HARD", "FOLD" };
    int v = knob_get(k);
    switch (k) {
    case K_WAVE: strcpy(o, waves[v & 3]); break;
    case K_DRIVE: strcpy(o, drives[v & 3]); break;
    case K_FILT: strcpy(o, v ? "KEY" : "303"); break;
    case K_TEST: strcpy(o, v ? "ON" : "OFF"); break;
    default: sprintf(o, "%d", v); break;
    }
}

/* a knurled metal knob: lit value arc, grip ring, lit-from-top-left cap */
static void draw_knob(int cx, int cy, int v, int lo, int hi, int sel)
{
    int pos = hi > lo ? ((v - lo) * 768) / (hi - lo) : 0, i, j;
    for (i = 0; i <= 768; i += 48) {                       /* value arc */
        int a = (i - 384) & 1023;
        int dx = (sine_tab[a] * 13) >> 15, dy = -((sine_tab[(a + 256) & 1023] * 13) >> 15);
        gfx_rect(fb_bot, cx + dx, cy + dy, 2, 2, i <= pos ? COL_AMBER : COL_GHOST);
    }
    gfx_disc(fb_bot, cx + 1, cy + 1, 10, C(1, 1, 1));       /* shadow */
    for (j = -10; j <= 10; j++)                             /* cap with knurled rim */
        for (i = -10; i <= 10; i++) {
            int d2 = i * i + j * j;
            if (d2 > 100) continue;
            if (d2 > 64) {
                int k = (((i * 7 + j * 3) ^ (i * 3 - j * 7)) >> 2) & 1;
                fb_bot[(cy + j) * 256 + cx + i] = k ? C(15, 15, 16) : C(6, 6, 7);
            } else {
                int l = 14 - ((i + j) >> 2);
                fb_bot[(cy + j) * 256 + cx + i] = C(l, l, l + 1);
            }
        }
    if (sel) gfx_ring(fb_bot, cx, cy, 11, COL_SULFUR);
    {
        int a = (pos - 384) & 1023;
        int dx = (sine_tab[a] * 7) >> 15, dy = -((sine_tab[(a + 256) & 1023] * 7) >> 15);
        gfx_line(fb_bot, cx, cy, cx + dx, cy + dy, C(2, 2, 2));
        gfx_disc(fb_bot, cx + dx, cy + dy, 1, COL_AMBER);
    }
}

static void knob_draw(int k)
{
    int x0 = (k & 3) * 64, y0 = 32 + (k >> 2) * 40;
    char val[10];
    restore_bot(x0, y0 + 7, 64, 25);
    draw_knob(x0 + 32, y0 + 19, knob_get(k), knob_lo[k], knob_hi[k], k == knob_cur);
    knob_value(k, val);
    txt(&con_bot, (y0 >> 3), (k & 3) * 8, k == knob_cur ? T_SULFUR : T_BRIGHT, "%-8.8s", knob_names[k]);
    txt(&con_bot, (y0 >> 3) + 4, (k & 3) * 8 + 1, T_HOT, "%-6.6s", "");
    txt(&con_bot, (y0 >> 3) + 4, (k & 3) * 8 + 4 - (int)strlen(val) / 2, T_HOT, "%s", val);
}

static void panel_draw_static(void)
{
    int k;
    for (k = 0; k < K_COUNT; k++) {
        int x0 = (k & 3) * 64, y0 = 32 + (k >> 2) * 40;
        gfx_well(fb_bot, x0 + 8, y0 + 31, 48, 10, COL_LCD);
    }
    gfx_hazard(fb_bot, 0, 150, 3, 40);
    snapshot_bot();
    memset(knob_sig, 0xFF, sizeof knob_sig);
}

static void panel_frame(void)
{
    int k;
    for (k = 0; k < K_COUNT; k++) {
        uint32_t sig = (uint32_t)knob_get(k) | ((uint32_t)(k == knob_cur) << 16);
        if (sig != knob_sig[k]) { knob_draw(k); knob_sig[k] = sig; }
    }
}

static void panel_touch(int phase)
{
    static uint8_t saved;
    if (phase == 0) {
        int k = ((t_y - 32) / 40) * 4 + t_x / 64;
        knob_drag = 0;
        if (t_y < 32 || k >= K_COUNT) return;
        knob_cur = (uint8_t)k;
        knob_start = knob_get(k);
        saved = (uint8_t)knob_start;
        knob_drag = 1;
    } else if (phase == 1 && knob_drag) {
        int dy = t_sy - t_y, k = knob_cur;
        knob_set(k, knob_start + (knob_is_enum(k) ? dy / 14 : dy / 2));
    } else if (phase == 2 && knob_drag) {
        int k = knob_cur;
        if (t_temp && k != K_TEST) knob_set(k, saved);                /* L: temporary */
        else if (!t_moved && knob_is_enum(k)) {                       /* tap steps a choice */
            int v = knob_get(k) + 1;
            knob_set(k, v > knob_hi[k] ? knob_lo[k] : v);
        }
        knob_drag = 0;
    }
}

static void panel_keys(uint32_t down, uint32_t held, uint32_t rep)
{
    (void)down;
    if (held & KEY_A) {
        int d = 0, k = knob_cur;
        if (rep & KEY_RIGHT) d = 1;
        if (rep & KEY_LEFT) d = -1;
        if (rep & KEY_UP) d = knob_is_enum(k) ? 1 : 8;
        if (rep & KEY_DOWN) d = knob_is_enum(k) ? -1 : -8;
        if (d) knob_set(k, knob_get(k) + d);
        return;
    }
    if ((rep & KEY_LEFT) && (knob_cur & 3)) knob_cur--;
    if ((rep & KEY_RIGHT) && (knob_cur & 3) < 3) knob_cur++;
    if ((rep & KEY_UP) && knob_cur >= 4) knob_cur -= 4;
    if ((rep & KEY_DOWN) && knob_cur + 4 < K_COUNT) knob_cur += 4;
}

/* ================================= GEN ==================================== */
enum { G_KEY, G_SCALE, G_STYLE, G_OCT, G_DENS, G_SYNC, G_ACC, G_SLIDE, G_MODE, G_NPARAM };
enum { GI_CODE = G_NPARAM, GI_BUTTONS, GI_MUTATE, GI_COUNT };
static const char * const gen_names[G_NPARAM] = {
    "KEY", "SCALE", "STYLE", "OCTAVE", "DENSITY", "SYNCOP", "ACCENT", "SLIDE", "EVO"
};
static const uint8_t gen_max[G_NPARAM] = { 11, SCALE_COUNT - 1, STYLE_COUNT - 1, 2, 15, 15, 15, 15, EVO_COUNT - 1 };
static uint8_t gen_item, gen_sub;
static uint32_t gen_sig = 0xFFFFFFFF;
static uint8_t gen_flash[3];
static int8_t t_gen_row;
#define SL_X0 152
#define SL_W  96

static uint8_t *gen_field(int i)
{
    gen_t *g = &SH->proj.gen;
    switch (i) {
    case G_KEY: return &g->key;       case G_SCALE: return &g->scale;
    case G_STYLE: return &g->style;   case G_OCT: return &g->octave;
    case G_DENS: return &g->density;  case G_SYNC: return &g->sync;
    case G_ACC: return &g->accent;    case G_SLIDE: return &g->slide;
    default: return &g->mode;
    }
}

static int code_x(int i) { return 8 * (2 + 2 * i + (i / 4) * 2); }

static void gen_set_digit(int i, int d)
{
    char code[12];
    gen_t g;
    int tries, cur;
    corr_code_encode(&SH->proj.gen, code);
    cur = code[i] >= 'A' ? code[i] - 'A' + 10 : code[i] - '0';
    for (tries = 0; tries < 16; tries++) {
        cur = (cur + d) & 15;
        code[i] = (char)(cur < 10 ? '0' + cur : 'A' + cur - 10);
        g = SH->proj.gen;
        if (corr_code_decode(&g, code)) { SH->proj.gen = g; return; }
    }
}

static void gen_draw(void)
{
    const gen_t *g = &SH->proj.gen;
    char code[12], v[16];
    int i;
    restore_bot(0, 25, 256, 167);
    for (i = 0; i < G_NPARAM; i++) {
        int row = 4 + i, val = *gen_field(i), w, y = row * 8;
        int sel = gen_item == i;
        switch (i) {
        case G_KEY: strcpy(v, notes12[val % 12]); if (v[1] == '-') v[1] = 0; break;
        case G_SCALE: strcpy(v, corr_scale_names[val % SCALE_COUNT]); break;
        case G_STYLE: strcpy(v, corr_style_names[val % STYLE_COUNT]); break;
        case G_OCT: sprintf(v, "%d", val + 1); break;
        case G_MODE: strcpy(v, corr_mode_names[val % EVO_COUNT]); break;
        default: sprintf(v, "%d", val); break;
        }
        txt(&con_bot, row, 0, sel ? T_SULFUR : T_BRIGHT, "%c%-7s", sel ? '>' : ' ', gen_names[i]);
        txt(&con_bot, row, 9, sel ? T_SULFUR : T_HOT, "%-10.10s", v);
        w = (val * (SL_W - 6)) / gen_max[i];
        gfx_rect(fb_bot, SL_X0, y + 3, SL_W, 2, C(1, 1, 1));               /* fader slot */
        gfx_rect(fb_bot, SL_X0, y + 3, w, 2, sel ? COL_SULFUR : COL_AMBER);
        gfx_rect(fb_bot, SL_X0 + w, y, 6, 8, C(1, 1, 1));                  /* cap */
        gfx_rect(fb_bot, SL_X0 + w, y, 5, 7, C(15, 15, 16));
        gfx_vline(fb_bot, SL_X0 + w + 2, y + 1, 5, C(4, 4, 5));
    }
    /* seed code on a 7-segment display: tap above a digit to raise it, below to lower it */
    corr_code_encode(g, code);
    txt(&con_bot, 13, 0, gen_item == GI_CODE ? T_SULFUR : T_DIM, "SEED CODE  TAP ABOVE/BELOW");
    for (i = 0; i < 12; i++) {
        int x = code_x(i), sel = gen_item == GI_CODE && gen_sub == i;
        if (sel) gfx_rect(fb_bot, x - 2, 112, 15, 21, C(8, 4, 1));
        gfx_seg7(fb_bot, x, 114, code[i], 6, i < 4 ? C(31, 26, 14) : COL_AMBER, COL_GHOST);
    }
    for (i = 0; i < 3; i++) {
        int x = 4 + i * 84, sel = gen_item == GI_BUTTONS && gen_sub == i;
        gfx_button(fb_bot, x, 140, 80, 24, gen_flash[i] != 0);
        gfx_led(fb_bot, x + 9, 152, gen_flash[i] != 0, COL_AMBER);
        if (sel) gfx_frame(fb_bot, x - 1, 139, 82, 26, COL_SULFUR);
    }
    txt(&con_bot, 19, 2, T_BRIGHT, "GEN ALL");
    txt(&con_bot, 19, 12, T_BRIGHT, "GEN BASS");
    txt(&con_bot, 19, 23, T_BRIGHT, "DRUMS");
    draw_knob(24, 179, g->mutation, 0, 15, gen_item == GI_MUTATE);
    txt(&con_bot, 21, 6, gen_item == GI_MUTATE ? T_SULFUR : T_BRIGHT, "MUTATE %2d", g->mutation);
    txt(&con_bot, 22, 6, T_DIM, "EVOLUTION BUDGET");
}

static void gen_action(int i)
{
    static const uint8_t what[3] = { GEN_ALL, GEN_BASS, GEN_DRUMS };
    gen_flash[i] = 10;
    app_generate(what[i]);
}

static void gen_frame(void)
{
    uint32_t sig = 0, i;
    const uint8_t *b = (const uint8_t *)&SH->proj.gen;
    for (i = 0; i < sizeof(gen_t); i++) sig = sig * 31 + b[i];
    sig = sig * 31 + gen_item * 16 + gen_sub;
    for (i = 0; i < 3; i++) { sig = sig * 31 + (gen_flash[i] != 0); if (gen_flash[i]) gen_flash[i]--; }
    if (sig != gen_sig) { gen_draw(); gen_sig = sig; }
}

static void gen_touch(int phase)
{
    static int dial_start;
    if (phase == 0) {
        int row = t_y / 8;
        t_gen_row = -1;
        if (row >= 4 && row < 4 + G_NPARAM) { gen_item = (uint8_t)(row - 4); t_gen_row = (int8_t)gen_item; }
        else if (t_y >= 108 && t_y < 138) {
            int i;
            for (i = 0; i < 12; i++) if (t_x >= code_x(i) - 4 && t_x < code_x(i) + 12) {
                gen_item = GI_CODE; gen_sub = (uint8_t)i;
                gen_set_digit(i, t_y < 122 ? 1 : -1);
            }
            return;
        } else if (t_y >= 140 && t_y < 164) {
            int i = (t_x - 4) / 84;
            if (i > 2) i = 2;
            gen_item = GI_BUTTONS; gen_sub = (uint8_t)i;
            gen_action(i);
            return;
        } else if (t_y >= 166 && t_x < 120) {
            gen_item = GI_MUTATE; t_gen_row = 100;
            dial_start = SH->proj.gen.mutation;
            return;
        } else return;
    }
    if (phase < 2 && t_gen_row >= 0 && t_gen_row < G_NPARAM && t_x >= SL_X0 - 8) {
        int v = ((t_x - SL_X0) * gen_max[t_gen_row] + SL_W / 2) / SL_W;
        *gen_field(t_gen_row) = (uint8_t)(v < 0 ? 0 : v > gen_max[t_gen_row] ? gen_max[t_gen_row] : v);
    } else if (phase == 1 && t_gen_row == 100) {
        int v = dial_start + (t_sy - t_y) / 6;
        SH->proj.gen.mutation = (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v);
    }
}

static void gen_keys(uint32_t down, uint32_t held, uint32_t rep)
{
    if (held & KEY_A) {
        int d = (rep & KEY_RIGHT) ? 1 : (rep & KEY_LEFT) ? -1 : (rep & KEY_UP) ? 4 : (rep & KEY_DOWN) ? -4 : 0;
        if (gen_item < G_NPARAM && d) {
            int v = *gen_field(gen_item) + d;
            *gen_field(gen_item) = (uint8_t)(v < 0 ? 0 : v > gen_max[gen_item] ? gen_max[gen_item] : v);
        } else if (gen_item == GI_CODE && d) gen_set_digit(gen_sub, d > 0 ? 1 : -1);
        else if (gen_item == GI_MUTATE && d) {
            int v = SH->proj.gen.mutation + (d > 0 ? 1 : -1);
            SH->proj.gen.mutation = (uint8_t)(v < 0 ? 0 : v > 15 ? 15 : v);
        }
        if ((down & KEY_A) && gen_item == GI_BUTTONS) gen_action(gen_sub);
        return;
    }
    if ((rep & KEY_UP) && gen_item > 0) gen_item--;
    if ((rep & KEY_DOWN) && gen_item < GI_COUNT - 1) gen_item++;
    if (gen_item == GI_CODE) {
        if ((rep & KEY_LEFT) && gen_sub > 0) gen_sub--;
        if ((rep & KEY_RIGHT) && gen_sub < 11) gen_sub++;
    } else if (gen_item == GI_BUTTONS) {
        if (gen_sub > 2) gen_sub = 2;
        if ((rep & KEY_LEFT) && gen_sub > 0) gen_sub--;
        if ((rep & KEY_RIGHT) && gen_sub < 2) gen_sub++;
    }
}

/* =============================== dispatch ================================= */
static void enter_tab(void)
{
    consoleSelect(&con_bot);
    consoleClear();
    gfx_plate(fb_bot, 0, 0, 256, 192, COL_STEEL_D);
    gfx_rust(fb_bot, 250, 186, 5, 7 + tab);
    gfx_rust(fb_bot, 4, 120, 4, 3 + tab);
    gfx_rivet(fb_bot, 251, 187);
    draw_tabs();
    snapshot_bot();
    switch (tab) {
    case TAB_LIVE: live_draw_static(); break;
    case TAB_XY: xy_draw_static(); break;
    case TAB_PANEL: panel_draw_static(); break;
    default:
        gfx_well(fb_bot, code_x(0) - 6, 110, code_x(11) - code_x(0) + 22, 26, COL_LCD);
        gfx_rivet(fb_bot, 248, 30);
        snapshot_bot();
        gen_sig = 0xFFFFFFFF;
        break;
    }
}

static void tab_touch(int phase)
{
    switch (tab) {
    case TAB_LIVE: live_touch(phase); break;
    case TAB_XY: xy_touch(phase); break;
    case TAB_PANEL: panel_touch(phase); break;
    default: gen_touch(phase); break;
    }
}

void ui_init(void)
{
    gfx_init();
    keysSetRepeat(15, 4);
    need_full = 1;
}

void ui_frame(uint32_t down, uint32_t held)
{
    uint32_t rep = keysDownRepeat(), up = keysUp();
    frame++;

    /* ---- global buttons ---------------------------------------------- */
    if (held & KEY_SELECT) {
        if (down & KEY_LEFT) { tab = (uint8_t)((tab + TAB_COUNT - 1) % TAB_COUNT); need_full = 1; }
        if (down & KEY_RIGHT) { tab = (uint8_t)((tab + 1) % TAB_COUNT); need_full = 1; }
        if (down & KEY_A) app_evolve();
        if (down & KEY_B) app_undo();
    } else {
        switch (tab) {
        case TAB_LIVE: live_keys(down, held, rep); break;
        case TAB_XY: xy_keys(down, held, rep); break;
        case TAB_PANEL: panel_keys(down, held, rep); break;
        default: gen_keys(down, held, rep); break;
        }
    }
    if (down & KEY_R) app_fill_latch(1);              /* hold R: latched fill */
    if (up & KEY_R) app_fill_latch(0);

    /* ---- touch: one point, down / drag / up --------------------------- */
    if (held & KEY_TOUCH) {
        touchPosition tp;
        touchRead(&tp);
        t_x = tp.px; t_y = tp.py;
        if (!t_active) {
            t_active = 1; t_moved = 0;
            t_sx = t_x; t_sy = t_y;
            t_temp = (held & KEY_L) != 0;             /* hold L: temporary drag */
            t_on_tabs = t_y < 23;
            if (t_on_tabs) {
                uint8_t nt = (uint8_t)(t_x / 64);
                if (nt < TAB_COUNT && nt != tab) { tab = nt; need_full = 1; }
            } else tab_touch(0);
        } else if (!t_on_tabs) {
            if (t_x - t_sx > 2 || t_sx - t_x > 2 || t_y - t_sy > 2 || t_sy - t_y > 2) t_moved = 1;
            tab_touch(1);
        }
    } else if (t_active) {
        t_active = 0;
        if (!t_on_tabs) tab_touch(2);
    }

    if (need_full) { need_full = 0; enter_tab(); }
    switch (tab) {
    case TAB_LIVE: live_frame(); break;
    case TAB_XY: xy_frame(); break;
    case TAB_PANEL: panel_frame(); break;
    default: gen_frame(); break;
    }
}
