#!/usr/bin/env python3
"""make_mp4.py — 位置のスナップショット (pos_*.bin) から mp4 を作る。手元で実行する。

nbody が書く pos_0000.bin, pos_0001.bin, ... は、1 コマが「粒子ごとに x, y, z の double」を
粒子数ぶん並べたもので、コマが続けて並ぶ。粒子数は <結果ディレクトリ>/run.json の "N" から読む。

1 コマごとに matplotlib で x-y 平面への射影を描き、ffmpeg で mp4 にする。星団 2 個の衝突を想定して、
粒子番号の前半と後半を別の色にする (gen_ic_plummer.py は星団 1 個ずつを連続した番号で出す)。
脱出する少数の粒子で枠が広がりすぎないよう、枠は全コマの位置の 99% 点から決める。

Usage: make_mp4.py <結果ディレクトリ> <out.mp4> [--fps=<n>] [--lim=<L>] [--axes=xy|xz|yz] [--size=<px>]
"""
import glob
import json
import os
import subprocess
import sys
import tempfile

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    opts = dict(a[2:].split("=", 1) for a in sys.argv[1:] if a.startswith("--") and "=" in a)
    if len(args) != 2:
        sys.stderr.write(__doc__)
        return 1
    result_dir, out = args
    fps = int(opts.get("fps", 30))
    axes = opts.get("axes", "xy")
    size = int(opts.get("size", 720))
    ia, ib = {"xy": (0, 1), "xz": (0, 2), "yz": (1, 2)}[axes]

    with open(os.path.join(result_dir, "run.json")) as f:
        n = json.load(f)["N"]
    frames = []
    for path in sorted(glob.glob(os.path.join(result_dir, "pos_*.bin"))):
        a = np.fromfile(path, dtype="<f8")
        frames.extend(a.reshape(-1, n, 3))
    if not frames:
        sys.stderr.write("pos_*.bin が見つからない: %s\n" % result_dir)
        return 1
    pos = np.array(frames)                      # (コマ, 粒子, 3)
    print("N=%d コマ数=%d" % (n, len(pos)))

    if "lim" in opts:
        lim = float(opts["lim"])
    else:
        lim = 1.2 * float(np.percentile(np.abs(pos[:, :, [ia, ib]]), 99))
    half = n // 2
    colors = np.where(np.arange(n) < half, "tab:orange", "tab:cyan")
    dpi = 100
    with tempfile.TemporaryDirectory() as tmp:
        for k, frame in enumerate(pos):
            fig = plt.figure(figsize=(size / dpi, size / dpi), dpi=dpi, facecolor="black")
            ax = fig.add_axes([0, 0, 1, 1], facecolor="black")
            ax.scatter(frame[:, ia], frame[:, ib], s=1.0, c=colors, linewidths=0)
            ax.set_xlim(-lim, lim)
            ax.set_ylim(-lim, lim)
            ax.set_aspect("equal")
            ax.axis("off")
            ax.text(0.02, 0.97, "frame %d / %d" % (k, len(pos) - 1), color="white",
                    transform=ax.transAxes, va="top", fontsize=9)
            fig.savefig(os.path.join(tmp, "frame_%05d.png" % k), facecolor="black")
            plt.close(fig)
        subprocess.run(["ffmpeg", "-y", "-loglevel", "error", "-framerate", str(fps),
                        "-i", os.path.join(tmp, "frame_%05d.png"),
                        "-pix_fmt", "yuv420p", out], check=True)
    print("作成: %s" % out)
    return 0


if __name__ == "__main__":
    sys.exit(main())
