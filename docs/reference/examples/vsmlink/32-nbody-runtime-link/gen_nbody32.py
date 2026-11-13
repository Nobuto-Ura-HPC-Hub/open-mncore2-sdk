#!/usr/bin/env python3
"""gen_nbody32.py — 手書き kernel と生成 accumulate_PH kernel を 1 本の `_vsm` へ束ね、
対応する `.param` を出力する。

31 (gen_nbody31.py) との違いは、accumulate の他粒子ぶんの `@broadcast` が読む
スロット番号を、生成時に確定させず `PH` という目印のまま残すこと。accumulate の CHUNK 個の
`@broadcast` はすべて同じスロット（`from_param PH`）を使い、そのスロットの続きを読む。
この目印は `from_param` の引数として整数ではないため、directive の文法に通らない
（`vsmlink --split-only` はこの行を解釈しないので分割はできるが、通常の `vsmlink`
呼び出しはこの行の展開に失敗する）。host が chunk ごとに `from_param PH` を、その chunk の
スロット番号へ書き換えてから `vsmlink` を呼ぶ（詳細は README.md）。

31 は accumulate が CHUNK 粒子ぶんの固定 PDM 領域を読み、host が毎回そこへデータを
送り直す。32 は全 N 粒子を 1 回だけ PDM へ送り（28 と同じ）、host は「どの chunk の
スロット番号を使うか」だけを選ぶ。`.param` には chunk ごとに 1 つのスロットがあり、その
`:addr` は chunk の先頭粒子である。したがって `.param` のスロット数は N / CHUNK に依存する。N が CHUNK の倍数でない場合、末尾を質量 0 の粒子で
CHUNK の倍数まで埋める（host 側もこの埋めた個数ぶん PDM へ送る。31 と同じ「質量 0 なら
力への寄与が 0」という考え方を、毎回の送信ではなく最初の 1 回だけに適用する）。

Usage: gen_nbody32.py <n> <chunk> <newton> <out_vsm> <out_param>
"""
import os
import sys

# 他粒子ぶんの broadcast 元 PDM 番地 (粒子ごと並び [x,y,z,m] x N の先頭)。
# 28-nbody-3d / 31-nbody-host-mediated-loop と同じ番地。
BASE = 20480
PJ_SLOT0 = 40

HEADER = """\
# ABI PFN3_SBY64
#
# nbody32: host が実行時に vsmlink を呼び直す 3D N 体 vsm。init / reset_acc /
# accumulate_PH / kick / drift / collect_pos の 6 kernel を @kernel_begin/@kernel_end
# で束ねる。accumulate_PH は @broadcast のスロット番号がまだ確定していない
# (from_param の引数が目印 PH のまま)。host が chunk ごとにこの目印を
# その chunk のスロット番号に書き換えてから vsmlink を呼ぶ (README.md 参照)。
# ** 自動生成: gen_nbody32.py。手で編集しない **
#
"""

# 1 ブロックの前段: broadcast から r2、drsqrt まで。y(inv_r)=$lr24、r2=$ls0 が残る。
# 31 の PRE と同一だが、slot の位置に整数ではなく PH という目印を残す (全ブロックで同じ)。
PRE = """\
	# --- ブロック {k}: 粒子 (host が実行時に選ぶ) からの力 ---
	@broadcast _pj from_param PH size 4 $lr16
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
# (31 と同一)
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

# 後段: inv_r3 を作り、力を ax,ay,az に累積。y=$lr24 を読む (補正後)。(31 と同一)
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


def read_body(name):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), name)
    with open(path) as f:
        return f.read()


def gen_accumulate_ph_body(chunk, newton):
    lines = [
        "# accumulate_PH: 他粒子 {0} 個ぶんの @broadcast スロット番号がまだ確定していない。\n"
        "# host が chunk ごとに from_param の引数の目印 PH をその chunk のスロット番号へ書き換えてから\n"
        "# vsmlink を呼ぶ (README.md 参照)。3D softening 付き重力で加速度 (ax,ay,az) を LM に累積する。\n"
        .format(chunk)
    ]
    for j in range(chunk):
        lines.append(PRE.format(k=j))
        for _ in range(newton):
            lines.append(NEWTON)
        lines.append(POST)
    return "".join(lines)


def wrap_kernel(name, body):
    return "@kernel_begin {}\n{}@kernel_end {}\n".format(name, body, name)


def gen_param(padded_n, chunk):
    lines = ["((:version :0.1)", " (:reserved_wait_tag #x3f)"]
    # init: 位置 (x,y,z,m) distribute、定数 broadcast、速度 distribute (31 と同一)
    lines.append(" (:slot 8  :place :pdm0 :addr 0     :send_wait_tag #x10)")
    lines.append(" (:slot 16 :place :pdm0 :addr 16384)")
    lines.append(" (:slot 24 :place :pdm0 :addr 40960)")
    # collect_pos: 更新後の位置 (31 と同一)
    lines.append(" (:slot 32 :place :pdm0 :addr 131072 :recv_wait_tag #x1e)")
    # chunk ごとのスロット。:addr は chunk の先頭粒子。host が accumulate_PH の
    # from_param PH に埋め込む値は、このスロット番号のいずれか。:send_wait_tag は付けない
    # (host が全粒子ぶんをタグ 0 で送り、mnc2_wait_idma_idle で待ってから exec するため、
    # kernel 内で待つ必要が無い)。
    for g in range(padded_n // chunk):
        slot = PJ_SLOT0 + g * 8
        addr = BASE + g * chunk * 4
        lines.append(" (:slot {} :place :pdm0 :addr {})".format(slot, addr))
    return "\n".join(lines) + ")\n"


def main():
    if len(sys.argv) != 6:
        sys.stderr.write("usage: gen_nbody32.py <n> <chunk> <newton> <out_vsm> <out_param>\n")
        return 1
    n = int(sys.argv[1])
    chunk = int(sys.argv[2])
    newton = int(sys.argv[3])
    out_vsm = sys.argv[4]
    out_param = sys.argv[5]
    padded_n = ((n + chunk - 1) // chunk) * chunk

    kernels = [
        ("init", read_body("init._vsm")),
        ("reset_acc", read_body("reset_acc._vsm")),
        ("accumulate_PH", gen_accumulate_ph_body(chunk, newton)),
        ("kick", read_body("kick._vsm")),
        ("drift", read_body("drift._vsm")),
        ("collect_pos", read_body("collect_pos._vsm")),
    ]
    with open(out_vsm, "w") as f:
        f.write(HEADER)
        for name, body in kernels:
            f.write(wrap_kernel(name, body))

    with open(out_param, "w") as f:
        f.write(gen_param(padded_n, chunk))
    return 0


if __name__ == "__main__":
    sys.exit(main())
