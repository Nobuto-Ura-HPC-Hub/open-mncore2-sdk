# 28-nbody-3d: 3D N 体シミュレーション

完全な 3D N 体 (逆二乗則 + 速度と時間積分 + 全対全) を目指す example。設計の全体像は
`docs/nbody-3d-design.md` を参照。ここは段階を追って実装する。

入力の粒子データ (Plummer 球など) は NEMO の mkplummer 等で生成し、正規化した共通形式で
読み込む (段階 6)。NEMO: https://carma.astro.umd.edu/nemo/archive/

## 実装状況: 段階 1〜7 まで実装済み

3D 力 -> 分割 accumulate -> ニュートン補正 -> 速度と時間積分 -> 複数ステップ + ホスト経由の
書き戻し -> leapfrog + エネルギー保存 -> 入出力ファイル、まで実装済み。CI (emu) は N=32 で
本物の全対全を回して検証する。大きい N (128 / 1024 / 4096) は device 用の手動ターゲット
`test-device-n128 / -n1024 / -n4096`。全 N を 1 カーネルに unroll すると device の連続 DMA
確保に失敗して load できないため、accumulate は 128 粒子ずつの分割カーネルにし、
host が順に exec する (下記「分割 accumulate」)。N=4096 (full) まで実機 PASS 済み。

N 粒子 (PE 0..N-1) の全対全。leapfrog (kick-drift-kick) で NSTEP ステップ回す。

```
a_i = sum_j  m_j (r_j - r_i) / (|r_j - r_i|^2 + eps^2)^(3/2)   (rsqrt + ニュートン補正)
kick:  v += a*(dt/2)
drift: x += v*dt
```

1 ステップ = kick (半) -> drift -> 書き戻し -> force (accumulate) -> kick (半)。力の評価は
1 ステップに 1 回 (加えてループ前に初回の力を 1 回)。各ステップで更新後の位置・速度を
collect し、次のステップの broadcast 元をホストが作り直す (下記)。

ホストは各ステップで全エネルギー `E = KE + PE` を倍精度で計算し、初期からのずれが有界で
あることを見る (leapfrog はシンプレクティックなのでずれが有界。実測で N=32・3 ステップの
ずれ 約 5e-7)。値照合 (device vs ホスト倍精度の leapfrog) は数ステップまで有効
(chaos で rsqrt の誤差が増幅するため)。多数ステップの物理は device (段階 7)。

- 位置 `(x,y,z,m)` を `@distribute size 4`、速度 `(vx,vy,vz)` を `@distribute size 3` で配る。
- 他粒子を `@broadcast size 4` で受ける。MN-Core 2 はループが無いので他粒子ぶんを書き並べる。
  ただし全 N を 1 カーネルにすると device で load できない (下記) ので、128 粒子ずつの
  分割カーネルにし (`gen_accumulate.py` が 1 分割を生成)、host が順に exec する。
- 先頭分割だけ accumulator を 0 にリセットし、以降の分割は LM の accumulator に加算する
  (複数ステップで毎回リセットが要る。リセットしないと力が積み上がって発散する)。
- 更新後の位置を `@collect size 3`、速度を `@collect size 3` で回収する (別カーネル)。
- 符号規約は `dx = rj - ri` (引力の向き。累積で `dx` をそのまま使える)。
- 自粒子の質量 `m` は力計算では使わない (加速度は受ける側の質量に依存しない) が、設計どおり
  持たせる。後段のエネルギー計算と、他粒子として回る側の質量で使う。
- 状態 (位置・速度・accumulator) は exec をまたいで LM に保持される。GRF も保持されるが、
  間の broadcast/collect が GRF に着地して上書きしうるので、持続状態は LM に置く。

## 入出力ファイル

### 入力 (mmap)

- struct 形バイナリ 1 種類 (`struct { x,y,z,m,vx,vy,vz,pad }`、1 粒子 8 double、little
  endian)。ホストは mmap して先頭 N 粒子を読む。並びは外部ツールと合わせる。
- パスは argv で受ける。省略時は N 別の `_build/ic_n{N}.bin` (CI 用に `gen_ic.py` が束縛系を
  生成)。realistic な Plummer 球はユーザが NEMO の mkplummer で作り、同じ struct 形に正規化して
  引数で渡す。
- 系に属さない PE (N..4095) は粒子 0 で埋める (計算は無視するが NaN を避ける)。

### 出力 (`_result_n{N}/`)

- `_result_n{N}/` (git 管理外)。スナップショットを K 個ずつまとめて `<tag>_<ts>_<NNNN>.bin`
  (tag=pos/vel、ts=開始時刻、NNNN=ファイル連番) に fwrite。1 スナップショットは粒子ごと
  `x,y,z` の N 粒子 x 3 double。位置と速度は別間隔で出せる。
- run のマニフェスト `_result/run_<ts>.json`: N、dt、ステップ数、eps2、間隔、1 スナップ
  ショットのバイト構成、命名パターン、単位系。外部の解析ツールが読む。

## メモリレイアウト (PDM)

PDM の番地はすべて u64 単位 (byte は x8)。distribute は成分ごと、broadcast 元は粒子ごとの
並び。入力と出力を別番地にしてデバッグしやすくする。

| 番地 (u64) | 内容 | 並び | 使う directive |
|---|---|---|---|
| 0 | 位置 (x,y,z,m) | `[x×4096][y×4096][z×4096][m×4096]` (成分ごと) | distribute size 4 |
| 16384 | 定数 (eps2,0.5,1.5,dt) | 先頭 4 u64 | broadcast size 4 |
| 20480 | broadcast 元 (他粒子) | `[x,y,z,m] × N` (粒子ごと) | broadcast size 4 x N |
| 40960 | 速度 (vx,vy,vz) | `[vx×4096][vy×4096][vz×4096]` (成分ごと) | distribute size 3 |
| 131072 | 更新後の位置 | `[x×4096][y×4096][z×4096]` (成分ごと) | collect size 3 |
| 143360 | 更新後の速度 | `[vx×4096][vy×4096][vz×4096]` (成分ごと) | collect size 3 |

各 PE の LM0: xi=$lm0 yi=$lm2 zi=$lm4 mi=$lm6 / ax=$lm8 ay=$lm10 az=$lm12 /
eps2=$lm14 c_half=$lm16 c_onehalf=$lm18 dt=$lm20 / vx=$lm22 vy=$lm24 vz=$lm26。

## ホスト経由の書き戻し (案 c、いったんホストでやっている)

複数ステップでは、更新後の位置を次のステップの力計算に渡すため、broadcast 元 (粒子ごと並び)
に反映する必要がある。ところが collect は成分ごと (`[x×4096]...`) にしか PDM へ書けない
(`src/templates/collect.vsm` の mvp が成分ごと)。broadcast 元は粒子ごと (`[x,y,z,m]`) で
並びが逆なので、collect した結果をそのまま broadcast 元にできない。

いまはこの変換を**ホストにやらせている** (案 c)。各ステップで:

1. accumulate (力) -> integrate (積分) を exec。
2. 更新後の位置を collect でホストに引き上げる (成分ごと)。
3. **ホストが粒子ごと並びの broadcast 元を作り直して** PDM へ送る。
4. 次のステップの accumulate がそれを読む。

vsm を変えずにホストのループだけで済むのが利点。device で効率が要るときは、broadcast を
成分ごと配列から size 1 で読む形にして書き戻しを普通の collect にする案 (案 b) に移る
(`docs/nbody-3d-design.md` 参照)。粒子ごとへの直接 scatter (案 a) は vsmlink に新しい機構が
要るので採らない。

## rsqrt のニュートン補正

rsqrt は 5 ビット近似なので、drsqrt の後にニュートン補正 `y <- y*(1.5 - 0.5*r2*y^2)` を
N 反復入れて精度を上げる。二次収束し、ホスト倍精度との相対誤差はこうなる (力のみ、M=64 実測)。

| newton | max_rel | 有効ビット |
|--------|---------|-----------|
| 0 | 1.9e-2 | ~5 |
| 1 | 6.4e-4 | ~10 |
| 2 | 5.8e-7 | ~21 |
| 3 | 9.9e-13 | ~40 |
| 4 | 2.5e-16 | ~52 (machine epsilon) |

既定は `newton=3`。反復数は `build.ninja` の `newton` で決める。

## カーネル

| ファイル | 役割 |
|---------|------|
| `init._vsm` | `(x,y,z,m)` を `@distribute size 4`、`(vx,vy,vz)` を size 3 で LM 常駐。定数 `(eps2,0.5,1.5,dt)` を broadcast。accumulator を 0 |
| `gen_accumulate.py` | accumulate の 1 分割 (グローバル粒子 start..start+count-1) を生成。各ブロックが粒子を `@broadcast size 4` で受け、3D 重力 + ニュートン補正で LM に累積。`first=1` の分割だけリセット + 送信 wait |
| `gen_chunks_ninja.py` | N の集合と `chunk` から分割カーネルの ninja edge を生成 (`accumulate-chunks.ninja`)。N ごとに phony `accum-n{N}` を提供 |
| `kick._vsm` | leapfrog の半キック `v += a*(dt/2)` (dthalf は init で dt*0.5 を作る) |
| `drift._vsm` | leapfrog のドリフト `x += v*dt` |
| `collect_pos._vsm` / `collect_vel._vsm` | 更新後の位置 / 速度を `@collect size 3` で回収 |
| `gen_ic.py` | CI 用の束縛系を struct 形バイナリで生成 (`_build/ic_n{N}.bin`) |
| `test_nbody3d.c` | 入力を mmap で読み、init + 初回 force + (kick+drift+書き戻し+force+kick+collect) x NSTEP、`_result/` へスナップショット出力、ホスト倍精度の leapfrog と照合し、エネルギー保存を見る |

分割カーネル (`accumulate_n{N}_g{g}._vsm` / `.param`) は `_build/` に生成される。分割の edge は
`gen_chunks_ninja.py` が `accumulate-chunks.ninja` に生成し (生成 ninja 方式、コミット対象)、
`build.ninja` が include する。分割単位 `chunk` (既定 128)、`newton`、`nstep` は `build.ninja` で
決める。`chunk` を変えたら `gen_chunks_ninja.py` を再実行する (host の `-DCHUNK` と一致必須)。

### 全 @broadcast で同じ id を使う

生成カーネルは全ブロックで同じ id `_pj` を使う。vsmlink の broadcast は id ごとに L1BM の
区画を持つので、id を別々にすると 64 ブロックで L1BM を使い切る (E117)。同じ id なら区画を
載せ直して再利用するため、M を大きくできる。

## 分割 accumulate (device の 1 カーネル load 上限)

全 N を 1 カーネルに unroll すると device で `mnc2_load_kernel` が NULL を返す。原因は命令数
上限ではなく、**host 側の連続 DMA バッファ確保の失敗** (要求サイズを 2 の冪乗に切り上げる)。実測で N=128 (`.idma.dat` 1.44 MB -> 2 MB 確保) は load 成功、N=1024 (11.5 MB
-> 16 MB) と N=4096 (46 MB -> 64 MB) は失敗。上限は CMA 設定と断片化に依存し固定値ではない。

そこで accumulate を `chunk` 粒子ずつの分割カーネルにする (既定 128。1 分割 約 1.44 MB で
load 実績サイズ内)。host は 1 回の力評価で分割を順に exec する。要点:

- **加算順序は単一版と同一** (分割 0 が粒子 0..127、分割 1 が 128.. と続く) なので結果は bit 一致。
- **accumulator は exec をまたいで LM に残る** (init -> accumulate -> kick が既にこの性質に依存)。
  先頭分割だけリセット、以降は加算。
- **送信は全 N を 1 回 PDM に置くだけ** (各分割はその slice を読む)。分割間で PDM 往復なし。
- **IDMA queue の深さは 31**。分割を積むとき `mnc2_get_idma_stat` の `n_dma` を
  見て 31 未満で積む (`exec_q`)。完全 idle 待ちで毎回 drain すると並列性が切れるので、分割間は
  drain しない。
- **collect の出力を host が recv する前に `mnc2_wait_idma_idle` で IDMA を流し切る**。流し切らないと、多数分割の後ろに積まれた collect の done タグが立つ前に recv の DDMA
  待ちが尽きる (N=4096 で顕在化)。
- リングシフト (msl/msr) は不要。MAB 内 4 PE の局所シフトで全 4096 のリングにならず、力の数も
  減らないため、broadcast-unroll で足りる。

### CI と full の使い分け

- CI (lit): `unroll=32` (本物の 32 体)、`nstep=3` (約 34 秒)。ホスト倍精度の leapfrog と
  6 成分を突き合わせ (値の計算チェック)、あわせてエネルギーのずれが有界であることを見る。
  系の粒子と golden が同じ N 個なので厳密。leapfrog は初回の力ぶんも含め (NSTEP+1) 回の
  力評価があり emu では重いので、CI は小さい N・数ステップ。
- device で大きい N / full を回す: 手動ターゲット `ninja test-device-n128 / -n1024 / -n4096`。
  CI・lit の N=32 とは別で、init/kick/drift/collect の 5 カーネルを共有し、accumulate は N 別の
  分割 (`accum-n{N}`)、ic/host も N 別に作る。多数ステップの物理検証 (dynamical time ぶんの
  エネルギー保存・束縛) は device。

## 物理検証

ホストが 3 つを見る。値照合と 2 つの物理量。

- **値照合**: device の x,v をホスト倍精度の leapfrog と突き合わせる。数ステップまで有効
  (chaos で rsqrt の誤差が増幅する)。
- **エネルギー保存**: 全エネルギー `E=KE+PE` の初期からのずれが有界 (leapfrog はシンプレ
  クティック)。
- **束縛**: 系の重心からの最大半径が有界 (初期の `BOUND_FACTOR` 倍以内)。**符号の取り違えは
  エネルギー保存もすり抜ける** (斥力でもエネルギーは保存する) が、斥力なら系が飛び散るので
  最大半径で捕まえられる。

## ビルド・実行

```
source scripts/overlay
ninja -C examples/28-nbody-3d build-e2e      # 生成 + vsmlink + assemble3 + C ビルド
ninja -C examples/28-nbody-3d test-emu-lib   # emu:lib で実行・照合 (CI は N=32・3 ステップ)
```

### device で全 4096・多数ステップ (段階 7)

device は HW 速度なので大きい N・多数ステップが実用的に回る。物理検証 (エネルギー保存・
束縛) はホスト側計算なので device 実行でも同じコードで働く。N=128 / 1024 / 4096 の手動
ターゲットを用意してある (default・lit には入らない。名指ししたときだけ build される)。

```
# nstep を増やすとき (dynamical time ぶん、例 数百) は build.ninja の nstep を編集
../../libmnc2-0.4.1-rc2/u02-run-idma/_build/reset   # 27 の教訓: reset してから
ninja -C examples/28-nbody-3d test-device-n4096     # または -n128 / -n1024
```

出力の `N=...` と `_result_nN/run_*.json` の `"N"`、値照合・エネルギー・束縛の PASS を見る。
入力は既定で N 別の `_build/ic_nN.bin` (gen_ic 生成)。mkplummer の Plummer 球を struct 形に
正規化したものを使うときは、生成した host バイナリ (`_build/test_nbody3d_nN`) を引数付きで
直接実行する。accumulate は分割カーネルなので大きい N でも load でき、N=32 / 128 / 1024 / 4096
すべて実機 PASS 済み (値 max_rel は N=4096 で 5.85e-13)。32 分割では IDMA queue depth 31 の
管理 (`exec_q`) と 64 MB の多バッファ確保も実機で通った。
