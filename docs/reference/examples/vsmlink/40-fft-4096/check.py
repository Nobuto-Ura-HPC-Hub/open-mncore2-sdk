#!/usr/bin/env python3
"""check.py <m> <out.bin> <golden.bin>

kernel の出力と golden を、ノルム誤差で比べる。基準は kernel を実行する前に決めたもので、実測では調整しない。

  ‖X_kernel - X_golden‖2 <= C * log2(N) * 2^-53 * ‖X_golden‖2,   C = 16

根拠: 基数 2 の FFT の相対ノルム誤差は、丸めの単位を u = 2^-53 として O(log2(N) * u) である（係数は数〜10 程度。
Higham, Accuracy and Stability of Numerical Algorithms 第 24 章）。丸めは最近接偶数丸め（dev manual 4.3。dvadd は
vfma の第 2 入力を 1 としたもの）で、fp64 の積の下位 16 ビット同士の部分積の省略による誤差は 2^-74 の桁で、u に比べて無視できる。
C = 16 は、この係数に golden（numpy）側の誤差を加えても収まるように、kernel を実行する前に決めた値である。

要素ごとの相対誤差は使わない。単一の正弦波では、ほとんどの周波数の値が理論上 0 になるため。
あわせて、‖X_golden‖2 を単位にした要素ごとの絶対誤差の最大値も表示する（判定は同じ上限）。
"""
import sys
import math
import numpy as np

C = 16.0
m = int(sys.argv[1])
N = 1 << m
y = np.fromfile(sys.argv[2], dtype=np.complex128)
g = np.fromfile(sys.argv[3], dtype=np.complex128)
assert y.size == N and g.size == N, f"点数が違う: kernel {y.size}、golden {g.size}、期待 {N}"

gn = np.linalg.norm(g)
en = np.linalg.norm(y - g)
bound = C * m * 2.0 ** -53
rel = en / gn if gn > 0 else en
maxabs = np.max(np.abs(y - g)) / gn if gn > 0 else np.max(np.abs(y - g))
ok = bool(np.isfinite(rel)) and rel <= bound and maxabs <= bound
print(f"fft N={N}: ノルム相対誤差 {rel:.3e}、要素ごとの絶対誤差の最大（‖X‖2 比）{maxabs:.3e}、上限 {bound:.3e} -> {'PASS' if ok else 'FAIL'}")
sys.exit(0 if ok else 1)
