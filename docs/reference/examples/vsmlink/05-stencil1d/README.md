# 05-stencil1d: 1D ステンシル袖交換の E2E テスト

## 概要

このテストは `@stencil` ディレクティブが **MN-Core 2 の全階層**で正しく袖交換を行うことを検証する。
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

`@stencil $lr400 -1 _arg_a`（左隣を取得）の場合:

| 対象 PE | 隣接 PE の位置 | データパス |
|---------|-------------|-----------|
| PE 1-3 | 同一 MAB 内 | `msl`（PE 間シフト）|
| PE 0 (MAB 1-15) | 隣接 MAB | `l1bmd → l1bmd+1 → msr*3` |
| PE 0 (MAB 0, L1B 1-7) | 隣接 L1B | `l1bmd → l2bm → l2bmb → l1bmd+1 → msr*3` |
| PE 0 (MAB 0, L1B 0, Sec 1-7) | 隣接セクション | `l1bmd → l2bm → mvp → l2bmb → l1bmd+1 → msr*3` |
| PE 0 (MAB 0, L1B 0, Sec 0) | 存在しない | 境界条件（値 = 0）|

## 生成される .vsm の構造

`stencil1d._vsm` から vsmlink が生成する `.vsm` は以下のフェーズで構成される。
生成される `.vsm`（`expected.vsm`）にも各フェーズのコメントが記載されている。

### 1. @distribute: ホスト → PE

```
PDM → (mvp) → L2BM → (l2bmb) → L1BM → (l1bmd) → PE
```

ホストから送られた入力データを全 4096 PE に配布する。

### 2. @stencil_boundary: 境界 PE 検出

HW レジスタ（`$subpeid`, `$mabid`, `$l1bid`）を使って各 PE が自分の位置を判定し、
omr フラグレジスタに書き込む。

| omr | 条件 | 用途 |
|-----|------|------|
| omr13 | `$subpeid == 0` (PE 左端) | PE マージで使用 |
| omr14 | `$subpeid == 3` (PE 右端) | PE マージで使用 |
| omr11 | `$mabid == 0` (MAB 左端) | MAB マージで使用 |
| omr12 | `$mabid == 15` (MAB 右端) | MAB マージで使用 |
| omr9  | `$l1bid == 0` (L1B 左端) | L1B マージで使用 |
| omr10 | `$l1bid == 7` (L1B 右端) | L1B マージで使用 |

### 3. cross MAB

L1BM を経由して隣接 MAB のデータを取得する。

```
PE → (l1bmd) → L1BM → (l1bmd+1) → PE → (msr*3) → PE
```

- `l1bmd`: 元データを L1BM に gather
- `l1bmd+1`: 隣接 MAB（番号 -1）のスロットを読み出し
- `msr*3`: PE 位置を 3 つ回転（MAB 15 の PE 3 → PE 0 に補正）

**注意**: MAB 0 は L1B 内でラップして MAB 15 のデータを読む。
これは L1B 0 以外では正しいが、L1B 0 では不正確 → 後のマージで修正される。

### 4. cross L1B

L2BM を経由して隣接 L1B のデータを取得する。

```
PE → (l1bmd) → L1BM → (l2bm@N) → L2BM → (l2bmb@N-1) → L1BM → (l1bmd+1) → PE → (msr*3)
```

- `l2bm@0..@7`: L1BM のデータを L2BM に scatter（各 L1B → 各 L2BM スロット）
- `l2bmb@1..@7`: L2BM スロット N-1 のデータを L1B N の L1BM に broadcast
  - L1B 0 は受信しない（左隣が同一セクション内にないため）
- `l1bmd+1 → msr*3`: cross MAB と同じ補正

### 5. cross L2B（セクション間）

PDM を経由して隣接セクションのデータを取得する。

```
PE → L1BM → L2BM → (mvp collect) → PDM → (mvp redistribute) → L2BM → (l2bmb@0) → L1BM → (l1bmd+1) → PE → (msr*3)
```

- **collect**: 各セクションの L2BM → PDM（8 セクション × 512 u64 = 4096 u64 の一時領域を使用）
- **redistribute**: PDM → 隣接セクションの L2BM
  - offset=-1: sec 0→1, 1→2, ..., 6→7（7 転送）
  - sec 0 は左隣なし → 受信データなし → 境界条件
- `l2bmb@0`: L1B 7 のデータ（L2BM 内 offset 7*64）を L1B 0 の L1BM にロード
- `l1bmd+1 → msr*3`: 同じ補正

### 6. マージ（内側から外側へ）

各階層の結果を `maskr`/`ipassa` で段階的にマージする。
外側の結果が内側の結果を上書きする。

```
L1B マージ:  L1B 0/7 → L2B 結果で上書き（omr9/10）
MAB マージ:  MAB 0/15 → L1B/L2B 結果で上書き（omr11/12）
PE マージ:   PE 0/3 → マージ結果で上書き、PE 1-3 は msl/msr（omr13/14）
```

### 7. @collect: PE → ホスト

```
PE → (l1bmd) → L1BM → (l2bm) → L2BM → (mvp) → PDM
```

計算結果をホストに回収する。

## 検証結果

```
=== stencil1d: 3-point sum with @stencil (4096 PE) ===

  [boundary] PE0:    got=3       (left=0, center=1, right=2)
  [boundary] PE4095: got=8191    (left=4095, center=4096, right=0)
  PASS: 3-point sum correct for 4094 internal PEs
```

- **内部 PE (1-4094)**: `output[i] = input[i-1] + input[i] + input[i+1]` が全て一致
- **境界 PE (0, 4095)**: 隣接セクションが存在しないため `left` または `right` が 0 になる

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `stencil1d._vsm` | 入力: @stencil ディレクティブを含む _vsm |
| `stencil1d.param` | 配置パラメタ: PDM アドレス |
| `stencil1d.stparam` | 構造パラメタ: ステンシル型 |
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
