# ETCH DS — milestone 1 report: "It squelches"

ETCH DS — by Synth Centric · BlocksDS 1.25.0 · melonDS (git `497aa6c`, interpreter, no JIT)

## 1. What works

| # | Deliverable | Status | Evidence |
|---|---|---|---|
| 1 | One `make` builds a `.nds` | **Done** | `make` → `etch_ds.nds` (185 KB). It also builds in the official `skylyrac/blocksds:slim-latest` image from a clean tree, to a byte-identical ROM. |
| 2 | ARM7 clock + streaming, ETCH's step rules, fixed swing | **Done** | `common/seq_core.c` runs on the ARM7 block interrupt. 216 host checks cover tempo exactness, swing offsets, ETCH II's swing-change fix (0 skipped gates when swing jumps at every phase at 220 BPM), step rules, dice and conditions, the commit point, the edit epoch, stop and stress. In melonDS: 2 475 steps → 2 475 gate points, 0 late. |
| 3 | CORROSION byte-identical, checksummed, ETCH's 44 tests pass | **Done** | `corrosion/SHA256SUMS` matches ETCH 2.5 (upstream `62ac4c6`, the same files ETCH II carries). `make` refuses to build if they change. `make test` runs ETCH's unmodified `test_generator.c` against the copy: **44 checks, 0 failures**. |
| 4 | ACID: 4 oscillators, real-time filter, accent, slide, drive | **Done** | 81 DSP checks. The ladder stays inside ±1.0 over 256 settings × 2 s; RES 120 lifts the harmonics at the cutoff by +27.9 dB; every interval slides in exactly SLIDE ticks with no overshoot; accent runs build (peak cutoff +200, +186, +84 in 1/64 semitone); 303 mode ignores the note; KEY mode tracks it exactly. |
| 5 | ETCH demo song from its seed code, DRIFT on boot; minimal FOUNDRY | **Done** | CORRODE's code `0F1C-9009-5870`, run through CORROSION's GEN ALL, gives the boot pattern; a host check proves it equals CORROSION's output for that code. Sound and evolution settings are CORRODE's. FOUNDRY has six lanes: ETCH's 16 kit sounds plus METAL and NOISE. In melonDS, DRIFT committed 38 times in 154 bars, exactly every 4 bars. |
| 6 | XY pad (X cutoff, Y resonance), status line | **Done** | melonDS touch test: tap → (13, 107); drag → (116, 11); L held while dragging → (63, 123), which springs back to (116, 11) on release. The top screen shows song, code, mode, bars to the next evolution, generation, pattern, parameters and the CPU meter. |
| 7 | CPU per stage with ACID + 6 lanes + FX; sample rate | **Done** | See §2. |

**Boot.** Audio runs 0.37 s after power-on, and the title waits for START. Pressing START is all it takes; touch is never needed.

**Audio path.** `tools/check_audio_path.py` walks `arm9.elf` from the render interrupt. 21 functions are reachable, and none is a division, modulo, soft-float or libm routine. The hot ones are in ITCM and their state is in DTCM.

### One interpretation to confirm

"One ETCH demo song loaded from its seed code" is implemented literally: the boot pattern is what CORROSION composes from CORRODE's 12-digit code. ETCH's own CORRODE track is **not** generated from that code. It is a hand-written 4-pattern arrangement (`tools/presets.py` in ETCH), so the boot groove differs from the ETCH ROM's track 1. Loading the hand-written arrangement instead means one 330-byte table and no other change. Tell me which you want.

## 2. Measurements

### CPU (ARM9), measured by the ROM itself

The ROM times itself with the hardware timers: TIMER2+3 count at the bus clock, and every stage of every block is accumulated. Figures are a percentage of real time, averaged over 1 s windows. The block period is 128 samples = 3.91 ms = 262 144 ARM9 cycles.

| Stage | Boot song (DRIFT) | CPU test: ACID retrigger + accent on every 16th, all 6 lanes every 16th, all FX active |
|---|---|---|
| events + bus clear | 0.33 % | 0.33 % |
| ACID (osc + 2× ladder + VCA + drive + DC block) | 5.5 – 5.8 % | 8.56 % (≈175 cycles/sample) |
| FOUNDRY (6 lanes) | 3.1 – 4.5 % | 10.2 – 11.3 % |
| FX: drive | 2.51 % | 2.51 % |
| FX: crush | 0.02 % (bypassed) | 1.01 % |
| FX: delay | 2.54 % | 2.54 % |
| FX: comb | 1.50 % | 1.50 % |
| FX: limiter + int16 out | 2.20 % | 2.20 % |
| cache flush | 0.06 % | 0.06 % |
| **total, average** | **17.8 – 19.2 %** | **29.4 – 30.1 %** |
| **worst single block** | **26.1 %** | **31.0 %** |
| ARM7 sequencer, worst block | 1.6 % | 1.6 % |

Source: a 5-minute melonDS soak (`tools/run_melonds.py --frames 18000 --stress`). The first half plays the song and the second half runs the CPU test.

**How far to trust these numbers.** melonDS emulates the ARM9's instruction cache but **not its data cache**, so every wavetable and drum-sample read is charged at main-RAM latency. On hardware most of those reads hit the 4 KB data cache, so FOUNDRY in particular is probably over-estimated. Other ARM9 timings are approximate too. The figures are a planning estimate, not a measurement. The same on-screen meter runs on a real DS and gives real numbers there.

### Sample rate

The engine runs at **32 728.5 Hz**: a sound timer period of 512, with exactly one ARM7 timer tick per sample. This is the DS mixer's own rate, so a higher rate would buy nothing on the original DS.

**Recommendation: keep 32 kHz.** At about 30 % worst case for ACID plus six lanes plus the full FX chain, there is roughly 3× headroom for RUST, the UI and later features. Nothing needs to be lowered. Spend the headroom on features first. If RUST's paraphonic mode and the patch panel eat it, cut the ladder from 2× to 1× oversampling (about −2 % in the CPU test) before touching the rate.

### Audio dropouts in melonDS

| Check | Result |
|---|---|
| ARM7 dropout counter (block not rendered when it started to play), 5 minutes, song + CPU test | **0** |
| Repeated stale 128-sample blocks in the recording | **0** |
| Silent gaps ≥ 2 ms after START | **0** |
| Sequencer gate points skipped | **0** |

**Level.** The limiter holds 0.85 of full scale. The host render peaks at 0.84; the boot song averages −8.8 dBFS RMS. melonDS's recording shows a few hundred samples at full scale under the CPU test. These are overshoot from melonDS's band-limited output resampler (blip_buf) on the crushed, stair-stepped signal, not from the ROM. The ROM's own output never exceeds the ceiling, and the host render of the same code shows it.

### Memory

* ITCM: 7.2 KB of 32 KB
* DTCM: 3.9 KB of 16 KB
* main RAM: about 0.6 MB of 4 MB (wavetables 64 KB, delay line 64 KB, FOUNDRY samples about 360 KB, XY-pad background 96 KB)

## 3. What I could not verify

* **Real hardware.** Nothing has run on a DS, DS Lite or DSi. The CPU figures are emulator estimates (see above), and the interrupt latency and cache behaviour of the render interrupt are unmeasured on hardware.
* **Flash carts.** Untested. Milestone 1 has no saves, so it needs no DLDI. It should run from any cart that boots homebrew, but no cart has tried it.
* **DSi mode.** Not tested; the ROM targets DS mode.
* **How it sounds.** I cannot listen. The sound is checked by tests and spectrograms: filter sweeps, the resonance peak, accent build-up, levels and clipping. Please listen to these:
  * `build/host/engine_boot_song.wav`: 36 s of the boot song through the real sequencer and engine, rendered on the host
  * `build/host/acid_waves_drives.wav`: the boot bass line on SAW/OFF, SQUARE/SOFT, HOLLOW/HARD and BITE/FOLD
  * `build/melonds/*.wav`: recordings from the emulator
  
  The accent capacitor rates, drive curves and FOUNDRY kits are first drafts tuned by measurement, not by ear.
* **Touch calibration** on real hardware. In melonDS the touch coordinates are ideal.
* **ETCH II's music.** Its extension tracks are out of scope; only its fixes were reused.

## 4. Open questions and recommendations

1. **Final name: ETCH DS or ETCH III?** Recommendation: **ETCH DS**. It tells the homebrew audience the platform at a glance, sits naturally beside ETCH (Game Boy) and ETCH II (GBA), and the DS identity (touch, two screens) is the point of this machine. Keep "III" free in case a later machine is the true third chapter.
2. **Should ETCH Game Boy saves import?** Recommendation: **yes, read-only import**. The GB format is ETCH's `project_t`, which is already this project's data model byte for byte. Importing means:
   * read a `.sav` (32 KB SRAM dump) from the SD card
   * verify ETCH's Fletcher-16 checksum per slot
   * copy the patterns as they are
   * map the coarse synth settings to ACID's finer ones (`demo_import_sound` already does that mapping)
   
   It is cheap, carries a lot of goodwill, and needs no export path.
3. **A fourth evolution mode, DECAY (the song slowly thins and degrades)?** Recommendation: **yes, as a DS layer outside CORROSION**. Passing a new mode value into `corr_mutate` would change CORROSION's behaviour, so don't. Build DECAY instead from:
   * CORROSION's own DRIFT mutations
   * a DS-side thinning operator with its own PRNG: hits and notes fall away with rising probability, but the downbeat root and the kick on 1 always survive
   * sound degradation: crush bits drop, cutoff and delay darken
   
   Store it in DS-only project data. ETCH's `gen.mode` stays ≤ 2, so imported and exported projects stay valid.
4. **How free should the patch panel be?** Recommendation: **fixed jacks, any source to any destination jack, at control rate**. Sources would be LFO 1–2, filter env, accent, XY-X/Y, mic level and random. Destinations would be a fixed list of about 16 per voice. There would be a few cables, each with depth and polarity. No audio-rate cables: modulation at the 1 kHz control rate keeps the CPU budget predictable, which is what lets the 32 kHz guarantee above hold. This keeps DS-10's playfulness without its blank-page problem.
5. **Should the microphone excite the resonator, or only stir randomness?** Recommendation: **default to stirring randomness**, by feeding the entropy pool, plus a mic-level envelope as a patch-panel source. **Make resonator excitation opt-in** and off by default. The DS speaker sits next to the mic, so live excitation howls without headphones. It also means streaming mic samples to the ARM9, which the ARM7 core can add later.
6. **Original DS or DSi as the target?** Recommendation: **keep the original DS as the target**. Even pessimistic emulator figures leave about 70 % of the ARM9 free with the whole milestone-1 engine running. Treat DSi mode as an optional quality tier with no DSi-only features: the 133 MHz clock and 16 MB could buy 4× ladder oversampling, longer delays and RUST's paraphony at full quality, and the DSi's internal SD card could hold projects.

## 5. Deviations and notes

* **ETCH's LFO** is not ported; it is not in the milestone-1 list and is noted in `docs/IDEAS.md`.
* **Fill and REGEN pads.** The sequencer already has ETCH's fill masks and conditions, but there are no pads yet; the UI is a later milestone.
* **Lanes 4–5 (METAL, NOISE)** exist and are budgeted. ETCH patterns never write them, so only the CPU test plays them.
* **The milestone-1 parameter list** on the top screen is a test interface that follows ETCH's A + D-pad grammar. It is not the planned screen design.
* **CPU-test mode** (row CPU TEST) is kept in the ROM so the meter can be read on hardware.
