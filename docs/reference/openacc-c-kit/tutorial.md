# OpenACC C → MN-Core 2 変換チュートリアル (0.4.2)

## このコンバータは何をするか

OpenACC C ソースの `#pragma acc parallel loop` 領域を、
MN-Core 2 上で実行可能な形式に変換する。具体的には:

- **デバイスカーネル (.cl)** — 4096 PE が同時実行する MNCL ソース
- **構造パラメタ (.stparam)** — PE の配置情報（vsmlink が使う）
- **ホスト呼び出しコード** — `#pragma acc` 領域を DMA 転送 + カーネル実行に置き換えたコード

つまり、「ホスト CPU で逐次実行される OpenACC C」を
「ホストが DMA でデータを送り、4096 PE が並列演算し、結果を回収する」形に変換する。

## コンパイラではない

このコンバータは本格的なコンパイラではない。以下を簡略化している:

- **`#pragma acc` のパースをしない** — ソース内のマーカーコメント (`// @name { ... }`) で変換対象領域を手動指定する。本格的なコンパイラでは `#pragma acc` を直接パースする必要がある
- **演算は vecadd (`c = a + b`) のみ** — 任意の演算式への対応が必要
- **配列型は double のみ** — float, int 等への対応が必要
- **ステンシル（PE 間通信）非対応** — 隣接 PE とのデータ交換が必要な計算パターン
- **PE あたり 1 要素固定** — N>4096 はホスト側バッチで対応

これらの制限の範囲内で、「OpenACC C → MN-Core 2 の変換パターン」を
6 つの example で示すのがこのチュートリアルの目的。

## 前提知識

- MN-Core 2 は 4096 PE の SIMD 型プロセッサ
- メモリ階層: LM → L1BM (64 PE) → L2BM (512 PE) → DRAM → PDM
- データ転送は明示的（GPU のようなキャッシュは存在しない）
- 全 PE が同一命令を実行する

## 変換の全体像

OpenACC C コンパイラは、OpenACC C ソースから以下の 3 種類のファイルを生成する。

| 出力ファイル | 内容 | 後段ツール |
|-------------|------|-----------|
| `.cl` | デバイスカーネル（MNCL ソース） | clang (MNCL) → vsmlink → assemble3 |
| `.stparam` | 構造パラメタ（PE 配置情報） | vsmlink |
| ホスト呼び出しコード | `#pragma acc` 領域を DMA 転送 + カーネル実行に置換 | gcc + libmnc2 |

パイプライン全体:

```
input.c (OpenACC C)
  |  コンパイラ（これを作る）
  v
vecadd.cl + vecadd.stparam + ホスト呼び出しコード
  |  clang (MNCL)
  v
vecadd._vsm
  |  vsmlink (vecadd._vsm + vecadd.param + vecadd.stparam)
  v
vecadd.vsm
  |  assemble3
  v
vecadd.idma.dat
  |  gcc (ホストコード + libmnc2)
  v
実行バイナリ
```

## OpenACC C ソースから抽出すべき情報

コンパイラが `#pragma acc` 領域から抽出する必要がある情報は 4 つだけ。

| 項目 | 抽出元 | 例 |
|------|--------|-----|
| 配列名と入出力方向 | 演算式 `c[i] = a[i] + b[i]` | a(in), b(in), c(out) |
| ndim（次元数） | `collapse(N)` の値（省略時 1） | 1, 2, 3 |
| total（総要素数） | ループの繰り返し回数の積 | 4096, 8192 |
| 演算内容 | ループ本体の式 | `c = a + b` |

これらから、.cl / .stparam / ホスト呼び出しコードのすべてが生成できる。

.stparam の `pe_shape` は ndim から決まるハードウェア定数であり、
ソースからの「抽出」ではなく、バックエンドが決定する。

---

## Example 01: 1D vecadd, 4096 要素 — 最小ケース

### input.c を見る

```c
#define N 4096

#pragma acc parallel loop
for (int i = 0; i < N; i++) {
    c[i] = a[i] + b[i];
}
```

ここから抽出できる情報:

- **配列**: a(in), b(in), c(out) — 演算式の左辺が出力、右辺が入力
- **ndim**: 1 — `collapse` なし = ループ 1 段
- **total**: 4096 — ループの繰り返し回数
- **演算**: `c = a + b`

### 生成物 1: vecadd.cl（デバイスカーネル）

```c
__kernel void main_kernel0(__global double* _arg_a, __global double* _arg_b, __global double* _arg_c)
{
    double a = distribute(_arg_a);
    double b = distribute(_arg_b);

    double c = a + b;

    collect(_arg_c, c);
}
```

- 入力配列ごとに `distribute` で PDM から PE にデータを分配する
- PE 上でスカラ演算を行う
- 出力配列を `collect` で PE から PDM に回収する
- **4096 PE が同時に同じ演算を行うので、ループはない**

### 生成物 2: vecadd.stparam（構造パラメタ）

```lisp
((:version :0.0)
 (:ndim 1)
 (:pe_shape (4096))
 (:pe_local (1))
 (:data_layout :any))
```

- `pe_shape`: 4096 PE の配置。ndim=1 なので 1D フラット `(4096)`
- `pe_local`: 各次元 PE あたりの要素数。現状 `(1)` 固定
- `data_layout`: vecadd は配置に依存しないので `:any`

### 生成物 3: ホスト呼び出しコード

`#pragma acc` 領域が以下のような処理に置き換わる:

1. MN-Core 2 デバイスを開く
2. カーネル（.idma.dat）をロードする
3. 入力配列 a, b を DMA でデバイスに送信（`mnc2_send`）
4. カーネルを実行（`mnc2_exec_kernel`）
5. 出力配列 c を DMA でデバイスから受信（`mnc2_recv`）
6. デバイスを閉じる

4096 要素 = 4096 PE なので、1 回の送受信 + 1 回のカーネル実行で完了する。

---

## Example 02: 1D vecadd, 8192 要素 — バッチ処理

### input.c を見る

```c
#define N 8192

#pragma acc parallel loop
for (int i = 0; i < N; i++) {
    c[i] = a[i] + b[i];
}
```

抽出結果: ndim=1, total=8192

### 01 との違い

**8192 要素 > 4096 PE なので、1 回では処理できない。**

- batch = 8192 / 4096 = **2 ラウンド**
- ホスト呼び出しコードにバッチループが追加される:

```
for (int _r = 0; _r < 2; _r++) {
    offset = _r * 4096;
    送信: a[offset..offset+4095], b[offset..offset+4095]
    カーネル実行
    受信: c[offset..offset+4095]
}
```

- **.cl は 01 と同一** — カーネルは常に 4096 要素を処理する
- **.stparam も 01 と同一** — PE 配置は変わらない
- **変わるのはホスト側のバッチループだけ**

---

## Example 03: 2D vecadd, 64x64 = 4096 要素 — collapse

### input.c を見る

```c
#define NI 64
#define NJ 64

#pragma acc parallel loop collapse(2)
for (int i = 0; i < NI; i++)
    for (int j = 0; j < NJ; j++)
        c[i][j] = a[i][j] + b[i][j];
```

抽出結果: ndim=2, total=4096, batch=1

### 01 との違い

- `collapse(2)` から ndim=2 を得る
- ループが 2 段になり、上限は NI=64, NJ=64
- **.cl は 01 と同一** — カーネルは次元に依存しない
- **.stparam の pe_shape が変わる**: `(4096)` → `(64 64)`

pe_shape は MN-Core 2 のハードウェア定数で、ndim だけで決まる:

| ndim | pe_shape | 意味 |
|------|----------|------|
| 1 | (4096) | 4096 PE フラット |
| 2 | (64 64) | 64×64 = 4096 PE |
| 3 | (16 16 16) | 16×16×16 = 4096 PE |

---

## Example 04: 2D vecadd, 64x128 = 8192 要素 — 2D + バッチ

### input.c を見る

```c
#define NI 64
#define NJ 128

#pragma acc parallel loop collapse(2)
for (int i = 0; i < NI; i++)
    for (int j = 0; j < NJ; j++)
        c[i][j] = a[i][j] + b[i][j];
```

抽出結果: ndim=2, total=8192, batch=2

03 + バッチ。ホスト呼び出しコードのバッチループが 2 回になる。
.cl と .stparam は 03 と同一。

---

## Example 05: 3D vecadd, 16x16x16 = 4096 要素 — 3D collapse

### input.c を見る

```c
#define NI 16
#define NJ 16
#define NK 16

#pragma acc parallel loop collapse(3)
for (int i = 0; i < NI; i++)
    for (int j = 0; j < NJ; j++)
        for (int k = 0; k < NK; k++)
            c[i][j][k] = a[i][j][k] + b[i][j][k];
```

抽出結果: ndim=3, total=4096, batch=1

- .stparam の pe_shape が `(16 16 16)` になる
- .cl は 01 と同一

---

## Example 06: 3D vecadd, 16x16x32 = 8192 要素 — 3D + バッチ

### input.c を見る

```c
#define NI 16
#define NJ 16
#define NK 32

#pragma acc parallel loop collapse(3)
for (int i = 0; i < NI; i++)
    for (int j = 0; j < NJ; j++)
        for (int k = 0; k < NK; k++)
            c[i][j][k] = a[i][j][k] + b[i][j][k];
```

抽出結果: ndim=3, total=8192, batch=2

05 + バッチ。6 ケース中最も複雑だが、パターンは 02 や 04 と同じ。

---

## 6 ケースから見える設計原則

### 1. デバイスカーネル (.cl) は全ケースで同一

vecadd の場合、カーネルは「PE ごとに 1 要素を加算する」だけであり、
次元にもバッチにも依存しない。

コンパイラが `.cl` 生成で必要とするのは **配列名と演算式** だけ。

### 2. pe_shape は ndim だけで決まる

ソースの要素数には依存しない。MN-Core 2 のハードウェア定数。

コンパイラが `.stparam` 生成で必要とするのは **ndim** だけ。

### 3. バッチ処理はホスト側の責任

カーネルは常に 4096 要素を処理する。要素数 > 4096 の場合、
ホストが複数ラウンドに分割してカーネルを繰り返し実行する。

コンパイラがホスト呼び出しコード生成で必要とするのは **配列名、total、batch 数**。

### 4. コンパイラのフロントエンドとバックエンドの分離

| | フロントエンド（ソース依存） | バックエンド（HW 定数） |
|---|---|---|
| 入力 | `#pragma acc`, ループ構造, 演算式 | ndim, PE 数 (4096) |
| 出力する情報 | 配列名, ndim, total, 演算内容 | pe_shape, pe_local, batch 数 |

フロントエンドが抽出した情報を、バックエンドが MN-Core 2 のハードウェア定数と
組み合わせて .cl / .stparam / ホスト呼び出しコードを生成する。

---

## ビルドと実行

コンバータ (`openacc2mncore.sh`) で各 example の出力ファイルを生成し、
input.c と見比べて変換パターンを確認できる。

```bash
cd 01-vecadd-1d-4096
openacc2mncore.sh input.c vecadd    # _build/ に .cl, .stparam 等を生成
```

各 example ディレクトリで使える make ターゲット:

| ターゲット | 内容 |
|-----------|------|
| `make` (default) | S2S 変換 + MNCL コンパイル（`input.c` → `.cl` → `._vsm`） |
| `make build-e2e` | + vsmlink + assemble3 + ホスト C コンパイル |
| `make test` | E2E バイナリをエミュレータで実行 |
| `make verify` | plain C の結果と MN-Core 2 の結果を比較 |

### make test — エミュレータ実行

```
LD_LIBRARY_PATH=$SDK_ROOT/lib/emu-lib ./_build/output
```

input.c のエピローグ（`#pragma acc` 領域の後のコード）がそのまま実行される。
エピローグは全要素を stdout にダンプし、期待値との比較を stderr に出力する:

```
c[0] = 11
c[1] = 22
...
PASS: c[i] == a[i] + b[i] for all 4096 elements
```

失敗した場合は不一致要素が最大 5 件表示され、`FAIL` が出力される。

### make verify — plain C との比較

input.c を普通の gcc でコンパイルした結果と、
MN-Core 2 エミュレータで実行した結果を diff で比較する。

```
VERIFY OK: ref and mncore2 outputs match
```

verify は回帰テストとして使える。
input.c を普通の C として実行した結果が正解であり、
コンパイラを通した結果がそれと一致するかを確認する。


## 変更履歴

- 0.4.2: 現行 SDK 版 (vsmlink 0.8.x / libmnc2 0.4.x) の API に追従。
  - 生成ホストコードの `mnc2_open` / `mnc2_close` / `mnc2_send` / `mnc2_recv` /
    `mnc2_load_kernel` / `mnc2_exec_kernel` 呼び出しを現行シグネチャに更新
  - `vecadd.param` に `:reserved_wait_tag` と `:send_wait_tag` / `:recv_wait_tag`
    を明示（vsmlink の W001 警告を抑制）
  - examples Makefile を `LD_LIBRARY_PATH` 切替方式に更新（`MNC2_BACKEND` 環境
    変数廃止）。リンクに `-lgpfn3 -lpthread` を追加
- 0.4.1: `.param` 形式を vsmlink 0.6.x 新形式に更新（`:place`、`:version :0.1`、
  `:group` 廃止）
