#!/usr/bin/env python3
"""gen_golden.py <m> <input.bin> <golden.bin>

input.bin（gen_input.py の出力）の FFT を numpy.fft.fft で計算し、kernel の出力の順（PE の番号の順）に並べて書く。
kernel は周波数間引き型なので、PE p は X[bitrev(p)] を持つ。出力は N 点の complex128（リトルエンディアン）。
"""
import sys
import numpy as np

m = int(sys.argv[1])
N = 1 << m
x = np.fromfile(sys.argv[2], dtype=np.complex128)
assert x.size == N, f"入力は {N} 点のはずが {x.size} 点"
X = np.fft.fft(x)

rev = np.zeros(N, dtype=np.int64)
for p in range(N):
    r = 0
    for b in range(m):
        if (p >> b) & 1:
            r |= 1 << (m - 1 - b)
    rev[p] = r
X[rev].astype(np.complex128).tofile(sys.argv[3])
