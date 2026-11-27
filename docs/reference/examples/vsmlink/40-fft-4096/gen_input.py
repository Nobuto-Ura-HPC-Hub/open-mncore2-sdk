#!/usr/bin/env python3
"""gen_input.py <m> <input.bin> <tw.bin> [--kind random|sine|file] [--k0 K] [--file F]

N = 2^m 点の入力 <input.bin> と、w の表 <tw.bin> を書く。どちらも、リトルエンディアンの f64。

  input.bin: N 点の複素数。(re0, im0, re1, im1, ...)  numpy の complex128 の tofile と同じ並び
  tw.bin   : j = 1..m-1 の順に、wr の 4096 個、wi の 4096 個。PE i（i < N）の値は exp(-2πi * (i mod 2^j) / 2^(j+1))。
             i >= N の PE は 0

--kind random : 実部・虚部が [-1, 1) の疑似乱数（種は固定）
--kind sine   : exp(2πi * k0 * n / N)（既定の k0 = 5）
--kind file   : --file の波形。テキストで、1 列なら実部、2 列なら（実部 虚部）。先頭から N 行を使い、足りなければ 0 を補う。
                拡張子が .npy なら numpy の配列（実数または複素数）
"""
import argparse
import numpy as np

ap = argparse.ArgumentParser()
ap.add_argument("m", type=int)
ap.add_argument("input_bin")
ap.add_argument("tw_bin")
ap.add_argument("--kind", choices=["random", "sine", "file"], default="random")
ap.add_argument("--k0", type=int, default=5)
ap.add_argument("--file")
a = ap.parse_args()

N = 1 << a.m
n = np.arange(N)
if a.kind == "random":
    rng = np.random.default_rng(20261118)
    x = rng.uniform(-1.0, 1.0, N) + 1j * rng.uniform(-1.0, 1.0, N)
elif a.kind == "sine":
    x = np.exp(2j * np.pi * a.k0 * n / N)
else:
    if a.file is None:
        ap.error("--kind file には --file が要る")
    w = np.load(a.file) if a.file.endswith(".npy") else np.loadtxt(a.file, ndmin=2)
    w = np.asarray(w)
    if np.iscomplexobj(w):
        z = w.reshape(-1)
    elif w.ndim == 1 or w.shape[1] == 1:
        z = w.reshape(-1).astype(np.float64) + 0j
    else:
        z = w[:, 0] + 1j * w[:, 1]
    x = np.zeros(N, dtype=np.complex128)
    k = min(N, len(z))
    x[:k] = z[:k]
x.astype(np.complex128).tofile(a.input_bin)

tw = np.zeros((a.m - 1, 2, 4096), dtype=np.float64)
for j in range(1, a.m):
    i = np.arange(N)
    ang = -2.0 * np.pi * (i % (1 << j)) / (1 << (j + 1))
    tw[j - 1, 0, :N] = np.cos(ang)
    tw[j - 1, 1, :N] = np.sin(ang)
tw.tofile(a.tw_bin)
