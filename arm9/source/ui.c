/*
 * ETCH DS -- by Synth Centric
 * Milestone-1 screens.
 *
 *   top     a plain status display: song, seed code, CORROSION mode, bars to
 *           the next evolution, generation, the playing pattern, a short
 *           parameter list (ETCH grammar: UP/DOWN pick a row, A + D-pad
 *           changes it) and the CPU meter, per stage.
 *   bottom  the XY pad: X = cutoff, Y = resonance.  Touch to play it; hold L
 *           while dragging to make the move temporary (it springs back when
 *           you let go).  Touch is never needed for sound.
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "corrosion.h"
#include "app.h"
#include "audio.h"

static PrintConsole top;
static uint16_t *fb;                      /* bottom screen, 256x192 RGB15 */
static uint16_t bg[256 * 192];
static int cross_x = -1, cross_y = -1;
static uint8_t cursor, scroll;
static uint8_t fx_on = 1;
static fx_params_t fx_saved;
static uint8_t stress_on;
static uint8_t touch_active, touch_temp, saved_cut, saved_res;
static uint32_t frame;

#define PAD_X0 16
#define PAD_X1 239
#define PAD_Y0 16
#define PAD_Y1 175
#define RGB(r, g, b) (RGB15(r, g, b) | BIT(15))

/* ---- parameter rows ------------------------------------------------------- */
enum { R_WAVE, R_CUT, R_RES, R_ENV, R_DEC, R_ACC, R_SLIDE, R_DRIVE, R_DAMT,
       R_FILT, R_EVO, R_BPM, R_FX, R_STRESS, R_COUNT };
static const char * const row_name[R_COUNT] = {
    "WAVE", "CUTOFF", "RES", "ENV MOD", "DECAY", "ACCENT", "SLIDE", "DRIVE",
    "DRV AMT", "FILTER", "EVO", "BPM", "FX", "CPU TEST"
};
static const char * const wave_names[4] = { "SAW", "SQUARE", "HOLLOW", "BITE" };
static const char * const drive_names[4] = { "OFF", "SOFT", "HARD", "FOLD" };
static const char * const filt_names[2] = { "303 (NO KEY TRACK)", "KEY TRACK (ETCH)" };
static const char * const note_names[12] = { "C-", "C#", "D-", "D#", "E-", "F-", "F#", "G-", "G#", "A-", "A#", "B-" };

static int get_row(uint8_t r)
{
    switch (r) {
    case R_WAVE: return acid_p.wave;
    case R_CUT: return acid_p.cutoff;
    case R_RES: return acid_p.res;
    case R_ENV: return acid_p.envmod;
    case R_DEC: return acid_p.decay;
    case R_ACC: return acid_p.accent;
    case R_SLIDE: return acid_p.slide;
    case R_DRIVE: return acid_p.drive;
    case R_DAMT: return acid_p.drive_amt;
    case R_FILT: return acid_p.keytrack;
    case R_EVO: return SH->proj.gen.mode;
    case R_BPM: return SH->proj.synth.bpm;
    case R_FX: return fx_on;
    case R_STRESS: return stress_on;
    default: return 0;
    }
}

static void set_row(uint8_t r, int v)
{
    static const int lo[R_COUNT] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 60, 0, 0 };
    static const int hi[R_COUNT] = { 3, 127, 127, 127, 127, 127, 127, 3, 127, 1, 2, 220, 1, 1 };
    if (v < lo[r]) v = lo[r];
    if (v > hi[r]) v = hi[r];
    switch (r) {
    case R_WAVE: acid_p.wave = (uint8_t)v; break;
    case R_CUT: acid_p.cutoff = (uint8_t)v; break;
    case R_RES: acid_p.res = (uint8_t)v; break;
    case R_ENV: acid_p.envmod = (uint8_t)v; break;
    case R_DEC: acid_p.decay = (uint8_t)v; break;
    case R_ACC: acid_p.accent = (uint8_t)v; break;
    case R_SLIDE: acid_p.slide = (uint8_t)v; break;
    case R_DRIVE: acid_p.drive = (uint8_t)v; break;
    case R_DAMT: acid_p.drive_amt = (uint8_t)v; break;
    case R_FILT: acid_p.keytrack = (uint8_t)v; break;
    case R_EVO: SH->proj.gen.mode = (uint8_t)v; break;
    case R_BPM: SH->proj.synth.bpm = (uint8_t)v; fx_p.bpm = (uint8_t)v; break;
    case R_FX:
        if (v && !fx_on) fx_p = fx_saved;
        if (!v && fx_on) {
            fx_saved = fx_p;
            fx_p.drive = 0; fx_p.crush_bits = 16; fx_p.crush_hold = 1;
            fx_p.dly_mix = 0; fx_p.comb_mix = 0;
        }
        fx_on = (uint8_t)v;
        break;
    case R_STRESS:
        stress_on = (uint8_t)v;
        app_send(CMD(CMD_STRESS, v));
        if (v) {                                   /* every stage working */
            if (!fx_on) set_row(R_FX, 1);
            fx_p.crush_hold = 2;
            fx_p.crush_bits = 12;
        } else {
            fx_p.crush_hold = 1;
            fx_p.crush_bits = 16;
        }
        break;
    default: break;
    }
}

static void row_value(uint8_t r, char *out)
{
    int v = get_row(r);
    switch (r) {
    case R_WAVE: strcpy(out, wave_names[v & 3]); break;
    case R_DRIVE: strcpy(out, drive_names[v & 3]); break;
    case R_FILT: strcpy(out, filt_names[v & 1]); break;
    case R_EVO: strcpy(out, corr_mode_names[v % EVO_COUNT]); break;
    case R_FX: strcpy(out, v ? "ON" : "OFF"); break;
    case R_STRESS: strcpy(out, v ? "ON (ACID+6 LANES/16TH)" : "OFF"); break;
    default: sprintf(out, "%d", v); break;
    }
}

/* ---- XY pad ---------------------------------------------------------------- */
static void pad_bg(void)
{
    int x, y;
    for (y = 0; y < 192; y++)
        for (x = 0; x < 256; x++) {
            uint16_t c = RGB(2, 1, 1);
            if (x >= PAD_X0 && x <= PAD_X1 && y >= PAD_Y0 && y <= PAD_Y1) {
                c = RGB(4, 2, 1);
                if (((x - PAD_X0) % 28) == 0 || ((y - PAD_Y0) % 20) == 0) c = RGB(8, 4, 2);
                if (x == PAD_X0 || x == PAD_X1 || y == PAD_Y0 || y == PAD_Y1) c = RGB(20, 10, 3);
            }
            bg[y * 256 + x] = c;
        }
}

static void pad_cross(void)
{
    int x = PAD_X0 + (acid_p.cutoff * (PAD_X1 - PAD_X0)) / 127;
    int y = PAD_Y1 - (acid_p.res * (PAD_Y1 - PAD_Y0)) / 127;
    int i;
    if (x == cross_x && y == cross_y) return;
    if (cross_x >= 0) {                                   /* erase */
        for (i = 0; i < 256; i++) fb[cross_y * 256 + i] = bg[cross_y * 256 + i];
        for (i = 0; i < 192; i++) fb[i * 256 + cross_x] = bg[i * 256 + cross_x];
    }
    for (i = PAD_X0; i <= PAD_X1; i++) fb[y * 256 + i] = RGB(31, 14, 2);
    for (i = PAD_Y0; i <= PAD_Y1; i++) fb[i * 256 + x] = RGB(31, 14, 2);
    cross_x = x;
    cross_y = y;
}

static void pad_touch(uint32_t down, uint32_t held)
{
    touchPosition tp;
    if (held & KEY_TOUCH) {
        int cut, res;
        touchRead(&tp);
        if (!touch_active) {
            touch_active = 1;
            touch_temp = (held & KEY_L) != 0;           /* hold L: temporary drag */
            saved_cut = acid_p.cutoff;
            saved_res = acid_p.res;
        }
        cut = ((tp.px - PAD_X0) * 127) / (PAD_X1 - PAD_X0);
        res = ((PAD_Y1 - tp.py) * 127) / (PAD_Y1 - PAD_Y0);
        if (cut < 0) cut = 0;
        if (cut > 127) cut = 127;
        if (res < 0) res = 0;
        if (res > 127) res = 127;
        acid_p.cutoff = (uint8_t)cut;
        acid_p.res = (uint8_t)res;
    } else if (touch_active) {
        touch_active = 0;
        if (touch_temp) { acid_p.cutoff = saved_cut; acid_p.res = saved_res; }
    }
    (void)down;
}

/* ---- top screen ------------------------------------------------------------- */
static void at(int row, int col) { printf("\x1b[%d;%dH", row, col); }

static void pct(char *o, uint16_t v) { sprintf(o, "%2d.%02d", v / 100, v % 100); }

static void draw_status(void)
{
    const project_t *p = &SH->proj;
    const pattern_t *pat = &p->pat[SH->cur];
    char code[13], a[8], b[8], c[8];
    uint8_t s, l, cur_step = SH->step, play = SH->playing;
    static const char lane_ch[4] = { 'K', 'S', 'H', 'O' };

    /* 32 columns x 24 rows */
    corr_code_encode(&p->gen, code);
    code[12] = 0;
    at(0, 0);
    printf("ETCH DS  \"IT SQUELCHES\"      M1");
    at(1, 0);
    printf("%.10s %.4s-%.4s-%.4s %s", p->name, code, code + 4, code + 8, play ? "PLAY" : "STOP");
    at(2, 0);
    printf("%s %-8.8s %-10.10s %3dBPM", note_names[p->gen.key % 12], corr_scale_names[p->gen.scale % SCALE_COUNT],
           corr_style_names[p->gen.style % STYLE_COUNT], p->synth.bpm);
    at(3, 0);
    {
        uint8_t bars = bars_to_evolution(SH);
        printf("%-7.7s GEN %03d ", corr_mode_names[p->gen.mode % EVO_COUNT], pat->gen);
        if (bars && play) printf("NEXT EVO %d BAR%c", bars, bars == 1 ? ' ' : 'S');
        else printf("               ");
    }
    at(4, 0);
    printf("PAT %02d  BAR %04d  STEP %02d      ", SH->cur, SH->bar_total, cur_step + 1);

    /* the pattern: bass on two rows of 8 steps, then ETCH's four drum lanes */
    for (s = 0; s < 16; s++) {
        uint8_t f = pat->flags[s], t = f & STEP_TYPE_MASK;
        char cell[5];
        if (t == STEP_NOTE)
            sprintf(cell, "%s%d%c", note_names[pat->note[s] % 12], pat->note[s] / 12 + 1,
                    (f & STEP_ACCENT) ? '!' : (f & STEP_SLIDE) ? '~' : ' ');
        else if (t == STEP_TIE) strcpy(cell, " == ");
        else strcpy(cell, " .  ");
        at(6 + (s >> 3), (s & 7) * 4);
        if (s == cur_step && play) printf("\x1b[41m%-4s\x1b[39m", cell);
        else printf("%-4s", cell);
    }
    for (l = 0; l < NUM_LANES; l++) {
        at(8 + l, 0);
        printf("%c ", lane_ch[l]);
        for (s = 0; s < 16; s++) putchar((pat->drum[l] >> s) & 1 ? 'x' : (s == cur_step && play ? '|' : '.'));
        printf(" %s", foundry_kit_names[l][pat->kit[l] & 3]);
    }
    at(12, 0);
    printf(play ? "                                " : "         PRESS START            ");

    /* parameter list: 6 rows visible */
    if (cursor < scroll) scroll = cursor;
    if (cursor >= scroll + 6) scroll = (uint8_t)(cursor - 5);
    for (s = 0; s < 6; s++) {
        uint8_t r = (uint8_t)(scroll + s);
        char v[32];
        at(13 + s, 0);
        row_value(r, v);
        printf("%c%-8s %-22.22s", r == cursor ? '>' : ' ', row_name[r], v);
    }

    /* CPU per stage, % of real time over the last second */
    pct(a, astats.avg_total);
    pct(b, astats.max_total_pct);
    at(19, 0);
    printf("CPU %s%% MAX %s%% DROPS %-4lu", a, b, (unsigned long)SH->dropouts);
    pct(a, astats.avg_pct[ST_ACID]); pct(b, astats.avg_pct[ST_DRUMS]); pct(c, astats.avg_pct[ST_EVENTS]);
    at(20, 0);
    printf("ACID %s DRUMS %s EVT %s", a, b, c);
    pct(a, astats.avg_pct[ST_DRIVE]); pct(b, astats.avg_pct[ST_CRUSH]); pct(c, astats.avg_pct[ST_DELAY]);
    at(21, 0);
    printf("DRIVE %s CRUSH %s DLY %s", a, b, c);
    pct(a, astats.avg_pct[ST_COMB]); pct(b, astats.avg_pct[ST_LIMIT]); pct(c, astats.avg_pct[ST_OUT]);
    at(22, 0);
    printf("COMB %s LIMIT %s OUT %s", a, b, c);
    pct(a, (uint16_t)((SH->arm7_ticks_max * 10000u) >> 11));          /* of 2048 ticks */
    at(23, 0);
    printf("ARM7 %s%%  XY %3d/%3d  L=TEMP", a, acid_p.cutoff, acid_p.res);
}

void ui_init(void)
{
    videoSetMode(MODE_5_2D);
    videoSetModeSub(MODE_0_2D);
    vramSetBankA(VRAM_A_MAIN_BG);
    vramSetBankC(VRAM_C_SUB_BG);
    lcdMainOnBottom();
    {
        int id = bgInit(3, BgType_Bmp16, BgSize_B16_256x256, 0, 0);
        fb = bgGetGfxPtr(id);
    }
    consoleInit(&top, 0, BgType_Text4bpp, BgSize_T_256x256, 31, 0, false, true);
    consoleSelect(&top);
    pad_bg();
    memcpy(fb, bg, sizeof bg);
    keysSetRepeat(15, 4);
}

void ui_frame(uint32_t down, uint32_t held)
{
    uint32_t rep = keysDownRepeat();
    frame++;
    if (held & KEY_A) {
        int d = 0;
        if (rep & KEY_RIGHT) d = 1;
        if (rep & KEY_LEFT) d = -1;
        if (rep & KEY_UP) d = 8;
        if (rep & KEY_DOWN) d = -8;
        if (d) {
            if (cursor == R_WAVE || cursor == R_DRIVE || cursor == R_FILT || cursor == R_EVO ||
                cursor == R_FX || cursor == R_STRESS)
                d = d > 0 ? 1 : -1;                   /* choices step one at a time */
            set_row(cursor, get_row(cursor) + d);
        }
    } else {
        if ((rep & KEY_DOWN) && cursor < R_COUNT - 1) cursor++;
        if ((rep & KEY_UP) && cursor > 0) cursor--;
    }
    pad_touch(down, held);
    pad_cross();
    if ((frame & 3) == 0) draw_status();
}
