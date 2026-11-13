# scripts — examples と tests が共有する道具

ここに置いたものは、MNCL の中でここだけに置く。examples（kit に同梱）からも、meta_docs の tests（kit に入らない）
からも、このディレクトリを相対パスで参照する。**写して別の場所に置かない**（違う版が散らばるのを防ぐため）。

| ファイル | 役目 |
|---|---|
| `mncl.ninja` | examples の build の共通の規則 |
| `gen_ic_plummer.py` | N 体の初期条件（Plummer 星団 1 個または 2 個）を作る |
| `render_particles.py` | N 体の結果（粒子の位置のスナップショット）を MP4 にする |

## gen_ic_plummer.py

**書き換えない。** vsm-linker チームの 4096 粒子の N 体の例（soa2aos 版） の `gen_ic_plummer.py` を、中身を変えずに写したもの
（2026-11-07 に写した。md5 `56f304350881bacc50a9535ac71b7a1a`）。中身が同じなら、元と比べて同じかどうかを確かめられる。変更が要るときは、
元と合わせて変える。

Plummer 球を、分布関数から直接標本化し、力学的な平衡（ビリアル平衡）から作る。星団を 2 個置けば衝突を計算できる。

```
python3 gen_ic_plummer.py <N> <out.bin> [--clusters=n] [--sep=D] [--vrel=V] [--impact=B] [--scale=A] [--eps2=E]
```

- 出力の形: 1 粒子 8 個の double（little endian）: x, y, z, m, vx, vy, vz, 空き。粒子の数 N。合計の質量は 1、G = 1
- **`--eps2` には、使う側の kernel の softening の 2 乗と同じ値を渡す。** ビリアル平衡の調整に softening を使うため、
  値が違うと平衡でない状態から計算が始まる。既定の 0.01 は vsm-linker の N 体の例の値
- 説明の中の vsm-linker の N 体の例向けの記述（総粒子数は PE の数 4096 ちょうどにすること、`gen_accumulate.py`、`nbody.c`）は、
  vsm-linker の N 体の例の中のファイルを指していて、ここからは指す先が無い。MNCL で使うときの粒子の数の条件は、使う側の kernel の
  分割の作りで決まる
- 乱数の種は固定（同じ引数なら同じ出力）。粒子の数の 2 乗に比例する計算を Python でするので、4096 粒子で数秒かかる

## render_particles.py

```
python3 render_particles.py <結果のディレクトリ> [-o <出力.mp4>] [--fps <n>] [--views <v,...>] [--groups <n>] [--lim <L>] [--size <s>] [--trail] [--png <i,j,...>]
```

既定は x-y の投影 1 画面の MP4 を作る（動画では複数の画面を同時に追えないため）。ほかの向きも要るときは
`--views xy,xz` のように足す（衝突径数を与えた衝突など）。`--groups` で粒子の番号を連続した組に分けて色を変える（星団 2 個なら 2）。
`--trail` は天体の少ない計算（粒子 64 個まで）用で、通った軌道を暗い色の線で残し、いまいる場所を明るい色の点で、
直前のコマを古いものほど暗くした残像で描く。色と残像の数（既定 15 コマ）は、`render_particles.py` の先頭の定数
（`GROUP_COLORS`、`TRAIL_HISTORY`、`TRAIL_PATH_SCALE` など）で決める。
必要なもの: numpy、matplotlib、ffmpeg。

### render_particles.py の入力

**入力の形はここで定める。** 結果を作るホストのプログラムは、この形で書く。

- `pos_<NNNN>.bin`（`NNNN` は 0 からの通し番号）: 1 コマは、粒子の数 × (x, y, z) の float64 か float32、little endian、粒子ごとの
  並び（粒子 0 の x, y, z、粒子 1 の x, y, z、…）。1 ファイルに `snapshots_per_file` コマまで入れ、番号順につなげると
  全コマになる
- `run.json`: 次の項目を読む

| 項目 | 意味 |
|---|---|
| `snapshot` | `{"particles": <粒子の数>, "components": 3, "order": "x,y,z", "layout": "particle-major", "endian": "little", "dtype": "float64" か "float32"}`。形が違えば止める。映像を作るだけなら float32 で足りる（回収するデータを半分にできる） |
| `n_snapshot` | コマの数（`pos_*.bin` の全コマの数と一致しなければ止める） |
| `dt` | 時間刻み |
| `timestep_every` | 1 コマあたりのステップ数（画面の時刻 = コマの番号 × `timestep_every` × `dt`） |
| `snapshots_per_file` | 1 ファイルあたりのコマの数（読み込みでは使わない。つなげて読むため） |

