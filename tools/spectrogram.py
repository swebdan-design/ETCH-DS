#!/usr/bin/env python3
"""Spectrogram PNG of a WAV (mono mix): tools/spectrogram.py in.wav out.png [start_s] [dur_s] [max_hz]"""
import sys, wave
import numpy as np
from PIL import Image
w = wave.open(sys.argv[1]); fs = w.getframerate()
x = np.frombuffer(w.readframes(w.getnframes()), dtype=np.int16).reshape(-1, w.getnchannels()).mean(axis=1)
s0 = float(sys.argv[3]) if len(sys.argv) > 3 else 0; d = float(sys.argv[4]) if len(sys.argv) > 4 else 4
x = x[int(s0 * fs):int((s0 + d) * fs)]
fmax = float(sys.argv[5]) if len(sys.argv) > 5 else 12800
n, hop = 2048, 256
cols = []
for i in range(0, len(x) - n, hop):
    s = np.abs(np.fft.rfft(x[i:i + n] * np.hanning(n)))[:int(fmax * n / fs)]
    cols.append(20 * np.log10(s + 1e-3))
S = np.array(cols).T[::-1]
S = np.clip((S - S.max() + 90) / 90, 0, 1)
img = (np.stack([S ** 0.7, S ** 1.5, S ** 3], -1) * 255).astype(np.uint8)
Image.fromarray(img).resize((min(1600, img.shape[1]), 400)).save(sys.argv[2])
