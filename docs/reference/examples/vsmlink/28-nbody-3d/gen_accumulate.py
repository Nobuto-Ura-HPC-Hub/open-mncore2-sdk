#!/usr/bin/env python3
"""gen_accumulate.py — 3D 重力の accumulate カーネルを分割 1 個ぶん生成する。

MN-Core 2 はループが無いので、他粒子ぶんの力の評価を書き並べた vsm にする。ただし全 N を
1 カーネルに unroll すると device の 1 カーネル命令数上限を超えて load できない。
そこで粒子を count 個ずつの分割に切り、host が分割を順に exec する。この生成器は 1 分割
(グローバル粒子 start..start+count-1) を出す。

各ローカルブロック j は粒子 (start+j) を PDM の粒子ごと並び (BASE + (start+j)*4) から
@broadcast size 4 で受け、softening 付き重力の加速度を LM の accumulator に累積する。

rsqrt は 5 ビット近似なので、drsqrt の後にニュートン補正を N 反復入れて精度を上げる。
  y <- y * (1.5 - 0.5 * r2 * y^2)   (N 回)

first=1 の分割だけ accumulator を 0 にリセットし、先頭ブロックで送信を wait する
(host は全 N 粒子を 1 回だけ PDM に置く)。first=0 の分割は accumulator に加算するだけ。

Usage: gen_accumulate.py <start> <count> <newton> <first> <out_vsm> <out_param>
"""
import sys

# 粒子ごと並び [x,y,z,m] x N の PDM 先頭番地 (distribute 0..16383, 定数 16384 と別)
BASE = 20480

HEADER = """\
# ABI PFN3_SBY64
#
# accumulate 分割 (start={start} count={count}, newton={N}, first={first}): 他粒子 {count} 個
# (グローバル {start}..) を broadcast size 4 で順に受け、3D softening 付き重力で加速度
# (ax,ay,az) を LM に累積する。first=1 の分割だけ accumulator をリセットし送信を wait する。
# ** 自動生成: gen_accumulate.py。手で編集しない **
# LM0 常駐: xi=$lm0 yi=$lm2 zi=$lm4 mi=$lm6 ax=$lm8 ay=$lm10 az=$lm12
#          eps2=$lm14 c_half=$lm16 c_onehalf=$lm18
# バンク: LM0=$lm LM1=$ln GRF0=$lr GRF1=$ls
"""

# 前置き: accumulator ax,ay,az を 0 にする。複数ステップでは毎ステップ呼ぶので、
# ここでリセットしないと力が積み上がって発散する (init の 0 初期化は初回だけ)。
PREAMBLE = """\
	# accumulator を 0 にリセット
	imm i"0" $r0
	nop/4
	lpassa $lr0 $lm8
	nop/24
	lpassa $lr0 $lm10
	nop/24
	lpassa $lr0 $lm12
	nop/24
"""

# 1 ブロックの前段: broadcast から r2、drsqrt まで。y(inv_r)=$lr24、r2=$ls0 が残る。
PRE = """\
	# --- ブロック {k}: 粒子 {k} からの力 ---
	@broadcast _pj from_param {slot} size 4 $lr16
	dvadd $lr16 -$lm0 $ln0
	nop/2
	dvadd $lr18 -$lm2 $ln2
	nop/2
	dvadd $lr20 -$lm4 $ln4
	nop/2
	dvmulu $ln0 $ln0 $nowrite
	dvfmad $ln0 $ln0 $mauf $ls0
	nop/2
	dvmulu $ln2 $ln2 $nowrite
	dvfmad $ln2 $ln2 $mauf $lr24
	nop/2
	dvadd $ls0 $lr24 $ln6
	nop/2
	dvmulu $ln4 $ln4 $nowrite
	dvfmad $ln4 $ln4 $mauf $ls0
	nop/2
	dvadd $ln6 $ls0 $lr24
	nop/2
	dvadd $lr24 $lm14 $ls0
	nop/2
	drsqrt $ls0 $lr24
	nop/4
"""

# ニュートン補正 1 反復: y <- y*(1.5 - 0.5*r2*y^2)。y=$lr24、r2=$ls0 は不変で残す。
NEWTON = """\
	# ニュートン補正: y = y*(1.5 - 0.5*r2*y^2)
	dvmulu $lr24 $lr24 $nowrite
	dvfmad $lr24 $lr24 $mauf $ln6
	nop/2
	dvmulu $ls0 $ln6 $nowrite
	dvfmad $ls0 $ln6 $mauf $lr26
	nop/2
	dvmulu $lm16 $lr26 $nowrite
	dvfmad $lm16 $lr26 $mauf $ln6
	nop/2
	dvadd $lm18 -$ln6 $ls2
	nop/2
	dvmulu $lr24 $ls2 $nowrite
	dvfmad $lr24 $ls2 $mauf $ln6
	nop/2
	lpassa $ln6 $lr24
	nop/2
"""

# 後段: inv_r3 を作り、力を ax,ay,az に累積。y=$lr24 を読む (補正後)。
POST = """\
	dvmulu $lr24 $lr24 $nowrite
	dvfmad $lr24 $lr24 $mauf $ls0
	nop/2
	dvmulu $ls0 $lr24 $nowrite
	dvfmad $ls0 $lr24 $mauf $ln6
	nop/2
	dvmulu $lr22 $ln6 $nowrite
	dvfmad $lr22 $ln6 $mauf $ls0
	nop/2
	dvmulu $ls0 $ln0 $nowrite
	dvfmad $ls0 $ln0 $mauf $lr24
	nop/2
	dvadd $lm8 $lr24 $ln8
	nop/2
	lpassa $ln8 $lm8
	nop/24
	dvmulu $ls0 $ln2 $nowrite
	dvfmad $ls0 $ln2 $mauf $lr24
	nop/2
	dvadd $lm10 $lr24 $ln10
	nop/2
	lpassa $ln10 $lm10
	nop/24
	dvmulu $ls0 $ln4 $nowrite
	dvfmad $ls0 $ln4 $mauf $lr24
	nop/2
	dvadd $lm12 $lr24 $ln12
	nop/2
	lpassa $ln12 $lm12
	nop/24
"""


def main():
    if len(sys.argv) != 7:
        sys.stderr.write(
            "usage: gen_accumulate.py <start> <count> <newton> <first> <out_vsm> <out_param>\n")
        return 1
    start = int(sys.argv[1])
    count = int(sys.argv[2])
    newton = int(sys.argv[3])
    first = int(sys.argv[4])
    out_vsm = sys.argv[5]
    out_param = sys.argv[6]

    vsm = [HEADER.format(start=start, count=count, N=newton, first=first)]
    if first:
        vsm.append(PREAMBLE)
    for j in range(count):
        vsm.append(PRE.format(k=start + j, slot=8 + j * 8))
        for _ in range(newton):
            vsm.append(NEWTON)
        vsm.append(POST)
    with open(out_vsm, "w") as f:
        f.write("".join(vsm))

    param = ["((:version :0.1)", " (:reserved_wait_tag #x3f)"]
    for j in range(count):
        slot = 8 + j * 8
        addr = BASE + (start + j) * 4
        tag = " :send_wait_tag #x10" if (first and j == 0) else ""
        param.append(" (:slot {} :place :pdm0 :addr {}{})".format(slot, addr, tag))
    with open(out_param, "w") as f:
        f.write("\n".join(param) + ")\n")
    return 0


if __name__ == "__main__":
    sys.exit(main())
