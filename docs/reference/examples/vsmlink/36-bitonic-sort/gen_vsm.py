#!/usr/bin/env python3
"""gen_vsm.py <steps> — 36-bitonic-sort の _vsm を標準出力に書く。

4096 個の f64 の値のバイトニックソート（PE の番号の昇順）の、先頭から <steps> ステップ（全体は 78 ステップ）を並べる。
段 s（1〜12）、ステップ j（s-1 から 0）で、PE i は、相手 i XOR 2^j の値を @get_hypercube_neighbor で受け取り、
(i の bit j) == (i の bit s) なら小さい方、そうでなければ大きい方を新しい値にする。
"""
import sys

steps = int(sys.argv[1])

seq = [(s, j) for s in range(1, 13) for j in range(s - 1, -1, -1)]
assert 0 < steps <= len(seq)

print("# ABI PFN3_SBY64")
print("#")
print(f"# バイトニックソート（4096 個）の先頭 {steps} ステップ（全体は {len(seq)} ステップ）")
print("#")
print("\t@identify 0 $lr8 4096                    # PE の番号 j")
print("\t@distribute _src from_param 16 size 1 $lm0")
# 定数 0 を $ln10 に置く（$ln0 は @get_hypercube_neighbor が使うので、定数は置かない）
print("\timm i\"0\" $r96")
print("\timm i\"0\" $r97")
print("\tnop/2")
print("\tlpassa $lr96 $ln10")
for t, (s, j) in enumerate(seq[:steps], 1):
    m = (1 << j) + (1 << s)
    print(f"# ステップ {t}: s={s}, j={j}（相手は PE の番号 XOR {1 << j}、M={m}）")
    print("\t@alloc hc :omr 9 :grf0 200 202 204 206 208 :n 0")
    print(f"\t@get_hypercube_neighbor $ls16 {j} $lm0 $lr8 nbuf0 hc")
    print("\t@free hc")
    print("\tdmin $lm0 $ls16 $lr24")
    print("\tdmax $lm0 $ls16 $lr32")
    print("\timm i\"0\" $r64")
    print(f"\timm i\"{m}\" $r65")
    print("\tnop/2")
    print("\tlpassa $lr64 $ln8")
    print("\tnop/2")
    print("\tiand $ln8 $lr8 $lr72")
    print("\tnop/2")
    print("\tlxor $lr72 $ln10 $omr4")
    print("\tlxor $lr72 $ln8 $omr5")
    print("\tipassa $lr32 $lr40")
    print("\t  maskr 4")
    print("\tipassa $lr24 $lr40")
    print("\t  maskr 5")
    print("\tipassa $lr24 $lr40")
    print("\t  mask 0")
    print("	nop/2")
    print("	lpassa $lr40 $lm0")
print("\t@collect _out to_param 24 size 1 $lm0")
