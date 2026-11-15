/*
 * ETCH DS -- by Synth Centric
 * Editing and performance actions (ETCH's app_* functions), for the GUI.
 *
 * Every change to pattern memory goes to the ARM7 as a command, so the ARM7
 * stays the only writer of the playing pattern and an edit can never race a
 * CORROSION commit.  Step edits are single-field commands; whole-pattern
 * operations (GEN, REGEN, evolve-while-stopped) go through the edit_pat
 * staging buffer with an acknowledgement.  As in ETCH, an edit makes the
 * edited pattern DRIFT's new anchor.
 */
#include <string.h>
#include "corrosion.h"
#include "app.h"

static uint32_t sent_replace;

void app_set_bass(uint8_t step, uint8_t note, uint8_t flags)
{
    uint8_t pat = SH->cur;
    if (note > NOTE_MAX) note = NOTE_MAX;
    app_send(CMD(CMD_SET_BASS, CMD_BASS_ARG(pat, step & 15, note, flags)));
    anchors_forget(pat);
}

void app_toggle_drum(uint8_t lane, uint8_t step)
{
    uint8_t pat = SH->cur, on = !((SH->proj.pat[pat].drum[lane & 3] >> (step & 15)) & 1);
    app_send(CMD(CMD_SET_DRUM, CMD_DRUM_ARG(pat, lane & 3, step & 15, on)));
    anchors_forget(pat);
}

void app_replace(const pattern_t *p, uint8_t undo)
{
    uint8_t pat = SH->cur;
    uint32_t spin = 0;
    /* the previous replace must have been taken before edit_pat is reused */
    while (SH->edit_ack != sent_replace && ++spin < 2000000u) { }
    memcpy(&SH->edit_pat, p, sizeof(pattern_t));
    sent_replace++;
    app_send(CMD(CMD_REPLACE, pat | (undo ? 0x100u : 0u)));
    anchors_forget(pat);
}

void app_generate(uint8_t what)
{
    pattern_t p;
    memcpy(&p, &SH->proj.pat[SH->cur], sizeof p);
    if (p.lock & PLOCK_ALL) return;
    corr_generate(&p, &SH->proj.gen, what);
    app_replace(&p, 1);
}

uint16_t app_regen(uint8_t what)
{
    pattern_t p, mat;
    uint16_t mask;
    memcpy(&p, &SH->proj.pat[SH->cur], sizeof p);
    if (p.lock & PLOCK_ALL) return 0;
    corr_regen_material(&mat, &SH->proj.gen, evo_random());
    mask = corr_regen_apply(&p, &mat, evo_random(), what);
    if (mask) app_replace(&p, 1);
    return mask;
}

static void fill_masks(void)
{
    uint16_t m[NUM_LANES];
    corr_fill(&SH->proj.pat[SH->cur], evo_random(), m);
    memcpy(SH->fill_req, m, sizeof m);
}

void app_fill(void)
{
    fill_masks();
    app_send(CMD(CMD_FILL, FILL_ONESHOT));
}

static uint8_t latch_bar, latch_pat;

void app_fill_latch(uint8_t on)
{
    if (on) {
        fill_masks();
        latch_bar = SH->bar_total;
        latch_pat = SH->cur;
        app_send(CMD(CMD_FILL, FILL_LATCH_ON));
    } else {
        app_send(CMD(CMD_FILL, FILL_LATCH_OFF));
    }
}

/* a latched fill plays a fresh variation every bar (ETCH) */
void app_fill_frame(void)
{
    if (SH->fill_hold && (SH->bar_total != latch_bar || SH->cur != latch_pat)) {
        latch_bar = SH->bar_total;
        latch_pat = SH->cur;
        fill_masks();
        app_send(CMD(CMD_FILL, FILL_UPDATE));
    }
}

void app_evolve(void)
{
    if (SH->playing) { app_send(CMD(CMD_EVOLVE, 0)); return; }
    {   /* stopped: mutate and apply now (ETCH) */
        pattern_t out;
        if (evo_mutate_now(SH->cur, &out)) app_replace(&out, 1);
    }
}

void app_undo(void)
{
    app_send(CMD(CMD_UNDO, 0));
    anchors_forget(SH->cur);
}

void app_toggle_mute(uint8_t bit)
{
    app_send(CMD(CMD_MUTES, SH->proj.mutes ^ (1u << bit)));
}
