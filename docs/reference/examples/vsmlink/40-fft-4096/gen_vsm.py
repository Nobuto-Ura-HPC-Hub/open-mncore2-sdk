#!/usr/bin/env python3
"""gen_vsm.py <m> <out._vsm> <out.param> — 40-fft-4096 の _vsm と .param を書く。

N = 2^m 点（m は 2〜12）の複素 f64 の FFT（周波数間引き型、基数 2）を、PE の番号 0..N-1 に 1 点ずつ置いて計算する。
入力は自然順、出力はビット反転順（PE p が X[bitrev(p)] を持つ）。

段 j（j = m-1 から 0）で、PE i は相手 i XOR 2^j の値 y を @get_hypercube_neighbor で受け取る（実部と虚部を 1 回ずつ）。
  i の bit j が 0 の PE: x + y
  i の bit j が 1 の PE: (y - x) * w、w = exp(-2πi * (i mod 2^j) / 2^(j+1))
w の表（j = 1..m-1。j = 0 は w = 1）は、gen_input.py が作り、ホストが @distribute で配る。
"""
import sys

m = int(sys.argv[1])
out_vsm, out_param = sys.argv[2], sys.argv[3]
assert 2 <= m <= 12

# PDM の配置（u64 単位）。すべて 64 の倍数
ADDR_IDS = 0
ADDR_SRC = 4096                         # 実部の 4096 個、虚部の 4096 個（@distribute size 2）
ADDR_TW0 = ADDR_SRC + 2 * 4096          # j = 1 の (wr, wi)。j ごとに 8192 u64
ADDR_OUT = ADDR_TW0 + (m - 1) * 2 * 4096
ADDR_BUF = ADDR_OUT + 2 * 4096


def tw_addr(j):
    return ADDR_TW0 + (j - 1) * 2 * 4096


def tw_slot(j):
    return 40 + j


def tw_tag(j):
    return 0x11 + j                     # 0x12..0x1c


def lm_wr(j):
    return 4 * j                        # $lm(4j) = wr、$lm(4j+2) = wi


v = []
v.append("# ABI PFN3_SBY64")
v.append("#")
v.append(f"# FFT（{1 << m} 点、複素 f64、周波数間引き型、基数 2、出力はビット反転順）")
v.append("#")
v.append("\t@identify 0 $lr8 4096                    # PE の番号 j")
v.append("\t@distribute _src from_param 16 size 2 $lm0   # $lm0 = 実部、$lm2 = 虚部")
for j in range(1, m):
    v.append(f"\t@distribute _tw{j} from_param {tw_slot(j)} size 2 $lm{lm_wr(j)}")
# 定数 0 を $ln10 に置く（$ln0 は @get_hypercube_neighbor が使うので、定数は置かない）
v.append("\timm i\"0\" $r96")
v.append("\timm i\"0\" $r97")
v.append("\tnop/2")
v.append("\tlpassa $lr96 $ln10")
for j in range(m - 1, -1, -1):
    M = 1 << j
    v.append(f"# 段 j={j}（相手は PE の番号 XOR {M}）")
    v.append("\t@alloc hc :omr 9 :grf0 200 202 204 206 208 :n 0")
    v.append(f"\t@get_hypercube_neighbor $ls16 {j} $lm0 $lr8 nbuf0 hc")
    v.append(f"\t@get_hypercube_neighbor $ls18 {j} $lm2 $lr8 nbuf0 hc")
    v.append("\t@free hc")
    # 和（下側の PE が取る）と差 y - x（上側の PE が w を掛けて取る）
    v.append("\tdvadd $lm0 $ls16 $lr24")
    v.append("\tdvadd $lm2 $ls18 $lr26")
    v.append("\tdvadd -$lm0 $ls16 $lr28")
    v.append("\tdvadd -$lm2 $ls18 $lr30")
    if j == 0:
        up_r, up_i = "$lr28", "$lr30"
    else:
        wr, wi = f"$lm{lm_wr(j)}", f"$lm{lm_wr(j) + 2}"
        # t1 = di * wi、pr = dr * wr - t1（di は直前の dvadd の結果なので、assemble3 の指摘で nop を 2 ステップ置く）
        v.append("\tnop/2")
        v.append(f"\tdvmulu $lr30 {wi} $nowrite")
        v.append(f"\tdvfmad $lr30 {wi} $mauf $ls20")
        v.append("\tnop/2")
        v.append(f"\tdvfmau $lr28 {wr} -$ls20 $nowrite")
        v.append(f"\tdvfmad $lr28 {wr} $mauf $lr32")
        # t2 = di * wr、pi = dr * wi + t2
        v.append(f"\tdvmulu $lr30 {wr} $nowrite")
        v.append(f"\tdvfmad $lr30 {wr} $mauf $ls22")
        v.append("\tnop/2")
        v.append(f"\tdvfmau $lr28 {wi} $ls22 $nowrite")
        v.append(f"\tdvfmad $lr28 {wi} $mauf $lr34")
        up_r, up_i = "$lr32", "$lr34"
    v.append("\timm i\"0\" $r64")
    v.append(f"\timm i\"{M}\" $r65")
    v.append("\tnop/2")
    v.append("\tlpassa $lr64 $ln8")
    v.append("\tnop/2")
    v.append("\tiand $ln8 $lr8 $lr72")
    v.append("\tnop/2")
    # フラグは「出力がすべて 0」のとき 1。omr4 = 下側（bit j が 0）、omr5 = 上側
    v.append("\tlxor $lr72 $ln10 $omr4")
    v.append("\tlxor $lr72 $ln8 $omr5")
    v.append("\t  maskr 4")
    v.append("\tipassa $lr24 $lr40")
    v.append("\tipassa $lr26 $lr42")
    v.append("\t  maskr 5")
    v.append(f"\tipassa {up_r} $lr40")
    v.append(f"\tipassa {up_i} $lr42")
    v.append("\t  mask 0")
    v.append("\tnop")
    v.append("\tlpassa $lr40 $lm0")
    v.append("\tlpassa $lr42 $lm2")
v.append("\t@collect _out to_param 24 size 2 $lm0")

with open(out_vsm, "w") as f:
    f.write("\n".join(v) + "\n")

p = []
p.append("((:version :0.1)")
p.append(" (:reserved_wait_tag #x3f)")
p.append(f" (:slot 8  :place :pdm0 :addr {ADDR_IDS}     :send_wait_tag #x10)   ; PE の番号 [0..4095]")
p.append(f" (:slot 16 :place :pdm0 :addr {ADDR_SRC}  :send_wait_tag #x11)   ; 入力（実部 4096 個、虚部 4096 個）")
for j in range(1, m):
    p.append(f" (:slot {tw_slot(j)} :place :pdm0 :addr {tw_addr(j)} :send_wait_tag #x{tw_tag(j):x})   ; w の表 j={j}（wr、wi）")
p.append(f" (:slot 24 :place :pdm0 :addr {ADDR_OUT} :recv_wait_tag #x1e)   ; 出力（実部 4096 個、虚部 4096 個）")
p.append(f" (:identify 0 :place :pdm0 :addr {ADDR_IDS} :send_wait_tag #x10)")
p.append(f" (:buffer nbuf0 :place :pdm0 :addr {ADDR_BUF} :buf_size 4160))")
with open(out_param, "w") as f:
    f.write("\n".join(p) + "\n")
