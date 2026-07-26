# 17-nbody-2d

2D 重力多体（nbody）の最小 example（f64）。**各 PE が 1 粒子を担当**し、粒子を 1 個ずつ broadcast して
全 PE が相互作用の力を積算する。**位置と力を double2 でまとめ、ベクタのまま計算する。**

いまのスコープは**力計算 1 ターン**（全ペアの力を 1 回計算する）。時間積分（位置・速度の更新）は次段。

## 何をするか

N 粒子について、各粒子 i が受ける重力
`f_i = sum_k  m_k * (r_k - r_i) / |r_k - r_i|^3`
を計算する（softening 込み）。各 PE が 1 粒子を持ち、host が粒子 k を broadcast してカーネルを起動、
各 PE が粒子 k との力を積算する。全 N 粒子を broadcast し終えると力が完成する。

## PE 状態を引き継げないことへの対処（重要）

MN-Core2 の OpenCL では**カーネルをまたいで PE の状態（レジスタ・LM）を保持できない**。そこで力の
積算値 f を **PDM に常駐**させ、各カーネルが `distribute2` で前回値を読み、加算して `collect2` で
同じ場所に書き戻す（in-place 更新。10-odd-even-sort と同じ「collect した値を次の distribute で
読み直す」パターン）。自粒子の位置は初回だけ送って PDM に常駐させ、以降は broadcast のトリガで読む
（14-odd-even-sort-full と同じ）。host が毎回送るのは broadcast する粒子だけ。

## 位置と力を double2 で扱う

x/y と fx/fy は 2 次元ベクトルなので `double2` にまとめる。`distribute` が 4 本から 2 本、`collect` が
2 本から 1 本に減る。距離・力の計算はベクタのまま通り、質量（スカラ）を力に掛けるところは
スカラ × ベクタ（全レーンに複製）で書ける。

**PDM 上の並びは「成分が外側、PE が内側」である。** つまり x が先頭に 4096 個、その後ろに y が
4096 個。OpenCL の double2 配列（PE ごとに x,y が隣接）とは違う。この並びを作るのはホストの責務で、
そのため `distribute` / `collect` の引数は `double*`（値のほうは double2）。

## 計算式

```
d   = r_k - r_i;                     // double2
r2  = d.x*d.x + d.y*d.y + eps2;      // softening（近接時の発散を抑える。eps2 は 2 冪）
ir  = rsqrt(r2)                      // HW rsqrt は約 5bit
ir  = ir * (1.5 - 0.5*r2*ir*ir)      // Newton-Raphson を 3 回（約 40bit へ）
f  += (m_k * ir^3) * d               // スカラ × ベクタ（splat）
```

- `rsqrt` を直接使う（`1/r^3` は fdiv より rsqrt が効率的）。`sqrt()` / `fma()` の明示呼び出しは
  backend が未対応なので使わない（`a*b+c` は fmul+fadd に展開されて通る）。
- 定数（eps2=0.0625, 1.5, 0.5）は**下位 32bit=0** の値にする。f64 の一般定数はレジスタに載らない。

## broadcast の HW 特性（host 側の協力）

broadcast は host が **放送領域の先頭を配りたい値で埋める**ことで全 PE 同値になる（16-broadcast-f64 /
15-broadcast の README 参照）。粒子は位置（size 2）と質量（size 1）を別々に broadcast する
（位置と質量を 1 本にまとめると、要素の取り出しが backend の未対応経路に落ちるため）。

## kernel インターフェース

```c
__kernel void nbody2d(__global const double*  _pos,   // 自粒子 (x,y)（distribute2, 常駐）
                      __global const double2* _bpos,  // broadcast 粒子 k の位置
                      __global const double*  _bmass, // broadcast 粒子 k の質量
                      __global double*        _f);    // 力 (fx,fy)（distribute2 で読み collect2 で書く）
```

## ビルドと実行

```bash
ninja build-e2e     # .asm / .idma.dat と host バイナリを生成
ninja test-emu-lib  # host エミュレータで N 粒子を積算し golden と照合
ninja test-device   # 実機で実行
```

## 検証の内容

host が同じ softened 式で O(N^2) 直接計算 (golden) を作り、全 N 粒子の力を相対誤差で照合する
（rsqrt + NR 3 回で相対誤差 ~1e-12）。テストは **N=64**（1 ターン = N カーネル起動）で emu の時間を
現実的にしている。使わない PE は質量 0 で無害化。self 項（k==i）は softening で力 0 になる。

## 性能について（先送りにした最適化）

いまは 1 粒子ずつ broadcast する構成（1 ターン = N カーネル起動）である。1 カーネルに複数粒子ブロックを
畳んで内側を unroll すればカーネル起動回数を減らせる（1 カーネルの命令数上限の確定待ち）。
