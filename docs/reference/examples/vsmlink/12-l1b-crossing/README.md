# 12-l1b-crossing: cross L1B 単体テスト

## 概要

L1B（64 PE）を越えて、隣接 L1B のデータを取得する命令シーケンスを単体で検証するテスト。`@stencil` ディレクティブが生成する 7 フェーズのうち、cross L1B 部分に対応する。

## 前提: なぜ cross L1B が必要か

MN-Core 2 では、PE が直接通信できるのは同じ MAB 内の 4 PE だけ。cross MAB には L1BM を経由する（`l1bmd+1`）が、L1BM は L1B 内の 16 MAB 分のデータしか持たない。

L1B 0 の MAB 0 にとって「左隣」は前の L1B の MAB 15（PE 63）だが、`l1bmd+1` で MAB 15 のスロットを読むと自分の L1B 内の MAB 15 のデータが来てしまう。正しいデータを得るには、前の L1B のデータを L2BM 経由で自分の L1BM に書き込む必要がある。

## トポロジ

```
L1B 0 (MAB 0-15, PE 0-63)
  └─ L1BM 0（64 PE 分のデータを保持）
L1B 1 (MAB 0-15, PE 64-127)
  └─ L1BM 1
...
L1B 7 (MAB 0-15, PE 448-511)
  └─ L1BM 7
└──── L2BM（8 L1B 間の共有バッファ）────┘
```

L2BM は L2B セクション内の 8 L1B 間で共有される通信バッファ。`l2bm` / `l2bmb` 命令で L1BM ↔ L2BM のデータ転送を行う。

## データパス（5 ステップ）

offset=-1（左隣取得）の場合:

```
Step 1: PE → (l1bmd) → L1BM      全 PE のデータを L1BM に集約
Step 2: L1BM → (l2bm@N) → L2BM   各 L1B の L1BM → L2BM のスロット N
Step 3: L2BM → (l2bmb@N) → L1BM  L2BM スロット N-1 → L1B N の L1BM
Step 4: L1BM → (l1bmd+1) → PE    前の MAB のスロットを読み出す
Step 5: (msr*3)                    PE 内回転で PE 0 に配置
```

### Step 3 の詳細

`l2bmb@N` は L2BM のスロット **N-1** のデータを L1B N の L1BM に書き込む:
- `l2bmb@1`: L2BM スロット 0（= L1B 0 のデータ）→ L1B 1 の L1BM
- `l2bmb@2`: L2BM スロット 1（= L1B 1 のデータ）→ L1B 2 の L1BM
- ...
- `l2bmb@7`: L2BM スロット 6（= L1B 6 のデータ）→ L1B 7 の L1BM

**L1B 0 は `l2bmb@0` を発行しない**（左隣がセクション外のため）。L1B 0 の L1BM には Step 1 で書いた自身のデータが残る（ラップアラウンド）。

### Step 5: msr*3 の理由

`l1bmd+1` は MAB 単位のシフトで、結果は PE 3 に左端のデータが来る。1D ステンシルでは PE 0 に配置したいので、`msr` を 3 回実行して PE 内を回転させる。

## 期待結果

入力: PE[i] = i + 1

| 対象 PE | 結果 | 説明 |
|---------|------|------|
| L1B 1 MAB 0 PE[0-3] | 64, 61, 62, 63 | L1B 0 MAB 15 のデータ（正しい） |
| L1B 2 MAB 0 PE[0-3] | 128, 125, 126, 127 | L1B 1 MAB 15 のデータ（正しい） |
| L1B 0 MAB 0 PE[0-3] | 64, 61, 62, 63 | 自身の MAB 15（ラップ。cross L2B で修正） |
| L1B 0 MAB 1 PE[0-3] | 4, 1, 2, 3 | MAB 0 のデータ（MAB 内シフト。正しい） |

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `l1b_crossing.vsm` | 展開済み vsm（各命令に日本語コメント付き） |
| `test_l1b_crossing.c` | 検証プログラム（emu:lib で実行） |
| `build.ninja` | ビルド定義 |

## 実行方法

```bash
ninja build    # assemble3 + host C ビルド
ninja test     # emu:lib で実行
```

## 関連

| example | 内容 |
|---------|------|
| `13-l2b-crossing` | cross L2B（セクション間、PDM 経由）の単体テスト |
| `14-stencil1d-debug` | 全階層の中間値を段階的に観察するチュートリアル |
| `05-stencil1d` | 全階層を一括実行する E2E テスト |
