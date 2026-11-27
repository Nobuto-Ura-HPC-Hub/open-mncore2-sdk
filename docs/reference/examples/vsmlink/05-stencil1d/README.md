# 05-stencil1d: 1D ステンシル袖交換の E2E テスト

## 概要

このテストは `@get_neighbor` ディレクティブが **MN-Core 2 の全階層**で正しく袖交換を行うことを検証する。
カーネルは 3 点和: `output[i] = left[i] + center[i] + right[i]` を計算する。

4096 PE に `input[i] = i + 1` を配置し、各 PE が左隣 (`offset=-1`) と右隣 (`offset=+1`) の値を取得して加算する。

## MN-Core 2 のトポロジと袖交換の課題

```
PE 0-3     PE 0-3     PE 0-3          PE 0-3
├─MAB 0──┤├─MAB 1──┤├─MAB 2──┤ ... ├─MAB 15─┤
└───────── L1B (64 PE) ──────────────────────┘

L1B 0    L1B 1    L1B 2  ...  L1B 7
└──────── L2B section (512 PE) ──────┘

Section 0  Section 1  ...  Section 7
└──────── チップ全体 (4096 PE) ──────┘
```

PE は直接通信できるのは **同じ MAB 内の 4 PE** だけ（`msl`/`msr`）。
隣接 PE が別の MAB・L1B・セクションにいる場合、上位メモリ階層を経由する必要がある。

## 袖交換のデータパス

`@get_neighbor $lr400 -1 $lm0 bf1 nbuf0 left`（左隣を取得）の場合:

| 対象 PE | 隣接 PE の位置 | データパス |
|---------|-------------|-----------|
| PE 1-3 | 同一 MAB 内 | `msl`（PE 間シフト）|
| PE 0 (MAB 1-15) | 隣接 MAB | `l1bmd → l1bmd+1 → msl` |
| PE 0 (MAB 0, L1B 1-7) | 隣接 L1B | `l1bmd → l2bm → l2bmb → l1bmd+1 → msl` |
| PE 0 (MAB 0, L1B 0, Sec 1-7) | 隣接セクション | `l1bmd → l2bm → mvp → l2bmb → l1bmd+1 → msl` |
| PE 0 (MAB 0, L1B 0, Sec 0) | 存在しない | 境界条件（値 = 0）|

## 生成される .vsm の構造

`stencil1d._vsm` から vsmlink が生成する `.vsm` は以下のフェーズで構成される。
生成される `.vsm`（`expected.vsm`）にも各フェーズのコメントが記載されている。

### 1. @distribute: ホスト → PE

```
PDM → (mvp) → L2BM → (l2bmb) → L1BM → (l1bmd) → PE
```

ホストから送られた入力データを全 4096 PE に配布する。

### 2. @boundary_flags: 境界フラグの配布

ホストが PDM に置いた境界フラグを、PDM → L2BM → L1BM → `$lr416` の経路で各 PE に配る。
各 PE には、その PE が接する境界のうち最も外側のレベルのビットが 1 つだけ立つ。ビットの割り当ては
`share/vsmlink/directives-spec.md` の `@boundary_flags` 節を参照。`@get_neighbor` はこのフラグ（`bf1`）を
見て、各 PE がどの経路の結果を使うかを選ぶ。

### 3. @get_neighbor: 左隣と右隣の値を受け取る

`@get_neighbor` を 2 回使う。左隣（offset -1）の結果は `$lr400`、右隣（offset +1）の結果は `$lr408` に入る。
端の PE（PE の番号 0 / 4095）には自分の値を入れるので、端の指定 `:reg $lm0`（src と同じレジスタ）を書いている。
展開には、本体のテンプレート（`get_neighbor_left.vsm` / `get_neighbor_right.vsm`）と、端の PE に書くテンプレート
（`get_neighbor_edge_left.vsm` / `get_neighbor_edge_right.vsm`）が使われる。左右は向きが違うだけで、構成は同じである。
以下は左隣の場合で、右隣では `l1bmd+1` が `l1bmd-1` に、`msl` が `msr` になる。

1 回の `@get_neighbor` の展開は、次の順に並ぶ（`share/vsmlink/directives-spec.md` の `@get_neighbor` の「展開内容」）。

1. **ゼロレジスタの作成と、入力データを GRF0 へ写す**
2. **cross L2B**: 隣の L2B の値を、`.param` の `:buffer`（`nbuf0`）で宣言した PDM0 上のバッファを経由して受け取る。
   各 L2B の L1B 7 の値を `l1bmd` → `l2bm@7` → `mvp` でバッファに書き（put）、1 L2B 分ずらした位置から
   `mvp` → `l2bmb@0` で読み戻し（get）、`l1bmd+1` と `msl` で PE の位置を合わせる
3. **cross L1B**: 隣の L1B の値を、L2BM を経由して受け取る。`l1bmd` で L1BM に出し、`l2bm@0..7` で L2BM に上げ、
   `l2bmb@1..7` で隣の L1B の L1BM に下ろし、`l1bmd+1` と `msl` で PE の位置を合わせる
4. **cross MAB**: 隣の MAB の値を、L1BM を経由して受け取る。`l1bmd` で L1BM に出し、`l1bmd+1` と `msl`
5. **within MAB**: 同じ MAB の中の隣の PE の値を、`msl` で受け取る
6. **omr の導出**: `@boundary_flags` で配った境界フラグ（`bf1`）と、各経路のビット（cross L2B、cross L1B、cross MAB）を
   `ixor` で比べ、一致した PE を omr に立てる。使う omr は `@alloc` で確保したもの
   （この example では `:omr 9 10 11 12`）
7. **マージ**: within MAB の結果を既定として `lpassa` で結果のレジスタに書き、cross MAB、cross L1B、cross L2B の
   順に `maskr` と `ipassa` で上書きする
8. **端の PE に書く**: 端の指定 `:reg $lm0` により、PE の位置から選んだ端の PE（左隣では PE の番号 0、右隣では 4095）の結果を、
   自分の `$lm0` で上書きする

どの経路の結果を使うかは、2〜5 をすべての PE で計算したあとに、マスクで選ぶ（directives-spec の
「マージフェーズの実装方針」）。

### 4. 3 点の和

`$lr0`（自分の値）に、`$lr400`（左隣）と `$lr408`（右隣）を `dvadd` で足す。

### 5. @collect: PE → ホスト

```
PE → (l1bmd) → L1BM → (l2bm) → L2BM → (mvp) → PDM
```

計算結果をホストに回収する。

## 検証結果

```
=== stencil1d: 3-point sum with @get_neighbor (4096 PE) ===

  [boundary] PE0:    got=3       (left=0, center=1, right=2)
  [boundary] PE4095: got=8191    (left=4095, center=4096, right=0)
  PASS: 3-point sum correct for 4094 internal PEs
```

- **内部 PE (1-4094)**: `output[i] = input[i-1] + input[i] + input[i+1]` が全て一致
- **境界 PE (0, 4095)**: 隣接セクションが存在しないため `left` または `right` が 0 になる

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `stencil1d._vsm` | 入力: @get_neighbor ディレクティブを含む _vsm |
| `stencil1d.param` | 配置パラメタ: PDM アドレス |
| `expected.vsm` | vsmlink が生成する展開済み .vsm |
| `test_stencil1d.c` | E2E テスト（emu:lib で実行） |
| `build.ninja` | ビルド定義 |

## 実行方法

```bash
# SDK パスを設定済みの状態で:
ninja build-e2e    # vsmlink + assemble3 + host C ビルド
ninja test         # emu:lib で実行・検証
```

## 関連テスト

| example | 検証内容 |
|---------|---------|
| `12-l1b-crossing` | cross L1B の命令シーケンス単体テスト |
| `13-l2b-crossing` | cross L2B の命令シーケンス単体テスト |
| `14-stencil1d-debug` | 全階層の中間値を debug_read で段階的に観察 |

## 依存

boundary flags データとして `../17-boundary-collect/_build/collected_flags.bin` を使用する。
事前に 17-boundary-collect のビルド・テストが必要:
```
ninja -C ../17-boundary-collect build-e2e && ninja -C ../17-boundary-collect test
```
