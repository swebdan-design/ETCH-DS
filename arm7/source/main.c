/*
 * ETCH DS -- by Synth Centric
 * ARM7 core: the sequencer clock, audio streaming, buttons and touch.
 *
 *   TIMER0  bus/1024 = exactly one tick per output sample (the sound
 *           channels run at a period of 512 bus/2 clocks), reloaded every
 *           AUDIO_BLOCK samples.  It was started together with the two sound
 *           channels, so it stays phase-locked to the hardware read position.
 *   block IRQ (k = block now starting to play):
 *           1. dropout check: block k must already be rendered by the ARM9
 *           2. run ETCH's step logic for block k + AUDIO_LEAD (seq_core.c)
 *           3. ring the ARM9 (IPC sync IRQ): it renders that block
 *   FIFO    commands from the ARM9 (start, stop, edits, undo...), executed in
 *           the FIFO interrupt, which never nests with the block interrupt.
 *   VBlank  inputGetAndSend(): buttons and touch to the ARM9 (libnds).
 */
#include <nds.h>
#include "ipc.h"
#include "seq_core.h"

static shared_t *volatile SH;
static volatile bool exit_loop;
static uint32_t blk;              /* index of the block starting to play now */

static void block_irq(void)
{
    uint16_t t0 = TIMER_DATA(1);
    uint32_t t, k = ++blk;

    /* 1: the block that starts playing now must have been rendered */
    if (k > AUDIO_LEAD && SH->blk_done < k + 1) SH->dropouts++;
    SH->blk_played = k;

    /* 2: sequence the block AUDIO_LEAD ahead */
    seq_render_block(k + AUDIO_LEAD);
    SH->blk_requested = k + AUDIO_LEAD + 1;

    /* 3: doorbell */
    REG_IPC_SYNC = IPC_SYNC_IRQ_REQUEST | IPC_SYNC_IRQ_ENABLE;

    t = (uint16_t)(TIMER_DATA(1) - t0);         /* bus/64 ticks */
    SH->arm7_ticks_last = t;
    if (t > SH->arm7_ticks_max) SH->arm7_ticks_max = t;
}

static void cmd_handler(u32 value, void *userdata)
{
    (void)userdata;
    if (SH) seq_command(value);
}

static void start_stream(void)
{
    uint32_t words = (AUDIO_RING * 2) / 4;
    int ch;

    /* channel 0 hard left, channel 1 hard right; both loop the whole ring */
    for (ch = 0; ch < 2; ch++) {
        SCHANNEL_CR(ch) = 0;
        SCHANNEL_SOURCE(ch) = (u32)(ch ? SH->buf_r : SH->buf_l);
        SCHANNEL_TIMER(ch) = (u16)(0x10000u - SND_TIMER);
        SCHANNEL_REPEAT_POINT(ch) = 0;
        SCHANNEL_LENGTH(ch) = words;
    }
    blk = 0;
    SH->blk_played = 0;
    SH->blk_requested = 0;
    TIMER_CR(0) = 0;
    TIMER_DATA(0) = (u16)(0x10000u - AUDIO_BLOCK);
    /* free-running bus/64 counter for the ARM7 cost measurement */
    TIMER_DATA(1) = 0;
    TIMER_CR(1) = TIMER_ENABLE | TIMER_DIV_64;

    {
        int old = enterCriticalSection();
        SCHANNEL_CR(0) = SCHANNEL_ENABLE | SOUND_REPEAT | SOUND_FORMAT_16BIT |
                         SOUND_VOL(127) | SOUND_PAN(0);
        SCHANNEL_CR(1) = SCHANNEL_ENABLE | SOUND_REPEAT | SOUND_FORMAT_16BIT |
                         SOUND_VOL(127) | SOUND_PAN(127);
        TIMER_CR(0) = TIMER_ENABLE | TIMER_IRQ_REQ | TIMER_DIV_1024;
        leaveCriticalSection(old);
    }
}

static void vblank_handler(void)
{
    inputGetAndSend();
}

static void power_button_callback(void)
{
    exit_loop = true;
}

int main(int argc, char *argv[])
{
    (void)argc; (void)argv;

    enableSound();
    REG_SOUNDCNT = SOUND_ENABLE | SOUND_VOL(127);
    readUserSettings();
    ledBlink(LED_ALWAYS_ON);
    touchInit();

    irqInit();
    fifoInit();
    installSystemFIFO();
    setPowerButtonCB(power_button_callback);
    initClockIRQTimer(3);

    irqSet(IRQ_VBLANK, vblank_handler);
    irqSet(IRQ_TIMER0, block_irq);
    fifoSetValue32Handler(FIFO_ETCH_CMD, cmd_handler, NULL);
    irqEnable(IRQ_VBLANK | IRQ_VCOUNT);

    /* wait for the ARM9 to hand over the shared block */
    while (!fifoCheckAddress(FIFO_ETCH_INIT)) swiWaitForVBlank();
    {
        shared_t *sh = (shared_t *)fifoGetAddress(FIFO_ETCH_INIT);
        if (sh->magic == SHARED_MAGIC) {
            seq_init(sh);
            SH = sh;
            start_stream();
            irqEnable(IRQ_TIMER0);
        }
    }
    fifoSendValue32(FIFO_ETCH_INIT, 1);         /* ready */

    while (!exit_loop) {
        const uint16_t key_mask = KEY_SELECT | KEY_START | KEY_L | KEY_R;
        uint16_t keys_pressed = ~REG_KEYINPUT;
        if ((keys_pressed & key_mask) == key_mask) exit_loop = true;
        swiWaitForVBlank();
    }
    return 0;
}
