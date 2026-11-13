#!/usr/bin/env python3
"""gen_nbody31.py — 手書き kernel と生成 accumulate kernel を 1 本の `_vsm` へ束ね、
対応する `.param` を出力する。

kernel 分割機能 (`@kernel_begin`/`@kernel_end`) を使い、vsmlink 1 回の呼び出しで
kernel ごとの `.vsm` に分割させる。分割ごとに別々の `_vsm`/`.param` を生成していた
28-nbody-3d (`gen_accumulate.py` + `gen_chunks_ninja.py`) と異なり、accumulate は
CHUNK 粒子分の固定 PDM 領域を読む 1 個の kernel にし、host が「次の区間を送信 -> 同じ
kernel を exec」を繰り返す (ループが host 側に移る)。

kernel 構成 (6 個、N に依存しない): init, reset_acc, accumulate, kick, drift, collect_pos。
速度 (vx,vy,vz) は device 上で kick が更新し続けるが、host へは回収しない
(collect_vel に相当する kernel は無い)。

accumulate の @broadcast は、CHUNK 個のブロックすべてで同じ slot (40) を使う。同じ slot の
2 回目以降は、PDM の粒子ごと並びの続き (次の粒子) を読む。kernel の実行ごとに slot の :addr
(BASE) から読み始めるので、host が毎回 BASE へ次の区間を送り直せばよい。

Usage: gen_nbody31.py <chunk> <newton> <out_vsm> <out_param>
"""
import os
import sys

# 他粒子ぶんの broadcast 元 PDM 番地 (粒子ごと並び [x,y,z,m] x N の先頭)。
# 28-nbody-3d と同じ番地。
BASE = 20480
ACC_SLOT = 40

HEADER = """\
# ABI PFN3_SBY64
#
# nbody31: kernel 分割版の 3D N 体 vsm。init / reset_acc / accumulate / kick /
# drift / collect_pos の 6 kernel を @kernel_begin/@kernel_end で束ねる。vsmlink がこれを
# kernel ごとの .vsm に分割し、host が accumulate を CHUNK 粒子ずつ繰り返し exec する。
# ** 自動生成: gen_nbody31.py。手で編集しない **
#
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


def read_body(name):
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), name)
    with open(path) as f:
        return f.read()


def gen_accumulate_body(chunk, newton):
    lines = [
        "# accumulate: 他粒子 {} 個 (固定 PDM 領域、slot {}) を @broadcast size 4 で順に受け、\n"
        "# 3D softening 付き重力で加速度 (ax,ay,az) を LM に累積する。host が CHUNK 粒子ずつ\n"
        "# 区間を切り替えながら、この kernel を繰り返し exec する。\n".format(chunk, ACC_SLOT)
    ]
    for j in range(chunk):
        lines.append(PRE.format(k=j, slot=ACC_SLOT))
        for _ in range(newton):
            lines.append(NEWTON)
        lines.append(POST)
    return "".join(lines)


def wrap_kernel(name, body):
    return "@kernel_begin {}\n{}@kernel_end {}\n".format(name, body, name)


def gen_param():
    lines = ["((:version :0.1)", " (:reserved_wait_tag #x3f)"]
    # init: 位置 (x,y,z,m) distribute、定数 broadcast、速度 distribute
    lines.append(" (:slot 8  :place :pdm0 :addr 0     :send_wait_tag #x10)")
    lines.append(" (:slot 16 :place :pdm0 :addr 16384)")
    lines.append(" (:slot 24 :place :pdm0 :addr 40960)")
    # collect_pos: 更新後の位置
    lines.append(" (:slot 32 :place :pdm0 :addr 131072 :recv_wait_tag #x1e)")
    # accumulate: 他粒子 broadcast (CHUNK 個のブロックで同じ slot を使う)
    lines.append(" (:slot {} :place :pdm0 :addr {} :send_wait_tag #x10)".format(ACC_SLOT, BASE))
    return "\n".join(lines) + ")\n"


def main():
    if len(sys.argv) != 5:
        sys.stderr.write("usage: gen_nbody31.py <chunk> <newton> <out_vsm> <out_param>\n")
        return 1
    chunk = int(sys.argv[1])
    newton = int(sys.argv[2])
    out_vsm = sys.argv[3]
    out_param = sys.argv[4]

    kernels = [
        ("init", read_body("init._vsm")),
        ("reset_acc", read_body("reset_acc._vsm")),
        ("accumulate", gen_accumulate_body(chunk, newton)),
        ("kick", read_body("kick._vsm")),
        ("drift", read_body("drift._vsm")),
        ("collect_pos", read_body("collect_pos._vsm")),
    ]
    with open(out_vsm, "w") as f:
        f.write(HEADER)
        for name, body in kernels:
            f.write(wrap_kernel(name, body))

    with open(out_param, "w") as f:
        f.write(gen_param())
    return 0


if __name__ == "__main__":
    sys.exit(main())
