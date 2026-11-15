# ETCH DS — ideas parked for later milestones

ETCH DS — by Synth Centric

Milestone 1 only proves the sound. These ideas came up while building it. None of them is implemented.

## Engine

* **ACID LFO.** ETCH's LFO (PITCH/CUT/RES, four shapes) is not ported yet. It fits naturally as a patch-panel source.
* **Retune FOUNDRY with longer drift.** Lanes are retuned by varispeed, which also changes decay length. That is fine for ±2 semitones of drift. Larger re-tunes should re-render the sample, double-buffered and swapped at a block boundary.
* **Filter quality options.** The ladder runs 2× oversampled. The CPU headroom measured in milestone 1 (see the report) would allow 4× on DSi, or a zero-delay-feedback ladder, which tunes more accurately at high cutoff.
* **Accent model.** The accent capacitor's rates (charge, discharge, and its interplay with ETCH's step charge) deserve a listening session on hardware. The constants are in `acid.arm.c`.
* **A drum accent.** ETCH's drums have none; an accent lane would be a cheap DS addition.
* **Delay length.** The delay line is 1 s (4 sixteenths at 60 BPM, 8 above 120 BPM). Longer musical delays at slow tempos need a 128 KB line.
* **The DS speaker mode.** A gentle high-pass and "speaker drive" curve for when headphones are unplugged, because the bass sits below what the speakers reproduce.

## Sequencer

* Per-track lengths, 64 patterns, the 64-slot chain with named sections, motion lanes, drum ratchets and scene snapshots (in the brief, later milestones).
* **Wireless clock sync.** The clock already lives on the ARM7, next to the wifi stack. Sync could nudge `pphase`, never the block clock.
* **Fills and REGEN pads.** The sequencer already ports ETCH's fill masks and the FILL/!FILL conditions; only the trigger commands and the UI are missing.

## CORROSION DS layers (must stay outside `corrosion/`)

* **Sound evolution.** Patch parameters drift around a sound anchor, with locks. Milestone 1 ports only ETCH's cutoff and drum-tuning drift.
* **A generator for RUST and lanes 4–5.** It reads CORROSION's output and uses its own PRNG.
* **CORRODE material from performance.** Feed captured KEYS/XY input to CORRODE as material.
* **New styles (EBM, DUB TECHNO, NOISE).** These may only be appended after ETCH's six. Note that `corr_code_decode` rejects style digits ≥ 6, so DS-only styles need a DS-side code extension (for example a 13th digit) rather than a change to CORROSION.

## UI

* **Still to build.** FILE (saves on the SD card; the save format is waiting on a decision) and PANEL's patch cables.
* **KEYS as CORRODE material.** Captured KEYS input could feed CORRODE, as the brief describes.
* **XY motion recording** (the trail already exists).
* **Step probability and conditions** in LIVE. ETCH edits them on its TRACK screen.
* **Locks** (step, lane, bass, pattern) shown and settable in LIVE and GEN.
