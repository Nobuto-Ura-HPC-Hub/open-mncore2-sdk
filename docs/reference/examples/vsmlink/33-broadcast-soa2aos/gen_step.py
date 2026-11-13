#!/usr/bin/env python3
"""gen_step.py — 1 タイムステップぶんの kernel と .param を生成する。

骨格は broadcast -> 計算 -> (レイアウト変換) -> collect。計算部分は別ファイル (rotate._vsm /
logistic._vsm) を挟み込むので、差し替えられる。

broadcast は全粒子ぶん (n 個) 並べ、すべて同じ slot を使う。同じ slot の 2 回目以降は、
PDM の粒子ごと並びの続き (次の粒子) を読む。1 回の転送 (mvb) で 64 u64 = 16 粒子ぶんが
L1BM に移動し、それを使い切るまで転送は出ない。したがって n 粒子を読むのに必要な転送は
n/16 回になる。これが成り立っているかは生成物の mvb/n64 を数えれば分かる
(build.ninja の measure ターゲット)。

**分割**: 全粒子を 1 カーネルに入れると device の連続 DMA 確保に失敗して load できない
(examples/28-nbody-3d の実測)。chunk 粒子ずつの分割カーネルにする。**chunk は 16 の倍数に
すること。** kernel の実行ごとに slot の :addr (その分割の先頭粒子) から読み始めるので、
16 の倍数でないと、分割の境目にある 64 u64 を前後の分割で 2 回移動することになる。
分割しても転送の合計回数は変わらない (n/16 回)。

レイアウト変換と collect は最後の分割にだけ入れる。途中の分割は計算して LM を更新するだけで、
書き戻さない (LM の値は exec をまたいで残る)。

ダブルバッファ: broadcast の読み元と collect の書き先を入れ替えた 2 種類を出す。
  a: BUF0 を読み BUF1 へ書く
  b: BUF1 を読み BUF0 へ書く

Usage: gen_step.py <n> <chunk> <start> <last:0|1> <compute._vsm> <transpose:0|1>
                  <out_vsm_a> <out_param_a> <out_vsm_b> <out_param_b>
"""
import os
import sys

# PDM のメモリマップ (u64 単位)。README のメモリマップ表と一致させること。
ADDR_INIT = 0        # 初期値 (成分ごと)。distribute が読む
ADDR_BUF0 = 20480    # 粒子ごと。broadcast が読み、collect が書く
ADDR_BUF1 = 40960    # 同上 (もう一方)

SLOT_INIT = 8        # distribute 用
SLOT_COLLECT = 16    # collect 用
SLOT_BCAST = 24      # broadcast 用。全粒子で同じ slot を使い、続きを読む

HEADER = """\
# ABI PFN3_SBY64
#
# 1 タイムステップ: broadcast で全粒子を読み、計算し、{transpose}collect で書き戻す。
# ** 自動生成: gen_step.py。手で編集しない **
#
# LM0 レイアウト (init._vsm が決めたもの):
#   $lm0=x $lm2=y $lm4=z $lm6=m(PE ID)   計算対象
#   $lm8,10,12,14                        レイアウト変換の出力先
#
"""

# 他粒子 1 個ぶんの broadcast。同じ slot を使い続けるので、L1BM に移動した 64 u64 を
# 使い切るまで転送は出ない。
BCAST_AND_COMPUTE = """\
	# --- 粒子 {j} ---
	@broadcast _pj from_param {slot} size 4 $lr16
{compute}"""

# 転置 (dmwrite/dmread)。src/dst 両方に v (サイクルインクリメント) が要る。
# 無いと全サイクル同番地に上書きで壊れる (dev manual 3.6.1.6)。
TRANSPOSE = """\
	# --- MAB 内 4x4 転置 (SoA -> AoS)。粒子順は順列になる ---
	dmwrite $lm0v $lx0
	nop/24
	dmread  $lx0 $lm8v
	nop/24
"""


def gen_vsm(start, count, last, compute_body, transpose):
    """1 分割ぶんの _vsm を組み立てる。粒子 [start, start+count) を読む。
    レイアウト変換と collect は最後の分割 (last) にだけ入れる。"""
    out = [HEADER.format(transpose="レイアウト変換し、" if (transpose and last) else "")]
    for j in range(start, start + count):
        out.append(BCAST_AND_COMPUTE.format(
            j=j, slot=SLOT_BCAST, compute=compute_body))
    if not last:
        return "".join(out)     # 途中の分割は LM を更新するだけ (値は exec をまたいで残る)
    if transpose:
        out.append(TRANSPOSE)
        src = "$lm8"      # レイアウト変換の出力を書き戻す
    else:
        src = "$lm0"      # レイアウト変換しないなら計算結果をそのまま
    out.append("	@collect _out to_param {} size 4 {}\n".format(SLOT_COLLECT, src))
    return "".join(out)


def gen_param(start, count, last, read_addr, write_addr):
    """broadcast は全粒子で 1 つの slot。:addr はこの分割の先頭粒子にする。"""
    lines = ["((:version :0.1)", " (:reserved_wait_tag #x3f)"]
    lines.append(" (:slot {} :place :pdm0 :addr {} :send_wait_tag #x10)".format(SLOT_INIT, ADDR_INIT))
    # **send_wait_tag は付けない。** ホストが送るのは 1 タイムステップ目だけで、
    # 2 タイムステップ目以降は device の @collect がこの領域を書くため、kernel 内で
    # 送信完了を待つ形にすると 2 回目以降が成立しない (待ちが解けない)。
    # 1 回目の送信完了はホストが mnc2_wait_idma_idle で待ってから exec する。
    lines.append(" (:slot {} :place :pdm0 :addr {})".format(SLOT_BCAST, read_addr + start * 4))
    if last:
        lines.append(" (:slot {} :place :pdm0 :addr {} :recv_wait_tag #x1e)".format(SLOT_COLLECT, write_addr))
    return "\n".join(lines) + ")\n"


def main():
    if len(sys.argv) != 11:
        sys.stderr.write(
            "usage: gen_step.py <n> <chunk> <start> <last:0|1> <compute._vsm> <transpose:0|1> "
            "<out_vsm_a> <out_param_a> <out_vsm_b> <out_param_b>\n")
        return 1
    n = int(sys.argv[1])
    chunk = int(sys.argv[2])
    start = int(sys.argv[3])
    last = sys.argv[4] == "1"
    compute_path = sys.argv[5]
    transpose = sys.argv[6] == "1"
    out_vsm_a, out_param_a, out_vsm_b, out_param_b = sys.argv[7:11]

    if chunk % 16 != 0:
        sys.stderr.write("chunk は 16 の倍数にすること (64 u64 = 16 粒子)\n")
        return 1
    count = min(chunk, n - start)

    here = os.path.dirname(os.path.abspath(__file__))
    with open(os.path.join(here, compute_path)) as f:
        # 先頭のコメント行 (# で始まる) は骨格側で説明済みなので落とし、本体だけ挟む
        compute_body = "".join(l for l in f if not l.startswith("#"))

    vsm = gen_vsm(start, count, last, compute_body, transpose)
    with open(out_vsm_a, "w") as f:
        f.write(vsm)
    with open(out_vsm_b, "w") as f:
        f.write(vsm)   # _vsm は同一。読み書きの向きは .param で決まる
    with open(out_param_a, "w") as f:
        f.write(gen_param(start, count, last, ADDR_BUF0, ADDR_BUF1))
    with open(out_param_b, "w") as f:
        f.write(gen_param(start, count, last, ADDR_BUF1, ADDR_BUF0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
