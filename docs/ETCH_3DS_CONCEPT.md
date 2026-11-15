# ETCH 3DS — concept

ETCH 3DS — by Synth Centric

**ETCH 3DS is ETCH DS with room to grow.** It keeps the same composer, the same seed codes, the same 303 at the centre and the same project files. The 3DS adds a harmony layer, a second synth, a 3D view of the living song, and new ways to play it: the stick, tilt and the 3D slider.

## 1. What the 3DS gives us

| | DS (ETCH DS) | 3DS | New 3DS |
|---|---|---|---|
| CPU | ARM9 67 MHz (+ ARM7) | ARM11 268 MHz, 2 cores (app: core 0, part of core 1) | 804 MHz, 4 cores |
| RAM | 4 MB | 64 MB for apps | 124+ MB |
| Float | none | VFP hardware floats | same |
| Audio | 16 PCM channels, mixed by us | DSP (`ndsp`): 24 channels, resampling, per-channel biquads, 32 728 Hz | same |
| Screens | 256×192 ×2 | 400×240 stereoscopic top, 320×240 touch | same |
| Controls | D-pad, ABXY, L/R, touch | + Circle Pad, gyro, accelerometer, 3D slider | + C-stick, ZL/ZR |
| Other | mic, wifi | + 3 cameras, QR reading, sockets | + NFC |

**Budget.** ETCH DS uses about 30 % of a 67 MHz ARM9 in its worst case, roughly 20 MHz. An Old 3DS has about 13× that on one core, so the 3DS could run ETCH DS's whole engine several times over before any new feature costs anything.

## 2. Pillars (unchanged, plus one)

1. **Press START, hear music.**
2. **The 303 is the centre;** every other voice frames the bass.
3. **CORROSION stays the composer:** same files, same seed codes. New generators sit around it and never touch its random stream.
4. **Touch is for performing, buttons for editing.** The Circle Pad, tilt and 3D slider are performance controls too, and no feature requires two touches.
5. **Corroded, not clean**, in a minimal black-and-white skin.
6. *New:* **one project, three machines.** A seed code makes the same groove on Game Boy, GBA, DS and 3DS. ETCH DS saves open on the 3DS unchanged.

## 3. OXIDE — the chord generator (headline feature)

OXIDE reads the playing pattern and composes **harmony around the acid line**. It never touches CORROSION's stream. It has its own PRNG, seeded from the seed code plus a fixed salt, so the same code always gives the same chords.

### How it composes

1. **Reads the bass:** the root of each beat or bar (weighted to downbeats and accents), the notes the line uses, and the key and scale from GEN.
2. **Picks a progression:** for each harmonic step (MOTION: 1, 2 or 4 bars, or every beat), it chooses a diatonic chord that contains the bass root or tolerates it. It prefers the tonic, then chords a step away, with the style's tension rules on top.
3. **Voices it:** builds triads, then 7ths, 9ths and sus chords, depending on COLOUR. It chooses the inversion with the least movement from the previous chord (voice leading) inside a SPREAD window placed above the bass, so the chords never mud up the 303's register.
4. **Gives it rhythm** with its own small grammar per style:

   | Style | Chord rhythm |
   |---|---|
   | ACID | off-beat stabs |
   | ELECTRO | syncopated hits |
   | MINIMAL | one held chord, with the filter breathing |
   | INDUSTRIAL | clustered hits on the kick |
   | BROKEN | displaced stabs |
   | SPARSE | long pads |
   | DUB TECHNO (new style) | the classic minor-9 stab into a long, darkening echo |

5. **Lives:** DRIFT swaps a chord for a neighbour (relative minor, sus, add9, a borrowed iv) bounded around the anchor, as CORROSION does for the bass. CORRODE can rewrite the progression outright.

### Controls (a new HARMONY tab)

* **MOTION:** how often the chord changes.
* **COLOUR:** triad → 7th → 9th → clusters.
* **TENSION:** how far from the tonic and how much borrowing.
* **SPREAD:** how wide the voicing is.
* **RHYTHM:** STAB, PAD, ARP or STRUM.
* **FOLLOW:** chords follow the bass root (the default), or *PROGRESSION*: the bass is transposed bar by bar to follow a chord progression. That is new music from the same CORROSION material, with CORROSION itself untouched.
* **A chord strip.** The progression shows as chord names (Am7 | Fmaj7 | Dm9 …), and you can tap a chord to lock it or regenerate it.

### The chord voice: VAPOUR

A paraphonic or polyphonic synth (4–8 voices):

* Saw/pulse stacks with detune.
* One shared filter, envelope-swept for stabs. Dub-techno chords need exactly one filter for the whole chord.
* A tempo-synced echo and a small plate reverb of its own.

On a New 3DS it becomes fully polyphonic, with a filter per voice.

**Seed codes.** OXIDE's settings add digits to the end of the 12-digit code, for example `0F1C-9009-5870-H2A3`. The first 12 digits stay ETCH's, so old codes keep making the same bass and drums, and harmony is added only when the extra digits are present.

## 4. More new things

### Sound

* **RUST**, the second synth planned in the brief (two oscillators, sync, ring mod, industrial wavetables), runs comfortably here.
* **Reverb and space:** a small plate or room, and sidechain ducking from the kick. The DS can't afford these alongside everything else.
* **The 303, refined:** a zero-delay-feedback ladder, 4× oversampling, and a better accent and drive model. This needs a test against the fixed-point DS engine, because seed codes don't cover sound.
* **FOUNDRY grows:**
  * METAL and NOISE get their own sequencer lanes.
  * Drums get accent and ratchets.
  * **MIC SAMPLING:** record a found sound with the microphone, trim it, corrode it (crush, pitch, ring) and drop it on a lane.
* **BOUNCE:** record a performance to WAV on the SD card. This is the "keep this take" button.

### Performance

* **The Circle Pad and C-stick as PUNCH controls** (see below), while the touch screen works the XY pad. That gives two-handed playing with one touch.
* **The 3D slider as a macro fader** for, say, echo feedback or harmony level. It's a real analogue slider on every 3DS.
* **Tilt:** a gyro and accelerometer source on the patch panel (tilt to open the filter).
* **ZL/ZR (New 3DS):** extra modifiers, such as latching a punch.
* **Motion lanes:** record knob, XY and tilt moves into the pattern.

### PUNCH — live punch-in effects

Momentary effects you play over the song, in the spirit of a DJ mixer or a Pocket Operator's punch-ins:

* **Push** and the effect starts in time, on the next 16th (or instantly, as a setting).
* **Let go** and the song snaps back exactly where it would have been, because the sequencer never stops underneath.
* **Hold ZR as you let go** and the effect latches until you punch again.

Everything works from a **retro buffer**: the last bar of the mix (and of the drum bus on its own) is always being recorded. That costs about 260 KB.

**C-stick: BEAT REPEAT** (New 3DS). Flick a direction to repeat a slice, starting at the punch point:

| Direction | Repeat length |
|---|---|
| Up | 1/4 note |
| Right | 1/8 note |
| Down | 1/16 note |
| Left | 1/32 note (a roll) |

* **Target:** the repeat grabs the **drum bus only** by default, so the 303 keeps flowing over the stutter. Hold L to repeat the whole mix.
* **Variations** (PUNCH settings):
  * **DECAY:** each repeat is quieter.
  * **PITCH:** repeats drop like a slowing tape.
  * **FILTER:** repeats darken.
  * **REVERSE:** every other repeat plays backwards.
  * **GROW / SHRINK:** for example 1/4 → 1/8 → 1/16 while held, the classic build into a drop.

**Circle Pad: the PUNCH pad.** Its position is an effects XY. Centred, it does nothing.

* **Left / right:** a DJ filter. Push left to close a resonant low-pass, push right to open a high-pass. How far you push sets the cutoff.
* **Up:** ECHO THROW. The send rises into a tempo-synced echo; at full push it **freezes** into an infinite loop that keeps corroding (darker and fewer bits on every pass).
* **Down:** CRUSH. Bit and sample-rate reduction deepen the further you push.
* **Clicks:** a quick flick and release fires a **one-shot** instead: up is TAPE STOP, down is VINYL BRAKE, left is REVERSE (the last beat, backwards), right is GATE (a tempo-synced chopper for one bar).

**Record it.** Punches can be recorded into a **PUNCH lane** per pattern (16 steps, one effect per step), so a stutter or a filter rise becomes part of the song. Under DRIFT and CORRODE the lane is left alone unless you allow it, in which case CORROSION's evolution can sprinkle punches in (a new, bounded "performer" layer).

**On an Old 3DS** (no C-stick): hold ZL… which it also lacks. So instead, hold **Y** and the Circle Pad switches from the PUNCH pad to BEAT REPEAT, with the same four directions. The Circle Pad Pro accessory adds a second stick.

**Back to ETCH DS.** The DS has the CPU and memory for a reduced PUNCH: a one-bar buffer of about 260 KB, out of the 4 MB of which ETCH DS uses about 0.6 MB. Hold **Y + D-pad** for the four beat repeats, and **X + D-pad** for filter low/high, echo throw and crush. That makes a good first test of the idea before the 3DS exists.

### The living song in 3D (top screen)

* The pattern becomes a **landscape in depth**. Each generation of CORROSION stacks a layer behind the current one, so you can see the song erode and reform. Evolved steps rise toward you and sink back.
* The oscilloscope becomes a **3D trace**, with stereo spread as depth.
* It stays minimal: white lines on black (or the reverse), with the 3D slider setting depth as well as the macro.

### Songs and sharing

* **Song structure:** 64 patterns, the 64-slot chain with named sections, per-track lengths and scene snapshots, all from the original brief and finally with the RAM to fit.
* **QR sharing:** a song's seed code, OXIDE settings and sound fit in a QR code. Show it on screen and another 3DS scans it with the camera.
* **PHOTO SEED:** point the camera at something and its image is hashed into a seed code. The groove comes from the room you're in.
* **Sync:**
  * Local wireless clock sync between 3DS units for jamming.
  * Over Wi-Fi, an **Ableton Link** peer to sync with laptops and phones. Licensing needs a check: Link is GPLv2 or commercial, and ETCH's code is MIT.
* **Imports:** ETCH DS saves open unchanged from the same SD path, and ETCH Game Boy saves import as they do on DS.

### Evolution

* **DECAY** (open question from milestone 1) arrives here: a fourth mode in which the song slowly thins and rusts, built outside CORROSION.
* **Sound evolution:** patches drift around a sound anchor, with locks.

## 5. How it would be built

* **Toolchain:** devkitARM with libctru for the system, citro2d for the UI and citro3d for the stereo view. It ships as `.3dsx` for the Homebrew Launcher, and optionally `.cia`. It would be tested in Azahar/Lime3DS (successors to Citra) and on Old and New 3DS hardware.
* **Shared core, two platform layers.** The source splits into three parts:

  ```
  core/   corrosion/ (byte-identical), seq_core.c, acid, foundry, fx, storage, OXIDE
  ds/     ARM7 clock + streaming, ARM9 IPC renderer, 256x192 UI   (ETCH DS)
  3ds/    audio thread + ndsp streaming, 400x240 / 320x240 UI, sensors
  ```

  The DS engine is portable fixed-point C with host tests, so it moves across unchanged. ETCH DS and ETCH 3DS then sound identical by default, and 3DS-only extras can use floats.
* **Audio.** A high-priority audio thread renders blocks and runs `seq_core` in render time, as the ARM7 does on DS, then feeds `ndsp` wave buffers. The target latency is 10–15 ms. Old 3DS limits how much of core 1 an app may use, so heavy synthesis stays on core 0 and core 1 takes the UI and sensors. A New 3DS gets a third core for VAPOUR polyphony.
* **One project format.** ETCH DS's slot file grows a version 3 for the 3DS data (OXIDE, VAPOUR, RUST, motion). DS-only builds ignore that data, and DS saves load on 3DS.

## 6. Suggested milestones

1. **Port.** ETCH DS on 3DS with an identical sound and the same seed codes and saves, a 400×240 UI, and the Circle Pad as XY.
2. **OXIDE + VAPOUR.** Chords, the HARMONY tab, DUB TECHNO, and the extended seed codes.
3. **Performance.** PUNCH (Circle Pad effects pad, C-stick beat repeat, the retro buffer, PUNCH lanes), the 3D slider, tilt, the patch panel with the new sources, and motion lanes. A reduced PUNCH can be tried on ETCH DS first.
4. **3D landscape and BOUNCE to WAV.**
5. **Mic sampling, QR sharing, PHOTO SEED.**
6. **Sync:** local wireless, then Link (after the licence check).

## 7. Questions to settle

1. **OXIDE by default:** does a demo song start with chords on, or off until you turn them up? Recommendation: off for ETCH's demo songs, so they sound as they always have, and on for new 3DS-only demo songs.
2. **PROGRESSION mode:** may chords transpose the bass? It's powerful, but it changes what a seed code "means". Recommendation: allow it, as an explicit per-project switch shown on the top screen.
3. **Old 3DS:** must everything run on it? Recommendation: everything except full VAPOUR polyphony, which drops to paraphonic on Old 3DS.
4. **Distribution:** `.3dsx` only, or `.cia` as well? Recommendation: `.3dsx` first.
5. **Name:** ETCH 3DS, or ETCH III (the open question from ETCH DS)? Recommendation: ETCH 3DS, keeping "ETCH III" free.
6. **Beat repeat target:** drums only by default (the 303 keeps flowing), or the whole mix? Recommendation: drums only, with L for everything.
7. **Should CORROSION add punches by itself** when evolving? Recommendation: an opt-in setting, off by default.
