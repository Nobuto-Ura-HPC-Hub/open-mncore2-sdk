#!/usr/bin/env python3
"""gen_vsm.py <steps> | --verify — 37-bitonic-sort-8192 の _vsm を標準出力に書く。

8192 個の f64 の値のバイトニックソート（値の番号 i の昇順）の、先頭から <steps> ステップ（全体は 91 ステップ）を並べる。
各 PE j は 2 つの値を持つ。A は i = j、B は i = j + 4096。
段 s（1〜13）、ステップ j（s-1 から 0）で、値 i は、相手 i XOR 2^j の値と比べ、
(i の bit j) == (i の bit s) なら小さい方、そうでなければ大きい方を新しい値にする。
j が 0〜11 のときの相手は @get_hypercube_neighbor で受け取る。j が 12 のときの相手は、同じ PE のもう一方の値である。
--verify は、s と j の代表的な組だけを並べる（vsmlink の出力の golden 用。ソートとしては完結しない）。
"""
import sys

SEQ = [(s, j) for s in range(1, 14) for j in range(s - 1, -1, -1)]
if sys.argv[1] == "--verify":
    steps_list = [(2, 1), (12, 3), (13, 12), (13, 0)]
    title = "代表的なステップ " + " ".join(f"(s={s},j={j})" for s, j in steps_list)
else:
    n = int(sys.argv[1])
    assert 0 < n <= len(SEQ)
    steps_list = SEQ[:n]
    title = f"先頭 {n} ステップ（全体は {len(SEQ)} ステップ）"

out = []
def p(line):
    out.append(line)

def targets(s, r):
    """take_min になる t の値の種類。zero は t == 0、mask は t == Mmask。"""
    if s <= 11:
        return ["zero", "mask"]
    if s == 12:
        return ["zero"] if r == 0 else ["mask"]
    return ["zero"]

def mmask(s, j):
    return (1 << j) + (1 << s) if s <= 11 else (1 << j)

def update(reg, neighbor, mn, mx, new, s, r):
    """値 reg を、受け取った相手の値 neighbor との比較で更新する。"""
    p(f"\tdmin {reg} {neighbor} {mn}")
    p(f"\tdmax {reg} {neighbor} {mx}")
    flags = ["$omr4", "$omr5"]
    for k, tg in enumerate(targets(s, r)):
        const = "$ln10" if tg == "zero" else "$ln8"
        p(f"\tlxor $lr72 {const} {flags[k]}")
    if len(targets(s, r)) == 1:
        p("\tnop")
    p(f"\tipassa {mx} {new}")
    for k, _ in enumerate(targets(s, r)):
        p(f"\t  maskr {4 + k}")
        p(f"\tipassa {mn} {new}")
    p("\t  mask 0")

p("# ABI PFN3_SBY64")
p("#")
p(f"# バイトニックソート（8192 個、各 PE が 2 つの値を持つ）の{title}")
p("#")
p("\t@identify 0 $lr8 4096                    # PE の番号 j")
p("\t@distribute _src from_param 16 size 2 $lm0   # A = $lm0（i = j）、B = $lm2（i = j + 4096）")
p("\timm i\"0\" $r96")
p("\timm i\"0\" $r97")
p("\tnop/2")
p("\tlpassa $lr96 $ln10                       # 定数 0")
for (s, j) in steps_list:
    p(f"# s={s}, j={j}")
    if j == 12:
        # 相手は同じ PE のもう一方の値。A が小さい方、B が大きい方（s = 13 は昇順）
        p("\tnop/2")
        p("\tlpassa $lm2 $ls18")
        p("\tnop/2")
        p("\tdmin $lm0 $ls18 $lr24")
        p("\tdmax $lm0 $ls18 $lr32")
        p("\tnop/2")
        p("\tlpassa $lr24 $lm0")
        p("\tlpassa $lr32 $lm2")
        continue
    for reg, neighbor in (("$lm0", "$ls16"), ("$lm2", "$ls18")):
        p("\t@alloc hc :omr 9 :grf0 200 202 204 206 208 :n 0")
        p(f"\t@get_hypercube_neighbor {neighbor} {j} {reg} $lr8 nbuf0 hc")
        p("\t@free hc")
    p("\timm i\"0\" $r64")
    p(f"\timm i\"{mmask(s, j)}\" $r65")
    p("\tnop/2")
    p("\tlpassa $lr64 $ln8")
    p("\tnop/2")
    p("\tiand $ln8 $lr8 $lr72")
    p("\tnop/2")
    update("$lm0", "$ls16", "$lr24", "$lr32", "$lr40", s, 0)
    update("$lm2", "$ls18", "$lr48", "$lr56", "$lr80", s, 1)
    p("\tnop/2")
    p("\tlpassa $lr40 $lm0")
    p("\tlpassa $lr80 $lm2")
p("\t@collect _out to_param 24 size 2 $lm0")
print("\n".join(out))
