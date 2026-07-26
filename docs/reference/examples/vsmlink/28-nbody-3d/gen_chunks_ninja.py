#!/usr/bin/env python3
"""gen_chunks_ninja.py — accumulate 分割カーネルの ninja edge を生成する。

全 N を 1 カーネルに unroll すると device の連続 DMA 確保 (2 の冪乗切り上げ) に失敗して
mnc2_load_kernel が NULL を返す。粒子を CHUNK 個ずつの分割に切り、各分割を
個別のカーネルにする。この生成器は N の集合と CHUNK から、各分割の gen/vsmlink/asm3_loader
edge と、N ごとの phony accum-n{N} を accumulate-chunks.ninja に書き出す。build.ninja が
include し、$builddir / $newton は build.ninja 側で定義する。

N の集合か CHUNK を変えたら再実行して再生成する (gen-install-templates-ninja.sh と同じ運用)。

Usage: python3 gen_chunks_ninja.py
"""
import os
import sys

# 分割対象の N と分割単位。ここを変えたら再生成する。
# CHUNK=128 は 1 分割の .idma.dat が約 1.44 MB (-> 2 MB 連続確保) で device に load 実績あり。
NS = [32, 128, 1024, 4096]
CHUNK = 128


def chunks(n):
    """N を CHUNK ごとに分割し (g, start, count, first) を返す。first は先頭分割のみ 1。"""
    g, start = 0, 0
    while start < n:
        yield g, start, min(CHUNK, n - start), 1 if g == 0 else 0
        g, start = g + 1, start + CHUNK


def main():
    out = [
        "# ** 自動生成: gen_chunks_ninja.py。手で編集しない **",
        "# accumulate を CHUNK={} 粒子ずつの分割カーネルにする。".format(CHUNK),
        "# build.ninja が include する。$builddir / $newton は build.ninja 側で定義。",
        "",
    ]
    for n in NS:
        idmas = []
        for g, start, count, first in chunks(n):
            b = "$builddir/accumulate_n{}_g{}".format(n, g)
            out += [
                "build {b}._vsm {b}.param: gen_acc gen_accumulate.py".format(b=b),
                "  start = {}".format(start),
                "  count = {}".format(count),
                "  first = {}".format(first),
                "  out_vsm = {b}._vsm".format(b=b),
                "  out_param = {b}.param".format(b=b),
                "build {b}.vsm: vsmlink {b}._vsm {b}.param nbody3d.stparam".format(b=b),
                "  vsm = {b}._vsm".format(b=b),
                "  param = {b}.param".format(b=b),
                "  stparam = nbody3d.stparam",
                "build {b}.idma.dat: asm3_loader {b}.vsm".format(b=b),
                "  stem = {b}".format(b=b),
            ]
            idmas.append(b + ".idma.dat")
        out += ["build accum-n{}: phony {}".format(n, " ".join(idmas)), ""]

    outpath = os.path.join(os.path.dirname(os.path.abspath(__file__)),
                           "accumulate-chunks.ninja")
    with open(outpath, "w") as f:
        f.write("\n".join(out) + "\n")
    sys.stderr.write("generated {} (NS={}, CHUNK={})\n".format(outpath, NS, CHUNK))
    return 0


if __name__ == "__main__":
    sys.exit(main())
