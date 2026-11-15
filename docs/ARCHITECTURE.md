# ETCH DS — architecture (milestone 1)

ETCH DS — by Synth Centric

This document records how the two CPUs share the work, and the decisions that would be expensive to undo. Two of them were confirmed with the project owner before any code was written:

* The ARM7 core runs the sequencer.
* Shared state lives in uncached main RAM.

## 1. The two CPUs

```
            ARM7 (33 MHz, custom core)                   ARM9 (67 MHz)
 ┌──────────────────────────────────────┐   ┌──────────────────────────────────────────┐
 │ TIMER0 block IRQ (every 128 samples) │   │ IPC-sync IRQ: render every requested      │
 │   dropout check                      │──▶│   block (ITCM code, DTCM state):          │
 │   seq_core: ETCH step logic for      │   │   events → ACID → FOUNDRY → FX → int16    │
 │   block k+2 → events into a slot     │   │   flush the block out of the D-cache      │
 │   ring the ARM9 (IPC sync)           │   │                                            │
 │ FIFO IRQ: commands from the ARM9     │◀──│ main loop (60 Hz): input, CORROSION        │
 │ VBlank: buttons + touch → ARM9       │──▶│   (evolve.c), screens (ui.c)               │
 │ sound channels 0/1: loop the ring    │   │                                            │
 └──────────────────────────────────────┘   └──────────────────────────────────────────┘
                       ▲                                    │
                       └──────── audio ring (main RAM) ◀────┘
```

* **Clock.** ARM7 TIMER0 runs at bus/1024, which is exactly one tick per output sample. The sound channels use a period of 512 at bus/2, so the output rate is 16 756 991 / 512 = **32 728.5 Hz**. TIMER0 is started in the same critical section as the two channels and reloads every 128 samples, so it stays phase-locked to the hardware read position for good.
* **Render ahead.** In block interrupt *k*, block *k* starts to play. The ARM7 sequences block *k+2* and rings the ARM9, so the ARM9 has two blocks (7.8 ms) to render it. If block *k* has not been rendered when it starts to play, the ARM7 counts a **dropout**. Latency from the sequencer to the speaker is 2–3 blocks (7.8–11.7 ms). An XY-pad move is heard after the next block render, plus the same two to three blocks of buffer.
* **The clock never waits for the UI.** The ARM7 owns time. ARM9 rendering runs in an interrupt, so neither the main loop nor CORROSION's mutation work can delay it.

## 2. The sequencer (`common/seq_core.c`)

This is ETCH's `src/sequencer/seq.c` driven in render time instead of at 256 Hz. It is portable C: the ARM7 runs it, and so do the host tests.

* **Phase accumulator over a pair of 16ths.** It is 32 bits plus an 8-bit fraction, advanced once per 32-sample sub-block. The increment is `bpm × 139 978.879`, made from two integer constants. The long-run tempo error is 3.5 × 10⁻⁹. There is no division.
* **Swing, with ETCH II's fix.** The even 16th starts at the wrap, and the odd 16th when the phase enters `[swing point, end)`. A step only fires when the next step has the matching parity. In ETCH, raising SWING while the phase sat between the old and new swing points fired the odd step twice. The host test sweeps that case at 220 BPM and finds 0 skipped gate points.
* **Granularity.** Onsets and gate points are quantised to 32 samples (0.98 ms; ETCH used 3.9 ms). Events carry their sample offset, so they are block-quantised but exact on the grid.
* **Step rules.** These are ETCH's, unchanged: NOTE, REST, TIE, SLIDE (gate held, glide), ACCENT, PROB (live dice, a separate xorshift that never touches CORROSION's stream) and the 9 conditions. Chains, the pattern queue and fills are ported too. Milestone 1 does not expose fills, but the FILL and !FILL conditions already evaluate.
* **The voice interface becomes events:** `EV_NOTE`, `EV_GLIDE`, `EV_GATE_OFF`, `EV_DRUMS` (a 6-lane mask, plus the kits of ETCH's 4 lanes), `EV_STEP` (accent charge, UI) and `EV_KILL`. The ARM7 keeps its own copy of the gate state, which is all ETCH's TIE rule needs.

## 3. Shared memory (`common/ipc.h`)

`shared_t` sits in main RAM. The ARM7 has no cache. The ARM9 touches it **only through the uncached mirror** (`memUncached`), so no cache maintenance is ever needed for it. At boot the block is flushed and invalidated once, because the crt0 cleared `.bss` through the cache.

| Field | Writer | Reader |
|---|---|---|
| `proj` (ETCH's `project_t`, byte-identical layout) | ARM9 (load, edits); ARM7 (CORROSION commit, undo) | both |
| sequencer state (`step`, `cur`, `phrase`, …) | ARM7 | ARM9 (display, `evo_service`) |
| `evo_cand`, `evo_target`, `evo_epoch` | ARM9 | ARM7 |
| `evo_state` | ARM9 IDLE→READY only; ARM7 READY→IDLE only | both |
| `pat_epoch[]` | ARM7 only (on `CMD_EDITED`, undo) | ARM9 |
| event slots, `blk_requested`, `dropouts` | ARM7 | ARM9 |
| `blk_done` | ARM9 | ARM7 |

The audio ring (2 × 512 int16) is also in main RAM. The ARM9 writes it through the cache and flushes each finished block, because the sound hardware reads RAM directly.

**Commands** from the ARM9 travel on `FIFO_USER_02`: START (with the live-dice seed), STOP, TEMPO, EDITED, EVO_DROP, UNDO and STRESS. They execute in the ARM7's FIFO interrupt, which never nests with the block interrupt, so they are atomic with respect to the sequencer.

**The CORROSION hand-off** keeps ETCH's protocol and adds ETCH II's edit epoch:

1. The main loop copies the pattern that will play after the next bar line into cached RAM and runs `corr_mutate` on that copy.
2. It writes the candidate, its target and the epoch it read, then sets READY. Uncached stores drain in order.
3. At the gate point of step 16, the ARM7 commits the candidate if the target and epoch still match. The new material starts on the downbeat.
4. A candidate that targets the wrong pattern is dropped (`CMD_EVO_DROP`). One computed before an edit carries a stale epoch and is refused.

**Editing.** Pattern memory is written only by the ARM7. The GUI sends edits as commands:

* `CMD_SET_BASS` (note and flags of one step) and `CMD_SET_DRUM` (one hit).
* `CMD_REPLACE` for whole-pattern operations such as GEN and REGEN. The ARM9 writes the new pattern into `edit_pat` and waits for `edit_ack` before reusing that buffer.
* `CMD_FILL` (one-shot, latch on/off, fresh masks each bar), `CMD_EVOLVE` and `CMD_MUTES`.
* `CMD_QUEUE`, `CMD_CHAIN`, `CMD_SONG` and `CMD_JUMP` (SONG tab).
* `CMD_EDITED(0xFF)` after a project load.

KEYS doesn't go through the ARM7. Live notes go from the main loop to the ARM9 render interrupt through a small ring (`live_note` in `render.arm.c`). While a key is held, the sequencer's bass events are ignored.

Every edit moves the pattern's edit epoch, so a CORROSION candidate prepared before the edit is refused instead of overwriting it. Edits and commits therefore compose with no race. The host test checks this.

## 4. The sound engine (ARM9)

Everything on the audio path is integer arithmetic. `tools/check_audio_path.py` walks the call graph of `arm9.elf` from the render interrupt. That includes long calls through literal pools, conservatively. It fails the check if any division, modulo, soft-float or libm routine is reachable. Today 21 functions are reachable, none of them is such a routine, and the hot ones are in ITCM.

| Stage | File | What it does |
|---|---|---|
| events | `render.arm.c` | Copies the block's slot. Each stage walks the block in 32-sample chunks (the control period) and applies the chunk's events first. |
| ACID | `acid.arm.c` | Band-limited wavetables (ETCH's SAW/SQUARE/HOLLOW/BITE recipes, 8 mip levels). A 4-pole ladder with cubic saturation in the feedback, 2× oversampled. 303 (no key tracking) or KEY (ETCH) cutoff. Exponential filter envelope; accents snap shut. An accent capacitor that charges on repeated accents, plus ETCH's per-step charge. A constant-time linear slide with the gate held. VCA with a click-free retrigger. Drive OFF/SOFT/HARD/FOLD after the VCA, then a DC blocker. |
| FOUNDRY | `foundry.arm.c` | Six lanes. Sounds are synthesised from parameters into RAM samples at build time (ETCH's 16 kit sounds plus METAL and NOISE). Playback is a sample player per lane, with varispeed for the tuning drift and closed hat choking open hat. |
| FX | `fx.arm.c` | Drive → crush (sample-and-hold + bit mask) → tempo-synced delay that darkens and loses bits per repeat → comb tuned to a note → limiter with 32-sample look-ahead (ceiling 0.85). |

Formats: oscillator phase is 32 bits per cycle; ACID's signal is Q14 and the ladder states provably stay within ±1.0; the mix bus is int32 at Q15 scale; coefficients are Q16; pitch is in 1/64 semitone. `tools/gen_tables.py` generates every table (wavetables, exponentials, coefficients, reciprocals) into `arm9/source/dsp_tables.c`, which is committed so `make` needs no Python.

## 5. The GUI

Each screen is a single 16-bit bitmap. All text is drawn into it with two proportional pixel fonts (`tools/gen_fonts.py`): 3×5 small caps for captions and a rounded 5×7 for values. There is no 8×8 character grid, so every label sits exactly where the layout puts it. The design system is in `gfx.h`:

* **Surfaces.** Flat dark surfaces, 1 px borders, 2 px rounded corners.
* **Spacing.** A 6 px outer margin and 4 px gaps.
* **Colour.** Black on white, minimal: hierarchy comes from weight alone, and boxes are kept to a minimum (readouts sit on the page, separated by hairlines). Grey captions sit over black values. Black marks what is active, and lit buttons invert to white on black. Accented notes and the kick are black, other hits dark grey, and steps CORROSION has just changed are washed in grey, fading.

Every hit test uses the same layout constants as the drawing code. Elements redraw only when what they show changes; with flat surfaces, a redraw is just "fill, then draw".

Files:

* `gfx.c`: primitives and text
* `scr_top.c`: the top screen
* `ui.c`: the tabs and the input grammar
* `edit.c`: the editing actions

The GUI runs in the main loop and cannot delay audio. A melonDS run with all tabs in use had 0 dropouts.

## 5b. Saves (`storage.c`)

The format was chosen by the project owner: one file of fixed slots, mirroring ETCH's SRAM. `/etch_ds/etch_ds.sav` holds 16 slots of 16 KB followed by a 16-byte metadata block (the last slot used).

* **Slot layout.** A header `"ETDS"` | version | 0 | 0 0 | size LE32 | Fletcher-16 LE16 | 0 0, then the data.
* **Data.** ETCH's `project_t` image, followed by `ds_ext_t` (ACID, FX and FOUNDRY settings, with spare bytes).
* **Write order.** As ETCH: invalidate, write the data, checksum the bytes read back from the card, write the header, write the magic last, verify. Every phase closes the file, so FAT's cache reaches the card in that order.
* **Loading.** The ARM9 stops playback, waits for the ARM7 to go quiet, swaps the project into the shared block, sends `CMD_EDITED(0xFF)` (new epochs, undo cleared, chain reset), then restarts.
* **ETCH Game Boy import.** `/etch_ds/etch_gb.sav` is read with ETCH's own slot layout and checksum (bit-exact with its assembly). v1 slots get default drum voicing.
* **Tests.** `tests/host/test_storage.c` covers the round trip, an interrupted save, corruption, erase, the last-slot marker and GB import (v1 and v2). The melonDS harness gives the console an SD card (`ETCH_SD=image`) for end-to-end runs.

## 6. Memory map (milestone 1)

| | Size | Notes |
|---|---|---|
| ITCM | 7.2 KB of 32 KB | renderer, ACID, FOUNDRY player, FX |
| DTCM | 3.9 KB of 16 KB | ACID state, mix buses, comb buffer, event copy |
| ARM9 `.rodata` | 80 KB | wavetables (64 KB), tables |
| ARM9 `.bss` | 171 KB | delay line (64 KB), XY-pad background (96 KB), shared block |
| heap | ~360 KB | FOUNDRY samples (5.5 s of audio) |
| ARM7 | 14 KB code, 3 KB data | |
| ROM | 185 KB | |

## 7. Files

```
Makefile, Makefile.arm7, Makefile.arm9   one `make` (BlocksDS rom_arm9arm7 template)
common/ipc.h          the ARM9 <-> ARM7 contract
common/seq_core.c     the sequencer (ARM7 + host tests)
arm7/source/main.c    the ARM7 core: clock, streaming, commands, input
arm9/source/          main.c (boot, render IRQ), evolve.c (CORROSION scheduling),
                      demo.c (boot song), ui.c (screens), audio engine (*.arm.c),
                      dsp_tables.c (generated)
corrosion/            CORROSION from ETCH 2.5, byte-identical (SHA256SUMS, UPSTREAM_COMMIT)
tests/host/           ETCH's 44 generator tests (unmodified), sequencer and DSP tests
tools/                gen_tables.py, check_audio_path.py, run_melonds.py,
                      melonds_harness/ (headless melonDS), spectrogram.py
```
