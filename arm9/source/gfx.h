/*
 * ETCH DS -- by Synth Centric
 * Drawing for the two screens.  Each screen is one 16-bit bitmap (BG3);
 * all text is drawn into it with two proportional pixel fonts, so every
 * label sits exactly where the layout puts it (no 8x8 character grid).
 *
 * Design system: flat dark surfaces, 1 px borders, 2 px rounded corners,
* 6 px outer margin, 4 px gaps.  Small grey captions over white values;
 * black on white, minimal: hierarchy by weight alone; black is what is
 * active (lit buttons invert to white on black), and steps CORROSION just
 * changed are washed in grey, fading.
 */
#ifndef ETCH_DS_GFX_H
#define ETCH_DS_GFX_H
#include <nds.h>
#include <stdint.h>

#define C(r, g, b) ((uint16_t)(RGB15(r, g, b) | BIT(15)))

#define COL_BG      C(31, 31, 31)   /* white screen            */
#define COL_SURF    C(29, 29, 29)   /* panels: barely there     */
#define COL_SURF2   C(27, 27, 27)   /* controls, cells off      */
#define COL_SURF3   C(23, 23, 23)   /* tracks, beat ticks       */
#define COL_BORDER  C(24, 24, 24)
#define COL_TEXT    C(2, 2, 2)      /* values: black            */
#define COL_TEXT2   C(13, 13, 13)   /* captions                 */
#define COL_TEXT3   C(21, 21, 21)   /* disabled / hints         */
#define COL_ACCENT  C(0, 0, 0)      /* active: black            */
#define COL_ACC_DIM C(18, 18, 18)
#define COL_ACC_LO  C(25, 25, 25)
#define COL_NOTE    C(11, 11, 11)   /* notes and hits           */
#define COL_HOT     C(0, 0, 0)      /* accented notes, kick     */
#define COL_EVOLVE  C(0, 0, 0)      /* CORROSION's changes      */
#define COL_GLOW    C(21, 21, 21)   /* evolved column wash      */
#define COL_RED     C(0, 0, 0)      /* warnings (by inversion)  */
#define COL_PLAY    C(25, 25, 25)   /* playhead column          */
#define COL_BEAT    C(28, 28, 28)   /* every 4th step           */

enum { F_SMALL, F_MAIN };           /* 5 px caps, 7 px caps    */

extern uint16_t *fb_top, *fb_bot;

void gfx_init(void);
void gfx_clear(uint16_t *fb, uint16_t c);
void gfx_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c);
void gfx_frame(uint16_t *fb, int x, int y, int w, int h, uint16_t c);
void gfx_rrect(uint16_t *fb, int x, int y, int w, int h, uint16_t c);    /* rounded */
void gfx_rframe(uint16_t *fb, int x, int y, int w, int h, uint16_t c);
void gfx_hline(uint16_t *fb, int x, int y, int w, uint16_t c);
void gfx_vline(uint16_t *fb, int x, int y, int h, uint16_t c);
void gfx_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t c);
void gfx_disc(uint16_t *fb, int cx, int cy, int r, uint16_t c);
void gfx_ring(uint16_t *fb, int cx, int cy, int r, uint16_t c);
/* arc of radius r (and r-1 when thick), from angle a0 to a1 (1024 = turn,
 * 0 = up, clockwise) */
void gfx_arc(uint16_t *fb, int cx, int cy, int r, int a0, int a1, int thick, uint16_t c);
uint16_t gfx_mix(uint16_t a, uint16_t b, int t);      /* t 0..256 */
void gfx_led(uint16_t *fb, int cx, int cy, int on, uint16_t col);
/* thin 7-segment character: height 2*s+3, width s+4 */
void gfx_seg7(uint16_t *fb, int x, int y, char ch, int s, uint16_t on, uint16_t ghost);

/* text: returns the x after the last glyph */
int  gfx_text_w(int font, const char *s);
int  gfx_text(uint16_t *fb, int x, int y, int font, uint16_t c, const char *s);
void gfx_text_c(uint16_t *fb, int cx, int y, int font, uint16_t c, const char *s);   /* centred */
void gfx_text_r(uint16_t *fb, int rx, int y, int font, uint16_t c, const char *s);   /* right   */
int  gfx_textf(uint16_t *fb, int x, int y, int font, uint16_t c, const char *fmt, ...);
#define FONT_H(f) ((f) == F_SMALL ? 5 : 7)
#endif
