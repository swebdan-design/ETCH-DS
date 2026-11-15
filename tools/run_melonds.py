#!/usr/bin/env python3
"""
ETCH DS -- by Synth Centric
Runs etch_ds.nds in the headless melonDS harness and reports what happened.

    tools/run_melonds.py [--harness PATH] [--frames N] [--stress] [--touch]
                         [--out DIR] [--rom etch_ds.nds]

Scenario: boot, press START at frame 60 (pillar 1: no touch needed), run.
Options add a CPU-test phase (cursor to CPU TEST, A+RIGHT) and an XY-pad
drag.  Reads the renderer's per-stage CPU statistics, dropouts, sequencer
counters and CORROSION commits straight from emulated RAM (addresses from
the ELF), writes the audio to OUT/<name>.wav and screenshots to PNG, and
analyses the audio: level, clipping, silent gaps (dropouts) and onset timing.
"""
import argparse
import os
import re
import struct
import subprocess
import sys
import tempfile
import wave

import numpy as np

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PREFIX = "/opt/wonderful/toolchain/gcc-arm-none-eabi/bin/arm-none-eabi-"
FS = 16756991 / 512

ST = ["EVENTS", "ACID", "DRUMS", "DRIVE", "CRUSH", "DELAY", "COMB", "LIMIT", "OUT"]


def nm_symbols(elf):
    out = subprocess.check_output([PREFIX + "nm", elf], text=True)
    syms = {}
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3:
            syms[p[2]] = int(p[0], 16)
    return syms


def shared_offsets():
    """offsetof(shared_t, field) and the stats layout, from the ARM compiler."""
    fields = ["playing", "step", "cur", "bar_total", "phrase", "stress", "evo_state",
              "evo_commits", "evo_stale_drops", "blk_played", "blk_requested", "blk_done",
              "dropouts", "steps_total", "gates_total", "gates_late", "arm7_ticks_last",
              "arm7_ticks_max"]
    src = ['#include <stddef.h>', '#include "ipc.h"', '#include "audio.h"']
    for f in fields:
        src.append("const unsigned off_%s = offsetof(shared_t, %s);" % (f, f))
    src.append("const unsigned off_proj = offsetof(shared_t, proj);")
    src.append("const unsigned off_pat0 = offsetof(shared_t, proj.pat);")
    src.append("const unsigned off_gen = offsetof(shared_t, proj.gen);")
    src.append("const unsigned off_astat_avg = offsetof(audio_stats_t, avg_pct);")
    src.append("const unsigned off_astat_tot = offsetof(audio_stats_t, avg_total);")
    src.append("const unsigned off_astat_max = offsetof(audio_stats_t, max_total_pct);")
    src.append("const unsigned off_astat_win = offsetof(audio_stats_t, windows);")
    with tempfile.TemporaryDirectory() as d:
        c = os.path.join(d, "o.c")
        o = os.path.join(d, "o.o")
        open(c, "w").write("\n".join(src) + "\n")
        subprocess.check_call([PREFIX + "gcc", "-mcpu=arm946e-s", "-D__NDS__", "-DARM9", "-c",
                               "-I" + os.path.join(ROOT, "common"), "-I" + os.path.join(ROOT, "corrosion"),
                               "-I" + os.path.join(ROOT, "arm9/source"),
                               "-I/opt/wonderful/thirdparty/blocksds/core/libs/libnds/include",
                               c, "-o", o])
        dump = subprocess.check_output([PREFIX + "objdump", "-s", "-j", ".rodata", o], text=True)
        names = subprocess.check_output([PREFIX + "nm", o], text=True)
    data = b""
    for line in dump.splitlines():
        m = re.match(r"^\s*([0-9a-f]{4,8}) ((?:[0-9a-f]{2,8} ?){1,4})", line)
        if m:
            data += bytes.fromhex(m.group(2).replace(" ", ""))
    offs = {}
    for line in names.splitlines():
        p = line.split()
        if len(p) == 3 and p[2].startswith("off_"):
            a = int(p[0], 16)
            offs[p[2][4:]] = struct.unpack_from("<I", data, a)[0]
    return offs


def run(args, script, name, frames):
    elf = os.path.join(ROOT, "build", "arm9.elf")
    syms = nm_symbols(elf)
    offs = shared_offsets()
    base = syms["shared_store"]
    ast = syms["astats"]
    watch = []
    for f, sz in (("playing", 1), ("step", 1), ("bar_total", 1), ("phrase", 1), ("evo_commits", 1),
                  ("evo_stale_drops", 1), ("blk_played", 4), ("blk_done", 4), ("dropouts", 4),
                  ("steps_total", 4), ("gates_total", 4), ("gates_late", 4), ("arm7_ticks_max", 4),
                  ("arm7_ticks_last", 4)):
        watch.append("%s=%x:%d" % (f, base + offs[f], sz))
    for i, s in enumerate(ST):
        watch.append("cpu_%s=%x:2" % (s, ast + offs["astat_avg"] + 2 * i))
    watch.append("cpu_total=%x:2" % (ast + offs["astat_tot"]))
    watch.append("cpu_max=%x:2" % (ast + offs["astat_max"]))
    watch.append("windows=%x:4" % (ast + offs["astat_win"]))
    watch.append("pat0_gen=%x:1" % (base + offs["pat0"] + 62))
    watch.append("xy_cutoff=%x:1" % (syms["acid_p"] + 1))
    watch.append("xy_res=%x:1" % (syms["acid_p"] + 2))
    os.makedirs(args.out, exist_ok=True)
    prefix = os.path.join(args.out, name)
    cmd = [args.harness, args.rom, str(frames), prefix, script, ";".join(watch)]
    res = subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode:
        print(res.stderr)
        sys.exit(1)
    mem = {}
    for line in res.stdout.splitlines():
        p = line.split()
        if p and p[0] == "MEM":
            mem.setdefault(int(p[1]), {})[p[2]] = int(p[3])
    for f in sorted(os.listdir(args.out)):
        if f.startswith(name + "_") and f.endswith(".ppm"):
            ppm = os.path.join(args.out, f)
            try:
                from PIL import Image
                Image.open(ppm).save(ppm[:-4] + ".png")
                os.remove(ppm)
            except Exception:
                pass
    return mem, prefix + ".wav", res.stderr.strip()


def analyse(wav, start_s=2.0):
    w = wave.open(wav)
    n = w.getnframes()
    x = np.frombuffer(w.readframes(n), dtype=np.int16).reshape(-1, 2).astype(np.float64)
    x = x[int(start_s * FS):]
    mono = x.mean(axis=1)
    out = {}
    out["seconds"] = len(x) / FS
    out["rms_dbfs"] = 20 * np.log10(np.sqrt(np.mean(mono ** 2)) / 32768 + 1e-12)
    out["peak"] = int(np.max(np.abs(x)))
    out["clipped_samples"] = int(np.sum(np.abs(x) >= 32760))
    # silent gaps: runs of >= 64 samples where |x| < 2 while the music plays
    silent = np.abs(mono) < 2
    longest, run_ = 0, 0
    gaps = 0
    for s in silent:
        if s:
            run_ += 1
        else:
            if run_ >= 64:
                gaps += 1
            longest = max(longest, run_)
            run_ = 0
    out["silent_gaps_ge_2ms"] = gaps
    out["longest_silence_ms"] = longest / FS * 1000
    # repeated-block detection (a dropout replays a stale 128-sample block)
    blocks = len(mono) // 128
    rep = 0
    for b in range(1, blocks):
        a = x[b * 128:(b + 1) * 128]
        p = x[(b - 4) * 128:(b - 3) * 128] if b >= 4 else None
        if p is not None and np.any(a) and np.array_equal(a, p):
            rep += 1
    out["stale_block_repeats"] = rep
    # spectral centroid over time (does the filter move?)
    seg = int(FS * 0.05)
    cents = []
    for i in range(0, len(mono) - seg, seg):
        s = np.abs(np.fft.rfft(mono[i:i + seg] * np.hanning(seg)))
        f = np.fft.rfftfreq(seg, 1 / FS)
        if s.sum() > 0:
            cents.append(float((s * f).sum() / s.sum()))
    out["centroid_hz_min_med_max"] = (int(np.min(cents)), int(np.median(cents)), int(np.max(cents))) if cents else None
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--harness", default=os.path.join(ROOT, "tools/melonds_harness/harness"))
    ap.add_argument("--rom", default=os.path.join(ROOT, "etch_ds.nds"))
    ap.add_argument("--out", default=os.path.join(ROOT, "build/melonds"))
    ap.add_argument("--frames", type=int, default=1800)
    ap.add_argument("--stress", action="store_true")
    ap.add_argument("--touch", action="store_true")
    ap.add_argument("--name", default="run")
    a = ap.parse_args()

    script = ["30:SHOT", "90:START:3", "300:SHOT"]
    dumps = list(range(120, a.frames, 120))
    if a.touch:
        # XY tab, then: tap, drag, and an L-held (temporary) drag
        script += ["380:TOUCH:64,10,3", "399:DUMP", "400:TOUCH:40,40,30", "429:DUMP", "430:SHOT", "460:TOUCH:220,160,60",
                   "519:DUMP", "520:SHOT", "560:L:80", "570:TOUCH:128,40,40", "605:DUMP", "606:SHOT",
                   "640:DUMP", "641:SHOT"]
    if a.stress:
        t = a.frames // 2
        # PANEL tab, tap the CPU TEST knob (a tap steps a choice), back to LIVE
        script += ["%d:TOUCH:106,10,3" % t, "%d:TOUCH:224,172,3" % (t + 20),
                   "%d:TOUCH:23,10,3" % (t + 60), "%d:SHOT" % (t + 300)]
    script += ["%d:DUMP" % d for d in dumps]
    script.append("%d:SHOT" % (a.frames - 1))
    mem, wav, log = run(a, ";".join(script), a.name, a.frames)
    print(log)
    last = mem[max(mem)]
    print("final:", {k: v for k, v in last.items() if not k.startswith("cpu_")})
    for fr in sorted(mem):
        m = mem[fr]
        cpu = " ".join("%s=%.2f" % (s, m["cpu_" + s] / 100) for s in ST)
        if "xy_cutoff" in m and fr in (399, 429, 519, 605, 640):
            print("frame %5d XY pad: cutoff %d res %d" % (fr, m["xy_cutoff"], m["xy_res"]))
            continue
        print("frame %5d windows=%3d total=%.2f%% max=%.2f%% | %s | arm7max=%.2f%% drops=%d commits=%d"
              % (fr, m["windows"], m["cpu_total"] / 100, m["cpu_max"] / 100, cpu,
                 m["arm7_ticks_max"] * 100 / 2048, m["dropouts"], m["evo_commits"]))
    print("audio:", analyse(wav))


if __name__ == "__main__":
    main()
