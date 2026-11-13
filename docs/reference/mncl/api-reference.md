# mncl-kit API Reference (0.3.4)

MNCL コンパイラ kit が OpenCL C カーネルから呼び出せる API の一覧。

## 概要

`mncl-kit` は OpenCL C (`.cl`) を MN-Core 2 用の `_vsm` に変換する clang を提供する。
カーネル内では通常の OpenCL C 構文に加え、以下の MNCL 拡張 API が使える。

これらは `include/mncl/opencl-c.h` で宣言されている。`-include <PREFIX>/include/mncl/opencl-c.h`
で先読みすると、ユーザコードからそのまま呼び出せる。

## データ転送 API

**PDM を指すポインタの引数の決まり**: `distribute` / `collect`（`distribute2`〜`4`、`collect2`〜`4` を含む）のポインタの引数には、
kernel 引数そのもの（`distribute(x)`、`collect(out, v)`）だけを書く。`&x[k]` や `x + k` のような配列の途中は書かない。各 PE が
配列のどの要素を受け持つかは `.stparam` の `pe_shape` / `layout` で決まり、引数で始まりの位置を選ぶものではないため。
配列の途中（`&arr[k]`）を書けるのは `broadcast` だけ（呼ぶたびに続きのデータを読むので、どこから読み始めるかに意味がある）。

### `distribute` — PDM から PE 各レーンに値を配る

```c
double distribute(const __global double *p);
float  distribute(const __global float  *p);
long   distribute(const __global long   *p);
```

PDM 上の配列 `p` を全 PE に分配し、各 PE に対応する 1 要素を返す。
PE インデックスと配列インデックスの対応は `.stparam` の `pe_shape` / `layout` で決まる。

`float` 版は `u64` の上位 32-bit に格納される（下位 32-bit は未定義）。
`long` 版は 64-bit 整数をそのまま扱う。

### `collect` — PE 各レーンの値を PDM に集める

```c
void collect(__global double *p, double v);
void collect(__global float  *p, float  v);
void collect(__global long   *p, long   v);
```

各 PE の `v` を PDM 上の配列 `p` に書き戻す。`distribute` と対の操作で、
`pe_shape` / `layout` が一致している必要がある。

### `load_neighbor` / `get_neighbor` — 隣の PE の値を受け取る

```c
double load_neighbor(const __global double *p, int offset);
float  load_neighbor(const __global float  *p, int offset);
double get_neighbor(double v, int offset);
float  get_neighbor(float  v, int offset);
```

隣の PE の値を受け取る関数は 2 つある。違いは、読む相手が「変わらない配列の要素」か「いま変わりうる変数」かである。

- **`load_neighbor(p, offset)`**: `distribute(p)` で配った kernel 引数の配列 `p` の、隣の要素を読む。返る値は kernel の
  実行の間ずっと変わらない
- **`get_neighbor(v, offset)`**: 隣の PE の変数 `v` の、いまの値を受け取る。`v` は計算の途中の値でもよい。
  全 PE が同じ命令列を同時に実行するので、隣の PE が `v` を計算し終えるのを待つ必要は無い（1 つのチャンク、4096 個の中）

`offset` の決まり（両方に共通）:

| `offset` | 扱い |
|---|---|
| `-1`（左隣）、`+1`（右隣） | 隣の値を返す |
| `0` | 警告を出し、自分自身の値を返す（`load_neighbor(p, 0)` は `distribute(p)` の値、`get_neighbor(v, 0)` は `v`） |
| ±2 以上の定数 | コンパイルの誤り（いまはサポートしていない。将来サポートするかもしれない） |
| 実行するまで値が決まらないもの | コンパイルの誤り（コンパイルの時点で決まる定数でなければならない） |

データの端の扱いは `.stparam` の `:boundary` で決まる（`:clamp` なら自分自身の値、`:periodic` なら反対の端の値）。
扱えるのは 1 次元だけ。

**`load_neighbor` の決まり**（外れるとコンパイルの誤り）:

- **第 1 引数 `p` には kernel 引数そのものだけを書く。** `p + 1`、`&p[k]`、kernel 引数でないポインタは書けない。
  `p` は「どの配列の隣を読むか」を指すもので、値や位置を渡すものではないため
- **同じ kernel の中で、`load_neighbor` より前に `distribute(p)` を呼ぶ。** `load_neighbor` は `distribute` と組で使う。
  `broadcast` や `distribute4_aos2soa` だけで配った配列には使えない
- **`load_neighbor` で読む配列に、同じ kernel の実行の中で書き戻さない**（`collect(p, v)` など）。書き戻すと、隣として読む
  値が PDM の値と食い違うことがある。コンパイラは検査しない

**両方に共通の決まり**:

- **`if` の中では使えない**（コンパイルの誤り。いまはサポートしていない。将来サポートするかもしれない）
- `.param` に、境界のフラグ（`:boundary_flags bf0`）と、隣の値の受け渡しに使う PDM の作業領域（`:buffer nbuf0`、
  `:buf_size 4160`）を書く。どちらも kernel ごとに 1 つで、`load_neighbor` / `get_neighbor` をいくつ呼んでも共通。
  例は `share/examples/mncl/05-stencil1d/stencil1d.param`
- ホストは exec の前に、境界のフラグのデータを `:boundary_flags` の番地に送る（例: `05-stencil1d/test_stencil1d.c`）

### `broadcast` — host の値を全 PE に同じ値として配る

```c
double  broadcast(const __global double *p);
long    broadcast(const __global long   *p);

double2 broadcast(const __global double2 *p);
double3 broadcast(const __global double3 *p);
double4 broadcast(const __global double4 *p);
```

`distribute` が「PE ごとに異なる 1 要素」を配るのに対し、`broadcast` は「全 PE に同じ値」を配る。
`distribute2`/`distribute3`/`distribute4`（後述）はポインタを `double *` のまま保つが、
**`broadcast` は引数の型（`double2`/`double3`/`double4`）そのものでまとめて配る個数が決まる**。
これは全 PE に同じ値を配るだけなので、`distribute` 系が抱える並びの制約（後述）が起きないため。
1 回の呼び出しで各 PE に届く量は、`double` が 1 u64、`double2` が 2 u64、`double3` と `double4` が
4 u64（`double3` は配列の中で 4 u64 間隔に並ぶ。有効なのは先頭 3 要素）。

**同じ引数で繰り返し呼ぶと、呼ぶたびに続きのデータを読む。** 1 回目は引数が指す領域の
先頭、2 回目はその次、と読み進める。`#pragma unroll` のループで呼べば、配列を順に全 PE に配れる。
kernel を実行するたびに、先頭から読み直す。

```c
double s = 0.0;
#pragma unroll
for (int k = 0; k < 80; k++)
    s = s + broadcast(xs);      // k 回目は xs[k]
```

**配列の途中から読み始めるときは `broadcast(&arr[k])`（k はコンパイル時の定数）と書く。** `arr` の
k 番目から始まり、そこから呼ぶたびに続きを読む。k が実行時の値だとコンパイルエラーになる（読む番地をコンパイル時に
決める必要があるため）。

同じ引数を `distribute` と `broadcast` の両方に使ってよい（同じ配列を、各 PE が自分の要素として受け取り、
全 PE に順に配ることもできる）。

**`.param` の書き方**: 引数そのものへの `broadcast` は、その引数の slot を使う。`broadcast(&arr[k])` には
新しい slot が割り当てられる。番号は、モジュール内の全 kernel の引数の slot の後ろに、kernel の宣言順・
kernel 内の出現順に 8 刻みで振られる（同じ kernel の中で同じ `&arr[k]` は同じ slot）。その slot には
`:addr` を書かず、`:src-slot`（`arr` の slot）と `:addr-offset`（`arr` の先頭からのずれ。u64 単位）を
書く（vsmlink 0.10.2-rc2 から）。ホストは、読ませる値を引数が指す PDM の領域に並べておく。

使用例: `15-broadcast`（scalar）、`16-broadcast-f64`（1 kernel に 2 本の broadcast）、
`18-broadcast-vec`（`double3`）。

### `distribute2`/`distribute3`/`distribute4`、`collect2`/`collect3`/`collect4` — 複数個をまとめて配る・集める

```c
double2 distribute2(const __global double *p);
double3 distribute3(const __global double *p);
double4 distribute4(const __global double *p);

void collect2(__global double *p, double2 v);
void collect3(__global double *p, double3 v);
void collect4(__global double *p, double4 v);
```

`distribute`/`collect` の複数個版（要 vsmlink-kit 0.9.0 以降）。**ポインタは `distribute2` 等でも
`double *` のままであり、`double2 *` 等にはならない。** PDM 上の並びが OpenCL の `doubleN` 配列
とは異なり（成分が外側、PE が内側: `[成分0を4096 PE分][成分1]...`）、ハードウェアの制約で
選べないため、型に並びを主張させないという設計。

PE に届いた値そのものは `double2`/`double3`/`double4` であり、ベクタのまま演算できる
（加減乗算・`rsqrt`・スカラとの掛け算に対応）。

```c
double4 v = distribute4(x);
double4 r = rsqrt(v * v);   // ベクタのまま演算
collect4(out, r);
```

使用例: `17-nbody-2d`（`double2`）、`19-nbody-3d`（`double3`、時間積分含む）。

ベクタは部品から組み立てられる（0.3.4 から）。`(double4)(p, m)`（`double3` と `double` から）、`(double4)(a, b, c, d)`、
`(double2)(x, y)` のような書き方と、成分ごとに違う定数のベクタ（`(double4)(1.0, 1.0, 1.0, 0.0)`）が使える。

```c
double4 pm = distribute4_aos2soa(_pm);        // x, y, z, m
double3 pos = pm.xyz + DT * vel;             // vel は double3、DT は定数（N 体の積分）
collect4_soa2aos(_out, (double4)(pos, pm.w));  // 質量を 4 つ目に戻して書く
```

### `distribute4_aos2soa`、`collect4_soa2aos` — 番号の順の並びのまま配る・集める（転置つき）

```c
double4 distribute4_aos2soa(const __global double4 *p);
void    collect4_soa2aos(__global double4 *p, double4 v);
```

PDM の上でデータ 1 個（4 個の値）ずつ、番号の順に並んだもの（`double4` の配列と同じ並び）を、そのまま配る・集める
（要 vsmlink-kit 0.10.2 以降。vsmlink の `@distribute_aos2soa` / `@collect_soa2aos`）。vsmlink が `distribute4` /
`collect4` と同じ転送に、MAB 内の 4x4 転置（`dmwrite` / `dmread`）を足して並びを変える。**ポインタは `double4 *` である**
（`distribute4` / `collect4` の `double *` と違う。PDM の並びが `double4` の配列と同じため）。

- **対で使うと元に戻る。** `distribute4_aos2soa` で配った値を、PE を変えずに（計算の有無にかかわらず）`collect4_soa2aos`
  で書くと、PDM の並びは配る前と同じになる。先頭 N 個に置いたデータは先頭 N 個に戻るので、使うデータが 4096 個より
  少なくても、`broadcast` などの読む側は先頭から続けて読める
- **データと PE の対応が変わる。** `distribute4_aos2soa` を通すと、番号 k のデータは PE `4 * (k % 1024) + k / 1024` に入る
  （PE k ではない）。`get_global_id` は番号 k ではない
- **`load_neighbor` と組み合わせられない**（隣の PE が隣の番号のデータを持たないため）。組み合わせるとコンパイルの誤りになる
- 転置の無い `distribute4` / `collect4` と同じ kernel で使ってよい。片方は番号の順、片方は PE の順で PDM に並ぶ
  （たとえば `distribute4_aos2soa` で配って `collect4` で書くと、成分 c の PE j の値が `p[4096 * c + j]` に入る）
- 値の数は常に 4。PDM は引数ごとに 16384 u64 を使う
- ポインタの引数は kernel 引数そのものだけ（上の「PDM を指すポインタの引数の決まり」と同じ）。配列の途中を書くとコンパイルの誤りになる
- 名前の `4` は値の数である（32 ビットの値を 8 x 8 で転置する命令もあるので、名前で区別する）

```c
double4 v = distribute4_aos2soa(p);   // p は double4 の配列（番号の順）
double4 w = v + v;
collect4_soa2aos(out, w);             // out も番号の順
```

使用例は、kit 同梱の examples にはまだ無い。

## 実行時 ID API

### `get_global_id` — PE のグローバル ID を返す

```c
long get_global_id(uint dim);
```

各 PE のグローバル ID（0..要素数-1）を `long`（64-bit 整数）で返す。標準 OpenCL の `get_global_id` だが、
MN-Core2 は 64-bit 整数がネイティブなので `long` を返す。host が flag を配らずに、PE 自身が役割
（例: ペアの左メンバか）を `((id & 1) == 0)` のように計算するのに使える。

**host 側の準備が要る**: vsmlink の `@identify` で PE 番号を配るため、host は ID 配列 `[0..要素数-1]` を
`.param` の `:identify` が指すアドレスへ事前送信する必要がある（送らないと全 PE が 0 になる）。
使用例は `13-odd-even-sort-gid` / `14-odd-even-sort-full`。

## 縮約 API

### `reduce_add` — 全 PE で加算縮約して PDM に書く

```c
void reduce_add(__global double *out, double v);
```

全 PE の `v` を加算し、PDM 上の `out` に書き込む。`out` は 4 要素の配列で、
PE position 別の部分和が 4 つ書かれる（PE i は group `i % 4` に属する）。
最終合計はホスト側で 4 要素を加算する。

### `reduce_max` — 全 PE で最大値縮約

```c
void reduce_max(__global double *out, double v);
```

`reduce_add` と同じ I/F で、加算の代わりに最大値を取る。

### サポート状況一覧

MNCL の縮約 builtin と、 内部で使用する vsmlink `@reduce` の operator の対応:

| MNCL builtin | 型 | vsmlink operator | 状態 |
|---|---|---|---|
| `reduce_add(double*, double)` | f64 加算 | `dfadd` | ✅ |
| `reduce_max(double*, double)` | f64 最大 | `dmax` | ✅ |
| (float 版 reduce_add) | f32 加算 | `ffadd` | 未提供 |
| (float 版 reduce_max) | f32 最大 | `fmax` | 未提供 |
| (reduce_min 系) | f64/f32 最小 | `dmin` / `fmin` | 未提供 |
| (整数 reduce 系) | i16/i32/i64 各種 | `siadd` / `iiadd` / `liadd` 等 | 未提供 |

vsmlink 自身は計 24 種の `@reduce` operator を持つが、 MNCL builtin として公開しているのは
上記の `reduce_add` / `reduce_max` のみ。 他は必要時に追加する。

## clang の呼び出し例

```bash
clang -cc1 -O1 \
    -triple=mncore2-unknown-unknown -target-cpu mncore2 \
    -cl-std=CL2.0 \
    -fno-builtin \
    -include $SDK_ROOT/include/mncl/opencl-c.h \
    -S kernel.cl -o kernel._vsm
```

詳細な使い方は `share/examples/mncl/` を参照。

## 既知の制限

- `fdiv` の精度は `drsqrt` 5 bit による近似（最大 5.6% 誤差）。高精度が必要な場合は Newton-Raphson を上位で実装する
- f64 のスカラの即値で下位 32-bit が非ゼロの場合は未対応（ベクタの定数は 0.3.4 から書ける）
- `distribute` / `collect` のポインタの引数に配列の途中（`&x[k]` など）を書いても、いまのコンパイラは誤りを出さず、誤った出力を作る（上の「PDM を指すポインタの引数の決まり」に反する書き方。誤りで止めるようにする予定）
- 制御フロー: `if/else` / `select` 対応済み。`for`/`while` のループは limited（ヘッダ展開が必要なケースあり）

## 変更履歴

- **0.3.4**:
  - 転置つきの `distribute4_aos2soa` / `collect4_soa2aos` を追加（PDM の上の番号の順の並び（`double4` の配列）のまま配る・集める。要 vsmlink-kit 0.10.2 以降）
  - `broadcast` を同じ引数で繰り返し呼ぶと、呼ぶたびに続きのデータを読む（`#pragma unroll` のループで配列を順に全 PE に配れる）。`broadcast(&arr[k])`（k はコンパイル時の定数）で配列の途中から読み始められる。要 vsmlink-kit 0.10.2 以降
  - `double3` の `broadcast` は 1 回で 4 u64 を読み進める（`double3` は配列の中で 4 u64 間隔に並ぶため）
  - ベクタ（`double2` / `double3` / `double4`）の成分を、分岐をまたいで取り出すとコンパイラが異常終了する不具合を修正
  - **`neighbor` を廃止し、`load_neighbor` と `get_neighbor` の 2 つにした。** `neighbor(p, offset)` は `load_neighbor(p, offset)` と書き換える（意味は同じ）。`get_neighbor(v, offset)` は、計算の途中の値の隣を受け取る
  - `load_neighbor` / `get_neighbor` の使い方の誤り（第 1 引数が kernel 引数そのものでない、`distribute` と組になっていない、`offset` が -1 / +1 / 0 以外や定数でない、`if` の中で呼ぶ）を、誤った出力を作らずにコンパイルの誤りで止める
  - ベクタを部品から組み立てる書き方（`(double4)(double3, double)` など）を追加。以前はコンパイラが異常終了した
  - 成分ごとに違う 0 でない定数のベクタ（`(double4)(1.0, 1.0, 1.0, 0.0)` など）が誤った命令になる不具合を修正。下位 32-bit が 0 でない定数も書ける
  - レジスタの間の値のコピー（分岐の中の値の受け渡しなど）で、誤った命令を出すことがある不具合を修正。コンパイラが対応していない形は、誤ったコードを出さずにエラーで止まるようにした
  - 使用例の `.param` を、vsmlink-kit 0.10.2 の待ちのタグの規則に合わせた
  - i64（`long`）の定数を 2 つ以上使うと、2 つ目以降が誤った値になる不具合を修正
  - 1 つの kernel で多くの値を同時に扱うと（たとえば N 体の粒子を組に分けて受け持つ形）、コンパイルが「spill が未対応の register class」で止まる不具合を修正（命令の並べ替えの方式を変えた）
  - 4 成分とも 0 のベクタの定数が、MN-Core2 に無い命令になっていた不具合を修正
  - 使用例の補助のスクリプトを追加: `scripts/`（N 体の初期条件の作成、結果の位置を動画にする）
- **0.3.3**:
  - `broadcast` の複数個対応: `double2` / `double3` / `double4` を 1 回で全 PE に配る
  - `distribute` / `collect` の複数個対応: `distribute2` / `distribute3` / `distribute4`、`collect2` / `collect3` / `collect4`（1 回で複数個を配る・回収する。要 vsmlink-kit 0.9.0 以降）
  - `double2` / `double4` のベクタ演算に対応: 加算・乗算・減算・`rsqrt`、およびスカラとの掛け算
  - 使用例を追加: `15-broadcast`、`16-broadcast-f64`、`17-nbody-2d`（2D 重力多体を double2 で計算）、`18-broadcast-vec`
  - `_vsm` ヘッダに生成元（clang バージョン・ビルドコミット・ビルド日）を記録
  - i64（`long`）整数の基本サポート: `distribute` / `collect` の `long` オーバーロード、加算・論理積、`== 0` / `!= 0` の等値比較
  - `get_global_id` が `long`（64-bit 整数）を返すよう変更（host が flag を配らなくても PE 自身が役割を判定できる）
  - 64-bit ポインタに対応（DataLayout を p:64:64 に）
  - 使用例を追加: `12-vecadd-i64`（i64 加算）、`13-odd-even-sort-gid`（get_global_id で左メンバ判定）、`14-odd-even-sort-full`（host ループで奇偶転置ソートを完全ソートまで）
  - f64 の条件分岐（`if`/`else`）と比較の不具合を修正（一部の比較で誤った結果を返す問題を解消）
  - f32 加算のレジスタ割り当てを改善（余分なコピーを削減）
- **0.3.2**:
  - f32 全演算（fadd/fsub/fmul/fneg/fdiv）対応
  - `distribute` / `collect` / `neighbor` の `float` オーバーロード追加
  - `reduce_add` / `reduce_max` 追加（全段縮約、PDM 出力）
  - GRF1 を MAU 演算の入力に使うレジスタ割り当てに対応（性能向上）
- **0.3.1**: `@get_neighbor` 直接出力、fix-stencil 後処理を廃止
- **0.3.0**: SDK v1（vsmlink 0.7.0）対応版
