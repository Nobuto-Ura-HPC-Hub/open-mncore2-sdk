#!/usr/bin/env python3
"""render_particles.py — N 体の結果（粒子の位置のスナップショット）を映像（MP4）にする。

使い方:
  python3 render_particles.py <結果のディレクトリ> [-o <出力.mp4>] [--fps <n>] [--views <v,...>] [--groups <n>] [--lim <L>]
                              [--size <点の大きさ>] [--trail] [--png <i,j,...>]

  <結果のディレクトリ>  pos_<NNNN>.bin と run.json があるディレクトリ
  -o                    出力。既定 <結果のディレクトリ>/particles.mp4
  --fps                 1 秒あたりのコマ数。既定 30
  --views               表示する投影（xy, xz, yz をカンマ区切り）。既定 xy の 1 画面
  --groups              粒子の番号を n 個の連続した組に分けて色を変える（星団 2 個の初期条件なら 2）。既定 1
  --lim                 表示の範囲 ±L。既定は最初のコマの |座標| の 99 パーセンタイルの 2 倍（Plummer 球は外側に
                        まばらな粒子があり、最大で決めると本体が小さくなるため）
  --size                点の大きさ。既定 1.0
  --trail               軌道と残像を描く（天体の少ない計算用。粒子が TRAIL_MAX_PARTICLES 個を超えると止める）。
                        通った軌道を暗い色の線で残し、いまいる場所を明るい色の点で、直前の TRAIL_HISTORY コマを
                        古いものほど暗くした残像で描く。色と残像の数は、このファイルの先頭の定数で決める
  --png                 動画を作らず、指定したコマ（0 始まり、カンマ区切り）を PNG に書き出す（確かめる用）

画面は既定で x-y の投影 1 つ（縦横の縮尺は同じ）。動画では複数の画面を同時に追えないため。衝突径数を与えたときなど、
ほかの向きも要るときだけ --views で足す。上に時刻（ステップ × dt）を出す。

入力の形（README.md の「render_particles.py の入力」と同じ）:
  - pos_<NNNN>.bin: 1 コマは粒子の数 × (x, y, z) の float64 または float32、little endian、粒子ごとの並び。
    1 ファイルに run.json の snapshots_per_file コマまで入り、番号順につなげると全コマになる
  - run.json: snapshot（particles, components = 3, order = "x,y,z", layout = "particle-major",
    endian = "little", dtype = "float64" か "float32"）、n_snapshot、dt、timestep_every を読む。形が違えば止める

必要なもの: numpy、matplotlib、ffmpeg（MP4 の書き出しに使う）。
"""
import argparse
import glob
import json
import os
import sys

import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
from matplotlib import animation

GROUP_COLORS = ["#ffcc55", "#55aaff", "#ff6677", "#77dd77", "#cc88ff", "#dddddd"]   # 組ごとの色（いまいる場所の色）

# --trail の見た目（ここで変える）
TRAIL_HISTORY = 15          # 残像として残す直前のコマの数
TRAIL_PATH_SCALE = 0.35     # 通った軌道の線の明るさ（いまいる場所の色に掛ける。0 で黒、1 で同じ明るさ）
TRAIL_PATH_WIDTH = 0.8      # 通った軌道の線の太さ
TRAIL_MAX_PARTICLES = 64    # --trail を使える粒子の数の上限（粒子ごとに線を描くので、多いと描画が重い）


def load(result_dir):
    with open(os.path.join(result_dir, "run.json")) as f:
        run = json.load(f)
    snap = run.get("snapshot", {})
    want = {"components": 3, "order": "x,y,z", "layout": "particle-major", "endian": "little"}
    for k, v in want.items():
        if snap.get(k) != v:
            sys.exit("render_particles.py: run.json の snapshot の %s が %r（想定は %r）" % (k, snap.get(k), v))
    dtypes = {"float64": ("<f8", 8), "float32": ("<f4", 4)}
    if snap.get("dtype") not in dtypes:
        sys.exit("render_particles.py: run.json の snapshot の dtype が %r（想定は float64 か float32）" % snap.get("dtype"))
    npdt, width = dtypes[snap["dtype"]]
    for k in ("n_snapshot", "dt", "timestep_every"):
        if k not in run:
            sys.exit("render_particles.py: run.json に %s が無い" % k)
    n = snap["particles"]
    files = sorted(glob.glob(os.path.join(result_dir, "pos_*.bin")))
    if not files:
        sys.exit("render_particles.py: %s に pos_*.bin が無い" % result_dir)
    raw = b"".join(open(p, "rb").read() for p in files)
    if len(raw) % (n * 3 * width) != 0:
        sys.exit("render_particles.py: pos_*.bin の大きさが 1 コマ（%d 粒子 × 3 × %d バイト）の倍数でない" % (n, width))
    pos = np.frombuffer(raw, dtype=npdt).reshape(-1, n, 3).astype(np.float64)
    if pos.shape[0] != run["n_snapshot"]:
        sys.exit("render_particles.py: コマ数 %d が run.json の n_snapshot %d と一致しない" % (pos.shape[0], run["n_snapshot"]))
    return run, pos


class Scene:
    def __init__(self, run, pos, views, groups, lim, size, trail):
        self.pos, self.dt_frame = pos, run["timestep_every"] * run["dt"]
        self.steps_frame = run["timestep_every"]
        n = pos.shape[1]
        if groups < 1 or n % groups != 0:
            sys.exit("render_particles.py: --groups %d で粒子の数 %d が割り切れない" % (groups, n))
        colors = np.empty(n, dtype=object)
        per = n // groups
        for g in range(groups):
            colors[g * per:(g + 1) * per] = GROUP_COLORS[g % len(GROUP_COLORS)]
        self.trail, self.size = trail, size
        if trail and n > TRAIL_MAX_PARTICLES:
            sys.exit("render_particles.py: --trail は粒子が %d 個以下のときだけ使える（%d 個）" % (TRAIL_MAX_PARTICLES, n))
        self.rgb = np.array([matplotlib.colors.to_rgb(c) for c in colors])
        if lim is None:
            lim = 2.0 * float(np.percentile(np.abs(pos[0]), 99))
        axis_no = {"x": 0, "y": 1, "z": 2}
        planes = []
        for v in views.split(","):
            if len(v) != 2 or v[0] not in axis_no or v[1] not in axis_no or v[0] == v[1]:
                sys.exit("render_particles.py: --views の %r は xy, xz, yz のような 2 文字で指定する" % v)
            planes.append((axis_no[v[0]], axis_no[v[1]], "%s-%s" % (v[0], v[1])))
        self.fig, axes = plt.subplots(1, len(planes), figsize=(6.5 * len(planes), 6.6), facecolor="black", squeeze=False)
        self.scats = []
        for ax, (i, j, title) in zip(axes[0], planes):
            ax.set_facecolor("black")
            ax.set_xlim(-lim, lim)
            ax.set_ylim(-lim, lim)
            ax.set_aspect("equal")
            ax.set_title(title, color="white")
            ax.tick_params(colors="#888888", labelsize=8)
            for s in ax.spines.values():
                s.set_color("#444444")
            paths, ghost = [], None
            if trail:
                for p in range(n):
                    line, = ax.plot([], [], "-", color=tuple(self.rgb[p] * TRAIL_PATH_SCALE), lw=TRAIL_PATH_WIDTH, zorder=1)
                    paths.append(line)
                ghost = ax.scatter([], [], s=size, linewidths=0, zorder=2)
            sc = ax.scatter(pos[0, :, i], pos[0, :, j], s=size, c=list(colors), linewidths=0, zorder=3)
            self.scats.append((sc, i, j, paths, ghost))
        self.title = self.fig.suptitle("", color="white", fontsize=13)
        self.fig.tight_layout(rect=(0, 0, 1, 0.94))

    def draw(self, k):
        artists = [self.title]
        for sc, i, j, paths, ghost in self.scats:
            sc.set_offsets(np.column_stack((self.pos[k, :, i], self.pos[k, :, j])))
            artists.append(sc)
            if not self.trail:
                continue
            for p, line in enumerate(paths):           # 通った軌道（最初からいまのコマまで）
                line.set_data(self.pos[:k + 1, p, i], self.pos[:k + 1, p, j])
            artists += paths
            # 残像: h コマ前（h = 1 .. TRAIL_HISTORY）。新しいものほど明るく大きく、古いものほど軌道の線の暗さに近づける
            hs = [h for h in range(TRAIL_HISTORY, 0, -1) if k - h >= 0]   # 古い順に描き、新しいものを上に重ねる
            if hs:
                xy = np.concatenate([np.column_stack((self.pos[k - h, :, i], self.pos[k - h, :, j])) for h in hs])
                fade = np.concatenate([np.full(len(self.rgb), 1.0 - h / (TRAIL_HISTORY + 1.0)) for h in hs])
                rgb = np.tile(self.rgb, (len(hs), 1))
                scale = TRAIL_PATH_SCALE + (1.0 - TRAIL_PATH_SCALE) * fade
                ghost.set_offsets(xy)
                ghost.set_facecolors(rgb * scale[:, None])
                ghost.set_sizes(self.size * (0.3 + 0.7 * fade))
            else:
                ghost.set_offsets(np.empty((0, 2)))
            artists.append(ghost)
        self.title.set_text("N = %d  step %d  t = %.3f" % (self.pos.shape[1], k * self.steps_frame, k * self.dt_frame))
        return artists


def main():
    ap = argparse.ArgumentParser(description="N 体の結果を MP4 にする")
    ap.add_argument("result_dir")
    ap.add_argument("-o", "--output")
    ap.add_argument("--fps", type=int, default=30)
    ap.add_argument("--views", default="xy")
    ap.add_argument("--groups", type=int, default=1)
    ap.add_argument("--lim", type=float)
    ap.add_argument("--size", type=float, default=1.0)
    ap.add_argument("--trail", action="store_true")
    ap.add_argument("--png")
    a = ap.parse_args()

    run, pos = load(a.result_dir)
    scene = Scene(run, pos, a.views, a.groups, a.lim, a.size, a.trail)
    n_frames = pos.shape[0]

    if a.png:
        for s in a.png.split(","):
            k = int(s)
            if not 0 <= k < n_frames:
                sys.exit("render_particles.py: コマ %d は範囲外（0〜%d）" % (k, n_frames - 1))
            scene.draw(k)
            out = os.path.join(a.result_dir, "frame_%05d.png" % k)
            scene.fig.savefig(out, facecolor="black", dpi=80)
            print("wrote", out)
        return

    out = a.output or os.path.join(a.result_dir, "particles.mp4")
    anim = animation.FuncAnimation(scene.fig, scene.draw, frames=n_frames, blit=False)
    writer = animation.FFMpegWriter(fps=a.fps, codec="libx264", extra_args=["-pix_fmt", "yuv420p"])
    anim.save(out, writer=writer, dpi=80, savefig_kwargs={"facecolor": "black"})
    print("wrote %s（%d コマ、%d fps、%.1f 秒。1 コマ %d ステップ）" % (out, n_frames, a.fps, n_frames / a.fps, scene.steps_frame))


if __name__ == "__main__":
    main()
