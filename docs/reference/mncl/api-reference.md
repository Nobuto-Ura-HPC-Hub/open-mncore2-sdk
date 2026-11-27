# mncl-kit API Reference (0.3.5)

MNCL コンパイラ kit で、OpenCL C のカーネルに書ける MNCL 拡張 API と `persistent` 修飾子の説明。

## 使い方の流れ

MN-Core 2 で 1 つの kernel を動かすには、次の 3 つのファイルを用意する。

| ファイル | 中身 | 使う道具 |
|---|---|---|
| `.cl` | kernel（OpenCL C）。全 PE が実行する計算 | clang（この kit）が `_vsm` にする |
| `.param` | kernel 引数の配列を PDM のどこに置くか、転送の完了を待つタグ | vsmlink が `_vsm` と合わせて `.vsm` にし、assemble3 が `.idma.dat`（kernel の実行形式）にする |
| ホストの C のプログラム | データを PDM に送り、kernel を読み込んで実行し、結果を受け取る | libmnc2 を使い、gcc でコンパイルする |

```
kernel.cl ──clang──> kernel._vsm ──vsmlink（kernel.param と）──> kernel.vsm ──assemble3──> kernel.idma.dat
host.c ──gcc（libmnc2）──> ホストのプログラム（kernel.idma.dat を読み込んで実行する）
```

- 最初に見る example は `share/examples/mncl/01-vecadd`（3 つのファイルがそろった最小の例。`README.md` に build と実行の手順がある）
- ホストのプログラムの書き方（libmnc2）: `share/mnc2/`（`usage.md`、`api-reference.md`）
- `.param` の書き方: `share/vsmlink/param-spec.md`

## 概要

`mncl-kit` は OpenCL C (`.cl`) を MN-Core 2 用の `_vsm` に変換する clang を提供する。
カーネル内では通常の OpenCL C 構文に加え、MNCL 拡張 API と `persistent` 修飾子が使える。

**MNCL 拡張 API**: 関数の形で書くが、MNCL が vsmlink の directive に変換するもの。ふつうの関数のように
呼び出されるわけではない（その意味で疑似関数である）。この文書の「データ転送 API」「実行時 ID API」「縮約 API」に
挙げたものがこれにあたる。宣言は `include/mncl/opencl-c.h` にあり、`-include <PREFIX>/include/mncl/opencl-c.h`
で先読みすると、ユーザコードにそのまま書ける。

**`persistent` 修飾子**: ファイルスコープの変数に付けると、その値が kernel の実行をまたいで残る。関数ではないので
MNCL 拡張 API には含めない。説明は「`persistent` 修飾子」の節。

**`.param` には `:reserved_wait_tag`（または `:reserved_wait_tags`）が必須。** vsmlink が kernel の中の転送の完了を待つのに使う
タグである。vsmlink-kit 0.10.3 から、省くと link がエラー（E128）で止まる（0.10.2 までは警告 W001 を出して続けていた）。
`.param` の書き方は vsmlink の `share/vsmlink/param-spec.md`。この kit の example の `.param` は、すべて指定している。

## MNCL 拡張 API と vsmlink の directive の対応

MNCL 拡張 API は、vsmlink の directive になる（directive の仕様は vsmlink の `share/vsmlink/directives-spec.md`、`.param` の書き方は `param-spec.md`）。

| MNCL 拡張 API | vsmlink の directive | `.param` に書くもの（slot のほか） |
|---|---|---|
| `distribute`、`distribute2`〜`4` | `@distribute`（`size` が値の数） | |
| `collect`、`collect2`〜`4` | `@collect` | |
| `distribute4_aos2soa`、`collect4_soa2aos` | `@distribute_aos2soa`、`@collect_soa2aos` | |
| `broadcast` | `@broadcast` | `broadcast(&arr[k])` は `:src-slot` と `:addr-offset` の slot |
| `load_neighbor`、`get_neighbor` と、その `_periodic`・`_boundary` | `@get_neighbor`（末尾に端の指定）。関数の始めに `@boundary_flags` が 1 回出る | `:boundary_flags`、`:buffer` |
| `get_hypercube_neighbor` | `@get_hypercube_neighbor` | `:identify`、`deep` 9〜11 は `:buffer` |
| `get_eoshift` と、その `_periodic`・`_boundary` | `@get_eoshift`（組の中に端の指定） | `M` が 10 以上は `:buffer` |
| `get_global_id` | `@identify` | `:identify` |
| `reduce_add`、`reduce_max` | `@reduce`（operator は `dfadd`、`dmax`） | |
| `emit_from` | `@emit_from` | |
| 1 つの `.cl` に複数の kernel | `@kernel_begin` / `@kernel_end` | 1 つの `.param` をすべての kernel に使う |

どの kernel でも、`.param` に `:reserved_wait_tag`（または `:reserved_wait_tags`）が要る（上）。

## kernel 引数

- **ポインタの引数**（`__global double *` など）: PDM の上の配列を指す。値は下の「データ転送 API」（`distribute`、`broadcast` など）で受け取り、
  `collect` などで返す。`.param` に、その slot の置き場所（`:place`、`:addr`）を書く
- **ポインタでない引数**（`int`、`long`、`float`、`double`）は、宣言はできるが、**kernel の中で値として使えない**（ホストからスカラの値を PE に渡す仕組みが無い）。
  - 式や `if` の条件に使うと、次のコンパイルの誤りで止まる:
    `MNCore2: ポインタでない kernel 引数 '<名前>'（<型>）は、kernel の中で値として使えない。値はホストが PDM に置き、broadcast で受け取る`
  - 宣言だけして使わないときは、コンパイルも link も通る。`.param` に、その slot を書かなくてよい
  - kernel に値を渡すときは、ホストが PDM に置き、`broadcast` で全 PE に配る（例 `15-broadcast`）
- **slot の番号**: kernel 引数は、ポインタでない引数も含めて、宣言の順に 8、16、24、… の番号を使う（`k(x, int n, out)` なら `x` が 8、`n` が 16、`out` が 24）。
  `.param` の `:slot` は、この番号で書く。1 つの `.cl` に kernel が複数あるときの番号は「`persistent` 修飾子」の節

## データ転送 API

**PDM を指すポインタの引数の決まり**: `distribute` / `collect`（`distribute2`〜`4`、`collect2`〜`4` を含む）のポインタの引数には、
kernel 引数そのもの（`distribute(x)`、`collect(out, v)`）だけを書く。`&x[k]` や `x + k` のような配列の途中は書けない（書くとコンパイルの誤りになる）。各 PE が
配列のどの要素を受け持つかは、vsmlink が決める対応（vsmlink の `share/vsmlink/data-to-pe-mapping.md`）で決まり、引数で始まりの位置を選ぶものではないため。
配列の途中（`&arr[k]`）を書けるのは `broadcast` だけ（呼ぶたびに続きのデータを読むので、どこから読み始めるかに意味がある）。

**PDM（`__global` のポインタの先）は、kernel の中から直接は読み書きできない。** `out[i] = v` や `v = in[i]` のような書き方、
`__global` のポインタの先への atomic な操作はできず、コンパイルの誤りになる。値は `distribute`（`distribute2`〜`4`）か `broadcast` で
受け取り、結果は `collect`（`collect2`〜`4`）か `reduce_add` / `reduce_max` で返す。

### `distribute` — PDM から PE 各レーンに値を配る

```c
double distribute(const __global double *p);
float  distribute(const __global float  *p);
long   distribute(const __global long   *p);
```

PDM 上の配列 `p` を全 PE に分配し、各 PE に対応する 1 要素を返す。
PE インデックスと配列インデックスの対応は、vsmlink の `share/vsmlink/data-to-pe-mapping.md` で決まる（要素 k は PE の番号 k の PE に入る）。

`float` 版は `u64` の上位 32-bit に格納される（下位 32-bit は未定義）。
`long` 版は 64-bit 整数をそのまま扱う。

### `collect` — PE 各レーンの値を PDM に集める

```c
void collect(__global double *p, double v);
void collect(__global float  *p, float  v);
void collect(__global long   *p, long   v);
```

各 PE の `v` を PDM 上の配列 `p` に書き戻す。`distribute` と対の操作で、`distribute` と同じ対応で書き戻す。

### `load_neighbor` / `get_neighbor` — 隣の PE の値を受け取る

```c
double load_neighbor(const __global double *p, int offset);
float  load_neighbor(const __global float  *p, int offset);
double get_neighbor(double v, int offset);
float  get_neighbor(float  v, int offset);

// 端の扱いを選ぶ版（0.3.5。下の「端の PE の値」）
double load_neighbor_periodic(const __global double *p, int offset);
float  load_neighbor_periodic(const __global float  *p, int offset);
double load_neighbor_boundary(const __global double *p, int offset, double b);
float  load_neighbor_boundary(const __global float  *p, int offset, float  b);
double get_neighbor_periodic(double v, int offset);
float  get_neighbor_periodic(float  v, int offset);
double get_neighbor_boundary(double v, int offset, double b);
float  get_neighbor_boundary(float  v, int offset, float  b);
```

隣の PE の値を受け取る MNCL 拡張 API は 2 つある。違いは、読む相手が「変わらない配列の要素」か「いま変わりうる変数」かである。

- **`load_neighbor(p, offset)`**: `distribute(p)` で配った kernel 引数の配列 `p` の、隣の要素を読む。返る値は kernel の
  実行の間ずっと変わらない
- **`get_neighbor(v, offset)`**: 隣の PE の変数 `v` の、いまの値を受け取る。`v` は計算の途中の値でもよい。
  全 PE が同じ命令列を同時に実行するので、隣の PE が `v` を計算し終えるのを待つ必要は無い（1 つのチャンクの中。チャンクは、1 回の kernel の実行で扱う 4096 個（1 チップの PE の数）の 1 組）

`offset` の決まり（両方に共通）:

| `offset` | 扱い |
|---|---|
| `-1`（左隣）、`+1`（右隣） | 隣の値を返す |
| `0` | 警告を出し、自分自身の値を返す（`load_neighbor(p, 0)` は `distribute(p)` の値、`get_neighbor(v, 0)` は `v`） |
| ±2 以上の定数 | コンパイルの誤り（いまはサポートしていない。将来サポートするかもしれない） |
| 実行するまで値が決まらないもの | コンパイルの誤り（コンパイルの時点で決まる定数でなければならない） |

扱えるのは 1 次元だけ。

**端の PE の値は、関数で選ぶ**（0.3.5。要 vsmlink-kit 0.10.3 以降）。端の PE は、区切り（4096 個、1 チップ）の外から値を受け取ることになる PE で、
`offset` が `-1` なら PE の番号 0、`+1` なら PE の番号 4095 の PE である。データの端かどうかは見ない。

| 関数 | 端の PE が受け取る値 | vsmlink の端の指定 |
|---|---|---|
| `load_neighbor` / `get_neighbor`（既定） | 自分の値（clamp） | `:reg <src_reg>` |
| `load_neighbor_periodic` / `get_neighbor_periodic` | 反対の端の PE の値（周期境界） | `:periodic` |
| `load_neighbor_boundary` / `get_neighbor_boundary` | 端の PE 自身の `b` の値 | `:reg <b のレジスタ>` |

- 端でない PE が受け取る値は、どの関数でも同じ
- 固定の境界値は `get_neighbor_boundary(v, -1, 0.0)` のように書く。端の PE に、代入先の変数の元の値を残すときは `y = get_neighbor_boundary(v, +1, y);` と書く
- 端の値が不定でよい場合（命令が増えない）の関数は、いまは無い

**4096 個を超えるデータ。** データを 4096 個を超えてチャンクに分けて kernel を実行するとき、チャンクの端の PE（チャンクの最初と最後の要素を受け持つ PE）が
受け取るのは、隣のチャンクの値ではなく、上の表の端の扱いで決まる値である。エラーにはならない。隣のチャンクの値が要るときは、その値を別に用意して
`_boundary` の `b` で渡す。

**`load_neighbor` の決まり**（外れるとコンパイルの誤り。`load_neighbor_periodic` / `load_neighbor_boundary` も同じ）:

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

### `get_hypercube_neighbor` — 超立方体の隣の PE の値を受け取る

```c
double get_hypercube_neighbor(double v, long id, int deep);
float  get_hypercube_neighbor(float  v, long id, int deep);
```

PE の番号 j の PE が、番号 `j XOR 2^deep` の PE の `v` のいまの値を受け取る。バイトニックソートの比較・交換の相手を取る用途に使う。

- **`id`**: 自分の PE の番号 j。`get_global_id(0)` の値を渡す。`id` と `id + 4096` のように、要素の番号でそろえて書ける。
  相手の選択に使うのは `deep` のビットだけで、`id` が j でないときの結果は保証されない（検査もされない）
- **`deep`**: 0〜11 の定数。範囲の外、実行するまで値が決まらないものはコンパイルの誤り
  - deep 0〜8 は PDM を経由せずに受け渡す。**deep 9〜11 は PDM を経由する**ので、deep 0〜8 より遅く、`.param` に作業領域
    （`:buffer nbuf0`、4096 u64 以上。`load_neighbor` / `get_neighbor` と同じ `nbuf0` を使える）が要る
- **`if` の中では使えない**（コンパイルの誤り）。相手は PE の番号で決まる（`j XOR 2^deep`）ので、`if` の中で使うものではない
- **4096 個（1 チップ、1 次元）だけを扱う。** 全体の要素数は 2^N でなければならない
- **`.param`** に `:identify`（`get_global_id` と同じ。ホストが PE の番号 `[0..4095]` を `:addr` へ exec の前に送る）、
  `:reserved_wait_tag`、deep 9〜11 を使うなら `:buffer nbuf0` を書く。境界のフラグ（`:boundary_flags`）は要らない
  （端が無いため）。例は `share/examples/mncl/22-hypercube-neighbor/`
- 相手は区切りの中に必ずあるので、端の扱いを選ぶ関数は無い

### `get_eoshift` — PE の並びの上で、位置の差が 2^N の PE の値を受け取る

```c
double get_eoshift(double v, int dir, int N, int M);
float  get_eoshift(float  v, int dir, int N, int M);

// 端の扱いを選ぶ版（0.3.5。下の「受け取らない PE」）
double get_eoshift_periodic(double v, int dir, int N, int M);
float  get_eoshift_periodic(float  v, int dir, int N, int M);
double get_eoshift_boundary(double v, int dir, int N, int M, double b);
float  get_eoshift_boundary(float  v, int dir, int N, int M, float  b);
```

PE の番号 j の PE が、`dir` が `-1` なら番号 `j - 2^N` の PE の、`+1` なら番号 `j + 2^N` の PE の `v` のいまの値を受け取る。
2 次元の格子の隣の値を取る用途に使う。**ただし、区切り（2^M 個）をまたぐ受け取りはしない。**
位置 j の区切りの番号は `j / 2^M`（切り捨て）で、相手が同じ区切りにある PE だけが受け取る。

- **区切りの外から受け取ることになる PE（端の PE）の値は、関数で選ぶ**（0.3.5。要 vsmlink-kit 0.10.3 以降）。端の PE は、`dir` が `-1` なら区切りの先頭の 2^N 個、
  `+1` なら区切りの末尾の 2^N 個の PE である
  - `get_eoshift`（既定）: 自分の値（clamp。vsmlink の `:reg <src_reg>`）
  - `get_eoshift_periodic`: 同じ区切りの反対の端の PE の値（`dir` が `-1` なら `j + 2^M - 2^N`、`+1` なら `j - 2^M + 2^N` の PE。vsmlink の `:periodic`）
  - `get_eoshift_boundary`: 端の PE 自身の `b` の値（vsmlink の `:reg <b のレジスタ>`）
  - 使用例は `share/examples/mncl/31-heat-diffusion-2d/`（2 次元の熱拡散。`get_eoshift_boundary` で格子の外側を温度 `b` に固定し、外周の PE かどうかを判定せずに全点を更新する）
- **2 次元・3 次元の対応は、この関数の外の約束である。** 関数は PE の番号の差だけを見る。たとえば 32 × 128 の格子で `j = y * 32 + x` と置くと、
  x の ±1 は `N = 0`、`M = 5`（区切りは 1 行 32 点）、y の ±1 は `N = 5`、`M = 12`（区切りは 4096 点全体）になる
- **`dir`、`N`、`M`**: 定数。`dir` は `-1` か `+1`、`N` は 0〜5、`M` は `N + 1`〜12。範囲の外、実行するまで値が決まらないものはコンパイルの誤り
- **`M` が 10 以上の組は PDM を経由する**ので遅く、`.param` に作業領域（`:buffer nbuf0`、576 u64 以上）と `:reserved_wait_tag` が要る
- 計算の途中の値も渡せる。全 PE が同じ命令を同時に実行するので、1 回の kernel の実行の中で何ステップも進められる
- **`if` の中では使えない**（コンパイルの誤り）。端の PE の値は、上の `get_eoshift_boundary` / `get_eoshift_periodic` で選ぶ
- **4096 個（1 チップ）だけを扱う。** 全体の要素数は 4096。4096 を超える通信はしない
- `.param` に `:identify`（端の判定に `get_global_id` を使うとき）、`:reserved_wait_tag` を書く

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
書く（vsmlink 0.10.2 から）。`:src-slot` と `:addr-offset` は番地の書き方で、待ちのタグとは関係しない。待ちのタグ（`:send_wait_tag` など）は
slot ごとに付け、`:src-slot` の slot には引き継がれない。その slot に待ちが要るなら、その slot に書く。ホストは、読ませる値を引数が指す PDM の領域に並べておく。

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
`collect4` と同じ転送の中で、4 個の値の並びを入れ替える。**ポインタは `double4 *` である**
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

### `float2` / `float8` — f32 を u64 に 2 つずつ詰めて配る・集める（0.3.5）

`float2` は u64 1 つに f32 を 2 つ、`float8` は u64 4 つに f32 を 8 つ置く。演算（`+`、`-`、`*`、単項の `-`、スカラとの積）は、
1 つの命令で u64 の上半分と下半分を別々に計算する。`float8` は 1 命令で 8 要素を計算する。`float`（スカラ）は u64 に 1 つしか置かないので、
多くの値を計算するときは `float2` / `float8` で書く（コンパイラが `float` を自動でまとめることはしない）。

```c
float2 distribute(const __global float2 *p);      // PE あたり u64 1 つ
void   collect(__global float2 *p, float2 v);
float2 broadcast(const __global float2 *p);       // 全 PE に同じ値
float2 get_eoshift(float2 v, int dir, int N, int M);
float2 get_eoshift_periodic(float2 v, int dir, int N, int M);
float2 get_eoshift_boundary(float2 v, int dir, int N, int M, float2 b);
float2 get_neighbor(float2 v, int offset);
float2 get_neighbor_periodic(float2 v, int offset);
float2 get_neighbor_boundary(float2 v, int offset, float2 b);
float8 distribute4(const __global float2 *p);     // PE あたり u64 4 つ
void   collect4(__global float2 *p, float8 v);
```

**u64 の中の並び**: `float2` の `.x` は u64 の**上位** 32 ビット、`.y` は**下位** 32 ビット。`float8` の要素 2c は u64 c の上位、2c + 1 は下位。
**ホストの C の `float2`（`cl_float2`、`float[2]`）をリトルエンディアンのまま送ると、`.x` と `.y` が入れ替わる。** ホストで次のように詰める。

```c
uint64_t w = ((uint64_t)bits(v.x) << 32) | bits(v.y);   // bits() は float のビットの並びを uint32_t で返す
```

`distribute4` / `collect4` の PDM の並びは `double` の `distribute4` と同じ（u64 c を全 PE 分並べたものを、c = 0〜3 の順に置く）。

- 要素の取り出し（`v.y`、`v.s5`）と組み立て（`(float2)(a, b)`、`(float8)(x, y, z, w)`（`float2` 4 つから））ができる。添字はコンパイルのときに決まる値だけ
- 値を選ぶ三項演算子は、条件が PE ごとの値（`long` や `float` のスカラの比較）のときだけ使える。`float2` どうしの要素ごとの比較で選ぶ形は、まだ使えない（コンパイルの誤り）
- `get_eoshift` / `get_neighbor` に `float8` は渡せない（`float2` 4 つに分けて渡す）

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

## `long`（64 ビット整数）で使える演算

`get_global_id` の値や、`distribute(long)` で配った値など、`long` / `ulong` の値に使える演算である（0.3.5 で `^`・`|`・`-`・シフトと、任意の値との `==` / `!=` を追加した）。

| 使える | 注意 |
|---|---|
| `+`、`-`、`&`、`\|`、`^` | |
| `<<`、`>>` | シフト量は 0〜63。`long` の `>>` は算術右シフト、`ulong` の `>>` は論理右シフト。シフト量は変数でもよい |
| `==`、`!=`（任意の値との比較） | **`if`・`select`・三項演算子の条件にだけ使える**（比較の結果を、整数や `bool` の値として使う形は使えない） |

| 使えない | 結果 |
|---|---|
| `*`、`/`、`%`、回転 | コンパイルの誤り（`MNCore2: 未対応の i64 演算: <名前>`） |
| `<`、`>`、`<=`、`>=`（順序比較） | コンパイルの誤り（`i64 比較は == / != のみ対応`） |
| 比較の結果を値として使う形（`(a == b)` を `long` や `bool` の値にする、`long` の値を比較で選ぶ三項演算子など） | コンパイルの誤り（`i64 の比較の結果を値として使う形は未対応`） |

**同時に生きる `long` の値が多いと、レジスタが足りなくなる。** `long` を置くレジスタ（XREG）は 32 本である（0.3.5。0.3.4 までは 8 本）。
足りないと、コンパイルの誤り（`同時に使う値が GRF0 / GRF1 / LM0 / LM1 に入りきらない（register class: XREG）`）で止まる（0.3.4 までは `spill が未対応の register class: XREG`）。値を使い回さず、使う直前に計算し直すと、同時に生きる数を減らせる。
例: バイトニックソートの `25-bitonic-sort-8192` は、20 本以上が要る。

## `mad` — f64 の積和

```c
double mad(double a, double b, double c);   // a * b + c
```

`a * b + c` を、MN-Core 2 の積和命令（`dvfmau` / `dvfmad` の 2 命令）で計算する（乗算と加算を別に書く形の 3 命令より少ない）。`double` のスカラだけで、
`float` とベクタの `mad` は使えない（コンパイルの誤りで止まる）。`mad(a, b, -c)`（`a*b - c`）も書ける。

**結果は、正確に丸めた積和（OpenCL の `fma()`）と一致しない場合がある。** MN-Core 2 の f64 の積和は、仮数部の下位 16 ビット同士の部分積を省略し、
補正項 2^-74 で置き換えるため（dev manual 4.3）。`fma()` は対応していない（コンパイルの誤りで止まる）。`a * b + c` と書いても `mad` にはならない
（乗算と加算のまま）。丸めの詳細、書き方への影響、MN-Core 2 の動きは **`share/mncl/mad.md`** を参照。

使用例: `26-fft-4096`（複素数の乗算。FFT）。

## `sin` / `cos` / `floor` — f64 の標準関数

```c
double sin(double x);
double cos(double x);
double floor(double x);
```

`double` のスカラだけで、`float` とベクタ（`double2` など）の版は使えない（コンパイルの誤りで止まる）。

- **精度**: `sin` と `cos` の最大相対誤差は、約 2.2e-16 以内（割り算 `/` と同じ目標。emu で、|x| が 1e6 までの 4096 点を `long double` の `sinl` / `cosl` と比べて、最大 1.9e-16）。
  積和 `mad` の丸めの性質（`share/mncl/mad.md`）を再現した計算でも、同じ値になる
- **引数の範囲**: **|x| < 約 1.6e6**（π/2 の約 2^20 倍）。範囲の外では、誤差が大きくなる（エラーにならない）
- `floor(x)` は、`-∞` 側への丸め（`dfloor`）
- 計算に使うレジスタは、コンパイラが割り当てる（`sin` または `cos` を 1 回呼ぶごとに、`_vsm` の行がおよそ 220 行増える。待ちの `nop` を含む）。**呼ぶ回数が多い kernel は、命令の数が増える**

使用例: `27-fft-4096-sincos`（FFT の w の表を、ホストではなく kernel の中の `sin` と `cos` で作る）。

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

縮約の MNCL 拡張 API と、 内部で使用する vsmlink `@reduce` の operator の対応:

| MNCL 拡張 API | 型 | vsmlink operator | 状態 |
|---|---|---|---|
| `reduce_add(double*, double)` | f64 加算 | `dfadd` | ✅ |
| `reduce_max(double*, double)` | f64 最大 | `dmax` | ✅ |
| (float 版 reduce_add) | f32 加算 | `ffadd` | 未提供 |
| (float 版 reduce_max) | f32 最大 | `fmax` | 未提供 |
| (reduce_min 系) | f64/f32 最小 | `dmin` / `fmin` | 未提供 |
| (整数 reduce 系) | i16/i32/i64 各種 | `siadd` / `iiadd` / `liadd` 等 | 未提供 |

vsmlink 自身は計 24 種の `@reduce` operator を持つが、 MNCL 拡張 API として公開しているのは
上記の `reduce_add` / `reduce_max` のみ。 他は必要時に追加する。

## `persistent` 修飾子 — kernel の実行をまたいで値を残す

```c
persistent double  acc;
persistent double4 pm;
persistent double  coef[64];
persistent float8  cf[8];
```

ファイルスコープの変数に `persistent` を付けると、kernel の実行が終わっても、その値が PE の中に残る。
次に実行する kernel は、前の実行が書いた値を読める。同じ kernel を繰り返し実行するときも、前回の実行が書いた値を読める。
正式な名前は `__persistent` で、`persistent` はその別名。

`persistent` は MNCL が OpenCL C に加えた修飾子で、clang が直接受け付ける。`opencl-c.h` には宣言が無い。

**決まり**:

- ファイルスコープの変数だけに付けられる。kernel の中の変数や、関数の仮引数には付けられない
- 型は `double` / `double2` / `double3` / `double4` と、`double`・`float2`・`float8` の 1 次元の配列（`persistent double coef[64];`、`persistent float8 cf[8];`）だけ。
  `float2`・`float8` は配列だけで、配列でない `persistent float2 x;` は書けない
- 初期化子を書けない。初期値は決まっていないので、読む前に、どれかの kernel で書いておく
- アドレスを取れない（`&acc` は書けない）。ポインタにもできない
- 宣言できる数と大きさには上限がある。超えると、原因を示すコンパイルの誤りで止まる
- 値として読むことには制限が無い（MNCL 拡張 API の引数にもそのまま書ける）

**配列**: `double`・`float2`・`float8` の 1 次元の配列を、`persistent` にできる。

- **添字は、コンパイルのときに決まる値だけ**（`coef[3]`、`#pragma unroll` で展開したループの `coef[k]`）。実行のときに決まる値の添字
  （`get_global_id` から計算した値や、展開されないループの変数）は、コンパイルの誤りで止まる。範囲の外の添字も、コンパイルの誤りで止まる
- 配列は、スカラの `persistent`（GRF0）とは別の場所（LM0 か LM1）に置く。1 つの配列は LM0 か LM1 のどちらかに収める。宣言の順に LM0 から詰め、入らなければ LM1 に置く。
  置ける大きさは u64 の数で、LM0 が 2047、LM1 が 2048（合わせて 4095。1 つの配列は 2048 まで）。`double` と `float2` の要素は u64 1 つ、`float8` の要素は u64 4 つ
  （`float8` の配列は、先頭を u64 4 つの境界に置く。そのために u64 が最大 3 つ空くことがある）
- 配列を大きくすると、kernel の中の計算に使える場所が、その分だけ減る

**値を共有する範囲**: 1 つの `.cl` に書いた kernel どうしで共有する。別の `.cl` の kernel とは共有しない。
値を共有する kernel は、1 つの `.cl` に書く。

**kernel を実行する順番**: どの kernel を先に実行するかはホストが決める。コンパイラは順番を調べないので、
値を書く kernel を、読む kernel より先に実行するようにホストを書く。

**ビルドと実行の流れ**（要 vsmlink-kit 0.10.2 以降）:

1. clang で `.cl` をコンパイルすると、すべての kernel を含む 1 つの `_vsm` が出る
2. vsmlink の出力先にディレクトリを渡すと、kernel ごとに `<_vsm の名前>-<kernel 名>.vsm` を書き出す
   （例: `persistent._vsm` に `init` と `accum` があれば、`persistent-init.vsm` と `persistent-accum.vsm`）
3. `.param` は 1 つを、すべての kernel に使う。kernel 引数の slot 番号は、kernel をまたいで重ならないように、
   kernel の宣言順に 8 刻みで振られる（例: `init(seed)` と `accum(in, out)` なら、`seed` が 8、`in` が 16、`out` が 24）。
   ポインタでない引数（`int`、`double`、`long`。「kernel 引数」の節）も、同じく宣言順に番号を使う（例: `init(seed, int n, double a)` と `accum(double b, in, long m, out)` なら、
   `seed` 8、`n` 16、`a` 24、`b` 32、`in` 40、`m` 48、`out` 56）
4. `.vsm` ごとに assemble3 を通し、ホストは `mnc2_load_kernel` / `mnc2_exec_kernel` で kernel を 1 つずつ読み込んで実行する

値が残るのは emu:lib と実機で、emu:process では残らない（emu:lib はエミュレータをホストのプログラムの中で動かす形、emu:process は別のプロセスで動かす形。`share/mnc2/usage.md`）。

使用例: `20-persistent`、`28-persistent-array`（配列）。

## デバッグ用の API

### `nop` — その位置に待ちを置く

```c
void nop(uint n);
```

書いた位置に、n ステップの待ち（アセンブリの `nop/n`。1 ステップは nop 1 個）を置く。
正しさのための待ちはコンパイラが入れるので、ふだんは要らない。待ちを足して実機の挙動を調べるときに使う。

- `n` はコンパイルの時点で決まる定数で、0 から 65535。`0` は何も置かない。定数でない値、65535 を超える値はコンパイルの誤り
- コンパイラは、`nop` をまたいで、メモリと副作用のある呼び出し（`distribute`、`collect` など）の順番を変えない。
  計算の命令は、コンパイラが `nop` をまたいで動かすことがある
- コンパイラが入れる待ちは、`nop` の分を数えに入れる（`nop` の後に、足りている待ちを重ねて入れない）

使用例: `21-nop`。

### `emit_from` — PE の値を PDM へ書き出す（デバッグ用）

```c
void emit_from(const __constant char *name, int pe, __global double *p, double v);
```

**デバッグ用の API である。通常の計算には使わない。** 実機では、PE の中の途中の値が見えない。`emit_from` は、HW PE ID が `pe` の PE の `v` を、
呼ぶたびに PDM の `p` の続きへ書く。ホストは、kernel が終わった後で、その列を読む。

- **`name`**: 文字列リテラル（例: `"em"`）。vsmlink の `<debug_name>` として、生成する `.vsm` のコメントに、そのまま書かれる。**文字の検査はしない。**
  空白など、vsmlink の directive の引数として使えない文字を含めたときの動きは、vsmlink の `share/vsmlink/directives-spec.md` の「@emit_from」を参照
- **`pe`**: 0〜4095 の定数。**HW PE ID** は、vsmlink の文書が決めた番号で、`distribute` が配列の i 番目のデータを配る先の PE の番号 i と同じ
  （`share/vsmlink/hw-pe-id.md`）。定数でない値、範囲の外は誤り
- **`p`**: kernel 引数そのもの（`collect` と同じ）。配列の途中は書けない。`.param` の slot は、`:place :pdm0`、`:addr` は 64 の倍数
- **`v`**: double のみ
- **書かれる量**: 1 回の呼び出しで、**64 u64** を書く。中身は、`pe` を含む 64 個の PE（HW PE ID を 64 で割った商が同じ PE）の値で、欲しい値は、そのブロックの `pe % 64` 番目に入る（ほかの 63 PE の値も一緒に残る）。
  同じ `p` に何回呼んでも、呼ぶたびに続きのブロックに書く。同じ kernel をホストが繰り返し実行すると、毎回、同じ番地に書く（前の回の値は上書きされる）
- **ホストと同期しない。** 書き込みの完了を、ホストに知らせない。**同じ kernel の `collect` や `reduce_add` の完了を待って、kernel の終わりを知り、その後で読む**
  （例: `23-emit-from-debug`）
- **書き込みの範囲の検査は、PDM 全体の大きさだけ。** `p` の後ろに、ほかの slot（kernel 引数の配列や、`collect` の書き先）を置いていると、その領域に書き込んでも、
  エラーにも警告にもならない。**`p` の後ろに、ほかの slot を置かないこと**
- 呼ぶ回数が多いと、PE の内部の専用の領域（L1BM、L2BM）が溢れて、PDM へ書き出される。その分、ほかの機能（`get_neighbor` など）が使える L1BM / L2BM が減る
- **`if` の中では使えない**（コンパイルの誤り。いまはサポートしていない）

使用例: `23-emit-from-debug`。

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

- 割り算 `/` は、逆数の Newton-Raphson で展開するので、最大相対誤差は約 2.2e-16 である。`-cl-fast-relaxed-math` を付けると、逆数平方根の近似命令（5 ビット）だけの計算になる（最大 5.6% 誤差）
- **標準関数**（対応状況の一覧は `standard-functions.md`）: `rsqrt`、`mad`、`sin`、`cos`、`floor`（いずれも f64 のスカラ）と、この文書に載せた関数のほかは使えない。`fabs`、`sqrt`、`fmax`、`fmin` などを呼ぶと、コンパイルの誤りで止まる。
  絶対値は、`if` / `else` で書く（`examples/mncl/09-abs`）
- **比較の結果を値として使えない**（`int flag = (x > y);` など）。比較は `if` / `else` と三項演算子（`select`）の条件に使う
- **`(double)(long)` の変換は、値が `-2^51` 以上 `2^51` 未満のときだけ正しい。範囲の外では、誤った値になる**（エラーにならない。実行時の値なので、コンパイルでは検出できない）
- **ポインタでない kernel 引数**（`int n`、`double a` など）は、kernel の中で値として使えない。使うとコンパイルの誤りで止まる（「kernel 引数」の節）
- **`long` の比較を `||` / `&&` や入れ子の `if` で重ねると、コンパイルの誤り（`i64 の比較の結果を値として使う形は未対応`）で止まることがある**（`x == 0 || x == 31` など。最適化で、比較の結果を値として使う形にまとめられるため）。端の PE の扱いが目的なら、比較を書かずに `get_eoshift_boundary` / `get_eoshift_periodic`、`get_neighbor_boundary` / `get_neighbor_periodic` で端の値を選ぶ（例 `31-heat-diffusion-2d`）
- **`persistent` の宣言が多すぎる**と、原因を示すコンパイルの誤りで止まる（スカラは GRF0 の確保できる範囲、配列は LM0・LM1 に収まらないとき）
- 制御フロー: `if/else` / `select` 対応済み。`for` のループは `#pragma unroll` で完全に展開できるものだけ書ける。展開した後の大きさが clang の閾値を超えると一部だけ展開されてループが残り、コンパイルの誤りになる（1 つの kernel で回す回数を減らす）。回数が実行時に決まるループは、ホスト側で kernel を繰り返し起動する形に書く

## 変更履歴

- **0.3.5**:
  - **依存する kit の下限を上げた: vsmlink-kit 0.10.3 以降、sdk-base-kit 0.1.13 以降、libmnc2-kit 0.4.3 以降。** link には `.param` だけを使う（`.stparam` は使わない。vsmlink の引数は 3 個）。
    `.stparam` の `:boundary` で選んでいた隣の値の端の扱いは、関数で選ぶ形に移った（次の項）
  - 隣の値の端の扱いを、関数で選べるようにした: `load_neighbor_periodic` / `get_neighbor_periodic` / `get_eoshift_periodic`（周期境界）、
    `load_neighbor_boundary` / `get_neighbor_boundary` / `get_eoshift_boundary`（端の PE に `b` の値）。既定（`load_neighbor` / `get_neighbor` / `get_eoshift`）は
    自分の値（clamp）。使用例 `30-neighbor-edge`、`31-heat-diffusion-2d`（`get_eoshift_boundary` による 2 次元の熱拡散）
  - 同時に使う値が GRF0・GRF1・LM0・LM1 に入りきらないとき（`if` 文を含む kernel では GRF0・GRF1 に入りきらないとき）は、退避（スピル）せず、原因を示すコンパイルの誤りで止まるようにした。最適化の後もメモリに置く変数（番地を取る配列など）が残ったときも、コンパイルの誤りで止まる
  - `persistent` の配列の要素に `float2`・`float8` を使えるようにした（`persistent float8 cf[8];`。係数などの f32 の値を、kernel の実行をまたいで PE の中に残せる）
  - `float2` / `float8` / `float` の値が多いとき、GRF0・GRF1 に収まらない分を LM0・LM1 に置くようにした（以前は退避が起き、コンパイルが止まった）。`if` 文を含む kernel では、これまでどおり
  - `float2`（u64 1 つに f32 を 2 つ）と `float8`（u64 4 つに f32 を 8 つ。1 命令で 8 要素）を使えるようにした。受け渡しは `distribute` / `collect` / `broadcast` / `get_eoshift` / `get_neighbor`（`float2`）と `distribute4` / `collect4`（`float8`）。`.x` が u64 の上位 32 ビット（ホストで詰める）。使用例 `29-float2-float8`
  - `float` の値を比較で選ぶ形（三項演算子。`a < b ? a : b`、`(e == 0) ? x : 0.0f` など）を使えるようにした（以前はコンパイルの誤りで止まった）
  - `float` から `double` への変換（`(double)x`）を使えるようにした（以前はコンパイルの誤りで止まった）
  - `float` の定数との積（`x * 4.0f`）が、アセンブラで誤りになる不具合を直した
  - `persistent` を `double` の 1 次元の配列に使えるようにした（`persistent double coef[64];`。添字はコンパイルのときに決まる値だけ。LM0・LM1 に置き、合わせて 4095 u64 まで）。使用例 `28-persistent-array`
  - `persistent` の宣言が多すぎるとき、コンパイラが内部の誤りで止まるのではなく、原因を示すコンパイルの誤りを出すようにした
  - f64 の値（`double`、`double2`、`double4`）を、GRF0・GRF1 が足りないときに LM0・LM1 にも置くようにした（以前は退避（スピル）が起き、多いとコンパイルが止まった）。`if` 文を含む kernel では、これまでどおり退避する
  - コンパイラが退避（スピル）したとき、kernel ごとに 1 回警告を出すようにした
  - `double4` などの幅の広いレジスタの一部の番地に書いた直後に読む形で、命令の間の待ち（`nop`）が足りず、assemble3 が止まることがある不具合を直した
  - `get_eoshift(v, dir, N, M)` を追加（PE の並びの上で、位置の差が 2^N の PE の値を受け取る。区切り 2^M 個をまたがない。4096 個。要 vsmlink の `@get_eoshift`）。2 次元の格子の隣の値を取る用途
  - `sin(x)`、`cos(x)`、`floor(x)`（f64 のスカラ）を追加。`sin` と `cos` の最大相対誤差は約 2.2e-16 以内（|x| < 約 1.6e6）。f32 とベクタは未対応。使用例 `27-fft-4096-sincos`
  - f64 の定数のうち、下位 32 ビットが 0 でないもの（`0.1` など）を、スカラでも書けるようにした（以前は、ベクタの定数だけ書けた）
  - `(double)(id & mask)` のように、値が負でないと分かる整数（`long`）から `double` への変換を使えるようにした（以前は選択できず、コンパイルの誤りになった）
  - `mad(a, b, c)`（f64 の積和。`a * b + c`）を追加。MN-Core 2 の積和命令（`dvfmau` / `dvfmad`）で計算する。**結果は、正確に丸めた積和（`fma()`）と一致しない場合がある**（dev manual 4.3）。`fma()` は対応していない。説明は `share/mncl/mad.md`。使用例 `26-fft-4096`（FFT）
  - デバッグ用の API `emit_from(name, pe, p, v)` を追加（HW PE ID が pe の PE の値を、PDM へ書き出す。要 vsmlink-kit 0.10.3 以降の `@emit_from`）。使用例 `23-emit-from-debug`
  - `long` の `^`・`|`・`-`・シフト（`<<`・`>>`。変数のシフト量も）と、任意の値との `==` / `!=` を追加（以前は `+`・`&` と、0 との `==` だけ）。使えない演算と、比較の結果を値として使う形は、診断で止まる（「`long`（64 ビット整数）で使える演算」の節）
  - `long` を置くレジスタを 8 本から 32 本に増やした（同時に生きる `long` の値が多い kernel で、`spill が未対応の register class: XREG` で止まらなくなった）
  - 使用例を追加: `24-bitonic-sort`（4096 個）、`25-bitonic-sort-8192`（8192 個）。`get_hypercube_neighbor` を使ったバイトニックソート
  - `get_hypercube_neighbor(v, id, deep)` を追加（超立方体の隣の PE の値を受け取る。deep 0〜11、4096 個。要 vsmlink-kit 0.10.3 以降の `@get_hypercube_neighbor`）。使用例 `22-hypercube-neighbor`
  - 展開されていないループ（`#pragma unroll` の無いループ、`#pragma unroll` を付けたが展開した後の大きさが閾値を超えて一部だけ展開されたループ）が残ったとき、コンパイラが落ちずに、原因を示すコンパイルの誤りで止まるようにした
  - `distribute` / `collect`（`distribute2`〜`4`、`collect2`〜`4`、f32、long を含む）のポインタの引数に配列の途中（`&x[k]`、`x + k`）を書くと、誤った出力を作らずにコンパイルの誤りで止めるようにした（以前は誤りを出さず、配列の先頭の slot を読み書きする出力を作っていた）
  - ポインタでない kernel 引数（`int`、`long`、`float`、`double`）を kernel の中で値として使ったとき、コンパイラの内部の誤り（`float` では、引数を読まない誤った出力）ではなく、原因と書き方（`broadcast` を使う）を示すコンパイルの誤りで止まるようにした
  - kernel の中で PDM（`__global` のポインタの先）を直接読み書きしたとき（`out[i] = v`、`v = in[i]` など）、コンパイラの内部の誤り（`Cannot select`）ではなく、原因と直し方（`collect` / `distribute` / `broadcast` を使う）を示すコンパイルの誤りで止まるようにした
  - f32 と f64 の値が、同じレジスタの番地を同時に使ってしまうことがある不具合を直した（f32 と f64 を混ぜて使う kernel で、値が誤るおそれがあった）
  - 使用例を追加: `20-persistent`（`persistent` の変数を 2 つの kernel で共有する）
  - kernel の末尾に、まだ終わっていない書き込みの分だけ待ちを入れるようにした。以前は、kernel の最後の命令が書いたレジスタを、
    続けて実行した次の kernel の最初の命令が待たずに読むことがあり、実機で値が合わなかった（`persistent` を使って kernel を
    続けて実行する場合。emu では起きない）
  - デバッグ用の API `nop(n)` を追加（その位置に n ステップの待ちを置く）。使用例 `21-nop`
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
