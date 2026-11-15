# ETCH 3DS — build prompt

You are building **ETCH 3DS**, a homebrew Nintendo 3DS sequel to ETCH DS, the generative acid machine by Synth Centric. You are an expert in Nintendo 3DS homebrew (devkitARM, libctru, citro2d/citro3d, the DSP audio service `ndsp`, threads on Old and New 3DS), real-time DSP in C, and acid, industrial and dub-techno synth design.

## Context: what exists

The series so far:

| Machine | Repository | Notes |
|---|---|---|
| Game Boy | `swebdan-design/Etch---GB` (ETCH 2.5) | The original. CORROSION, the generative composer, lives in `src/generator/corrosion.c`, `pattern.c`, `names.c`, `include/corrosion.h` and `include/etch.h`. It turns key, scale, style, density, syncopation, accent, slide, octave and a seed (a 12-digit seed code) into a 16-step acid bass line and a 4-lane drum groove. Living songs: STABLE, DRIFT (small changes every 4 bars, bounded around a per-pattern anchor) and CORRODE (bigger changes every 2 bars). There are locks, a one-level undo, and step rules: note/rest/tie/accent/slide, probability, 9 conditions, fills and REGEN. Project saves use a Fletcher-16 checksum, with the header written last. |
| GBA | `swebdan-design/Etch-II-GBA` (ETCH II) | Reuses CORROSION byte for byte. Adds the edit-epoch guard and the swing-change clock fix. |
| DS | `swebdan-design/ETCH-DS` (ETCH DS, branch `claude/etch-ds-build-prompt-3w47zj`) | **The direct parent of this project. Read it first.** |

ETCH DS has:

* **CORROSION** byte-identical, checksummed, with ETCH's 44 host tests.
* **The sequencer core** (`common/seq_core.c`): ETCH's step rules, a sample-accurate phase-accumulator clock on a 32-sample grid, the swing fix, the edit epoch, and commands for every edit.
* **ACID** (`arm9/source/acid.arm.c`), a real 303 voice:
  * band-limited SAW/SQUARE/HOLLOW/BITE
  * a 2×-oversampled resonant 4-pole ladder (303 or key-tracked)
  * an accent capacitor that charges on repeated accents
  * constant-time slide
  * drive OFF/SOFT/HARD/FOLD
* **FOUNDRY** (`foundry.arm.c`): six drum lanes synthesised from editable models into samples, safely re-rendered while playing.
* **The FX chain** (`fx.arm.c`): drive → crush → tempo-synced degrading delay → comb → limiter.
* **Everything fixed-point, with no division on the audio path**, enforced by `tools/check_audio_path.py`.
* **A minimal black-and-white UI** (`gfx.*`, `scr_top.c`, `ui.c`):
  * proportional pixel fonts (`tools/gen_fonts.py`)
  * a title screen ("by Synth Centric", PRESS START)
  * tabs LIVE / XY / PANEL·DRUM / KEYS / GEN / SONG·FILE, and ABOUT pages with the Synth Centric bio
* **Saves** (`storage.c`): `/etch_ds/etch_ds.sav`, 16 fixed 16 KB slots in ETCH's scheme, save format v2 (v1 still loads), plus read-only import of ETCH Game Boy `.sav` files.
* **Tests:** host tests (`tests/host`: generator, sequencer, DSP, storage) and a headless melonDS harness (`tools/melonds_harness`, `tools/run_melonds.py`).

**Read before writing code** (all in the ETCH-DS repository):

* `README.md`
* `docs/ARCHITECTURE.md`
* `docs/MILESTONE1_REPORT.md`
* `docs/IDEAS.md`
* **`docs/ETCH_3DS_CONCEPT.md`** — the design this prompt implements
* the ETCH and ETCH II READMEs and `docs/GENERATIVE_SYSTEM.md` in ETCH

## The concept

ETCH 3DS is ETCH DS with room to grow. It has the same composer, the same seed codes, the same 303 at the centre and the same project files. The 3DS adds:

* **OXIDE**, a chord generator that writes harmony around the acid line, played by **VAPOUR**, a chord synth.
* **PUNCH**, live punch-in effects on the Circle Pad and C-stick.
* **RUST**, the second synth.
* Reverb and ducking.
* Tilt and the 3D slider as performance controls.
* A **3D view** of the living song.
* Mic sampling, WAV bounce, QR sharing and wireless sync.

The full design is in `docs/ETCH_3DS_CONCEPT.md`.

## Design pillars (a feature that breaks one doesn't ship)

1. **Press START, hear music.** It boots to the title ("by Synth Centric", PRESS START), and START goes straight into a living song. Touch is never required for sound.
2. **The 303 is the centre;** every other voice, chords included, frames the bass. OXIDE voices its chords above the 303's register.
3. **CORROSION stays the composer:** its files are copied unmodified, and every ETCH seed code makes the identical bass and drums. New generators (OXIDE, the RUST generator, PUNCH lanes) read CORROSION's output and never touch its random stream.
4. **Touch is for performing, buttons are for editing.** ETCH's grammar works on every screen: the D-pad moves, A places or toggles, A + D-pad changes, B + A deletes, SELECT + LEFT/RIGHT changes tab. Touch is single-point. The Circle Pad, C-stick, tilt and 3D slider are performance controls.
5. **Corroded, not clean,** in ETCH DS's minimal black-and-white design system: flat surfaces, 1 px borders, 2 px corners, 6 px margins, 4 px gaps, captions over values, black as the active colour, lit buttons inverted.
6. **One project, every machine.** A seed code makes the same groove on GB, GBA, DS and 3DS. ETCH DS saves (v1, v2) open on the 3DS unchanged, and ETCH Game Boy saves import.

## Sound engine

* **The shared core is the reference.** CORROSION, `seq_core.c`, ACID, FOUNDRY, FX and storage move into a shared `core/` that builds for DS, 3DS and the host. In milestone 1 the 3DS must render **sample-identical** audio to ETCH DS for the same events and parameters. Prove it with a host test that renders through both build configurations and compares hashes.
* **3DS-only engines may use VFP floats** (OXIDE's voicing maths, VAPOUR, RUST, reverb). The shared core stays fixed-point and division-free on its audio path.
* **ACID** is unchanged in milestone 1. Later it may gain 4× oversampling or a zero-delay-feedback ladder as a *selectable* mode, never as a silent change.
* **VAPOUR (chords):**
  * Paraphonic on Old 3DS: 4 voices through one filter, for the dub-techno stab.
  * Up to 8-voice polyphony with a filter per voice on New 3DS.
  * Saw/pulse stacks with detune, a filter envelope, its own tempo-synced echo and a small plate reverb.
* **RUST:** two oscillators with detune, hard sync and ring mod, a few industrial wavetables, and a paraphonic 3-note mode.
* **FOUNDRY:**
  * METAL and NOISE get their own sequencer lanes.
  * Drum accent and ratchets.
  * **MIC SAMPLING:** record, trim, corrode (crush, pitch, ring) and assign to a lane.
* **FX:** the DS chain, plus a reverb and sidechain ducking from the kick.
* **PUNCH**, live punch-in effects:
  * A **retro buffer** always holds the last bar, for the mix and for the drum bus on its own.
  * **Starting and stopping:** a punch starts quantised to the next 16th (a setting allows "instant"). Releasing it snaps back to where the song would be, because the sequencer never stops underneath. Holding ZR (R on Old 3DS) while releasing latches it.
  * **C-stick (New 3DS): BEAT REPEAT.**
    * Up = 1/4, right = 1/8, down = 1/16, left = 1/32.
    * By default it repeats the **drum bus only**, so the 303 keeps flowing; hold L to repeat the whole mix.
    * Settings: DECAY, PITCH (tape drop), FILTER (darkens), REVERSE (every other repeat), GROW/SHRINK.
  * **Circle Pad: the PUNCH pad.**
    * Left/right is a DJ filter: a resonant low-pass to the left, a high-pass to the right, set by how far you push.
    * Up is ECHO THROW, which freezes into a looping, corroding echo at full push.
    * Down is CRUSH.
    * A quick flick and release fires a one-shot: up TAPE STOP, down VINYL BRAKE, left REVERSE, right GATE.
  * **Old 3DS:** hold Y to switch the Circle Pad to BEAT REPEAT.
  * **PUNCH lanes:** punches can be recorded into a 16-step lane per pattern.

## OXIDE (chord generator)

OXIDE composes harmony around the playing pattern.

**Its own PRNG.** OXIDE is seeded from the seed code plus a fixed salt, so the same code gives the same chords. It never calls CORROSION's PRNG.

**How it composes:**

1. **Reads the bass:** the per-beat or per-bar root (weighted to downbeats and accents), the notes used, and key and scale.
2. **Picks a progression:** for each harmonic step (MOTION), it chooses a diatonic chord containing or tolerating the bass root, preferring the tonic, then chords a step away, with style tension rules on top.
3. **Voices it:** triad, 7th, 9th or sus, depending on COLOUR, inside a SPREAD window *above the 303's register*. It picks the inversion with the least movement from the previous chord.
4. **Gives it rhythm** from a per-style grammar:
   * ACID: off-beat stabs
   * ELECTRO: syncopated hits
   * MINIMAL: one held chord with the filter breathing
   * INDUSTRIAL: clustered hits on the kick
   * BROKEN: displaced stabs
   * SPARSE: long pads
   * DUB TECHNO (a new style, appended after ETCH's six): a minor-9 stab into a long darkening echo
5. **Lives:**
   * DRIFT substitutes neighbour chords (relative, sus, add9, a borrowed iv), bounded around an anchor, at the same commit points CORROSION uses.
   * CORRODE may rewrite the progression.
   * Locks are restored exactly.

**HARMONY tab:**

* MOTION (every beat, or 1, 2 or 4 bars), COLOUR, TENSION, SPREAD.
* RHYTHM: STAB, PAD, ARP, STRUM.
* FOLLOW: chords follow the bass, or *PROGRESSION*, which transposes the bass per bar to a progression; it's opt-in per project and shown on the top screen.
* A chord strip (for example Am7 | Fmaj7 | Dm9) where a tap locks or regenerates a chord.

**Seed codes:** OXIDE settings are a suffix on the 12-digit code (for example `0F1C-9009-5870-H2A3`). The first 12 digits keep producing ETCH's bass and drums; harmony is present only when the suffix is.

**DUB TECHNO and other new styles** can only be appended *after* ETCH's six styles, and only in the DS/3DS layer: `corr_code_decode` rejects style digits of 6 or more, so the extended style lives in the suffix, not in CORROSION.

## Screens (400×240 stereoscopic top, 320×240 touch)

* **Top (always visible):**
  * The playing pattern: bass as pitch, plus the drum lanes and the chord strip.
  * Mode, pattern, generation, next evolution, tempo.
  * The scope and the DSP meter.
  * Evolved steps glow and fade.
  * The **3D landscape** comes later: each generation layers in depth, evolved steps rise forward, and the 3D slider sets depth as well as its macro.
* **Bottom tabs:**
  * LIVE
  * XY
  * PANEL·DRUM
  * KEYS
  * HARMONY
  * GEN
  * SONG·FILE (plus ABOUT)
  * PUNCH settings, as a page of LIVE or a tab, your call
* **Layout** follows ETCH DS's design system, scaled to 320×240. Every hit test uses the drawing constants.
* **Controls:**
  * START: play/stop.
  * SELECT + LEFT/RIGHT: change tab.
  * SELECT + A: evolve.
  * SELECT + B: undo.
  * R: latched fill. On a New 3DS, ZR is the PUNCH latch.
  * L while dragging: a temporary move.
  * Circle Pad: the PUNCH pad. C-stick: BEAT REPEAT.
  * 3D slider: an assignable macro (default: echo feedback).
  * Tilt: a patch-panel source.

## Sequencer

ETCH's step rules and swing scheme (`seq_core.c`, unchanged) run in render time in the audio thread, quantised to the block. Later:

* per-track lengths
* 64 patterns, and a 64-slot chain with named sections
* motion lanes and PUNCH lanes
* drum ratchets
* scene snapshots
* local-wireless clock sync, and Ableton Link over Wi-Fi (only after a licence check: Link is GPLv2 or commercial; ETCH is MIT)

## Technical approach

* **Build:** a `.3dsx` with one `make` (devkitPro devkitARM + libctru + citro2d; citro3d for the stereo view later). A `.cia` is optional.
* **Emulators and hardware:** test in Azahar or Lime3DS (successors to Citra). Build a headless test harness on one of them, as ETCH DS did with melonDS. Its jobs:
  * press buttons and the touch screen
  * move the Circle Pad and C-stick
  * record audio to WAV
  * read RAM for statistics
  * take screenshots

  Old and New 3DS hardware are the real targets.
* **Audio:**
  * A high-priority audio thread on core 0 renders blocks and runs `seq_core` in render time (the role the ARM7 plays on DS).
  * It feeds `ndsp` wave buffers in a ring at the DSP's 32 728 Hz rate.
  * Target latency: 10–15 ms from sequencer to speaker.
  * UI, CORROSION mutation work, file I/O and sensors run on other threads or cores. On Old 3DS, core 1 is time-limited (`APT_SetAppCpuTimeLimit`), so keep synthesis on core 0. New 3DS may use extra cores for VAPOUR polyphony.
  * The audio thread never waits on the UI, never allocates and never takes a lock the UI can hold for long. Use single-producer/single-consumer rings and the same command and edit-epoch protocol as ETCH DS.
* **Memory:** budget for Old 3DS (64 MB for apps). The retro buffer (about 1 bar, stereo, plus the drum bus) and sample memory are allocated at boot.
* **Saves:**
  * Same path family on the SD card: `/etch_ds/etch_ds.sav` is read as-is.
  * 3DS data (OXIDE, VAPOUR, RUST, PUNCH lanes, motion) goes in save format **v3**, using ETCH's checksum-then-header-last scheme.
  * DS builds must keep loading files that contain v3 slots: they ignore what they don't know, or the v3 data lives in its own file. Propose which, and ask.
* **Baseline:** the Old 3DS is the baseline. Every feature works there, except that full VAPOUR polyphony degrades to paraphonic.

## Rules

* **Never edit CORROSION's files.** If you think it needs a change, stop and tell me why.
* **Keep the shared core's audio path free of division and floating point,** and keep the audio-path checker passing for both DS and 3DS builds. 3DS-only DSP may use VFP floats but never allocates or blocks on the audio thread.
* **Don't break ETCH DS.** After the core is shared, ETCH DS must still build with one `make`, still pass its host tests and still render identically.
* **Ask before choosing between options that would be expensive to undo:**
  * repository layout (one monorepo with `core/` + `ds/` + `3ds/`, or a separate ETCH-3DS repository using a submodule)
  * the audio thread and core assignment
  * save format v3
  * the emulator and harness choice
* **Don't build features beyond the milestone you are on;** note ideas in `docs/IDEAS.md`.

## Your task now: Milestone 1, "It squelches in 3D"

Port ETCH DS to the 3DS, sound-identical, and prove the platform layer before any new feature. Deliver:

1. **One `make`** that builds a `.3dsx` (and the shared core restructure, with ETCH DS still building and passing all its tests).
2. **CORROSION** byte-identical (checksums), with ETCH's 44 host tests passing against the copy, and every existing DS host test passing against the shared core.
3. **The audio thread + `ndsp` streaming,** running `seq_core` in render time with ETCH's step rules, the fixed swing clock and the edit epoch. Measure and report latency and dropouts.
4. **Sound parity:** a host test showing the shared core renders the boot song identically in the DS and 3DS configurations. In the emulator, the 3DS build plays the same song.
5. **The title screen** ("ETCH 3DS", "A generative acid machine", "by Synth Centric", PRESS START), then CORRODE from its seed code `0F1C-9009-5870`, playing in DRIFT.
6. **The ETCH DS UI ported** to 400×240 (top, 2D) and 320×240 (bottom): LIVE, XY, PANEL·DRUM, KEYS, GEN, SONG·FILE and ABOUT. ABOUT carries the same four pages (product, generative music, Synth Centric bio, credits), updated for the 3DS.
7. **The Circle Pad as the PUNCH pad's filter axis only** (left low-pass / right high-pass, momentary, quantised). It's the first proof of the PUNCH architecture, including the retro buffer, even though only the filter ships.
8. **Saves:** ETCH DS v1/v2 saves load from `/etch_ds/etch_ds.sav`, and ETCH Game Boy import works.
9. **A CPU measurement per stage** (ACID, FOUNDRY 6 lanes, each FX stage, PUNCH filter, UI frame time) on Old and New 3DS configurations of the emulator, plus a statement of the remaining headroom for OXIDE + VAPOUR on Old 3DS.

## When you finish

Report:

* what works
* what you measured: CPU per stage on Old and New 3DS, latency, dropouts, sound-parity results
* what you couldn't verify: real Old or New 3DS hardware, flash carts or CFW setups, and exact emulator timing fidelity
* the open questions below, with your recommendation for each

**Open questions:**

1. Repository layout: a monorepo, or a separate repository with a submodule?
2. Save format v3: extend the slot file, or use a separate 3DS sidecar file?
3. OXIDE on ETCH's demo songs: on or off by default?
4. PROGRESSION mode: may chords transpose the bass?
5. BEAT REPEAT target: drums only by default?
6. May CORROSION add punches when evolving (an opt-in "performer" layer)?
7. Should the Old 3DS get everything except full VAPOUR polyphony?
8. Distribution: `.3dsx` only, or `.cia` too?
9. Name: ETCH 3DS or ETCH III?
10. Should a reduced PUNCH (Y/X + D-pad) be backported to ETCH DS?
