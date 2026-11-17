/*
 * ETCH DS -- by Synth Centric
 * Bottom screen: tabs LIVE / XY / PANEL / KEYS / GEN / SONG (FILE).
 *
 * Touch is for performing, buttons are for editing, and every tab speaks
 * ETCH's grammar: the D-pad moves a cursor, A places or toggles, A + D-pad
 * changes a value, B + A deletes.  Global: START play/stop, SELECT + LEFT /
 * RIGHT changes tab (or tap the tab bar; tap SONG again for FILE), SELECT + A
 * evolves at the next bar, SELECT + B undoes, hold R for a latched fill, hold
 * L while dragging to make the move temporary.  Touch is single-point and
 * never needed for sound.
 *
 * Layout grid (see gfx.h): 6 px outer margin, 4 px gaps, tab bar y 0-21,
 * content from y 26.  Every hit test below uses the same constants as the
 * drawing code, so what you see is what you touch.
 */
#include <stdio.h>
#include <string.h>
#include "corrosion.h"
#include "gfx.h"
#include "app.h"
#include "audio.h"
#include "dsp_tables.h"
#include "storage.h"

enum { TAB_LIVE, TAB_XY, TAB_PANEL, TAB_KEYS, TAB_GEN, TAB_SONG, TAB_COUNT };
static const char * const tab_names[TAB_COUNT] = { "LIVE", "XY", "SYNTH", "KEYS", "GEN", "SONG" };
#define TAB_X0 6
#define TAB_W  41
#define TOP_Y  26
static uint8_t tab = TAB_LIVE, need_full = 1, sg_page;   /* sg_page 1: FILE */
static uint8_t r_fill, b_alone;                            /* the fill R started; B tapped alone */
static uint8_t pn_page;                                   /* pn_page 1: DRUM */
uint8_t ui_stress;
static uint32_t frame;

/* touch state */
static uint8_t t_active, t_on_tabs, t_temp, t_moved;
static int t_x, t_y, t_sx, t_sy;

static const char * const notes12[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };
static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

/* a button: rounded, flat; lit = accent fill with black text (SERIES_SPEC.md 4.2) */
static void button(int x, int y, int w, int h, const char *label, int lit, int sel, uint16_t litcol)
{
    gfx_rect(fb_bot, x, y, w, h, COL_BG);
    gfx_rrect(fb_bot, x, y, w, h, lit ? litcol : COL_SURF2);
    if (sel) gfx_rframe(fb_bot, x, y, w, h, lit ? COL_TEXT : COL_ACCENT);
    gfx_text_c(fb_bot, x + w / 2, y + (h - 7) / 2, F_MAIN, lit && litcol != COL_ACCENT ? COL_BG : COL_TEXT, label);
}

/* Knob drags: the whole range fits in about 90 px of travel, and dragging
 * up or to the right both turn it up.  Choices step every 10 px. */
#define DRAG_SPAN 90
static int drag_value(int start, int lo, int hi, int is_enum)
{
    int d = (t_sy - t_y) + (t_x - t_sx), v;
    if (is_enum) v = start + d / 10;
    else v = start + (d * (hi - lo) + (d >= 0 ? DRAG_SPAN / 2 : -DRAG_SPAN / 2)) / DRAG_SPAN;
    return clampi(v, lo, hi);
}

/* caption over value, as on the top screen */
static void stat_block(int x, int y, int w, const char *cap, const char *val, uint16_t vc)
{
    gfx_rect(fb_bot, x, y, w, 20, COL_BG);
    if (x > 6) gfx_vline(fb_bot, x - 2, y + 2, 16, COL_SURF2);
    gfx_text(fb_bot, x + 5, y + 3, F_SMALL, COL_TEXT2, cap);
    gfx_text(fb_bot, x + 5, y + 10, F_MAIN, vc, val);
}

/* ============================== tab bar =================================== */
static void draw_tabs(void)
{
    int i;
    gfx_rect(fb_bot, 0, 0, 256, 24, COL_BG);
    for (i = 0; i < TAB_COUNT; i++) {
        const char *nm = (i == TAB_SONG && sg_page) ? "FILE" : (i == TAB_PANEL && pn_page) ? "DRUM" : tab_names[i];
        int cx = TAB_X0 + i * TAB_W + TAB_W / 2, w = gfx_text_w(F_MAIN, nm), act = i == tab;
        gfx_text_c(fb_bot, cx, 7, F_MAIN, act ? COL_TEXT : COL_TEXT3, nm);
        if (act) gfx_rect(fb_bot, cx - w / 2 - 2, 17, w + 4, 2, COL_ACCENT);
    }
    gfx_hline(fb_bot, 6, 21, 244, COL_SURF2);
}

/* ================================ LIVE ==================================== */
#define LX0       42
#define LCW       13            /* 12 px cell + 1 px gap */
#define BASS_T    26
#define BASS_B    73            /* note 0 (C1) sits on this row */
#define STRIP_Y   76
#define LANE_Y0   86
#define LANE_H    14
#define LANE_Y(l) (LANE_Y0 + (l) * LANE_H)
#define PAD_Y     150
#define PAD_H     36

static int8_t lv_col;               /* -1 = row label (mute) */
static uint8_t lv_row;              /* 0 bass, 1-4 lanes, 5 pads */
static uint8_t last_note = 21;      /* A2: what A writes on an empty step (ETCH) */
static uint32_t lv_sig[5][16], lv_label_sig, lv_pad_sig[4];
static uint8_t pad_flash[4];
static int8_t t_step, t_lane, t_paint;
static uint8_t t_kind;              /* 1 bass, 2 strip, 3 lane, 4 pad, 5 label */
static uint8_t t_start_note, t_start_flags, t_placed, t_changed;
#define BASS_STEP_PX 6                  /* one scale step per 6 px of drag */

static const char * const lane_names[4] = { "KICK", "SNARE", "HAT", "OPEN" };
static const char * const pad_names[4] = { "FILL", "REGEN", "EVOLVE", "UNDO" };

static int bass_y(int n) { if (n > 47) n = 47; if (n < 0) n = 0; return BASS_B - n; }

static void live_bass_cell(const pattern_t *p, int s, int play, int cur)
{
    int x = LX0 + s * LCW;
    uint8_t f = p->flags[s], t = f & STEP_TYPE_MASK;
    uint16_t bg = play ? COL_PLAY : (s & 3) == 0 ? COL_BEAT : COL_SURF, grid = gfx_mix(bg, COL_BORDER, 110);
    uint16_t c = (f & STEP_ACCENT) ? COL_HOT : COL_NOTE;
    if (SH->proj.mutes & 1) c = COL_TEXT3;
    gfx_rect(fb_bot, x, BASS_T, 12, BASS_B - BASS_T + 2, bg);
    gfx_hline(fb_bot, x, bass_y(12), 12, grid);
    gfx_hline(fb_bot, x, bass_y(24), 12, grid);
    gfx_hline(fb_bot, x, bass_y(36), 12, grid);
    if (t == STEP_NOTE) {
        int y = bass_y(p->note[s]);
        gfx_vline(fb_bot, x + 6, y + 2, BASS_B - y, gfx_mix(bg, c, 60));
        gfx_rrect(fb_bot, x + 1, y - 1, 10, 3, c);
        if (f & STEP_SLIDE) gfx_line(fb_bot, x + 10, y, x + LCW, bass_y(p->note[(s + 1) & 15]), c);
    } else if (t == STEP_TIE) {
        int k, n = p->note[s];
        for (k = 1; k < 16; k++)
            if ((p->flags[(s - k) & 15] & STEP_TYPE_MASK) == STEP_NOTE) { n = p->note[(s - k) & 15]; break; }
        gfx_rect(fb_bot, x, bass_y(n) - 1, LCW, 3, gfx_mix(COL_NOTE, bg, 140));
    }
    if (cur) gfx_frame(fb_bot, x, BASS_T, 12, BASS_B - BASS_T + 2, COL_ACCENT);
    /* A/S strip: accent = bright bar, slide = ramp */
    gfx_rect(fb_bot, x, STRIP_Y, 12, 5, COL_BG);
    gfx_rrect(fb_bot, x, STRIP_Y, 12, 5, COL_SURF);
    if (t == STEP_NOTE && (f & STEP_ACCENT)) gfx_rect(fb_bot, x + 2, STRIP_Y + 2, 4, 1, COL_HOT);
    if (t == STEP_NOTE && (f & STEP_SLIDE)) gfx_line(fb_bot, x + 7, STRIP_Y + 3, x + 10, STRIP_Y + 1, COL_ACCENT);
}

static void live_lane_cell(const pattern_t *p, int l, int s, int play, int cur)
{
    int x = LX0 + s * LCW, y = LANE_Y(l), on = (p->drum[l] >> s) & 1, muted = (SH->proj.mutes >> (l + 1)) & 1;
    uint16_t c = on ? (muted ? COL_TEXT3 : l == 0 ? COL_HOT : COL_NOTE)
                    : (play ? COL_SURF3 : (s & 3) == 0 ? COL_SURF2 : COL_SURF);
    gfx_rect(fb_bot, x, y, 12, 12, COL_BG);
    gfx_rrect(fb_bot, x, y, 12, 12, c);
    if (cur) gfx_rframe(fb_bot, x, y, 12, 12, on ? COL_BG : COL_ACCENT);
    if (cur && on) gfx_rframe(fb_bot, x + 1, y + 1, 10, 10, COL_ACCENT);
}

static void live_pad(int i)
{
    int x = 6 + i * 62, lit = pad_flash[i] || (i == 0 && SH->fill);
    button(x, PAD_Y, 58, PAD_H, pad_names[i], lit, lv_row == 5 && lv_col == i, COL_ACCENT);
    if (i == 1 || i == 2) gfx_disc(fb_bot, x + 52, PAD_Y + 5, 1, lit ? COL_BG : COL_ACCENT);   /* rewrites music */
}

static void live_draw_static(void)
{
    memset(lv_sig, 0, sizeof lv_sig);
    memset(lv_pad_sig, 0xFF, sizeof lv_pad_sig);
    lv_label_sig = 0xFFFFFFFF;
}

static void live_labels(void)
{
    int l, sel = lv_col < 0;
    gfx_rect(fb_bot, 0, BASS_T + 18, LX0 - 2, PAD_Y - BASS_T - 22, COL_BG);   /* below the note readout */
    gfx_text(fb_bot, 6, BASS_T + 21, F_SMALL, (SH->proj.mutes & 1) ? COL_TEXT3 : (sel && lv_row == 0) ? COL_TEXT : COL_TEXT2, "BASS");
    if (SH->proj.mutes & 1) gfx_text(fb_bot, 6, BASS_T + 29, F_SMALL, COL_RED, "MUTE");
    gfx_text(fb_bot, 6, STRIP_Y, F_SMALL, COL_TEXT3, "A/S");
    for (l = 0; l < 4; l++) {
        int m = (SH->proj.mutes >> (l + 1)) & 1;
        gfx_text(fb_bot, 6, LANE_Y(l) + 4, F_SMALL, m ? COL_TEXT3 : (sel && lv_row == l + 1) ? COL_TEXT : COL_TEXT2, lane_names[l]);
        if (m) gfx_disc(fb_bot, LX0 - 6, LANE_Y(l) + 6, 1, COL_RED);
    }
}

static void live_frame(void)
{
    const pattern_t *p = &SH->proj.pat[SH->cur];
    int s, l, step = SH->step, playing = SH->playing;
    uint32_t lsig = SH->proj.mutes | ((uint32_t)(lv_col < 0) << 8) | ((uint32_t)lv_row << 9);
    {   /* the edited bass note, large, beside the lane */
        uint8_t f0 = lv_col >= 0 ? p->flags[lv_col] : 0;
        uint32_t ns = lsig | ((uint32_t)(lv_row == 0 && lv_col >= 0) << 16) |
                      (lv_col >= 0 ? ((uint32_t)p->note[lv_col] << 17) | ((uint32_t)(f0 & 3) << 24) | ((uint32_t)lv_col << 26) : 0);
        static uint32_t note_sig = 0xFFFFFFFF;
        if (ns != note_sig || lsig != lv_label_sig) {
            gfx_rect(fb_bot, 0, BASS_T, LX0 - 2, 18, COL_BG);
            if (lv_row == 0 && lv_col >= 0) {
                char b[6];
                gfx_textf(fb_bot, 6, BASS_T + 1, F_SMALL, COL_TEXT3, "STEP %d", lv_col + 1);
                if ((f0 & STEP_TYPE_MASK) == STEP_NOTE) {
                    sprintf(b, "%s%d", notes12[p->note[lv_col] % 12], p->note[lv_col] / 12 + 1);
                    if (b[1] == '-') memmove(b + 1, b + 2, strlen(b + 1));
                } else strcpy(b, (f0 & STEP_TYPE_MASK) == STEP_TIE ? "TIE" : "REST");
                gfx_text(fb_bot, 6, BASS_T + 9, F_MAIN, COL_TEXT, b);
            }
            note_sig = ns;
        }
    }
    if (lsig != lv_label_sig) { live_labels(); lv_label_sig = lsig; }
    for (s = 0; s < 16; s++) {
        int play = playing && s == step;
        int cur0 = lv_row == 0 && lv_col == s;
        uint32_t sig = 0x80000000u | p->note[s] | ((uint32_t)(p->flags[s] & 15) << 6) |
                       ((uint32_t)play << 10) | ((uint32_t)cur0 << 11) | ((uint32_t)(SH->proj.mutes & 1) << 12);
        if (p->flags[s] & STEP_SLIDE) sig ^= (uint32_t)p->note[(s + 1) & 15] << 16;
        if ((p->flags[s] & STEP_TYPE_MASK) == STEP_TIE) sig ^= ((uint32_t)p->note[(s + 15) & 15] << 22) | (frame & ~63u);
        if (sig != lv_sig[0][s]) { live_bass_cell(p, s, play, cur0); lv_sig[0][s] = sig; }
        for (l = 0; l < 4; l++) {
            int cur = lv_row == l + 1 && lv_col == s;
            uint32_t ls = 0x80000000u | ((p->drum[l] >> s) & 1) | ((uint32_t)play << 1) | ((uint32_t)cur << 2) |
                          ((uint32_t)((SH->proj.mutes >> (l + 1)) & 1) << 3);
            if (ls != lv_sig[l + 1][s]) { live_lane_cell(p, l, s, play, cur); lv_sig[l + 1][s] = ls; }
        }
    }
    for (l = 0; l < 4; l++) {
        uint32_t st = (uint32_t)((pad_flash[l] ? 1 : 0) | ((lv_row == 5 && lv_col == l) ? 2 : 0) | (l == 0 && SH->fill ? 4 : 0));
        if (st != lv_pad_sig[l]) { live_pad(l); lv_pad_sig[l] = st; }
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
        if (t_y >= PAD_Y - 2) {
            int i = clampi((t_x - 6) / 62, 0, 3);
            t_kind = 4; lv_row = 5; lv_col = (int8_t)i; pad_action(i);
            return;
        }
        if (t_x < LX0 - 2) {
            int bit = t_y < LANE_Y0 - 2 ? 0 : (t_y - LANE_Y0 + 1) / LANE_H + 1;
            if (bit <= 4) { app_toggle_mute((uint8_t)bit); t_kind = 5; }
            return;
        }
        s = clampi(s, 0, 15);
        t_step = (int8_t)s;
        if (t_y < STRIP_Y) {
            /* bass: tap an empty step to place the last note, tap a note to
             * rest it, drag up / down to move it by scale steps */
            uint8_t f = p->flags[s];
            t_kind = 1; lv_row = 0; lv_col = (int8_t)s;
            t_start_flags = f;
            t_placed = 0;
            t_changed = 0;
            if ((f & STEP_TYPE_MASK) != STEP_NOTE) {
                t_start_note = snap_note(last_note);
                app_set_bass((uint8_t)s, t_start_note, (uint8_t)(STEP_NOTE | (f & STEP_LOCK)));
                t_placed = 1;
            } else t_start_note = p->note[s];
        } else if (t_y < LANE_Y0 - 2) {
            uint8_t f = p->flags[s];
            if ((f & STEP_TYPE_MASK) == STEP_NOTE) {        /* none -> A -> S -> A+S */
                uint8_t as = (uint8_t)((f & STEP_ACCENT ? 1 : 0) | (f & STEP_SLIDE ? 2 : 0));
                as = (uint8_t)((as + 1) & 3);
                f = (uint8_t)((f & ~(STEP_ACCENT | STEP_SLIDE)) | (as & 1 ? STEP_ACCENT : 0) | (as & 2 ? STEP_SLIDE : 0));
                app_set_bass((uint8_t)s, p->note[s], f);
            }
            t_kind = 2;
        } else {
            int l = clampi((t_y - LANE_Y0 + 1) / LANE_H, 0, 3);
            t_kind = 3; t_lane = (int8_t)l; lv_row = (uint8_t)(l + 1); lv_col = (int8_t)s;
            t_paint = (int8_t)!((p->drum[l] >> s) & 1);
            app_toggle_drum((uint8_t)l, (uint8_t)s);
        }
    } else if (phase == 1) {
        if (t_kind == 1) {
            int steps = (t_sy - t_y) / BASS_STEP_PX, k;
            uint8_t n = t_start_note, f = (uint8_t)(STEP_NOTE | (t_start_flags & ~STEP_TYPE_MASK));
            if (!t_placed) f = t_start_flags;
            for (k = 0; k < steps; k++) n = corr_scale_step(n, 1, &SH->proj.gen);
            for (k = 0; k > steps; k--) n = corr_scale_step(n, -1, &SH->proj.gen);
            if (n != p->note[t_step] && (p->flags[t_step] & STEP_TYPE_MASK) == STEP_NOTE) {
                app_set_bass((uint8_t)t_step, n, (p->flags[t_step] & ~STEP_TYPE_MASK) | STEP_NOTE);
                last_note = n;
                t_changed = 1;
            }
            (void)f;
        } else if (t_kind == 3 && s >= 0 && s <= 15 && s != t_step) {
            t_step = (int8_t)s;                               /* swipe paints */
            if ((int)((p->drum[t_lane] >> s) & 1) != t_paint) app_toggle_drum((uint8_t)t_lane, (uint8_t)s);
        }
    } else if (t_kind == 1 && !t_placed && !t_changed && (t_start_flags & STEP_TYPE_MASK) == STEP_NOTE) {
        app_set_bass((uint8_t)t_step, t_start_note, (uint8_t)(t_start_flags & STEP_LOCK));   /* tap: rest */
    }
}

static void live_keys(uint32_t down, uint32_t held, uint32_t rep)
{
    const pattern_t *p = &SH->proj.pat[SH->cur];
    if (lv_row == 0 && lv_col >= 0 && (p->flags[lv_col] & STEP_TYPE_MASK) == STEP_NOTE) {
        uint8_t f = p->flags[lv_col], n = p->note[lv_col];
        if (down & KEY_X) app_set_bass((uint8_t)lv_col, n, f ^ STEP_ACCENT);   /* the DS's extra buttons: */
        if (down & KEY_Y) app_set_bass((uint8_t)lv_col, n, f ^ STEP_SLIDE);    /* X accent, Y slide       */
    }
    if (held & KEY_A) {
        if (lv_row == 0 && lv_col >= 0) {
            uint8_t f = p->flags[lv_col], n = p->note[lv_col];
            if ((f & STEP_TYPE_MASK) == STEP_NOTE) {
                /* SERIES_SPEC.md 2.2: A+</> a semitone, A+^v an octave (as ETCH) */
                int d = (rep & KEY_RIGHT) ? 1 : (rep & KEY_LEFT) ? -1 : (rep & KEY_UP) ? 12 : (rep & KEY_DOWN) ? -12 : 0;
                if (d) { n = (uint8_t)clampi(n + d, 0, NOTE_MAX); app_set_bass((uint8_t)lv_col, n, f); last_note = n; }
            }
        }
        if (down & KEY_A) {                                    /* A places or toggles */
            if (held & KEY_B) {                                /* B + A deletes       */
                if (lv_row == 0 && lv_col >= 0) app_set_bass((uint8_t)lv_col, p->note[lv_col], p->flags[lv_col] & STEP_LOCK);
                else if (lv_row >= 1 && lv_row <= 4 && lv_col >= 0 && ((p->drum[lv_row - 1] >> lv_col) & 1))
                    app_toggle_drum((uint8_t)(lv_row - 1), (uint8_t)lv_col);
            } else if (lv_row == 5) pad_action(clampi(lv_col, 0, 3));
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
        lv_col = (int8_t)clampi(lv_col, 0, 3);
        if ((rep & KEY_LEFT) && lv_col > 0) lv_col--;
        if ((rep & KEY_RIGHT) && lv_col < 3) lv_col++;
    } else {
        if ((rep & KEY_LEFT) && lv_col > -1) lv_col--;
        if ((rep & KEY_RIGHT) && lv_col < 15) lv_col++;
    }
}

/* ================================= XY ===================================== */
#define XY_PX 6
#define XY_PY TOP_Y
#define XY_PW 244
#define XY_PH 136
#define XY_X0 (XY_PX + 4)
#define XY_X1 (XY_PX + XY_PW - 5)
#define XY_Y0 (XY_PY + 4)
#define XY_Y1 (XY_PY + XY_PH - 5)
static int16_t trail_x[24], trail_y[24];
static uint8_t trail_age[24], trail_head, xy_dirty;
static uint8_t xy_saved_cut, xy_saved_res;
static int xy_last_cut = -1, xy_last_res = -1, xy_last_temp = -1;

static void xy_draw_static(void)
{
    gfx_text(fb_bot, 6, 168, F_SMALL, COL_TEXT2, "CUTOFF");
    gfx_text(fb_bot, 62, 168, F_SMALL, COL_TEXT2, "RESONANCE");
    gfx_text_r(fb_bot, 250, 168, F_SMALL, COL_TEXT3, "HOLD L WHILE DRAGGING");
    gfx_text_r(fb_bot, 250, 176, F_SMALL, COL_TEXT3, "FOR A TEMPORARY MOVE");
    xy_dirty = 1;
    xy_last_cut = -1;
}

static void xy_frame(void)
{
    int i, x, y, any = xy_dirty, temp = t_active && t_temp;
    for (i = 0; i < 24; i++) if (trail_age[i]) { trail_age[i]--; any = 1; }
    if (acid_p.cutoff != xy_last_cut || acid_p.res != xy_last_res || temp != xy_last_temp) any = 1;
    if (!any) return;
    xy_dirty = 0;
    gfx_rrect(fb_bot, XY_PX, XY_PY, XY_PW, XY_PH, COL_SURF);
    for (y = XY_Y0; y <= XY_Y1; y += 22)                  /* dot grid */
        for (x = XY_X0; x <= XY_X1; x += 24) fb_bot[y * 256 + x] = COL_SURF3;
    for (i = 0; i < 24; i++)
        if (trail_age[i]) gfx_disc(fb_bot, trail_x[i], trail_y[i], 1 + trail_age[i] / 24,
                                   gfx_mix(COL_SURF, COL_NOTE, trail_age[i] * 3));
    x = XY_X0 + (acid_p.cutoff * (XY_X1 - XY_X0)) / 127;
    y = XY_Y1 - (acid_p.res * (XY_Y1 - XY_Y0)) / 127;
    gfx_hline(fb_bot, XY_X0, y, XY_X1 - XY_X0 + 1, COL_SURF3);
    gfx_vline(fb_bot, x, XY_Y0, XY_Y1 - XY_Y0 + 1, COL_SURF3);
    gfx_ring(fb_bot, x, y, 7, temp ? COL_TEXT2 : COL_TEXT3);
    gfx_disc(fb_bot, x, y, 3, COL_TEXT);
    gfx_rect(fb_bot, 6, 175, 110, 9, COL_BG);
    gfx_textf(fb_bot, 6, 176, F_MAIN, COL_TEXT, "%d", acid_p.cutoff);
    gfx_textf(fb_bot, 62, 176, F_MAIN, COL_TEXT, "%d", acid_p.res);
    if (temp) gfx_text(fb_bot, 100, 176, F_MAIN, COL_TEXT2, "TEMP");
    xy_last_cut = acid_p.cutoff;
    xy_last_res = acid_p.res;
    xy_last_temp = temp;
}

static void xy_touch(int phase)
{
    if (phase == 0) { xy_saved_cut = acid_p.cutoff; xy_saved_res = acid_p.res; }
    if (phase < 2) {
        int cut = ((t_x - XY_X0) * 127) / (XY_X1 - XY_X0);
        int res = ((XY_Y1 - t_y) * 127) / (XY_Y1 - XY_Y0);
        acid_p.cutoff = (uint8_t)clampi(cut, 0, 127);
        acid_p.res = (uint8_t)clampi(res, 0, 127);
        trail_x[trail_head] = (int16_t)clampi(t_x, XY_X0 + 3, XY_X1 - 3);
        trail_y[trail_head] = (int16_t)clampi(t_y, XY_Y0 + 3, XY_Y1 - 3);
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
    acid_p.cutoff = (uint8_t)clampi(c, 0, 127);
    acid_p.res = (uint8_t)clampi(r, 0, 127);
}

/* ================================ PANEL =================================== */
enum { K_WAVE, K_CUT, K_RES, K_ENV, K_DEC, K_ACC, K_SLIDE, K_DRIVE, K_DAMT, K_FILT,
       K_LEVEL, K_BPM, K_FXDRV, K_DELAY, K_COMB, K_TEST, K_COUNT };
static const char * const knob_names[K_COUNT] = {
    "WAVE", "CUTOFF", "RESONANCE", "ENV MOD", "DECAY", "ACCENT", "SLIDE", "DRIVE",
    "DRIVE AMT", "FILTER", "LEVEL", "TEMPO", "FX DRIVE", "DELAY", "COMB", "CPU TEST"
};
static const int16_t knob_lo[K_COUNT] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 60, 0, 0, 0, 0 };
static const int16_t knob_hi[K_COUNT] = { 3, 127, 127, 127, 127, 127, 127, 3, 127, 1, 127, 220, 127, 127, 127, 1 };
#define KN_X0 6
#define KN_W  61
#define KN_Y0 TOP_Y
#define KN_H  41
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
    v = clampi(v, knob_lo[k], knob_hi[k]);
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
    case K_FILT: strcpy(o, v ? "KEY" : "FIXED"); break;
    case K_TEST: strcpy(o, v ? "ON" : "OFF"); break;
    default: sprintf(o, "%d", v); break;
    }
}

/* a flat knob: 270-degree track, accent value arc, pointer dot */
static void draw_knob(int cx, int cy, int r, int v, int lo, int hi, int sel)
{
    int pos = hi > lo ? ((v - lo) * 768) / (hi - lo) : 0, a;
    gfx_arc(fb_bot, cx, cy, r, -384, 384, 1, COL_SURF3);
    if (pos > 0) gfx_arc(fb_bot, cx, cy, r, -384, -384 + pos, 1, sel ? COL_TEXT : COL_NOTE);
    gfx_disc(fb_bot, cx, cy, r - 4, COL_SURF2);
    a = (pos - 384) & 1023;
    gfx_disc(fb_bot, cx + ((sine_tab[a] * (r - 7)) >> 15), cy - ((sine_tab[(a + 256) & 1023] * (r - 7)) >> 15), 1, COL_TEXT);
}

static void knob_draw(int k)
{
    int x = KN_X0 + (k & 3) * KN_W, y = KN_Y0 + (k >> 2) * KN_H, cx = x + KN_W / 2 - 1, sel = k == knob_cur;
    char val[10];
    gfx_rect(fb_bot, x, y, KN_W - 3, KN_H - 3, COL_BG);
    gfx_rrect(fb_bot, x, y, KN_W - 3, KN_H - 3, sel ? COL_SURF : COL_BG);
    draw_knob(cx, y + 12, 10, knob_get(k), knob_lo[k], knob_hi[k], sel);
    knob_value(k, val);
    gfx_text_c(fb_bot, cx, y + 25, F_SMALL, sel ? COL_TEXT : COL_TEXT2, knob_names[k]);
    gfx_text_c(fb_bot, cx, y + 32, F_SMALL, COL_TEXT, val);
}

static void panel_draw_static(void)
{
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
        int c = clampi((t_x - KN_X0) / KN_W, 0, 3), r = (t_y - KN_Y0) / KN_H, k = r * 4 + c;
        knob_drag = 0;
        if (t_y < KN_Y0 || k >= K_COUNT) return;
        knob_cur = (uint8_t)k;
        knob_start = knob_get(k);
        saved = (uint8_t)knob_start;
        knob_drag = 1;
    } else if (phase == 1 && knob_drag) {
        int dy = t_sy - t_y, k = knob_cur;
        (void)dy;
        knob_set(k, drag_value(knob_start, knob_lo[k], knob_hi[k], knob_is_enum(k)));
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

/* ================================= DRUM ==================================== */
/* PANEL's second page (tap PANEL again): FOUNDRY's drum synth.  Pick a lane
 * (tapping it plays it), then shape the sound in the kit slot the playing
 * pattern uses: KIT, MODEL, PITCH, PITCH END, SWEEP, DECAY, NOISE, DRIVE,
 * TUNE, LEVEL.  Sound edits re-synthesise the drum in the background (at most
 * every 10 frames while dragging, and on release) and swap it in safely;
 * TUNE and LEVEL act at once.  Knobs a model doesn't use are dimmed.  RESET
 * restores ETCH's preset for that slot. */
enum { D_KIT, D_MODEL, D_PITCH, D_END, D_SWEEP, D_DECAY, D_NOISE, D_DRIVE, D_TUNE, D_LEVEL, D_COUNT };
static const char * const dk_names[D_COUNT] = {
    "KIT", "MODEL", "PITCH", "PITCH END", "SWEEP", "DECAY", "NOISE", "DRIVE", "TUNE", "LEVEL"
};
static const char * const dr_lane_names[4] = { "KICK", "SNARE", "HAT", "OPEN" };
/* knobs each model uses, bit per D_* */
static const uint16_t dr_uses[DT_COUNT] = {
    0x3FF,                                                     /* KICK  */
    0x3FF,                                                     /* SNARE */
    (1 << D_KIT) | (1 << D_MODEL) | (1 << D_PITCH) | (1 << D_DECAY) | (1 << D_DRIVE) | (3 << D_TUNE),
    (1 << D_KIT) | (1 << D_MODEL) | (1 << D_PITCH) | (1 << D_END) | (1 << D_DECAY) | (3 << D_TUNE),
    (1 << D_KIT) | (1 << D_MODEL) | (1 << D_PITCH) | (1 << D_SWEEP) | (1 << D_DECAY) | (1 << D_DRIVE) | (3 << D_TUNE),
    (1 << D_KIT) | (1 << D_MODEL) | (1 << D_PITCH) | (1 << D_SWEEP) | (1 << D_DECAY) | (1 << D_NOISE) | (1 << D_DRIVE) | (3 << D_TUNE),
    (1 << D_KIT) | (1 << D_MODEL) | (1 << D_PITCH) | (1 << D_END) | (1 << D_SWEEP) | (1 << D_DECAY) | (3 << D_TUNE),
    (1 << D_KIT) | (1 << D_MODEL) | (1 << D_PITCH) | (1 << D_END) | (1 << D_DECAY) | (1 << D_NOISE) | (1 << D_DRIVE) | (3 << D_TUNE),
    (1 << D_KIT) | (1 << D_MODEL) | (1 << D_PITCH) | (1 << D_DECAY) | (1 << D_DRIVE) | (3 << D_TUNE),
};
#define DR_X0   6
#define DR_W    49
#define DR_KY   48
#define DR_KH   41
#define DR_WY   134
#define DR_WH   34
static uint8_t dr_lane, dr_cur, dr_area = 1, dr_drag, dr_pending, dr_flash;
static int dr_start;
static uint32_t dr_last_build, dr_sig[D_COUNT], dr_head_sig = 0xFFFFFFFF, dr_wave_sig = 0xFFFFFFFF;
static uint16_t hz_tab[128];

static void dr_tables(void)
{
    uint32_t i, hz = 20u << 16;                        /* 20 Hz .. ~12.4 kHz, exponential */
    for (i = 0; i < 128; i++) { hz_tab[i] = (uint16_t)(hz >> 16); hz = (uint32_t)(((uint64_t)hz * 68916u) >> 16); }
}

static uint8_t dr_kit(void) { return (uint8_t)(SH->proj.pat[SH->cur].kit[dr_lane] & 3); }
static drum_def_t *dr_def(void) { return &foundry_defs[dr_lane][dr_kit()]; }
static int isqrt_i(int v) { int r = 0; while ((r + 1) * (r + 1) <= v) r++; return r; }
static int dr_hz_v(int hz) { int i; for (i = 0; i < 127 && hz_tab[i + 1] <= hz; i++) { } return i; }

static int dr_is_enum(int k) { return k == D_KIT || k == D_MODEL; }
static int dr_hi(int k) { return k == D_KIT ? 3 : k == D_MODEL ? DT_COUNT - 1 : k == D_TUNE || k == D_LEVEL ? 15 : 127; }

static int dr_get(int k)
{
    const drum_def_t *d = dr_def();
    switch (k) {
    case D_KIT: return dr_kit();
    case D_MODEL: return d->type;
    case D_PITCH: return dr_hz_v(d->f0);
    case D_END: return dr_hz_v(d->f1);
    case D_SWEEP: return isqrt_i(((int)d->pitch_ms - 1) * 80);
    case D_DECAY: return isqrt_i(((int)d->amp_ms - 10) * 8);
    case D_NOISE: return d->noise >> 1;
    case D_DRIVE: return d->drive >> 1;
    case D_TUNE: return foundry_tune[dr_lane];
    default: return foundry_level[dr_lane];
    }
}

static void dr_set(int k, int v)
{
    drum_def_t *d = dr_def();
    v = clampi(v, 0, dr_hi(k));
    if (v == dr_get(k)) return;
    switch (k) {
    case D_KIT: app_set_kit(dr_lane, (uint8_t)v); return;
    case D_TUNE: foundry_tune[dr_lane] = (uint8_t)v; SH->proj.drum_tune[dr_lane] = (uint8_t)v; return;
    case D_LEVEL: foundry_level[dr_lane] = (uint8_t)v; SH->proj.drum_level[dr_lane] = (uint8_t)v; return;
    case D_MODEL: d->type = (uint8_t)v; break;
    case D_PITCH: d->f0 = hz_tab[v]; break;
    case D_END: d->f1 = hz_tab[v]; break;
    case D_SWEEP: d->pitch_ms = (uint16_t)(1 + v * v / 80); break;
    case D_DECAY: d->amp_ms = (uint16_t)(10 + v * v / 8); foundry_set_len(d); break;
    case D_NOISE: d->noise = (uint8_t)(v * 2); break;
    default: d->drive = (uint8_t)(v * 2); break;
    }
    dr_pending = 1;                                    /* re-synthesise */
}

static void dr_value(int k, char *o)
{
    const drum_def_t *d = dr_def();
    switch (k) {
    case D_KIT: strcpy(o, foundry_kit_names[dr_lane][dr_kit()]); break;
    case D_MODEL: strcpy(o, foundry_model_names[d->type % DT_COUNT]); break;
    case D_PITCH: sprintf(o, "%d HZ", d->f0); break;
    case D_END: sprintf(o, "%d HZ", d->f1); break;
    case D_SWEEP: sprintf(o, "%d MS", d->pitch_ms); break;
    case D_DECAY: sprintf(o, "%d MS", d->amp_ms); break;
    case D_TUNE: sprintf(o, "%+d", (int)foundry_tune[dr_lane] - 8); break;
    default: sprintf(o, "%d", dr_get(k)); break;
    }
    { char *p = o; while (*p) { if (*p == ' ' && p[1] == 0) *p = 0; p++; } }
}

static void dr_knob(int k)
{
    int x = DR_X0 + (k % 5) * DR_W, y = DR_KY + (k / 5) * DR_KH, cx = x + DR_W / 2 - 2, sel = dr_area == 1 && k == dr_cur;
    int used = (dr_uses[dr_def()->type % DT_COUNT] >> k) & 1;
    char val[16];
    gfx_rect(fb_bot, x, y, DR_W - 3, DR_KH - 3, COL_BG);
    gfx_rrect(fb_bot, x, y, DR_W - 3, DR_KH - 3, sel ? COL_SURF : COL_BG);
    draw_knob(cx, y + 12, 10, dr_get(k), 0, dr_hi(k), sel);
    if (!used) {                                       /* this model ignores it */
        int i, j;
        for (j = y; j < y + DR_KH - 3; j++)
            for (i = x; i < x + DR_W - 3; i++) fb_bot[j * 256 + i] = gfx_mix(fb_bot[j * 256 + i], COL_BG, 170);
    }
    dr_value(k, val);
    gfx_text_c(fb_bot, cx, y + 25, F_SMALL, !used ? COL_TEXT3 : sel ? COL_TEXT : COL_TEXT2, dk_names[k]);
    gfx_text_c(fb_bot, cx, y + 32, F_SMALL, used ? COL_TEXT : COL_TEXT3, val);
}

static void dr_wave(void)
{
    uint32_t len, x;
    const int16_t *s = foundry_sample(dr_lane, dr_kit(), &len);
    char b[16];
    gfx_rect(fb_bot, 6, DR_WY, 244, DR_WH, COL_BG);
    gfx_rrect(fb_bot, 6, DR_WY, 244, DR_WH, COL_SURF);
    if (!s || !len) return;
    for (x = 0; x < 236; x++) {                        /* peak envelope, mirrored */
        uint32_t a = x * len / 236, e = (x + 1) * len / 236, i;
        int pk = 0, h;
        for (i = a; i < e; i++) { int v = s[i] < 0 ? -s[i] : s[i]; if (v > pk) pk = v; }
        h = (pk * (DR_WH / 2 - 3)) >> 15;
        gfx_vline(fb_bot, 10 + (int)x, DR_WY + DR_WH / 2 - h, 2 * h + 1, COL_NOTE);
    }
    sprintf(b, "%lu MS", (unsigned long)(len * 1000u / FS_HZ));
    gfx_text_r(fb_bot, 246, DR_WY + 3, F_SMALL, COL_TEXT2, b);
}

static void dr_head(void)
{
    int i;
    for (i = 0; i < 4; i++)
        button(DR_X0 + i * DR_W, TOP_Y, DR_W - 4, 18, dr_lane_names[i], i == dr_lane,
               dr_area == 0 && dr_cur == i, COL_ACCENT);
    button(DR_X0 + 4 * DR_W, TOP_Y, DR_W - 4, 18, "RESET", dr_flash != 0, dr_area == 0 && dr_cur == 4, COL_ACCENT);
}

static void dr_build_now(void)
{
    foundry_rebuild(dr_lane, dr_kit());
    dr_pending = 0;
    dr_last_build = frame;
}

static void dr_draw_static(void)
{
    if (!hz_tab[0]) dr_tables();
    memset(dr_sig, 0xFF, sizeof dr_sig);
    dr_head_sig = dr_wave_sig = 0xFFFFFFFF;
    gfx_text_c(fb_bot, 128, 178, F_SMALL, COL_TEXT3, "TAP A LANE TO HEAR IT   TAP DRUM AGAIN FOR SYNTH");
}

static void dr_frame(void)
{
    int k;
    uint32_t hs = dr_lane | (dr_area << 3) | (dr_cur << 4) | ((dr_flash != 0) << 8);
    if (dr_pending && (!dr_drag || frame - dr_last_build >= 10)) {
        dr_build_now();
        if (!dr_drag) app_audition(dr_lane, dr_kit());
    }
    if (dr_flash) dr_flash--;
    if (hs != dr_head_sig) { dr_head(); dr_head_sig = hs; }
    for (k = 0; k < D_COUNT; k++) {
        uint32_t sig = (uint32_t)dr_get(k) | ((uint32_t)(dr_area == 1 && k == dr_cur) << 16) |
                       ((uint32_t)dr_def()->type << 20) | ((uint32_t)dr_lane << 26) | ((uint32_t)dr_kit() << 29);
        if (sig != dr_sig[k]) { dr_knob(k); dr_sig[k] = sig; }
    }
    {
        uint32_t ws = (uint32_t)dr_last_build * 7u + dr_lane * 3u + dr_kit() * 5u;
        if (ws != dr_wave_sig) { dr_wave(); dr_wave_sig = ws; }
    }
}

static void dr_lane_tap(int i)
{
    if (i < 4) { dr_lane = (uint8_t)i; app_audition(dr_lane, dr_kit()); }
    else {                                              /* RESET this slot to ETCH's preset */
        foundry_defs[dr_lane][dr_kit()] = foundry_presets[dr_lane][dr_kit()];
        dr_flash = 8;
        dr_build_now();
        app_audition(dr_lane, dr_kit());
    }
}

static void dr_touch(int phase)
{
    static int saved;
    static drum_def_t saved_def;
    static uint8_t saved_tune, saved_level;
    if (phase == 0) {
        dr_drag = 0;
        if (t_y < DR_KY - 4) { dr_area = 0; dr_cur = (uint8_t)clampi((t_x - DR_X0) / DR_W, 0, 4); dr_lane_tap(dr_cur); return; }
        if (t_y >= DR_KY + 2 * DR_KH) { if (t_y >= DR_WY && t_y < DR_WY + DR_WH) app_audition(dr_lane, dr_kit()); return; }
        dr_area = 1;
        dr_cur = (uint8_t)(clampi((t_y - DR_KY) / DR_KH, 0, 1) * 5 + clampi((t_x - DR_X0) / DR_W, 0, 4));
        dr_start = saved = dr_get(dr_cur);
        saved_def = *dr_def();
        saved_tune = foundry_tune[dr_lane];
        saved_level = foundry_level[dr_lane];
        dr_drag = 1;
    } else if (phase == 1 && dr_drag) {
        int dy = t_sy - t_y;
        (void)dy;
        dr_set(dr_cur, drag_value(dr_start, 0, dr_hi(dr_cur), dr_is_enum(dr_cur)));
    } else if (phase == 2 && dr_drag) {
        if (t_temp) {                                              /* L: temporary, exact */
            if (dr_cur == D_KIT) dr_set(D_KIT, saved);
            else {
                *dr_def() = saved_def;
                foundry_tune[dr_lane] = saved_tune; SH->proj.drum_tune[dr_lane] = saved_tune;
                foundry_level[dr_lane] = saved_level; SH->proj.drum_level[dr_lane] = saved_level;
                dr_pending = 1;
            }
        }
        else if (!t_moved && dr_is_enum(dr_cur)) {                 /* tap steps a choice */
            int v = dr_get(dr_cur) + 1;
            dr_set(dr_cur, v > dr_hi(dr_cur) ? 0 : v);
        }
        dr_drag = 0;
        if (!dr_pending) app_audition(dr_lane, dr_kit());
    }
}

static void dr_keys(uint32_t down, uint32_t held, uint32_t rep)
{
    if (held & KEY_A) {
        if (dr_area == 0) { if (down & KEY_A) dr_lane_tap(dr_cur); return; }
        {
            int d = 0, k = dr_cur, big = dr_is_enum(k) || k >= D_TUNE ? 1 : 8;
            if (rep & KEY_RIGHT) d = 1;
            if (rep & KEY_LEFT) d = -1;
            if (rep & KEY_UP) d = big;
            if (rep & KEY_DOWN) d = -big;
            if (d) dr_set(k, dr_get(k) + d);
            if (down & KEY_A) app_audition(dr_lane, dr_kit());
        }
        return;
    }
    if (dr_area == 0) {
        if ((rep & KEY_LEFT) && dr_cur > 0) dr_cur--;
        if ((rep & KEY_RIGHT) && dr_cur < 4) dr_cur++;
        if (rep & KEY_DOWN) dr_area = 1;
    } else {
        if ((rep & KEY_LEFT) && dr_cur % 5) dr_cur--;
        if ((rep & KEY_RIGHT) && dr_cur % 5 < 4) dr_cur++;
        if (rep & KEY_UP) { if (dr_cur >= 5) dr_cur -= 5; else dr_area = 0; }
        if ((rep & KEY_DOWN) && dr_cur < 5) dr_cur += 5;
    }
}

/* ================================= GEN ==================================== */
enum { G_KEY, G_SCALE, G_STYLE, G_OCT, G_DENS, G_SYNC, G_ACC, G_SLIDE, G_MODE, G_NPARAM };
enum { GI_CODE = G_NPARAM, GI_BUTTONS, GI_MUTATE, GI_COUNT };
static const char * const gen_names[G_NPARAM] = {
    "KEY", "SCALE", "STYLE", "OCTAVE", "DENSITY", "SYNCOP", "ACCENT", "SLIDE", "EVOLUTION"
};
static const uint8_t gen_max[G_NPARAM] = { 11, SCALE_COUNT - 1, STYLE_COUNT - 1, 2, 15, 15, 15, 15, EVO_COUNT - 1 };
#define GR_Y0  TOP_Y
#define GR_H   11
#define GV_X   56              /* value column */
#define SL_X0  130             /* fader */
#define SL_W   118
#define CODE_Y 128
#define GB_Y   156
static uint8_t gen_item, gen_sub;
static uint32_t gen_sig = 0xFFFFFFFF;
static uint8_t gen_flash[3];
static int8_t t_gen_row;

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

static int code_x(int i) { return 44 + i * 15 + (i / 4) * 8; }

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
    gfx_rect(fb_bot, 0, TOP_Y, 256, 192 - TOP_Y, COL_BG);
    for (i = 0; i < G_NPARAM; i++) {
        int y = GR_Y0 + i * GR_H, val = *gen_field(i), w, sel = gen_item == i;
        switch (i) {
        case G_KEY: strcpy(v, notes12[val % 12]); if (v[1] == '-') v[1] = 0; break;
        case G_SCALE: strcpy(v, corr_scale_names[val % SCALE_COUNT]); break;
        case G_STYLE: strcpy(v, corr_style_names[val % STYLE_COUNT]); break;
        case G_OCT: sprintf(v, "%d", val + 1); break;
        case G_MODE: strcpy(v, corr_mode_names[val % EVO_COUNT]); break;
        default: sprintf(v, "%d", val); break;
        }
        if (sel) gfx_rrect(fb_bot, 4, y, 248, GR_H - 1, COL_SURF);
        gfx_text(fb_bot, 8, y + 3, F_SMALL, sel ? COL_TEXT : COL_TEXT2, gen_names[i]);
        gfx_text(fb_bot, GV_X, y + 2, F_MAIN, COL_TEXT, v);
        w = (val * (SL_W - 6)) / gen_max[i];
        gfx_rrect(fb_bot, SL_X0, y + 4, SL_W, 3, COL_SURF2);
        if (w > 1) gfx_rect(fb_bot, SL_X0 + 1, y + 5, w, 1, COL_NOTE);
        gfx_rrect(fb_bot, SL_X0 + w, y + 1, 6, 9, sel ? COL_TEXT : COL_NOTE);
    }
    /* seed code: tap the upper half of a digit to raise it, the lower half to lower it */
    corr_code_encode(g, code);
    gfx_rrect(fb_bot, 6, CODE_Y, 244, 23, COL_SURF);
    gfx_text(fb_bot, 12, CODE_Y + 6, F_SMALL, gen_item == GI_CODE ? COL_TEXT : COL_TEXT2, "SEED");
    gfx_text(fb_bot, 12, CODE_Y + 13, F_SMALL, COL_TEXT3, "+/-");
    for (i = 0; i < 12; i++) {
        int x = code_x(i), sel = gen_item == GI_CODE && gen_sub == i;
        gfx_seg7(fb_bot, x, CODE_Y + 5, code[i], 5, i < 4 ? COL_TEXT : COL_NOTE, COL_SURF2);
        if (sel) gfx_rect(fb_bot, x, CODE_Y + 20, 9, 1, COL_ACCENT);
    }
    for (i = 0; i < 3; i++) {
        static const char * const bn[3] = { "GEN ALL", "BASS", "DRUMS" };
        button(6 + i * 56, GB_Y, 52, 30, bn[i], gen_flash[i] != 0, gen_item == GI_BUTTONS && gen_sub == i, COL_ACCENT);
    }
    gfx_rrect(fb_bot, 174, GB_Y, 76, 30, gen_item == GI_MUTATE ? COL_SURF : COL_BG);
    draw_knob(190, GB_Y + 15, 11, g->mutation, 0, 15, gen_item == GI_MUTATE);
    gfx_text(fb_bot, 208, GB_Y + 6, F_SMALL, gen_item == GI_MUTATE ? COL_TEXT : COL_TEXT2, "MUTATE");
    gfx_textf(fb_bot, 208, GB_Y + 14, F_MAIN, COL_TEXT, "%d", g->mutation);
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
        t_gen_row = -1;
        if (t_y >= GR_Y0 && t_y < GR_Y0 + G_NPARAM * GR_H) {
            gen_item = (uint8_t)((t_y - GR_Y0) / GR_H);
            t_gen_row = (int8_t)gen_item;
        } else if (t_y >= CODE_Y && t_y < CODE_Y + 23) {
            int i;
            for (i = 0; i < 12; i++) if (t_x >= code_x(i) - 3 && t_x < code_x(i) + 12) {
                gen_item = GI_CODE; gen_sub = (uint8_t)i;
                gen_set_digit(i, t_y < CODE_Y + 11 ? 1 : -1);
            }
            return;
        } else if (t_y >= GB_Y - 2 && t_x < 172) {
            int i = clampi((t_x - 6) / 56, 0, 2);
            gen_item = GI_BUTTONS; gen_sub = (uint8_t)i;
            gen_action(i);
            return;
        } else if (t_y >= GB_Y - 2) {
            gen_item = GI_MUTATE; t_gen_row = 100;
            dial_start = SH->proj.gen.mutation;
            return;
        } else return;
    }
    if (phase < 2 && t_gen_row >= 0 && t_gen_row < G_NPARAM && t_x >= SL_X0 - 8) {
        int v = ((t_x - SL_X0) * gen_max[t_gen_row] + SL_W / 2) / SL_W;
        *gen_field(t_gen_row) = (uint8_t)clampi(v, 0, gen_max[t_gen_row]);
    } else if (phase == 1 && t_gen_row == 100) {
        SH->proj.gen.mutation = (uint8_t)drag_value(dial_start, 0, 15, 0);
    }
}

static void gen_keys(uint32_t down, uint32_t held, uint32_t rep)
{
    if (held & KEY_A) {
        int d = (rep & KEY_RIGHT) ? 1 : (rep & KEY_LEFT) ? -1 : (rep & KEY_UP) ? 4 : (rep & KEY_DOWN) ? -4 : 0;
        if (gen_item < G_NPARAM && d)
            *gen_field(gen_item) = (uint8_t)clampi(*gen_field(gen_item) + d, 0, gen_max[gen_item]);
        else if (gen_item == GI_CODE && d) gen_set_digit(gen_sub, d > 0 ? 1 : -1);
        else if (gen_item == GI_MUTATE && d)
            SH->proj.gen.mutation = (uint8_t)clampi(SH->proj.gen.mutation + (d > 0 ? 1 : -1), 0, 15);
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

/* ================================= KEYS ==================================== */
/* Two octaves of the GEN key and scale only (no wrong notes), an ACCENT and a
 * SLIDE latch, octave shift, and REC: while playing, a key writes into the
 * step that is sounding; while stopped, it writes step by step (ETCH-style
 * step entry; B + A writes a rest).  Holding a key takes the 303 from the
 * sequencer until you let go. */
#define KB_Y0    52
#define KB_Y1    144
#define KB_BTN_Y 150
static uint8_t kb_oct = 1, kb_acc, kb_slide, kb_rec, kb_rec_step;
static uint8_t kb_n, kb_note[26];
static int8_t kb_cur = 0, kb_down = -1, kb_row;
static uint8_t kb_by_button, kb_flash;
static uint32_t kb_sig = 0xFFFFFFFF;
static const char * const kb_btn_names[5] = { "ACCENT", "SLIDE", "OCT -", "OCT +", "REC" };

static void kb_layout(void)
{
    const gen_t *g = &SH->proj.gen;
    int root, m, max_oct = (NOTE_MAX - 24 - g->key % 12) / 12;
    if (kb_oct > max_oct) kb_oct = (uint8_t)max_oct;
    root = g->key % 12 + 12 * kb_oct;
    kb_n = 0;
    for (m = root; m <= root + 24 && kb_n < 25; m++)
        if (corr_in_scale((uint8_t)m, g->key % 12, g->scale % SCALE_COUNT)) kb_note[kb_n++] = (uint8_t)m;
    if (kb_cur >= kb_n) kb_cur = (int8_t)(kb_n - 1);
}

static int kb_pitch(void) { return 246 / (kb_n ? kb_n : 1); }
static int kb_x0(void) { return 6 + (246 - kb_pitch() * kb_n) / 2; }

static void kb_record(uint8_t note)
{
    uint8_t f = (uint8_t)(STEP_NOTE | (kb_acc ? STEP_ACCENT : 0) | (kb_slide ? STEP_SLIDE : 0));
    if (!kb_rec) return;
    if (SH->playing) app_set_bass(SH->step, note, f);
    else { app_set_bass(kb_rec_step, note, f); kb_rec_step = (uint8_t)((kb_rec_step + 1) & 15); }
    kb_flash = 6;
}

static void kb_play(int i, int legato)
{
    if (i < 0 || i >= kb_n) return;
    live_note(legato && kb_slide && kb_down >= 0 ? EV_GLIDE : EV_NOTE, kb_note[i], kb_acc);
    kb_down = (int8_t)i;
    kb_record(kb_note[i]);
}

static void kb_release(void)
{
    if (kb_down >= 0) live_note(EV_GATE_OFF, 0, 0);
    kb_down = -1;
}

static void kb_button(int i)
{
    switch (i) {
    case 0: kb_acc ^= 1; break;
    case 1: kb_slide ^= 1; break;
    case 2: if (kb_oct > 0) kb_oct--; break;
    case 3: kb_oct++; break;
    default: kb_rec ^= 1; kb_rec_step = 0; break;
    }
    kb_layout();
}

static void kb_draw(void)
{
    int i, w = kb_pitch(), x0 = kb_x0();
    char b[16];
    gfx_rect(fb_bot, 0, TOP_Y, 256, 192 - TOP_Y, COL_BG);
    if (kb_down >= 0) {
        uint8_t n = kb_note[kb_down];
        sprintf(b, "%s%d", notes12[n % 12], n / 12 + 1);
        if (b[1] == '-') memmove(b + 1, b + 2, strlen(b + 1));
    } else strcpy(b, "-");
    stat_block(6, TOP_Y, 58, "NOTE", b, kb_down >= 0 ? COL_TEXT : COL_TEXT3);
    sprintf(b, "%s %s", notes12[SH->proj.gen.key % 12], corr_scale_names[SH->proj.gen.scale % SCALE_COUNT]);
    if (b[1] == '-') memmove(b + 1, b + 2, strlen(b + 1));
    stat_block(68, TOP_Y, 86, "SCALE", b, COL_TEXT);
    sprintf(b, "%d", kb_oct + 1);
    stat_block(158, TOP_Y, 42, "OCTAVE", b, COL_TEXT);
    if (kb_rec) sprintf(b, "%02d", (SH->playing ? SH->step : kb_rec_step) + 1);
    else strcpy(b, "OFF");
    stat_block(204, TOP_Y, 46, "REC STEP", b, kb_rec ? COL_TEXT : COL_TEXT3);
    for (i = 0; i < kb_n; i++) {
        int x = x0 + i * w, root = (kb_note[i] % 12) == SH->proj.gen.key % 12;
        int down = i == kb_down, cur = kb_row == 0 && i == kb_cur;
        const char *nm = notes12[kb_note[i] % 12];
        char lab[3] = { nm[0], nm[1] == '#' ? '#' : 0, 0 };
        gfx_rrect(fb_bot, x, KB_Y0, w - 2, KB_Y1 - KB_Y0, down ? COL_TEXT : root ? COL_SURF3 : COL_SURF2);
        if (cur) gfx_rframe(fb_bot, x, KB_Y0, w - 2, KB_Y1 - KB_Y0, down ? COL_TEXT : COL_ACCENT);
        if (root) gfx_disc(fb_bot, x + (w - 2) / 2, KB_Y1 - 22, 1, down ? COL_BG : COL_TEXT);
        if (down && kb_acc) gfx_rect(fb_bot, x + 3, KB_Y0 + 4, w - 8, 2, COL_BG);
        if (w >= 12) gfx_text_c(fb_bot, x + (w - 2) / 2, KB_Y1 - 12, F_SMALL, down ? COL_BG : root ? COL_TEXT : COL_TEXT2, lab);
    }
    for (i = 0; i < 5; i++) {
        int on = i == 0 ? kb_acc : i == 1 ? kb_slide : i == 4 ? kb_rec : 0;
        button(6 + i * 50, KB_BTN_Y, 44, 36, kb_btn_names[i], on, kb_row == 1 && kb_cur == i, i == 4 ? COL_RED : COL_ACCENT);
    }
}

static void kb_frame(void)
{
    uint32_t sig;
    kb_layout();
    sig = (uint32_t)kb_down * 7 + kb_cur * 131 + kb_row * 3 + kb_acc * 17 + kb_slide * 19 + kb_rec * 23 +
          kb_oct * 29 + kb_n * 37 + SH->proj.gen.key * 41 + SH->proj.gen.scale * 43 +
          (kb_rec ? (uint32_t)(SH->playing ? SH->step : kb_rec_step) * 1009 : 0) + (kb_flash ? 1 : 0) * 7919;
    if (kb_flash) kb_flash--;
    if (sig != kb_sig) { kb_draw(); kb_sig = sig; }
}

static void kb_touch(int phase)
{
    int w = kb_pitch(), x0 = kb_x0();
    if (phase == 0 && t_y >= KB_BTN_Y - 2) {
        kb_row = 1; kb_cur = (int8_t)clampi((t_x - 6) / 50, 0, 4);
        kb_button(kb_cur);
        return;
    }
    if (t_y < KB_Y0 || t_y >= KB_Y1) { if (phase == 2) kb_release(); return; }
    if (phase < 2) {
        int i = clampi((t_x - x0) / w, 0, kb_n - 1);
        kb_row = 0;
        if (phase == 0 || i != kb_down) { kb_cur = (int8_t)i; kb_play(i, phase == 1); }
    } else kb_release();
}

static void kb_keys(uint32_t down, uint32_t held, uint32_t rep)
{
    if (kb_by_button && !(held & KEY_A)) { kb_release(); kb_by_button = 0; }
    if (held & KEY_A) {
        if ((down & KEY_A) && (held & KEY_B) && kb_rec && !SH->playing) {      /* B + A: a rest */
            app_set_bass(kb_rec_step, 12, 0);
            kb_rec_step = (uint8_t)((kb_rec_step + 1) & 15);
            return;
        }
        if (down & KEY_A) {
            if (kb_row == 1) kb_button(kb_cur);
            else { kb_play(kb_cur, 0); kb_by_button = 1; }
            return;
        }
        if (kb_row == 0 && kb_by_button) {                                  /* A + D-pad: next note */
            if ((rep & KEY_RIGHT) && kb_cur < kb_n - 1) { kb_cur++; kb_play(kb_cur, 1); }
            if ((rep & KEY_LEFT) && kb_cur > 0) { kb_cur--; kb_play(kb_cur, 1); }
        }
        return;
    }
    if (rep & KEY_UP) kb_row = 0;
    if (rep & KEY_DOWN) { kb_row = 1; if (kb_cur > 4) kb_cur = 4; }
    if ((rep & KEY_LEFT) && kb_cur > 0) kb_cur--;
    if ((rep & KEY_RIGHT) && kb_cur < (kb_row ? 4 : kb_n - 1)) kb_cur++;
}

/* ================================= SONG ==================================== */
/* The 32 patterns (tap to queue one for the next bar; with SONG on, tap to
 * put it in the selected chain slot) and ETCH's 16-slot chain with repeats. */
#define CELL_X(c) (6 + (c) * 31)
#define CELL_W    27
#define BANK_Y0   35
#define CHAIN_Y0  110
#define CELL_P    15
#define SG_BTN_Y  148
static uint8_t sg_sel, sg_area, sg_cur, sg_flash[6];
static uint32_t sg_sig = 0xFFFFFFFF;
static const char * const sg_btn_names[6] = { "SONG", "PAT -", "PAT +", "REP -", "REP +", "COPY" };

static void sg_draw(void)
{
    const chain_t *ch = &SH->proj.chain;
    int i, cur = SH->cur, q = SH->queued, blink = (frame >> 4) & 1;
    char b[12];
    gfx_rect(fb_bot, 0, TOP_Y, 256, 192 - TOP_Y, COL_BG);
    gfx_text(fb_bot, 6, TOP_Y + 1, F_SMALL, COL_TEXT2, "PATTERNS");
    gfx_text_r(fb_bot, 250, TOP_Y + 1, F_SMALL, COL_TEXT3, ch->song ? "TAP TO SET THE SLOT" : "TAP TO PLAY NEXT BAR");
    for (i = 0; i < NUM_PATTERNS; i++) {
        int x = CELL_X(i & 7), y = BANK_Y0 + (i >> 3) * CELL_P, empty = app_pattern_empty((uint8_t)i);
        int now = i == cur;
        gfx_rrect(fb_bot, x, y, CELL_W, 13, now ? COL_ACCENT : empty ? COL_SURF : COL_SURF2);
        if (i == q && !now) gfx_rframe(fb_bot, x, y, CELL_W, 13, blink ? COL_ACCENT : COL_ACC_DIM);
        if (sg_area == 0 && sg_cur == i) gfx_rframe(fb_bot, x, y, CELL_W, 13, now ? COL_TEXT : COL_TEXT);
        sprintf(b, "%02d", i);
        gfx_text_c(fb_bot, x + CELL_W / 2, y + 3, F_MAIN, now ? COL_TEXT : empty ? COL_TEXT3 : COL_TEXT, b);
    }
    gfx_text(fb_bot, 6, CHAIN_Y0 - 9, F_SMALL, COL_TEXT2, "CHAIN");
    gfx_text(fb_bot, 34, CHAIN_Y0 - 9, F_SMALL, ch->song ? COL_TEXT : COL_TEXT3, ch->song ? "SONG ON" : "SONG OFF");
    sprintf(b, "BAR %d/%d", SH->chain_bar + 1, ch->rep[SH->chain_pos & 15]);
    if (ch->song) gfx_text_r(fb_bot, 250, CHAIN_Y0 - 9, F_SMALL, COL_TEXT3, b);
    for (i = 0; i < CHAIN_LEN; i++) {
        int x = CELL_X(i & 7), y = CHAIN_Y0 + (i >> 3) * CELL_P;
        int playing = ch->song && SH->playing && SH->chain_pos == i;
        gfx_rrect(fb_bot, x, y, CELL_W, 13, playing ? COL_ACCENT : ch->pat[i] < NUM_PATTERNS ? COL_SURF2 : COL_SURF);
        if (sg_sel == i) gfx_rframe(fb_bot, x, y, CELL_W, 13, sg_area == 1 ? COL_TEXT : COL_TEXT3);
        if (ch->pat[i] < NUM_PATTERNS) {
            sprintf(b, "%02d", ch->pat[i]);
            gfx_text(fb_bot, x + 2, y + 3, F_MAIN, COL_TEXT, b);
            sprintf(b, "X%d", ch->rep[i]);
            gfx_text_r(fb_bot, x + CELL_W - 2, y + 5, F_SMALL, playing ? COL_TEXT : COL_TEXT3, b);
        } else gfx_text_c(fb_bot, x + CELL_W / 2, y + 4, F_SMALL, COL_TEXT3, "END");
    }
    for (i = 0; i < 6; i++)
        button(6 + i * 41, SG_BTN_Y, 38, 24, sg_btn_names[i], i == 0 ? ch->song : sg_flash[i] != 0,
               sg_area == 2 && sg_cur == i, i == 0 ? COL_EVOLVE : COL_ACCENT);
    gfx_text_c(fb_bot, 128, 180, F_SMALL, COL_TEXT3, "TAP THE SONG TAB AGAIN FOR FILE");
}

static void sg_frame(void)
{
    const chain_t *ch = &SH->proj.chain;
    uint32_t sig = SH->cur * 3u + SH->queued * 5u + ch->song * 7u + sg_sel * 11u + sg_area * 13u + sg_cur * 17u +
                   SH->chain_pos * 19u + SH->chain_bar * 23u + SH->playing * 29u + ((frame >> 4) & 1) * (SH->queued != 0xFF) * 31u;
    int i;
    for (i = 0; i < CHAIN_LEN; i++) sig = sig * 33 + ch->pat[i] * 9 + ch->rep[i];
    for (i = 0; i < NUM_PATTERNS; i++) sig = sig * 3 + app_pattern_empty((uint8_t)i);
    for (i = 0; i < 6; i++) { sig = sig * 5 + (sg_flash[i] != 0); if (sg_flash[i]) sg_flash[i]--; }
    if (sig != sg_sig) { sg_draw(); sg_sig = sig; }
}

static void sg_pick_pattern(uint8_t p)
{
    const chain_t *ch = &SH->proj.chain;
    if (ch->song) app_chain(sg_sel, p, ch->pat[sg_sel] < NUM_PATTERNS ? ch->rep[sg_sel] : 4);
    else app_queue(p);
}

static void sg_button(int i)
{
    const chain_t *ch = &SH->proj.chain;
    uint8_t pat = ch->pat[sg_sel], rep = ch->rep[sg_sel];
    sg_flash[i] = 8;
    switch (i) {
    case 0: app_song(!ch->song); break;
    case 1: app_chain(sg_sel, pat >= NUM_PATTERNS ? NUM_PATTERNS - 1 : pat == 0 ? 0xFF : (uint8_t)(pat - 1), rep); break;
    case 2: app_chain(sg_sel, pat >= NUM_PATTERNS ? 0 : pat == NUM_PATTERNS - 1 ? 0xFF : (uint8_t)(pat + 1), rep); break;
    case 3: if (pat < NUM_PATTERNS && rep > 1) app_chain(sg_sel, pat, (uint8_t)(rep - 1)); break;
    case 4: if (pat < NUM_PATTERNS && rep < 8) app_chain(sg_sel, pat, (uint8_t)(rep + 1)); break;
    default: {                                   /* copy into the next empty pattern, play it next */
        uint8_t k;
        for (k = 1; k < NUM_PATTERNS; k++) {
            uint8_t t = (uint8_t)((SH->cur + k) & 31);
            if (app_pattern_empty(t)) {
                pattern_t p = SH->proj.pat[SH->cur];
                app_replace_at(t, &p, 0);
                if (!ch->song) app_queue(t);
                break;
            }
        }
        break; }
    }
}

static void sg_touch(int phase)
{
    int c = clampi((t_x - 6) / 31, 0, 7);
    if (phase != 0) return;
    if (t_y >= BANK_Y0 - 1 && t_y < BANK_Y0 + 4 * CELL_P) {
        uint8_t p = (uint8_t)(clampi((t_y - BANK_Y0 + 1) / CELL_P, 0, 3) * 8 + c);
        sg_area = 0; sg_cur = p;
        sg_pick_pattern(p);
    } else if (t_y >= CHAIN_Y0 - 1 && t_y < CHAIN_Y0 + 2 * CELL_P) {
        uint8_t s = (uint8_t)(clampi((t_y - CHAIN_Y0 + 1) / CELL_P, 0, 1) * 8 + c);
        if (s == sg_sel && SH->proj.chain.song) app_jump(s);       /* tap again: jump there */
        sg_area = 1; sg_sel = s;
    } else if (t_y >= SG_BTN_Y - 2 && t_y < SG_BTN_Y + 26) {
        sg_area = 2; sg_cur = (uint8_t)clampi((t_x - 6) / 41, 0, 5);
        sg_button(sg_cur);
    }
}

static void sg_keys(uint32_t down, uint32_t held, uint32_t rep)
{
    const chain_t *ch = &SH->proj.chain;
    if (held & KEY_A) {
        if (sg_area == 1) {
            if (rep & KEY_UP) sg_button(2);
            if (rep & KEY_DOWN) sg_button(1);
            if (rep & KEY_RIGHT) sg_button(4);
            if (rep & KEY_LEFT) sg_button(3);
        }
        if (down & KEY_A) {
            if (held & KEY_B) { if (sg_area == 1) app_chain(sg_sel, 0xFF, 1); return; }   /* B + A: END */
            if (sg_area == 0) sg_pick_pattern(sg_cur);
            else if (sg_area == 1 && ch->song) app_jump(sg_sel);
            else if (sg_area == 2) sg_button(sg_cur);
        }
        return;
    }
    if (sg_area == 0) {
        if (rep & KEY_LEFT) sg_cur = (uint8_t)((sg_cur + 31) & 31);
        if (rep & KEY_RIGHT) sg_cur = (uint8_t)((sg_cur + 1) & 31);
        if ((rep & KEY_UP) && sg_cur >= 8) sg_cur -= 8;
        if (rep & KEY_DOWN) { if (sg_cur < 24) sg_cur += 8; else sg_area = 1; }
    } else if (sg_area == 1) {
        if (rep & KEY_LEFT) sg_sel = (uint8_t)((sg_sel + 15) & 15);
        if (rep & KEY_RIGHT) sg_sel = (uint8_t)((sg_sel + 1) & 15);
        if (rep & KEY_UP) { if (sg_sel >= 8) sg_sel -= 8; else sg_area = 0; }
        if (rep & KEY_DOWN) { if (sg_sel < 8) sg_sel += 8; else { sg_area = 2; sg_cur = 0; } }
    } else {
        if ((rep & KEY_LEFT) && sg_cur > 0) sg_cur--;
        if ((rep & KEY_RIGHT) && sg_cur < 5) sg_cur++;
        if (rep & KEY_UP) sg_area = 1;
    }
}

/* ================================= FILE ==================================== */
/* The SONG tab's second page.  16 save slots in one file on the SD card, and
 * ETCH Game Boy saves to import.  LOAD, ERASE and overwriting a full slot ask
 * twice; loading jumps to LIVE and plays. */
#define FL_Y0  35
#define FL_P   14
#define FL_W   121
#define FL_BTN 150
static uint8_t fl_src, fl_sel, fl_area, fl_btn, fl_arm = 0xFF, fl_arm_t, fl_valid;
#define FL_N() (fl_src == 2 ? GBA_SLOTS : fl_src ? GB_SLOTS : DS_SLOTS)   /* 0 DS, 1 ETCH (GB), 2 ETCH II (GBA) */
static uint8_t fl_status[DS_SLOTS], fl_last = SAVE_NONE;
static char fl_names[DS_SLOTS][PROJECT_NAME_LEN + 1];
static char fl_msg[40];
static uint8_t fl_msg_t;
static uint16_t fl_msg_col;
static uint32_t fl_sig = 0xFFFFFFFF;


/* ---- ABOUT (from FILE): four pages, standalone ------------------------------- */
#define N_ABOUT 4
static const char * const about_title[N_ABOUT] = { "ETCH DS", "GENERATIVE MUSIC", "SYNTH CENTRIC", "CREDITS" };
/* the series' ABOUT texts: the same on every ETCH */
static const char * const about_text[N_ABOUT] = {
    "A generative acid machine by Synth Centric. Version " ETCH_DS_VERSION ".\n"
    "Its composer, CORROSION, writes the music and keeps rewriting it. An acid bass voice, six drum lanes and an effects chain play it on the Nintendo DS, with the touch screen for performing.",
    "Generative music is made by rules and chance. CORROSION composes from a seed and keeps rewriting, so every listen is different.\n"
    "A seed code always makes the same groove: share it to share the song.",
    "Dan Schwebel, known as Synth Centric, is an Australian beat maker, music producer, writer and educator.\n"
    "ETCH DS is a Synth Centric release.",
    "Code and CORROSION: MIT licence. Music, artwork, names: (c) Synth Centric.\n"
    "Built with BlocksDS. Every sound is synthesised: no samples.",
};
static uint8_t fl_about, ab_page;

static void about_draw(void)
{
    int i;
    char b[8];
    gfx_rect(fb_bot, 0, TOP_Y, 256, 192 - TOP_Y, COL_BG);
    gfx_text(fb_bot, 6, TOP_Y + 1, F_SMALL, COL_TEXT2, "ABOUT");
    sprintf(b, "%d / %d", ab_page + 1, N_ABOUT);
    gfx_text_r(fb_bot, 250, TOP_Y + 1, F_SMALL, COL_TEXT3, b);
    gfx_text_scaled(fb_bot, 6, TOP_Y + 12, F_MAIN, 2, COL_TEXT, about_title[ab_page]);
    gfx_wrap(fb_bot, 6, TOP_Y + 34, 244, F_MAIN, 10, COL_TEXT, about_text[ab_page]);
    button(6, 162, 58, 24, "BACK", 0, 0, COL_ACCENT);
    button(162, 162, 42, 24, "<", 0, 0, COL_ACCENT);
    button(208, 162, 42, 24, ">", 0, 0, COL_ACCENT);
    for (i = 0; i < N_ABOUT; i++)                      /* page dots */
        gfx_disc(fb_bot, 104 + i * 10, 174, i == ab_page ? 2 : 1, i == ab_page ? COL_TEXT : COL_SURF3);
}

static void fl_say(uint16_t col, const char *m) { strncpy(fl_msg, m, 39); fl_msg[39] = 0; fl_msg_t = 180; fl_msg_col = col; }

static void fl_refresh(void)
{
    int i, n = FL_N();
    memset(fl_status, 0, sizeof fl_status);
    if (app_card)
        for (i = 0; i < n; i++) {
            memset(fl_names[i], 0, sizeof fl_names[i]);
            fl_status[i] = fl_src == 2 ? storage_gba_status((uint8_t)i, fl_names[i]) : fl_src ? storage_gb_status((uint8_t)i, fl_names[i]) : storage_slot_status((uint8_t)i, fl_names[i]);
        }
    fl_last = (app_card && !fl_src) ? storage_last_slot() : SAVE_NONE;
    fl_valid = 1;
}

static const char * const fl_btn_ds[5] = { "SAVE", "LOAD", "ERASE", "GB", "ABOUT" };
static const char * const fl_btn_gb[5] = { "IMPORT", "", "", "GBA", "ABOUT" };
static const char * const fl_btn_gba[5] = { "IMPORT", "", "", "DS", "ABOUT" };
#define FL_BP 49                /* button pitch: 5 x 45 + 4 x 4 */

static void fl_draw(void)
{
    int i, n = FL_N();
    char b[16];
    gfx_rect(fb_bot, 0, TOP_Y, 256, 192 - TOP_Y, COL_BG);
    if (!app_card) {
        gfx_text_c(fb_bot, 128, 80, F_MAIN, COL_TEXT, "NO SD CARD");
        gfx_text_c(fb_bot, 128, 94, F_SMALL, COL_TEXT2, "SAVES NEED A FLASH CART OR LOADER");
        gfx_text_c(fb_bot, 128, 102, F_SMALL, COL_TEXT2, "THAT DLDI-PATCHES HOMEBREW");
        button(6 + 4 * FL_BP, FL_BTN, 45, 22, "ABOUT", 0, fl_area == 1 && fl_btn == 4, COL_ACCENT);
        return;
    }
    gfx_text(fb_bot, 6, TOP_Y + 1, F_SMALL, COL_TEXT2, fl_src == 2 ? "ETCH II (GBA) SAVES" : fl_src ? "GAME BOY SAVES" : "SAVE SLOTS");
    gfx_text_r(fb_bot, 250, TOP_Y + 1, F_SMALL, COL_TEXT3, fl_src == 2 ? "/ETCH_DS/ETCH_GBA.SAV" : fl_src ? "/ETCH_DS/ETCH_GB.SAV" : "/ETCH_DS/ETCH_DS.SAV");
    for (i = 0; i < n; i++) {
        int x = 6 + (i >> 3) * (FL_W + 2), y = FL_Y0 + (i & 7) * FL_P, sel = fl_area == 0 && fl_sel == i;
        int ok = fl_status[i] == SLOT_OK;
        gfx_rrect(fb_bot, x, y, FL_W, 12, ok ? COL_SURF2 : COL_SURF);
        if (sel) gfx_rframe(fb_bot, x, y, FL_W, 12, COL_ACCENT);
        sprintf(b, "%02d", i + 1);
        gfx_text(fb_bot, x + 5, y + 4, F_SMALL, ok ? COL_TEXT2 : COL_TEXT3, b);
        if (ok) {
            int k;
            for (k = PROJECT_NAME_LEN - 1; k >= 0 && (fl_names[i][k] == ' ' || !fl_names[i][k]); k--) fl_names[i][k] = 0;
            gfx_text(fb_bot, x + 20, y + 3, F_MAIN, COL_TEXT, fl_names[i]);
            if (i == fl_last) gfx_disc(fb_bot, x + FL_W - 7, y + 6, 1, COL_TEXT);
        } else gfx_text(fb_bot, x + 20, y + 4, F_SMALL, fl_status[i] == SLOT_BAD ? COL_RED : COL_TEXT3,
                        fl_status[i] == SLOT_BAD ? "DAMAGED" : "EMPTY");
    }
    if (fl_src) {
        gfx_text(fb_bot, 129 + 6, FL_Y0 + 4, F_SMALL, COL_TEXT2, "COPY THE CARTRIDGE'S");
        gfx_text(fb_bot, 129 + 6, FL_Y0 + 12, F_SMALL, COL_TEXT2, "32 KB .SAV TO THE PATH");
        gfx_text(fb_bot, 129 + 6, FL_Y0 + 20, F_SMALL, COL_TEXT2, "ABOVE. IMPORT IS READ ONLY.");
    }
    for (i = 0; i < 5; i++) {
        const char *nm = fl_src == 2 ? fl_btn_gba[i] : fl_src ? fl_btn_gb[i] : fl_btn_ds[i];
        int armed = fl_arm == i;
        if (!nm[0]) continue;
        button(6 + i * FL_BP, FL_BTN, 45, 22, nm, armed && (fl_arm_t & 8), fl_area == 1 && fl_btn == i, COL_ACCENT);
        if (armed && !(fl_arm_t & 8)) gfx_rframe(fb_bot, 6 + i * FL_BP, FL_BTN, 45, 22, COL_ACCENT);
    }
    if (fl_msg_t) {                                   /* messages: black text, an accent mark for news */
        if (fl_msg_col == COL_EVOLVE) gfx_rect(fb_bot, 2, 180, 2, 7, COL_ACCENT);
        gfx_text(fb_bot, 6, 180, F_MAIN, COL_TEXT, fl_msg);
    }
}

static void fl_action(int b)
{
    char m[40];
    uint8_t s = fl_sel;
    if (b == 4) { fl_about = 1; ab_page = 0; fl_arm = 0xFF; return; }
    if (!app_card) return;
    if (b == 3) { fl_src = (uint8_t)((fl_src + 1) % 3); fl_sel = 0; fl_arm = 0xFF; fl_refresh(); return; }
    if (fl_src) {                                      /* IMPORT */
        if (b != 0) return;
        if (fl_status[s] != SLOT_OK) { fl_say(COL_RED, "NOTHING TO IMPORT THERE"); return; }
        if (fl_arm != 0) { fl_arm = 0; fl_arm_t = 120; fl_say(COL_ACCENT, "REPLACES YOUR SONG. PRESS AGAIN"); return; }
        fl_arm = 0xFF;
        if (fl_src == 2 ? app_import_gba(s) : app_import_gb(s)) {
            fl_say(COL_EVOLVE, fl_src == 2 ? "IMPORTED FROM ETCH II" : "IMPORTED FROM GAME BOY"); tab = TAB_LIVE; sg_page = 0; need_full = 1;
        }
        else fl_say(COL_RED, "IMPORT FAILED");
        return;
    }
    if (b == 0) {                                      /* SAVE */
        if (fl_status[s] == SLOT_OK && fl_arm != 0) {
            fl_arm = 0; fl_arm_t = 120;
            sprintf(m, "OVERWRITE %02d? PRESS AGAIN", s + 1); fl_say(COL_ACCENT, m); return;
        }
        fl_arm = 0xFF;
        if (app_save_slot(s)) { sprintf(m, "SAVED TO %02d AND VERIFIED", s + 1); fl_say(COL_EVOLVE, m); }
        else fl_say(COL_RED, "SAVE FAILED: CARD ERROR");
        fl_refresh();
    } else if (b == 1) {                               /* LOAD */
        if (fl_status[s] != SLOT_OK) { fl_say(COL_RED, fl_status[s] == SLOT_BAD ? "DAMAGED: NOT LOADED" : "EMPTY SLOT"); return; }
        if (fl_arm != 1) { fl_arm = 1; fl_arm_t = 120; sprintf(m, "LOAD %02d? PRESS AGAIN", s + 1); fl_say(COL_ACCENT, m); return; }
        fl_arm = 0xFF;
        if (app_load_slot(s)) { fl_say(COL_EVOLVE, "LOADED"); tab = TAB_LIVE; sg_page = 0; need_full = 1; }
        else fl_say(COL_RED, "LOAD FAILED");
    } else if (b == 2) {                               /* ERASE */
        if (fl_status[s] == SLOT_EMPTY) return;
        if (fl_arm != 2) { fl_arm = 2; fl_arm_t = 120; sprintf(m, "ERASE %02d? PRESS AGAIN", s + 1); fl_say(COL_RED, m); return; }
        fl_arm = 0xFF;
        if (storage_erase(s)) { sprintf(m, "ERASED %02d", s + 1); fl_say(COL_TEXT2, m); }
        else fl_say(COL_RED, "ERASE FAILED");
        fl_refresh();
    }
}

static void fl_frame(void)
{
    uint32_t sig;
    int i;
    if (fl_about) {
        sig = 0xA0000000u | ab_page;
        if (sig != fl_sig) { about_draw(); fl_sig = sig; }
        return;
    }
    if (!fl_valid) fl_refresh();
    if (fl_arm_t && !--fl_arm_t) fl_arm = 0xFF;
    if (fl_msg_t) fl_msg_t--;
    sig = fl_sel * 7u + fl_area * 131u + fl_btn * 17u + (fl_arm + 1) * 19u + ((fl_arm_t >> 3) & 1) * 23u +
          fl_src * 29u + (fl_msg_t != 0) * 31u + fl_last * 37u + (uint32_t)fl_msg[0] * 41u + (uint32_t)fl_msg[6] * 43u;
    for (i = 0; i < DS_SLOTS; i++) sig = sig * 3 + fl_status[i] + (uint8_t)fl_names[i][0];
    if (sig != fl_sig) { fl_draw(); fl_sig = sig; }
}

static void fl_touch(int phase)
{
    int n = FL_N();
    if (phase != 0) return;
    if (fl_about) {
        if (t_y >= 160) {
            if (t_x < 66) { fl_about = 0; fl_sig = 0xFFFFFFFF; }
            else if (t_x >= 160 && t_x < 206 && ab_page > 0) ab_page--;
            else if (t_x >= 206 && ab_page + 1 < N_ABOUT) ab_page++;
        } else if (ab_page + 1 < N_ABOUT) ab_page++;      /* tap the page: next */
        return;
    }
    if (t_y >= FL_Y0 - 1 && t_y < FL_Y0 + 8 * FL_P) {
        int i = (t_x >= 6 + FL_W + 1 ? 8 : 0) + clampi((t_y - FL_Y0 + 1) / FL_P, 0, 7);
        if (i < n) { if (i != fl_sel) fl_arm = 0xFF; fl_sel = (uint8_t)i; fl_area = 0; }
    } else if (t_y >= FL_BTN - 2 && t_y < FL_BTN + 24) {
        int b = clampi((t_x - 6) / FL_BP, 0, 4);
        fl_area = 1; fl_btn = (uint8_t)b;
        fl_action(b);
    }
}

static void fl_keys(uint32_t down, uint32_t held, uint32_t rep)
{
    int n = FL_N();
    if (fl_about) {
        if (down & KEY_B) { fl_about = 0; fl_sig = 0xFFFFFFFF; }
        if (((rep & KEY_RIGHT) || (down & KEY_A)) && ab_page + 1 < N_ABOUT) ab_page++;
        if ((rep & KEY_LEFT) && ab_page > 0) ab_page--;
        return;
    }
    if (held & KEY_A) {
        if (down & KEY_A) {
            if (fl_area == 1) fl_action(fl_btn);
            else if (held & KEY_B) { fl_btn = 2; fl_action(2); }      /* B + A: erase */
            else { fl_area = 1; fl_btn = 1; }                          /* A on a slot: to LOAD */
        }
        return;
    }
    if (fl_area == 0) {
        if ((rep & KEY_UP) && fl_sel > 0) { fl_sel--; fl_arm = 0xFF; }
        if (rep & KEY_DOWN) { if (fl_sel + 1 < n && (fl_sel & 7) != 7) { fl_sel++; fl_arm = 0xFF; } else fl_area = 1; }
        if ((rep & KEY_RIGHT) && fl_sel + 8 < n) { fl_sel += 8; fl_arm = 0xFF; }
        if ((rep & KEY_LEFT) && fl_sel >= 8) { fl_sel -= 8; fl_arm = 0xFF; }
    } else {
        if ((rep & KEY_LEFT) && fl_btn > 0) fl_btn--;
        if ((rep & KEY_RIGHT) && fl_btn < 4) fl_btn++;
        if (rep & KEY_UP) fl_area = 0;
    }
}

/* =============================== dispatch ================================= */
static void enter_tab(void)
{
    gfx_clear(fb_bot, COL_BG);
    draw_tabs();
    switch (tab) {
    case TAB_LIVE: live_draw_static(); break;
    case TAB_XY: xy_draw_static(); break;
    case TAB_PANEL: if (pn_page) dr_draw_static(); else panel_draw_static(); break;
    case TAB_KEYS: kb_sig = 0xFFFFFFFF; break;
    case TAB_SONG: sg_sig = 0xFFFFFFFF; fl_sig = 0xFFFFFFFF; fl_valid = 0; fl_about = 0; break;
    default: gen_sig = 0xFFFFFFFF; break;
    }
}

static void tab_touch(int phase)
{
    switch (tab) {
    case TAB_LIVE: live_touch(phase); break;
    case TAB_XY: xy_touch(phase); break;
    case TAB_PANEL: if (pn_page) dr_touch(phase); else panel_touch(phase); break;
    case TAB_KEYS: kb_touch(phase); break;
    case TAB_SONG: if (sg_page) fl_touch(phase); else sg_touch(phase); break;
    default: gen_touch(phase); break;
    }
}

void ui_init(void)
{
    keysSetRepeat(15, 4);
    need_full = 1;
}

void ui_frame(uint32_t down, uint32_t held)
{
    uint32_t rep = keysDownRepeat(), up = keysUp();
    frame++;

    /* ---- global buttons ---------------------------------------------- */
    if (held & KEY_SELECT) {
        if (down & KEY_LEFT) {                        /* pages count as stops */
            if (tab == TAB_SONG && sg_page) sg_page = 0;
            else if (tab == TAB_PANEL && pn_page) pn_page = 0;
            else {
                tab = (uint8_t)((tab + TAB_COUNT - 1) % TAB_COUNT);
                if (tab == TAB_SONG) sg_page = 1;
                if (tab == TAB_PANEL) pn_page = 1;
            }
            need_full = 1;
        }
        if (down & KEY_RIGHT) {
            if (tab == TAB_SONG && !sg_page) sg_page = 1;
            else if (tab == TAB_PANEL && !pn_page) pn_page = 1;
            else { tab = (uint8_t)((tab + 1) % TAB_COUNT); sg_page = 0; pn_page = 0; }
            need_full = 1;
        }
        if (down & KEY_A) app_evolve();
        if (down & KEY_B) app_undo();
        if (down & KEY_START) app_fill_latch(!SH->fill_hold);     /* SERIES_SPEC.md 2.1: latched fill */
        if (down & (KEY_UP | KEY_DOWN)) {                          /* queue the next / previous pattern */
            uint8_t base = SH->queued != 0xFF ? SH->queued : SH->cur;
            app_queue((uint8_t)((base + ((down & KEY_UP) ? 1 : NUM_PATTERNS - 1)) % NUM_PATTERNS));
            if (SH->proj.chain.song) app_song(0);
        }
    } else {
        switch (tab) {
        case TAB_LIVE: live_keys(down, held, rep); break;
        case TAB_XY: xy_keys(down, held, rep); break;
        case TAB_PANEL: if (pn_page) dr_keys(down, held, rep); else panel_keys(down, held, rep); break;
        case TAB_KEYS: kb_keys(down, held, rep); break;
        case TAB_SONG: if (sg_page) fl_keys(down, held, rep); else sg_keys(down, held, rep); break;
        default: gen_keys(down, held, rep); break;
        }
    }
    /* hold R: fill while held; it lets go only of the fill it started */
    if ((down & KEY_R) && !SH->fill_hold) { app_fill_latch(1); r_fill = 1; }
    if ((up & KEY_R) && r_fill) { r_fill = 0; if (SH->fill_hold) app_fill_latch(0); }
    /* B tapped on its own on LIVE: the latched fill on / off (as ETCH and ETCH II) */
    if (down & KEY_B) b_alone = 1;
    if ((held & KEY_B) && (down & ~KEY_B)) b_alone = 0;
    if ((up & KEY_B) && b_alone && tab == TAB_LIVE && !(held & KEY_SELECT)) app_fill_latch(!SH->fill_hold);
    if (up & KEY_B) b_alone = 0;

    /* ---- touch: one point, down / drag / up --------------------------- */
    if (held & KEY_TOUCH) {
        touchPosition tp;
        touchRead(&tp);
        t_x = tp.px; t_y = tp.py;
        if (!t_active) {
            t_active = 1; t_moved = 0;
            t_sx = t_x; t_sy = t_y;
            t_temp = (held & KEY_L) != 0;             /* hold L: temporary drag */
            t_on_tabs = t_y < 22;
            if (t_on_tabs) {
                uint8_t nt = (uint8_t)clampi((t_x - TAB_X0) / TAB_W, 0, TAB_COUNT - 1);
                if (nt != tab) { tab = nt; sg_page = 0; pn_page = 0; need_full = 1; }
                else if (nt == TAB_SONG) { sg_page ^= 1; need_full = 1; }
                else if (nt == TAB_PANEL) { pn_page ^= 1; need_full = 1; }
            } else tab_touch(0);
        } else if (!t_on_tabs) {
            if (t_x - t_sx > 2 || t_sx - t_x > 2 || t_y - t_sy > 2 || t_sy - t_y > 2) t_moved = 1;
            tab_touch(1);
        }
    } else if (t_active) {
        t_active = 0;
        if (!t_on_tabs) tab_touch(2);
    }

    if (need_full) { need_full = 0; if (tab != TAB_KEYS) kb_release(); enter_tab(); }
    switch (tab) {
    case TAB_LIVE: live_frame(); break;
    case TAB_XY: xy_frame(); break;
    case TAB_PANEL: if (pn_page) dr_frame(); else panel_frame(); break;
    case TAB_KEYS: kb_frame(); break;
    case TAB_SONG: if (sg_page) fl_frame(); else sg_frame(); break;
    default: gen_frame(); break;
    }
}
