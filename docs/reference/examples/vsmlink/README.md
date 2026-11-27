# vsmlink examples

vsmlink コマンドの使い方の例。各 example は `_vsm` と配置パラメタ（`.param`）から `.vsm` を作り、
多くは emu:lib（と実機）で結果を確かめる。

## 動かし方

SDK を有効にし、`sdk-examples` で examples を作業ディレクトリにコピーして、各 example のディレクトリで `ninja` を実行する。

```bash
source <SDK>/bin/activate
sdk-examples vsmlink                 # vsmlink-<版>/ にコピーする
cd vsmlink-<版>/02-vecadd
ninja                                # vsmlink コマンドで .vsm を生成
ninja build-e2e                      # assemble3 とホスト側のビルド
ninja test-emu-lib                   # emu:lib で実行
```

lit で全体を流すときは、`vsmlink-<版>/` で `lit -j1 --order=lexical lit/test-emu-lib/`（実機では `lit/test-device/`）。
時間のかかるテストは `--param long=1` を付けたときだけ流れる。

## examples

### 01-roundtrip — DMA いってかえって

最小構成。

### 02-vecadd — 1D ベクトル加算

4096 PE の 1D 独立計算。golden data による検証あり。

```bash
vsmlink vecadd._vsm vecadd.param out.vsm
diff out.vsm expected.vsm   # 一致すれば OK
```

### 03-vecadd-2d — 2D ベクトル加算

02-vecadd と同じカーネルを、ホストの側で 64 x 64 の 2 次元の配列として扱う。

### 04-vecadd-3d — 3D ベクトル加算

02-vecadd と同じカーネルを、ホストの側で 16 x 16 x 16 の 3 次元の配列として扱う。

### 05-stencil1d — 1D 3 点ステンシル

`@get_neighbor` で左右の隣の値を受け取る。端の PE には自分の値を入れる（`:reg <src>`）。golden data による検証あり。

06〜08 は欠番である（番号は詰めない）。

### 09-dmaid-conflict — DMA タグ衝突の検証

ホストが vsmlink の予約タグ（この example では `#x3f`）と同じ dmaid で送ると、タグが衝突して失敗することの検証。

### 10-mem-default — :place 省略（デフォルト :pdm0）

01-roundtrip と同一カーネルだが、.param で `:place` を省略。
`:place` なしでも `:pdm0` として動作することの検証。

### 11-odd-even-sort — Odd-Even Transposition Sort

各 PE が左右の隣接 PE とデータを比較し、奇数/偶数フェーズを交互に実行してソートする。

### 12-l1b-crossing — cross L1B テスト

隣接 L1B からデータを取得するテスト。L2BM 経由の 5 段階データパスを示す。

### 13-l2b-crossing — cross L2B テスト

cross L2B のデータ取得。L2BM はセクション局所なので PDM 経由の collect/redistribute が必要。

### 14-stencil1d-debug — 1D ステンシル袖交換チュートリアル

PE シフト・MAB 越え・L1B 越え・L2B 越え・セクション越えの 5 段階を個別に観察できるデバッグ用。

### 15-mask-safety-T4 — ネスト if-then-else + @get_neighbor

maskr を使ったネスト分岐のテスト。フラグ組み合わせで異なる値を設定する「初期値 + 例外上書き」パターン。ただしこのパターンは if/then/else の中身を外に出すため、副作用で破壊を起こす問題がある。正しい if/then/else に要修正。

### 16-mask-safety-T4-debug — 15 のデバッグ版

debug_read で GRF0 を直接読む。emu:lib または emu:process で動かす。実機は `mnc2_debug_read` が未サポートなので対象外（libmnc2 の `api-reference.md`「対応メモリ領域 (バックエンド別)」）。

### 17-boundary-collect — @boundary_flags collect

各 PE の HW レジスタ（`$subpeid`、`$mabid`、`$l1bid` など）から計算した 10bit の境界フラグを
@collect で PDM に回収し、ホスト側で期待値と照合する。`@boundary_flags` で配るデータを作るためのもの。

### 18-bf-verify — @boundary_flags 検証（golden override → stencil）

外部 golden data で境界フラグを上書きし、stencil 結果で検証する。
golden が正しければ PASS、全ゼロなら境界 PE で結果がずれて FAIL。

### 19-collect-x3 — 3 点ステンシル（collect × 3）

output[i] = left[i] + center[i] + right[i] の 1D 3 点ステンシル。
@boundary_flags でホストから配布した境界フラグを使用。

### 20-stencil1d-distribute — 1D ステンシル（境界フラグを PE の上で計算）

05 と同じ 1D 3 点ステンシル。境界フラグは各 PE が自分の位置のレジスタから計算し、ホストからは送らない
（計算の命令列は `_vsm` に直接書いてある）。

### 21-get-neighbor — @get_neighbor テスト

全階層（MAB 内、cross MAB、cross L1B、cross L2B）での隣接値取得を検証する。

### 22-identify-reduce-add — @identify + @reduce パイプライン

@identify で PE ID [0..4095] を各 PE に配布し、@reduce :liadd で全 PE の整数加算 reduce を実行。
4 部分和（PE position 0〜3）を個別に検証する最小パイプライン。

### 23-identify — @identify サンプル

@identify directive の最小サンプル（PE 座標配布）。

### 24-odd-even-sort-reduce — odd-even sort + reduce

odd-even sort と @reduce を組み合わせたパイプライン。

### 25-broadcast-reduce-add — @broadcast (l1bmp) + @reduce

放送命令 l1bmp による全 PE 同値放送を @broadcast で行い、@reduce :liadd で検証する。

### 26-broadcast-accumulate-2d — 2D 質量重み付き総和（distribute / broadcast / collect）

各 PE が自分の星を持ち、他の星を @broadcast で順に配って総和を LM に累積する。distribute（自粒子 size 2）と broadcast（他粒子 size 2 と size 1）と collect（結果 size 2）を実アプリで使う骨格。N 体と同じデータの流れだが、rsqrt を使わず加算と乗算だけで組み、golden とビット一致で照合する。

### 27-roundtrip-size4 — @distribute / @collect 往復（成分数可変）

`@distribute`と`@collect`の往復を、成分数（size）を引数で変えながら検証する。

### 28-nbody-3d — 3D N 体（distribute / broadcast / collect + rsqrt 重力）

3D N 体の example。段階実装で、段階 1〜7 まで実装済み（N=4096 まで実機 PASS）。段階 1 では各 PE が自粒子 (x,y,z,m) を持ち、他粒子を @broadcast size 4 で配って softening 付き重力の加速度を collect size 3 で回収する。rsqrt をそのまま使い、ホスト倍精度と緩い許容誤差で照合する。

### 29-broadcast-collect — broadcast + collect の往復

`@broadcast`で全 PE に配った struct を各 PE で加工し、`@collect`で PDM に集めて回収する最小の往復 example。集める側のレイアウトとハードウェア経路の説明は `share/vsmlink/directives-spec.md` の「PDM 上の並び」。

### 30-soa2aos — collect の SoA 出力を device 上で AoS に並べ替える

`@collect`の出力（成分ごと SoA）を、device 上で`struct {x,y,z,m}[]`（AoS）に並べ替えるカーネル。`dmwrite`/`dmread`による MAB 内 4x4 転置を使う。並びは `share/vsmlink/directives-spec.md` の「@distribute_aos2soa / @collect_soa2aos」。

### 31-nbody-host-mediated-loop — kernel 数の N 非依存と host 不介在ループは両立しない

`@kernel_begin`/`@kernel_end` によるマルチカーネル `_vsm` を、単体テストの外で実アプリケーション規模の入力に対して検証する example。28-nbody-3d を基礎に、accumulate（全対全の力計算）を「CHUNK 粒子ずつ固定 PDM 領域を読む 1 個の kernel」にし、host が「送信 -> exec」を繰り返す形に書き換えた。手書き kernel 6 個は本来 1 個ずつ別々に vsmlink へかければ済むが、あえて 1 本の `_vsm` に合成してから kernel ごとに変換している（詳細は本 example の README.md）。28 は分割ごとに別々の kernel を生成していた（kernel 数が N に依存するが host 不介在）のに対し、31 は kernel 数が N に依存しない代わりに host が毎回データを詰め替える。この trade-off が MN-Core2 の本質的な制約（`@broadcast` の PDM 番地が link 時固定、ループを持たない静的スケジュール）に由来する。速度はホストへ回収しない。

### 32-nbody-runtime-link — host が実行時に vsmlink を呼び直す 3D N 体

28・31 と同じ 3D 重力の全対全 N 体を題材に、28（ビルド時に必要な数だけ kernel を生成）・
31（1 個の kernel を使い回し host がデータを送り直す）とも異なる第三の構成として、
全粒子を 1 回だけ PDM へ送り、host が chunk ごとに `accumulate_PH` kernel の
`@broadcast` スロット番号（まだ確定していない）を埋めて `vsmlink`（+ `assemble3`）を
実行時に呼び直す構成を実装した。emu:lib での実行・値照合まで確認済み（N=32）。
詳細は本 example の README.md。

### 35-emit-from-debug — @emit_from でデバッグ用に PE の値を書き出す

全 PE に初期値を @distribute で配り、@broadcast の値（1〜4197）を 1 回ごとに足し、足した後の値を @emit_from で書き出す（2048 回目までは HW PE ID 817、以降は 529）。最後に @reduce で全 PE の合計を取り、その受信で kernel の終わりを待って、ホストが書き出しと合計を f64 の完全一致で照合する。演算は dvadd（f64）。

### 36-bitonic-sort — バイトニックソート（4096 個、f64、@get_hypercube_neighbor）

比較・交換の相手（PE の番号の 1 つのビットを反転した PE）の値を @get_hypercube_neighbor で受け取る、4096 個のバイトニックソート。全 78 ステップを 1 つの kernel に並べる。ホストは、kernel と同じ規則で計算した配列と、4096 個すべてを比べる。emu:lib も実機も、78 ステップを実行して、全体が昇順になることを確かめる。比較は dmin / dmax（fp64）。

### 37-bitonic-sort-8192 — バイトニックソート（8192 個、f64、各 PE が 2 つの値を持つ）

36 の続きで、4096 個を超える個数でも @get_hypercube_neighbor が使えることを見せる。各 PE が 2 つの値（A と B）を持ち、deep 0〜11 の段は A と B のそれぞれに @get_hypercube_neighbor を呼び、deep 12 にあたる段は同じ PE の中で dmin / dmax で入れ替える。全 91 ステップを 1 つの kernel に並べ、emu:lib も実機も、全体が昇順になることを確かめる。

### 38-nbody-4096-dram — 3D N 体（他粒子を各グループの DRAM から @broadcast で受ける）

28-nbody-3d と同じ物理で、他粒子を @broadcast が読む場所だけを PDM から DRAM（`.param` の `:place :group-dram`）に変えた example。ホストが PDM0 に送った粒子ごと並びを、stage kernel（`_vsm` に `mvp` を直接書いた kernel）が全グループの DRAM へ置く。emu:lib は N=32、実機は N=4096。ホストの引数 `--dev=<n>` で実機のデバイス番号を選べる。ホスト・`_vsm` に直接書いた `mvp`・directive のどれがどのデータを動かすかと、PDM0 と DRAM の配置は、本 example の README.md の「データの移動」に書いてある。

### 39-nbody-3d-multi-device — 3D N 体（MN-Core2 を複数枚使う。他粒子は各デバイスの DRAM から @broadcast で受ける）

38 の方式に、複数のデバイスへの分割を足した example。粒子を 1 枚あたり N / (デバイス数) 個に分けて各デバイスに割り当て、毎タイムステップ、ホストが全デバイスの結果を集めて全粒子の並びにし、全デバイスの DRAM へ入れ直す（デバイス同士は直接つながらない）。実機は 2 枚で N=8192、4 枚で N=16384（星団 2 個の衝突を 2000 タイムステップ流し、`make_mp4.py` で mp4 にできる）。ホストの引数 `--devs=<番号>,<番号>` で使うデバイスを選ぶ。emu:lib は、デバイス 1 枚、N=32 だけである。ホスト・`_vsm` に直接書いた `mvp`・directive のどれがどのデータを動かすかと、PDM0 と DRAM の配置は、本 example の README.md の「データの移動」に書いてある。

### 40-fft-4096 — FFT（4096 点、複素 f64、@get_hypercube_neighbor）

周波数間引き型、基数 2 の 4096 点の FFT。各 PE が 1 点（実部と虚部）を持ち、12 段それぞれで、相手（PE の番号の 1 つのビットを反転した PE）の値を @get_hypercube_neighbor で受け取る。下側の PE は x + y、上側の PE は (y − x) · w を計算し、w の表はホストが @distribute で配る。出力はビット反転順。入力は引数のファイル（実際の波形も使える）で渡し、期待値は Python（numpy の FFT）で作って、ノルム誤差で比べる（基準は README.md の「許容誤差」）。8 点の試作が `proto-8pt/` にある。

## .param 形式

`.param` の仕様は [`param-spec.md`](../../vsmlink/param-spec.md) を参照。
