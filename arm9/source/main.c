/*
 * ETCH DS -- by Synth Centric
 * ARM9: boot, the render interrupt, the main loop.
 *
 * Boot: allocate the shared block and the audio ring, hand them to the ARM7
 * (which starts the clock and the two streaming channels), build FOUNDRY's
 * drum sounds, load the boot song (ETCH's CORRODE from its seed code, DRIFT).
 * Press START: the moment of the press seeds the living song, and acid plays.
 *
 * Render interrupt (IPC sync, rung by the ARM7 once per block): render every
 * block the ARM7 has sequenced and not yet been rendered.  If the ARM9 ever
 * falls more than a ring behind, the stale blocks' events are still applied
 * (so no note hangs) but only the newest block is rendered.
 *
 * Main loop (60 Hz): input, CORROSION (evolve.c), screens (ui.c).
 */
#include <nds.h>
#include <stdio.h>
#include <string.h>
#include "app.h"
#include "audio.h"
#include "storage.h"

static shared_t shared_store __attribute__((aligned(32)));
static int16_t ring_l[AUDIO_RING] __attribute__((aligned(32)));
static int16_t ring_r[AUDIO_RING] __attribute__((aligned(32)));

shared_t *SH;
uint8_t app_card;
volatile uint16_t ui_entropy;
static uint32_t rendered;            /* blocks [0, rendered) are done */

uint16_t hw_noise(void) { return TIMER_DATA(2); }

void app_send(uint32_t cmd) { fifoSendValue32(FIFO_ETCH_CMD, cmd); }

/* the pattern after the next bar line (advisory, as in ETCH's main loop) */
uint8_t app_next_bar_pattern(void)
{
    uint8_t p = SH->cur;
    if (SH->proj.chain.song) {
        if ((uint8_t)(SH->chain_bar + 1) >= SH->proj.chain.rep[SH->chain_pos]) {
            uint8_t pos = SH->chain_pos, i;
            for (i = 0; i < CHAIN_LEN; i++) {
                pos = (uint8_t)((pos + 1) & (CHAIN_LEN - 1));
                if (SH->proj.chain.pat[pos] < NUM_PATTERNS) { p = SH->proj.chain.pat[pos]; break; }
                if (SH->proj.chain.pat[pos] == 0xFF) pos = CHAIN_LEN - 1;
            }
        }
    } else if (SH->queued != 0xFF) {
        p = SH->queued;
    }
    return p;
}

static void render_irq(void)
{
    uint32_t req = SH->blk_requested;
    while (rendered < req) {
        uint32_t b = rendered, slot = b & (AUDIO_NBLK - 1);
        if (req - b > AUDIO_NBLK - 1) {
            /* hopelessly late: keep the voices' state, skip the audio */
            static int16_t junk_l[AUDIO_BLOCK], junk_r[AUDIO_BLOCK];
            audio_render_block(&SH->ev[slot], b, junk_l, junk_r);
        } else {
            audio_render_block(&SH->ev[slot], b, ring_l + slot * AUDIO_BLOCK,
                               ring_r + slot * AUDIO_BLOCK);
        }
        rendered = b + 1;
        SH->blk_done = rendered;
        req = SH->blk_requested;
    }
}

/* The newest rendered audio for the oscilloscope: the three most recent
 * blocks are stable (the fourth slot is the next to be written).  Looks for a
 * rising zero crossing in the first block so the trace stands still. */
uint32_t audio_scope(int16_t *out, uint32_t n)
{
    uint32_t r = rendered, start, i, trig = 0;
    int16_t mono[3 * AUDIO_BLOCK];
    if (r < 3) return 0;
    start = r - 3;
    for (i = 0; i < 3 * AUDIO_BLOCK; i++) {
        uint32_t b = start + i / AUDIO_BLOCK, slot = b & (AUDIO_NBLK - 1);
        uint32_t k = slot * AUDIO_BLOCK + (i % AUDIO_BLOCK);
        mono[i] = (int16_t)((ring_l[k] + ring_r[k]) >> 1);
    }
    for (i = 1; i < AUDIO_BLOCK; i++)
        if (mono[i - 1] < 0 && mono[i] >= 0) { trig = i; break; }
    if (n > 3 * AUDIO_BLOCK - trig) n = 3 * AUDIO_BLOCK - trig;
    for (i = 0; i < n; i++) out[i] = mono[trig + i];
    return n;
}

static void shared_init(void)
{
    /* the block is only ever touched through the uncached mirror: write back
     * and drop any cached lines first (the crt0 cleared .bss through the cache) */
    DC_FlushRange(&shared_store, sizeof shared_store);
    DC_InvalidateRange(&shared_store, sizeof shared_store);
    SH = (shared_t *)memUncached(&shared_store);
    memset(SH, 0, sizeof *SH);
    memset(ring_l, 0, sizeof ring_l);
    memset(ring_r, 0, sizeof ring_r);
    DC_FlushRange(ring_l, sizeof ring_l);
    DC_FlushRange(ring_r, sizeof ring_r);
    SH->buf_l = ring_l;
    SH->buf_r = ring_r;
    SH->queued = 0xFF;
    SH->evo_undo_pat = 0xFF;
    demo_load(&SH->proj);
    demo_import_sound(&SH->proj);
    SH->magic = SHARED_MAGIC;
}

int main(int argc, char **argv)
{
    (void)argc; (void)argv;

    audio_engine_init();                /* also starts the TIMER2/3 counter */
    foundry_build();
    shared_init();
    ui_init();
    top_init();

    irqSet(IRQ_IPC_SYNC, render_irq);
    REG_IPC_SYNC = IPC_SYNC_IRQ_ENABLE;
    irqEnable(IRQ_IPC_SYNC);

    fifoSendAddress(FIFO_ETCH_INIT, &shared_store);
    while (!fifoCheckValue32(FIFO_ETCH_INIT)) swiWaitForVBlank();
    fifoGetValue32(FIFO_ETCH_INIT);
    app_card = storage_init();                /* audio is already running */

    while (1) {
        uint32_t down, held;
        swiWaitForVBlank();
        scanKeys();
        down = keysDown();
        held = keysHeld();
        if (down) ui_entropy = (uint16_t)((ui_entropy * 31u) ^ hw_noise() ^ down);

        if (down & KEY_START) {
            if (!SH->playing) {
                uint16_t e = (uint16_t)(ui_entropy ^ hw_noise());
                evo_seed(e);                        /* the moment START was pressed */
                app_send(CMD(CMD_START, e));
            } else {
                app_send(CMD(CMD_STOP, 0));
            }
        }
        evo_frame();
        app_fill_frame();
        ui_frame(down, held);
        top_frame();
    }
    return 0;
}
