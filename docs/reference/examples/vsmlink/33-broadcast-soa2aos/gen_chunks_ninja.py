#!/usr/bin/env python3
"""gen_chunks_ninja.py — 1 タイムステップを刻んだ kernel の ninja edge を生成する。

この example は **1 kernel = 16 粒子 = 1 転送** で刻む。CHUNK=16 は `@broadcast` が 1 回に
移動する 64 u64 にちょうど対応するので、生成される kernel が「PDM から 1 回転送し、
その 16 粒子を配る」という 1 対 1 の対応になり、仕組みがそのまま読める。

**性能のための刻み方ではない。** 刻み数を変えても転送の合計回数 (n/16) も命令数も変わらない。
変わるのは kernel の個数だけである。実行を目的とする N 体のアプリは kernel 数を
減らす側 (128 粒子ずつ) を採っており、この example は仕組みを見せる側を採っている。

**emu:lib で回すのは N=64 (4 分割)。** 1 kernel = 16 粒子で刻むと N=4096 では 256 分割になり、
emu:lib での実行が現実的でなくなる。仕組みの確認は少ない粒子数でできるので CI は N=64 とし、
転送回数の測定 (mvb を数える) だけは build.ninja が別途 N=4096 の単一 kernel で行う。

計算部分 (rotate / logistic) とレイアウト変換の有無で 3 通り。読み書きの向き (a/b) も併せて生成する。

N か CHUNK を変えたら再実行して再生成する。

Usage: python3 gen_chunks_ninja.py
"""
import os
import sys

N = 64              # emu:lib で回す粒子数。1 kernel 16 粒子なので 4 分割になる
CHUNK = 16          # 1 回の転送 = 64 u64 = 16 粒子。16 の倍数でなければ、分割の境目の 64 u64 を 2 回移動する

# (名前, 計算 ._vsm, レイアウト変換するか)
#   rot  … 段階 1 の確認 (レイアウト変換なし。順列が発生しないので値がそのまま巡回する)
#   rotx … 本命 (レイアウト変換あり。順列が発生し、2 回で戻る)
#   logx … 計算部分を差し替えられることの確認
VARIANTS = [
    ("rot", "rotate._vsm", 0),
    ("rotx", "rotate._vsm", 1),
    ("logx", "logistic._vsm", 1),
]


def chunks(n, chunk):
    """(g, start, last) を返す。last は最終分割のみ 1 (レイアウト変換と collect が入る)。"""
    g, start = 0, 0
    while start < n:
        yield g, start, 1 if start + chunk >= n else 0
        g, start = g + 1, start + chunk


def main():
    nsplit = (N + CHUNK - 1) // CHUNK
    out = [
        "# ** 自動生成: gen_chunks_ninja.py。手で編集しない **",
        "# 1 タイムステップを CHUNK={} 粒子 (= 1 回の転送ぶん) ずつの kernel に刻む (N={}, {} 分割)。".format(
            CHUNK, N, nsplit),
        "# build.ninja が include する。$builddir は build.ninja 側で定義。",
        "",
    ]
    for name, compute, transpose in VARIANTS:
        for side in ("a", "b"):
            idmas = []
            for g, start, last in chunks(N, CHUNK):
                b = "$builddir/{}_{}_{}".format(name, side, g)
                # gen は a/b 両方を一度に出すので、edge は side=a のときだけ書く
                if side == "a":
                    ba, bb = b, "$builddir/{}_b_{}".format(name, g)
                    out += [
                        "build {ba}._vsm {ba}.param {bb}._vsm {bb}.param: gen_step gen_step.py {compute}".format(
                            ba=ba, bb=bb, compute=compute),
                        "  n = {}".format(N),
                        "  start = {}".format(start),
                        "  last = {}".format(last),
                        "  compute = {}".format(compute),
                        "  transpose = {}".format(transpose),
                        "  out_a = {ba}._vsm".format(ba=ba),
                        "  param_a = {ba}.param".format(ba=ba),
                        "  out_b = {bb}._vsm".format(bb=bb),
                        "  param_b = {bb}.param".format(bb=bb),
                    ]
                out += [
                    "build {b}.vsm: vsmlink {b}._vsm {b}.param step.stparam".format(b=b),
                    "  vsm = {b}._vsm".format(b=b),
                    "  param = {b}.param".format(b=b),
                    "  stparam = step.stparam",
                    "build {b}.idma.dat: asm3_loader {b}.vsm".format(b=b),
                    "  stem = {b}".format(b=b),
                ]
                idmas.append(b + ".idma.dat")
            out += ["build steps-{}-{}: phony {}".format(name, side, " ".join(idmas)), ""]

    outpath = os.path.join(os.path.dirname(os.path.abspath(__file__)), "steps.ninja")
    with open(outpath, "w") as f:
        f.write("\n".join(out) + "\n")
    sys.stderr.write("generated {} (N={}, CHUNK={}, {} 分割 x {} 種 x 2 方向)\n".format(
        outpath, N, CHUNK, nsplit, len(VARIANTS)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
