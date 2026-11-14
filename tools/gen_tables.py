#!/usr/bin/env python3
"""
ETCH DS -- by Synth Centric
Generates the ARM9 DSP lookup tables: arm9/source/dsp_tables.c / dsp_tables.h.

Everything the audio path needs that would otherwise take a division, an
exponential or a sine is precomputed here, so the real-time code is integer
multiply/shift/add only.  The output is committed (like ETCH's generated
tables), so `make` does not need Python or NumPy.

Tables
  wt[4][8][1025]   band-limited single-cycle waves, one mip level per octave.
                   The four oscillators use ETCH's own recipes
                   (tools/generate_waves.py in ETCH): SAW, SQUARE, HOLLOW, BITE.
                   Harmonics are kept up to 0.475 * FS for the top note of the
                   octave, with a gentle raised-cosine taper over the top
                   quarter to tame Gibbs ringing.  Entry 1024 = entry 0 (guard).
  inc_top[768]     32-bit phase increment for the octave starting at C9
                   (1/64-semitone steps); shift right for lower octaves.
  cut_g[512]       one-pole coefficient g = 1 - exp(-2 pi fc / (2 FS)) (Q16)
                   for the 2x oversampled ladder, fc in quarter semitones
                   from C0 (16.35 Hz).
  res_k[128]       ladder feedback k (Q12), 0 .. 4.4 (self-oscillates near 4).
  dec_coef[128]    filter envelope decay per control tick (Q16): 30 ms .. 4.5 s
                   to e^-6, as ETCH's DECAY.
  vca_coef[128]    VCA decay per control tick (Q16).
  slide_ticks[128] constant-time slide length in control ticks (4 ms .. 250 ms)
  slide_recip[128] floor(65536 / slide_ticks) (Q16)
  semi_ratio[49]   2^(n/12), n = -24..24 (Q16) for drum tuning
  sine[1024]       Q15 sine for FOUNDRY
  step_len[256]    samples per 16th note for bpm 0..255 (bpm < 30 clamped)
  limit_recip[1024] Q15 gain = LIMIT_CEIL / peak for peak = i << 8
  note_period[128] Q8 period in samples of note n (0 = C1), for the comb
"""
import math
import os
import sys

import numpy as np

FS = 16756991.0 / 512.0          # 32 728.5 Hz
CR = FS / 32.0                   # control rate (one tick per 32 samples)
OS = 2                           # ladder oversampling
C1 = 32.703195662574829
N_WAVE, N_MIP, TBL = 4, 8, 1024
LIMIT_CEIL = 27853               # 0.85 of full scale (headroom for reconstruction overshoot)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT_C = os.path.join(ROOT, "arm9", "source", "dsp_tables.c")
OUT_H = os.path.join(ROOT, "arm9", "source", "dsp_tables.h")


def osc_cycle(osc, n=1 << 16):
    """One cycle of ETCH's oscillator personalities (same recipes)."""
    t = np.arange(n) / n
    if osc == 0:      # SAW: falling ramp, all harmonics 1/n
        x = 1.0 - 2.0 * t
    elif osc == 1:    # SQUARE: slightly asymmetric pulse with a sloped top
        x = np.where(t < 0.47, 1.0, -1.0) * 0.82 + (1.0 - 2.0 * t) * 0.18
    elif osc == 2:    # HOLLOW: rounded odd-harmonic tone (square/triangle)
        sq = np.where(t < 0.5, 1.0, -1.0)
        tri = 1.0 - 4.0 * np.abs(t - 0.5)
        x = 0.45 * sq + 0.55 * tri
    else:             # BITE: hard-synced ramp (slave ratio 2.6) + ramp
        slave = 1.0 - 2.0 * ((t * 2.6) % 1.0)
        x = 0.6 * slave + 0.4 * (1.0 - 2.0 * t)
    return x - x.mean()


def mip_harmonics(m):
    fmax = C1 * 2.0 ** (m + 1)
    return max(1, min(TBL // 2 - 1, int(0.475 * FS / fmax)))


def build_waves():
    out = np.zeros((N_WAVE, N_MIP, TBL + 1), dtype=np.int16)
    for w in range(N_WAVE):
        spec_full = np.fft.rfft(osc_cycle(w)) / (1 << 16)
        if w == 2:    # HOLLOW's extra roll-off (ETCH: 1/n^0.35)
            n = np.arange(len(spec_full))
            n[0] = 1
            spec_full = spec_full / n ** 0.35
        mips = []
        for m in range(N_MIP):
            h = mip_harmonics(m)
            spec = np.zeros(TBL // 2 + 1, dtype=complex)
            spec[1:h + 1] = spec_full[1:h + 1]
            taper_from = int(h * 0.75)
            for k in range(taper_from + 1, h + 1):
                a = (k - taper_from) / max(1, h - taper_from)
                spec[k] *= 0.5 + 0.5 * math.cos(math.pi * a)
            mips.append(np.fft.irfft(spec, TBL) * TBL)
        scale = 0.92 / max(np.max(np.abs(x)) for x in mips)
        for m in range(N_MIP):
            q = np.clip(np.round(mips[m] * scale * 32767), -32767, 32767).astype(np.int16)
            out[w, m, :TBL] = q
            out[w, m, TBL] = q[0]
    return out


def tables():
    t = {}
    t["inc_top"] = [min(0xFFFFFFFF, int(round(C1 * 2 ** 8 * 2 ** (r / 768.0) / FS * 2 ** 32)))
                    for r in range(768)]
    g = []
    for i in range(512):
        fc = C1 / 2.0 * 2 ** (i / 48.0)              # quarter semitones from C0
        fc = min(fc, 0.42 * FS * OS / 2.0)
        g.append(int(round((1.0 - math.exp(-2 * math.pi * fc / (FS * OS))) * 65535)))
    t["cut_g"] = g
    t["res_k"] = [int(round(4.4 * (i / 127.0) ** 1.35 * 4096)) for i in range(128)]
    dec = []
    vca = []
    for i in range(128):
        T = 0.030 * (4.5 / 0.030) ** (i / 127.0)    # 30 ms .. 4.5 s to e^-6
        dec.append(min(65535, int(round(math.exp(-6.0 / (T * CR)) * 65536))))
        Tv = 0.35 + 2.65 * (i / 127.0)               # VCA: 0.35 s .. 3 s to e^-3
        vca.append(min(65535, int(round(math.exp(-3.0 / (Tv * CR)) * 65536))))
    t["dec_coef"] = dec
    t["vca_coef"] = vca
    ticks = []
    for i in range(128):
        T = 0.004 * (0.250 / 0.004) ** (i / 127.0)
        ticks.append(max(1, int(round(T * CR))))
    t["slide_ticks"] = ticks
    t["slide_recip"] = [min(65535, int(65536 // n)) for n in ticks]     # floor: never overshoots
    t["semi_ratio"] = [int(round(2 ** (n / 12.0) * 65536)) for n in range(-24, 25)]
    t["sine"] = [int(round(math.sin(2 * math.pi * i / 1024) * 32767)) for i in range(1024)]
    t["step_len"] = [int(round(FS * 15.0 / max(30, b))) for b in range(256)]
    t["limit_recip"] = [min(32767, int(round(LIMIT_CEIL * 32768.0 / max(1, i << 8)))) for i in range(1024)]
    t["note_period"] = [int(round(FS / (C1 * 2 ** (n / 12.0)) * 256)) for n in range(128)]
    return t


def emit_array(f, ctype, name, vals, per_line=12):
    f.write("const %s %s[%d] = {\n" % (ctype, name, len(vals)))
    for i in range(0, len(vals), per_line):
        f.write("    " + ", ".join(str(int(v)) for v in vals[i:i + per_line]) + ",\n")
    f.write("};\n\n")


def main():
    waves = build_waves()
    t = tables()
    with open(OUT_C, "w") as f:
        f.write("/* AUTOGENERATED by tools/gen_tables.py -- do not edit. */\n")
        f.write('#include "dsp_tables.h"\n\n')
        f.write("const int16_t wt[WT_WAVES][WT_MIPS][WT_LEN + 1] = {\n")
        for w in range(N_WAVE):
            f.write("  {\n")
            for m in range(N_MIP):
                vals = waves[w, m]
                f.write("    {\n")
                for i in range(0, len(vals), 16):
                    f.write("      " + ",".join(str(int(v)) for v in vals[i:i + 16]) + ",\n")
                f.write("    },\n")
            f.write("  },\n")
        f.write("};\n\n")
        emit_array(f, "uint32_t", "inc_top", t["inc_top"], 8)
        emit_array(f, "uint16_t", "cut_g", t["cut_g"])
        emit_array(f, "uint16_t", "res_k", t["res_k"])
        emit_array(f, "uint16_t", "dec_coef", t["dec_coef"])
        emit_array(f, "uint16_t", "vca_coef", t["vca_coef"])
        emit_array(f, "uint16_t", "slide_ticks", t["slide_ticks"])
        emit_array(f, "uint16_t", "slide_recip", t["slide_recip"])
        emit_array(f, "uint32_t", "semi_ratio", t["semi_ratio"], 8)
        emit_array(f, "int16_t", "sine_tab", t["sine"])
        emit_array(f, "uint16_t", "step_len", t["step_len"])
        emit_array(f, "uint16_t", "limit_recip", t["limit_recip"])
        emit_array(f, "uint32_t", "note_period", t["note_period"], 8)
    with open(OUT_H, "w") as f:
        f.write("""/* AUTOGENERATED by tools/gen_tables.py -- do not edit. */
#ifndef ETCH_DS_DSP_TABLES_H
#define ETCH_DS_DSP_TABLES_H
#include <stdint.h>

#define WT_WAVES 4
#define WT_MIPS  8
#define WT_LEN   1024
#define LIMIT_CEIL %d

extern const int16_t  wt[WT_WAVES][WT_MIPS][WT_LEN + 1];
extern const uint32_t inc_top[768];       /* octave from C9, 1/64 semitone */
extern const uint16_t cut_g[512];         /* Q16, quarter semitones from C0 */
extern const uint16_t res_k[128];         /* Q12 */
extern const uint16_t dec_coef[128];      /* Q16 per control tick */
extern const uint16_t vca_coef[128];      /* Q16 per control tick */
extern const uint16_t slide_ticks[128];
extern const uint16_t slide_recip[128];   /* Q16 */
extern const uint32_t semi_ratio[49];     /* Q16, -24..+24 semitones */
extern const int16_t  sine_tab[1024];     /* Q15 */
extern const uint16_t step_len[256];      /* samples per 16th by bpm */
extern const uint16_t limit_recip[1024];  /* Q15 */
extern const uint32_t note_period[128];   /* Q8 samples */

#endif
""" % LIMIT_CEIL)
    print("wrote", OUT_C, OUT_H)
    print("mip harmonics:", [mip_harmonics(m) for m in range(N_MIP)])


if __name__ == "__main__":
    main()
