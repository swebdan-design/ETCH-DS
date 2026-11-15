/*
 * ETCH DS -- by Synth Centric
 * Screen layers: on each screen a 16-bit bitmap (BG3) for graphics under a
 * libnds text console (BG0, 8x8 font, 32 x 24 cells) for labels.
 */
#ifndef ETCH_DS_GFX_H
#define ETCH_DS_GFX_H
#include <nds.h>
#include <stdint.h>

#define C(r, g, b) ((uint16_t)(RGB15(r, g, b) | BIT(15)))

/* the corroded palette */
#define COL_BG      C(2, 2, 3)      /* near black steel   */
#define COL_STEEL   C(9, 9, 10)     /* plate base         */
#define COL_STEEL_D C(5, 5, 6)      /* darker plate       */
#define COL_LCD     C(1, 1, 1)      /* display well       */
#define COL_AMBER   C(31, 18, 3)    /* lit segment / LED  */
#define COL_GHOST   C(5, 3, 1)      /* unlit segment      */
#define COL_HAZARD  C(29, 21, 2)
#define COL_PANEL   C(4, 4, 5)
#define COL_CELL    C(6, 5, 5)
#define COL_LINE    C(9, 7, 6)
#define COL_DIM     C(12, 10, 9)
#define COL_RUST    C(22, 8, 3)
#define COL_HOT     C(31, 15, 4)    /* accent / active    */
#define COL_SULFUR  C(30, 26, 8)    /* cursor             */
#define COL_ACID    C(10, 30, 16)   /* evolved / playhead */
#define COL_BONE    C(27, 26, 23)
#define COL_PLAY    C(9, 9, 11)     /* playhead column    */

/* text colours (ConsoleColor slots, re-tinted in gfx_init) */
enum { T_DIM = CONSOLE_GRAY, T_TEXT = CONSOLE_LIGHT_GRAY, T_BRIGHT = CONSOLE_WHITE,
       T_RUST = CONSOLE_RED, T_HOT = CONSOLE_LIGHT_RED, T_SULFUR = CONSOLE_LIGHT_YELLOW,
       T_ACID = CONSOLE_LIGHT_GREEN };

extern uint16_t *fb_top, *fb_bot;
extern PrintConsole con_top, con_bot;

void gfx_init(void);
void gfx_clear(uint16_t *fb, uint16_t c);
void gfx_rect(uint16_t *fb, int x, int y, int w, int h, uint16_t c);
void gfx_frame(uint16_t *fb, int x, int y, int w, int h, uint16_t c);
void gfx_hline(uint16_t *fb, int x, int y, int w, uint16_t c);
void gfx_vline(uint16_t *fb, int x, int y, int h, uint16_t c);
void gfx_line(uint16_t *fb, int x0, int y0, int x1, int y1, uint16_t c);
void gfx_disc(uint16_t *fb, int cx, int cy, int r, uint16_t c);
void gfx_ring(uint16_t *fb, int cx, int cy, int r, uint16_t c);
uint16_t gfx_mix(uint16_t a, uint16_t b, int t);      /* t 0..256 */
uint16_t gfx_shade(uint16_t c, int d);                /* +-d on every channel */

/* industrial surfaces */
void gfx_plate(uint16_t *fb, int x, int y, int w, int h, uint16_t base);  /* brushed steel */
void gfx_bevel(uint16_t *fb, int x, int y, int w, int h, int raised);
void gfx_well(uint16_t *fb, int x, int y, int w, int h, uint16_t fill);   /* inset */
void gfx_button(uint16_t *fb, int x, int y, int w, int h, int pressed);   /* raised plate */
void gfx_rivet(uint16_t *fb, int cx, int cy);
void gfx_rivets(uint16_t *fb, int x, int y, int w, int h);                /* four corners */
void gfx_hazard(uint16_t *fb, int x, int y, int w, int h);
void gfx_rust(uint16_t *fb, int cx, int cy, int r, uint32_t seed);
void gfx_led(uint16_t *fb, int cx, int cy, int on, uint16_t col);
/* 7-segment character (0-9, A-F, '-', ' ') of height 2*s+3, width s+4 */
void gfx_seg7(uint16_t *fb, int x, int y, char ch, int s, uint16_t on, uint16_t ghost);
int  gfx_seg7_str(uint16_t *fb, int x, int y, const char *str, int s, uint16_t on, uint16_t ghost);

/* text at a cell; pass NULL fmt with n>0 to blank n cells */
void txt(PrintConsole *con, int row, int col, int color, const char *fmt, ...);
void txt_clear(PrintConsole *con, int row, int col, int n);
#endif
