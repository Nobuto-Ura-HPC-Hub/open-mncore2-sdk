#!/usr/bin/env python3
"""gen_stage.py — PDM0 の粒子ごと並びを、全グループの DRAM へ置く kernel を生成する。

accumulate の @broadcast は、各グループの DRAM から粒子を読む (slot の :place は :group-dram)。
ホストは DRAM へ直接送れない (mnc2_send が書くのは PDM0 だけ) ので、ホストが PDM0 に送った
粒子ごと並び [x,y,z,m] x N を、この kernel が mvp で全グループの DRAM の同じ番地へ置く。
directive ではなく、_vsm に MV 命令を直接書く。

PDM0 は グループ 0 の PDM で、`$p<addr>@0` と書く。DRAM は `$d<addr>@<g>` (g = 0..3)。
タグ付きの mvp には、同じタグの wait を置く。wait の後は nop/5 (host-dma の examples と同じ形)。
1 回の mvp は PIECE u64 以下に分ける。

Usage: gen_stage.py <n> <out_vsm> <out_param>
"""
import sys

# ホストが粒子ごと並びを送る PDM0 の先頭番地 (u64 単位)。nbody.c の OFF_PJ / 8 と一致させる。
PJ_PDM = 262144
# 各グループの DRAM で、粒子ごと並びを置く先頭番地 (gen_accumulate.py の DBASE と一致させる)。
DBASE = 0
GROUPS = 4
# 1 回の mvp で運ぶ u64 の数の上限 (64 の倍数)。
PIECE = 4096
# mvp の完了を待つタグ。.param の :reserved_wait_tags の 2 番目に置く (1 番目は vsmlink が使う)。
TAG = "3e"


def main():
    if len(sys.argv) != 4:
        sys.stderr.write("usage: gen_stage.py <n> <out_vsm> <out_param>\n")
        return 1
    n = int(sys.argv[1])
    out_vsm, out_param = sys.argv[2], sys.argv[3]
    # ホストが送る u64 の数。@broadcast の移動の単位 (64 u64) の倍数に切り上げる。
    total = ((4 * n + 63) // 64) * 64

    vsm = [
        "# ABI PFN3_SBY64\n",
        "#\n",
        "# stage: PDM0 の粒子ごと並び {total} u64 (PDM0 の {src} 番地から) を、全グループ (0..3) の DRAM の {dst} 番地から置く。\n".format(
            total=total, src=PJ_PDM, dst=DBASE),
        "# ** 自動生成: gen_stage.py。手で編集しない **\n",
        "#\n",
    ]
    for g in range(GROUPS):
        off = 0
        while off < total:
            size = min(PIECE, total - off)
            vsm.append("\tmvp/n{size}i{tag} $p{src}@0 $d{dst}@{g}\n".format(
                size=size, tag=TAG, src=PJ_PDM + off, dst=DBASE + off, g=g))
            vsm.append("\tnop/3\n")
            vsm.append("\tnop; wait i{}\n".format(TAG))
            vsm.append("\tnop/5\n")
            off += size
    with open(out_vsm, "w") as f:
        f.write("".join(vsm))

    with open(out_param, "w") as f:
        f.write("((:version :0.1)\n (:reserved_wait_tags #x3f #x{}))\n".format(TAG))
    return 0


if __name__ == "__main__":
    sys.exit(main())
