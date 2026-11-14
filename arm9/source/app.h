/*
 * ETCH DS -- by Synth Centric
 * ARM9 application glue shared by main.c, evolve.c, demo.c and ui.c.
 */
#ifndef ETCH_DS_APP_H
#define ETCH_DS_APP_H
#include <stdint.h>
#include "ipc.h"

extern shared_t *SH;                 /* uncached view of the shared block */
extern volatile uint16_t ui_entropy; /* stirred by every button press      */

uint16_t hw_noise(void);             /* free-running timer low bits        */
void     app_send(uint32_t cmd);     /* command to the ARM7                */
uint8_t  app_next_bar_pattern(void);

/* evolve.c */
void     evo_seed(uint16_t entropy);
uint16_t evo_random(void);
uint8_t  evo_service(void);
uint8_t  evo_frame(void);
void     anchors_forget(uint8_t pat);

/* demo.c */
#define DEMO_CODE "0F1C90095870"      /* ETCH demo song 1, CORRODE: 0F1C-9009-5870 */
void demo_load(project_t *p);        /* portable: also used by the host tests */
void demo_import_sound(const project_t *p);

/* ui.c */
void ui_init(void);
void ui_frame(uint32_t keys_down, uint32_t keys_held);
#endif
