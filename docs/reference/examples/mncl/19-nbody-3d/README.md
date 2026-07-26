# 19-nbody-3d

3D 重力多体（nbody）シミュレーション（f64）。**各 PE が 1 粒子を担当**し、粒子を 1 個ずつ broadcast
して全 PE が相互作用の力を積算し、**semi-implicit Euler（Euler-Cromer）で粒子を複数タイムステップ
進める。** 位置・速度を double3（HW では v4f64）でまとめ、ベクタのまま計算する。

この example の目的は **size 3 / v4f64 のベクタ演算・splat / broadcast size 3 を実機で検証する**こと。
17-nbody-2d は double2（v2f64）までしか踏まなかった。

## 2 カーネル構成（力フェーズ + 積分フェーズ）

MN-Core2 の OpenCL では**カーネルをまたいで PE の状態（レジスタ・LM）を保持できない**。そこで
**状態（位置 pos・速度 vel・加速度 acc）をすべて PDM に常駐**させ、2 本のカーネルが読み書きする
（14-odd-even-sort-full と同じ 2 カーネル + PDM 常駐パターン）。

- **`nbody3d_force`**: 各 PE が担当粒子の加速度 `a_i = sum_k m_k (r_k-r_i)/|r|^3` を `_acc` に積む。
  host が粒子 k を 1 個ずつ broadcast して N 回起動。**積算値は加速度そのもの**（重力加速度は
  担当粒子の質量に依らないので m_i で割らない）。
- **`nbody3d_integ`**: `v <- v + dt*a`、`x <- x + dt*v`（更新後 v で位置更新 = Euler-Cromer）。
  1 タイムステップに 1 回起動。

各タイムステップは「acc を 0 埋め → 力フェーズ（N 回）→ 積分フェーズ（1 回）→ 更新後 pos を recv」。
**更新後の位置を次ステップの broadcast 元へ書き戻す（伝播）**ので、全 PE が最新の位置を見る。

## 積分カーネルは triggerless（起動トリガの送信が不要。ISS-213）

積分カーネルは pos/vel/acc/dt をすべて PDM 常駐で読むだけで、host から送る新しいデータが無い。
**`.param` に `send_wait_tag` を付けなければ、vsm-linker は先頭の起動ゲート wait を出さず、
`mnc2_exec_kernel` 単独でカーネルが走る**（10-odd-even-sort の reduce カーネルと同型）。順序（力フェーズ
→ 積分）は host の sync `mnc2_recv`（力フェーズの acc drain）で担保する。**値として不要なダミー送信は
要らない。** dt は init で 1 回だけ送って常駐させる。

これは `.param` だけで切り替わり（カーネル本体 `.cl` は不変）、当初 dt を起動トリガに兼ねていた回避策を
撤去した。カーネル起動と先頭 wait の仕組み・残る設計課題は ISS-213 と `kanban/dma-wait-tag-naming.md`。

## double3（v4f64）で扱う

位置・速度は 3 次元なので `double3`（OpenCL 規約で v4f64、4 レーン目 padding）で扱う。距離・力は
ベクタのまま、質量やスカラ × ベクタは splat で書ける。**4 レーン目は空振りで計算されるが、`r2` も
`collect3` も 3 レーンしか読まないので無害**。

**PDM 上の並びは「成分が外側、PE が内側」である。** x が先頭 4096 個、その後ろに y、さらに z。
OpenCL の double3 配列とは違う。並べるのはホストの責務で、引数は `double*`（値は double3）。

## 計算式（力フェーズ）

```
d   = r_k - r_i;                          // double3
r2  = d.x*d.x + d.y*d.y + d.z*d.z + eps2; // softening（eps2 は 2 冪）
ir  = rsqrt(r2)                           // HW rsqrt は約 5bit
ir  = ir * (1.5 - 0.5*r2*ir*ir)           // Newton-Raphson を 3 回
a  += (m_k * ir^3) * d                    // スカラ × ベクタ（splat）
```

## ビルドと実行

**N と STEPS は環境変数 `NBODY_N` / `NBODY_STEPS` で受ける（default N=128 / STEPS=2）。**

```bash
ninja build-e2e         # 2 カーネルの .asm / .idma.dat と host バイナリを生成
ninja test-emu-lib      # host エミュレータで N=128 / STEPS=2（CI。各ステップ golden 照合）
ninja test-emu-full     # host エミュレータで N=4096 の full（重い）
ninja test-device       # 実機で N=128
ninja test-device-full  # 実機で N=4096
```

診断出力（初期エネルギー、各ステップの E / drift / 照合結果、最大相対誤差）は **stderr** に出る。
`./_build/test_nbody3d --silent` で診断を止め、判定行（PASS/FAIL）だけにできる。
**PASS/FAIL 判定は golden 照合で行い、診断出力には依存しない。**

## 検証の内容

host が同じ semi-implicit Euler を O(N^2) で 1 ステップずつ回し、**各ステップ後の pos / vel を全 N
粒子ぶんデバイスと相対誤差で照合**する（転送成功だけでなく計算結果。rsqrt + NR 3 回で相対誤差
~1e-10）。**CI は N=128 / STEPS=2** で emu の時間を抑える（STEPS=2 で伝播を必ず踏む）。N=128 でも
subpeid 0..3 の全 4 種を踏み、全 PE を照合するので「PE の一部だけ壊れる不具合」を検出できる。

## エネルギー誤差について（診断値。MNCL の課題ではない）

エネルギー E を診断で print するが、観測されるのは**単調ドリフト**（初期条件が全粒子静止 → 重力
collapse なので、数ステップでは collapse の途中相にいて E が単調に動く。~2e-6/step 程度）。有界振動は
collapse-再膨張を通す多数ステップで初めて見える話で、この example では確認していない。

**これは数値積分（semi-implicit Euler）の性質であって、MN-Core2 やコンパイラの誤差ではない。**
host golden も同じ積分法なので**ドリフトは device と golden で同一**であり、両者は相対誤差 ~1e-10 で
一致する（PASS 判定は golden 照合で行う）。誤差を減らすなら積分方式を上げる（Verlet / leapfrog 等）か
dt を細かくする話になるが、それは数値解析の領域で、この example の目的（size 3 / v4f64 / broadcast /
splat のコンパイラ機能の実機検証）とは別。**MNCL の課題ではない。**

なお device と golden の差（~1e-10）は、**HW rsqrt（5bit）+ Newton-Raphson 3 回 ≈ 40bit（~1e-12）**と
**総和順序の違い**で説明がつき、ステップを重ねても増幅しない（力計算のみの Step B と時間積分 2 ステップ
で同じ 7.4e-10）。コンパイラ／HW が誤差を貯めている兆候は無い。

## 性能について（先送りにした最適化）

いまは 1 粒子ずつ broadcast する構成（1 タイムステップ = N + 1 カーネル起動）で、host loop の DMA が
律速になりやすい。1 カーネルに複数粒子ブロックを畳んで内側を unroll すればカーネル起動回数を
減らせる（1 カーネルの命令数上限の確定待ち、ISS-181）。
