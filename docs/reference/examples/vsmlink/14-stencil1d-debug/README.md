# 14-stencil1d-debug: ステンシル袖交換の段階観察チュートリアル

## このテストの意義

MN-Core 2 で 1D ステンシル（左隣取得）を実現するには、**5 種類の異なるデータパス**を使い分ける必要がある。example 05（stencil1d）はこれらを一括で実行して最終結果を検証するが、途中で何が起きているかは見えない。

このテストは、各階層の命令を **1 つずつ実行して中間状態を観察** する教育用プログラムである。「なぜこの命令が必要か」「どの PE のデータが正しくて、どの PE がまだ不正確か」を段階的に確認できる。

## 前提知識

### MN-Core 2 のトポロジ

```
PE 0-3     PE 0-3     PE 0-3          PE 0-3
├─MAB 0──┤├─MAB 1──┤├─MAB 2──┤ ... ├─MAB 15─┤
└───────── L1B (64 PE = 16 MAB) ─────────────┘

L1B 0    L1B 1    L1B 2  ...  L1B 7
└──────── L2B section (512 PE = 8 L1B) ──────┘

Section 0  Section 1  ...  Section 7
└──────── チップ全体 (4096 PE = 8 section) ──┘
```

- **PE**: 演算ユニット。各 MAB に 4 PE
- **MAB**: PE のグループ。各 L1B に 16 MAB
- **L1B**: L1BM（ローカルメモリバンク）を持つ中間階層。各 L2B section に 8 L1B
- **L2B section**: L2BM を共有する最大のグループ。チップに 8 section

PE が直接通信できるのは **同じ MAB 内の 4 PE だけ**（`msl` / `msr` 命令）。

### メモリ階層とデータ転送命令

| 階層 | バッファ | 主な転送命令 | 備考 |
|------|---------|-------------|------|
| PE | GRF（レジスタ） | `msl`, `msr` | MAB 内 4 PE 間のシフト |
| MAB → L1B | L1BM | `l1bmd`, `l1bmd+1` | L1B 内 16 MAB 間の gather/scatter |
| L1B → L2B | L2BM | `l2bm`, `l2bmb` | L2B section 内 8 L1B 間の通信 |
| L2B → chip | PDM | `mvp` | セクション間はPDM経由（DMA転送） |

### 入力データ

PE[i] = i + 1（PE 番号 + 1 の整数値）。offset=-1（左隣取得）なので、PE[i] が取得すべき値は PE[i-1] の値 = i。

## 各ステージの動き

### Stage 0: Distribute（PDM → PE）

ホストが PDM に書き込んだデータを全 4096 PE に配布する。
経路: `PDM → (mvp) → L2BM → (l2bmb) → L1BM → (l1bmd) → PE`

結果: 各 PE の lr0 に PE 番号 + 1 の値が入る。

### Stage 1: msl（PE 間左シフト）→ lr2

`msl` は同一 MAB 内で「左隣の PE」のデータを取得する命令。PE j は PE (j-1) mod 4 のデータを受け取る。

```
MAB 0 の例:
  入力:  PE[0]=1  PE[1]=2  PE[2]=3  PE[3]=4
  msl後: PE[0]=4  PE[1]=1  PE[2]=2  PE[3]=3
              ↑ MAB 内ラップ（PE 3 のデータ。本来ほしいのは前の MAB の PE 3）
```

**PE 1-3 は正しい**が、**PE 0 は MAB 内ラップで不正確**。→ Stage 3 で修正。

### Stage 2: msr（PE 間右シフト）→ lr4

`msr` は右隣を取得。offset=+1 の観察用。PE 3 が MAB ラップになる。

### Stage 3: cross MAB → lr6

L1BM を経由して隣接 MAB のデータを取得する。
経路: `PE → (l1bmd) → L1BM → (l1bmd+1) → PE → (msr*3)`

- `l1bmd`: 元データを L1BM に gather（全 16 MAB 分）
- `l1bmd+1`: L1BM から「1 つ前の MAB」のスロットを読み出す
- `msr*3`: l1bmd+1 の結果は PE 3 に左端データが来るので、3 回右シフトして PE 0 に配置

```
L1B 0 の例:
  MAB 0 PE[0-3]: 64 61 62 63  ← MAB 15 のデータ（L1B 内ラップ）
  MAB 1 PE[0-3]:  4  1  2  3  ← MAB 0 のデータ（正しい）
  MAB15 PE[0-3]: 60 57 58 59  ← MAB 14 のデータ（正しい）
```

**MAB 1-15 の PE 0 は正しい**。**MAB 0 は L1B 内ラップで不正確**（L1B 0 では MAB 15 は自分自身の L1B のもの。本来ほしいのは前の L1B の MAB 15）。→ Stage 4 で修正。

### Stage 4: cross L1B → lr8

L2BM を経由して隣接 L1B のデータを取得する。
経路: `PE → L1BM → (l2bm) → L2BM → (l2bmb) → L1BM → (l1bmd+1) → PE → (msr*3)`

- `l2bm@N`: L1B N の L1BM → L2BM のスロット N に転送
- `l2bmb@N`: L2BM スロット N-1 を L1B N に配信（L1B 1 は L1B 0 のデータを受信）
- L1B 0 は受信しない（`l2bmb@0` を発行しない）

```
Section 0 の例:
  L1B 0 MAB 0: 64 61 62 63   ← ラップのまま（L1B 0 は受信しない）
  L1B 1 MAB 0: 64 61 62 63   ← L1B 0 のデータ（正しい！）
  L1B 7 MAB 0: 444 441 442 443 ← L1B 6 のデータ（正しい！）
```

**L1B 1-7 は正しい**。**L1B 0 はまだ不正確**（左隣がセクション外）。→ Stage 5 で修正。

### Stage 5: cross L2B → lr10

PDM を一時バッファとして、隣接セクションのデータを取得する。L2BM 間は直接通信できないため、PDM 経由で受け渡す。

経路（3 段階）:
1. **collect**: `L1BM → L2BM → (mvp) → PDM`（一時領域に退避）
2. **redistribute**: `PDM → (mvp) → 隣接セクションの L2BM`（シフトして書き戻し）
3. **distribute**: `L2BM → (l2bmb@0) → L1B 0 の L1BM → (l1bmd+1) → PE → (msr*3)`

```
結果:
  Sec 0 L1B 0: 0 0 0 0       ← 左隣なし（境界条件）
  Sec 1 L1B 0: 512 509 510 511 ← Sec 0 の L1B 7 のデータ（正しい！）
  Sec 7 L1B 0: 3584 3581 3582 3583 ← Sec 6 の L1B 7 のデータ（正しい！）
```

**Section 1-7 の L1B 0 は正しい**。**Section 0 の L1B 0 は境界条件（= 0）**。

## まとめ: 各 PE はどの階層を使うか

| 対象 PE | 隣接 PE の位置 | 使う階層 | 命令 |
|---------|--------------|---------|------|
| PE 1-3 | 同一 MAB 内 | PE 間 | `msl` |
| PE 0（MAB 1-15）| 隣接 MAB | L1BM 経由 | `l1bmd+1 + msr*3` |
| PE 0（MAB 0, L1B 1-7）| 隣接 L1B | L2BM 経由 | `l2bm + l2bmb` |
| PE 0（MAB 0, L1B 0, Sec 1-7）| 隣接 Section | PDM 経由 | `mvp` |
| PE 0（MAB 0, L1B 0, Sec 0）| なし | 境界条件 | 値 = 0 |

実際の `@stencil` ディレクティブでは、これら 5 種の結果を `maskr`/`ipassa`（条件付き代入）でマージして、各 PE に正しい左隣の値を配置する。マージの詳細は `05-stencil1d/README.md` を参照。

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `stencil1d_debug.vsm` | 展開済み vsm（各命令に日本語コメント付き） |
| `test_stencil1d_debug.c` | 可視化アプリ（各ステージの説明付き） |
| `build.ninja` | ビルド定義 |

## 実行方法

```bash
ninja build    # assemble3 + host C ビルド
ninja test     # emu:lib で実行
```

## 関連

| example | 内容 |
|---------|------|
| `05-stencil1d` | 全階層を一括実行する E2E テスト。README に袖交換の全体像あり |
| `12-l1b-crossing` | cross L1B 単体の検証（Stage 4 に対応） |
| `13-l2b-crossing` | cross L2B 単体の検証（Stage 5 に対応） |
