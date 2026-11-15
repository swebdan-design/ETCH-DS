/*
 * ETCH DS -- by Synth Centric
 * Drawing for the two screens.  Each screen is one 16-bit bitmap (BG3);
 * all text is drawn into it with two proportional pixel fonts, so every
 * label sits exactly where the layout puts it (no 8x8 character grid).
 *
 * Design system: flat dark surfaces, 1 px borders, 2 px rounded corners,
* 6 px outer margin, 4 px gaps.  Small grey captions over white values;
 * grayscale throughout: hierarchy by brightness alone; white is what is
 * active (lit buttons invert to white on black), and steps CORROSION just
 * changed glow with a fading light wash.
 */
#ifndef ETCH_DS_GFX_H
#define ETCH_DS_GFX_H
#include <nds.h>
#include <stdint.h>

#define C(r, g, b) ((uint16_t)(RGB15(r, g, b) | BIT(15)))

#define COL_BG      C(1, 1, 1)      /* screen                  */
#define COL_SURF    C(3, 3, 3)      /* panels                  */
#define COL_SURF2   C(5, 5, 5)      /* controls, raised cells  */
#define COL_SURF3   C(8, 8, 8)      /* beat columns, tracks    */
#define COL_BORDER  C(7, 7, 7)
#define COL_TEXT    C(29, 29, 29)   /* values                  */
#define COL_TEXT2   C(15, 15, 15)   /* captions                */
#define COL_TEXT3   C(9, 9, 9)      /* disabled / hints        */
#define COL_ACCENT  C(31, 31, 31)   /* active: white           */
#define COL_ACC_DIM C(13, 13, 13)
#define COL_ACC_LO  C(6, 6, 6)
#define COL_NOTE    C(19, 19, 19)   /* notes and hits          */
#define COL_HOT     C(31, 31, 31)   /* accented notes, kick    */
#define COL_EVOLVE  C(31, 31, 31)   /* CORROSION's changes     */
#define COL_GLOW    C(13, 13, 13)   /* evolved column wash     */
#define COL_RED     C(31, 31, 31)   /* warnings (by inversion) */
#define COL_PLAY    C(7, 7, 7)      /* playhead column         */
#define COL_BEAT    C(4, 4, 4)      /* every 4th step          */

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
