# ETCH DS

**A generative acid machine for the Nintendo DS — by Synth Centric**

ETCH DS is the Nintendo DS sequel to [ETCH](https://github.com/swebdan-design/Etch---GB), the Game Boy generative acid machine. It keeps ETCH's composer, CORROSION (byte for byte, so every ETCH seed code makes the same groove), synthesises the 303 sound for real, and adds the touch screen.

This is **milestone 1, "It squelches"**. It proves the sound before any interface is built:

* **Press START, hear music.** It boots into ETCH's demo song CORRODE, composed by CORROSION from its seed code `0F1C-9009-5870` and living in DRIFT: every 4 bars, every channel changes a little.
* **ACID**, the 303 voice: SAW, SQUARE, HOLLOW and BITE (band-limited), a real-time resonant 4-pole ladder (303 mode or ETCH's key tracking), a filter envelope, an accent that charges on repeated accents, constant-time slide and drive (OFF / SOFT / HARD / FOLD).
* **FOUNDRY**: six drum lanes synthesised into samples, with ETCH's kits as presets.
* **FX chain**: drive → crush → tempo-synced degrading delay → metallic comb → limiter.
* **Sequencer** on the ARM7: ETCH's step rules and ETCH II's fixed swing clock, sample-accurate on a 32-sample grid.
* **XY pad** on the bottom screen: X = cutoff, Y = resonance.

The milestone report is [`docs/MILESTONE1_REPORT.md`](docs/MILESTONE1_REPORT.md), the design is in [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md), and parked ideas are in [`docs/IDEAS.md`](docs/IDEAS.md).

## Controls (milestone 1)

| | |
|---|---|
| START | play / stop (the moment you press it seeds the living song) |
| UP / DOWN | choose a row on the top screen |
| A + LEFT / RIGHT | change the row by 1 |
| A + UP / DOWN | change the row by 8 |
| touch (bottom) | XY pad: X = cutoff, Y = resonance |
| hold L while dragging | temporary move: it springs back when you let go |

The rows are WAVE, CUTOFF, RES, ENV MOD, DECAY, ACCENT, SLIDE, DRIVE, DRV AMT, FILTER (303 / KEY), EVO (STABLE / DRIFT / CORRODE), BPM, FX (on/off) and CPU TEST. CPU TEST fires ACID and all six drum lanes on every 16th with every FX stage working. The bottom of the top screen shows the CPU meter, per stage.

## Building

Requirements: [BlocksDS](https://blocksds.skylyrac.net/) (tested with 1.25.0 / Wonderful GCC 16.2). The simplest route is BlocksDS's Docker image:

```bash
docker run --rm -v "$PWD":/work -w /work skylyrac/blocksds:slim-latest make
```

With BlocksDS installed natively (in `/opt/blocksds/core`, or `/opt/wonderful/thirdparty/blocksds/core`, or `BLOCKSDS=...`):

```bash
make
```

This writes `etch_ds.nds`. Before compiling, `make` checks that `corrosion/` still matches ETCH 2.5 byte for byte (`corrosion/SHA256SUMS`).

## Testing

The host tests need a host C compiler. The audio-path check and melonDS runs need Python 3 with NumPy, plus Pillow for screenshots.

```bash
make test
```

```bash
python3 tools/check_audio_path.py
```

```bash
tools/melonds_harness/build.sh /path/to/melonDS
```

```bash
python3 tools/run_melonds.py --frames 3600 --stress --touch
```

* `make test` first checks the CORROSION checksums, then runs:
  * ETCH's 44 CORROSION tests (`tests/host/test_generator.c`, copied unmodified) against `corrosion/`
  * 216 sequencer checks: tempo, swing, the swing-change fix, step rules, dice and conditions, the commit point, the edit epoch, stop and stress
  * 81 sound-engine checks: stability over every setting, the resonant peak, constant-time slide, accent charging, 303 versus KEY tracking, and a full render of the boot song through the real sequencer
  
  It writes listenable WAVs to `build/host/`.
* `tools/check_audio_path.py` proves that no division, modulo or floating-point routine is reachable from the render interrupt.
* `tools/melonds_harness/build.sh` builds the headless melonDS harness against a melonDS checkout (core library built with CMake and `-DBUILD_QT_SDL=OFF`).
* `tools/run_melonds.py` runs the ROM in that harness. It presses START, optionally drags the XY pad and runs the CPU test, reads the per-stage CPU meter, the dropout counter and the sequencer counters from emulated RAM, records `build/melonds/*.wav`, takes screenshots and analyses the audio.

To regenerate the DSP tables (Python 3 + NumPy):

```bash
make tables
```

## Layout

```
common/        ipc.h (ARM9<->ARM7 contract), seq_core.c (the sequencer)
arm7/source/   the ARM7 core: clock, streaming, commands, input
arm9/source/   boot + render IRQ, CORROSION scheduling, screens, sound engine
corrosion/     CORROSION from ETCH 2.5, byte-identical (checksummed)
tests/host/    host tests
tools/         table generator, audio-path checker, melonDS harness + runner
docs/          architecture, milestone report, ideas
```

## Credits

**ETCH DS — by Synth Centric.** Synth Centric is Dan Schwebel. CORROSION and ETCH's oscillator recipes come from ETCH (MIT; see `corrosion/LICENSE.ETCH`); the swing-change fix and the edit epoch come from ETCH II. Built with BlocksDS; tested with melonDS.
