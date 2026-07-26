# 13-l2b-crossing: cross L2B 単体テスト

## 概要

L2B セクション（512 PE）を越えて、隣接セクションのデータを取得する命令シーケンスを単体で検証するテスト。`@stencil` ディレクティブが生成する 7 フェーズのうち、cross L2B 部分に対応する。

## 前提: なぜ PDM を経由するか

MN-Core 2 では、L2BM は L2B セクション内の 8 L1B 間で共有されるバッファ。**異なるセクションの L2BM 間に直接転送命令がない**。そのため、セクション間のデータ転送は PDM（PCIe 直結メモリ）を一時バッファとして経由する必要がある。

## トポロジ

```
Section 0 (512 PE)                    Section 1 (512 PE)
├─L1B 0..7─┤                         ├─L1B 0..7─┤
└── L2BM 0 ┘                         └── L2BM 1 ┘
  Group 0 L2B 0                         Group 0 L2B 1

Section 2                             Section 3
└── L2BM 2 ┘                         └── L2BM 3 ┘
  Group 1 L2B 0                         Group 1 L2B 1

  ... (合計 8 section, 4 group × 2 L2B)

                  ┌─────────┐
                  │   PDM   │ ← 全セクションからアクセス可能
                  └─────────┘
```

## データパス（7 ステップ）

offset=-1（左隣取得）の場合:

```
C-1: PE → (l1bmd) → L1BM           全 PE のデータを L1BM に集約
C-2: L1BM → (l2bm@N) → L2BM       各 L1B → L2BM のスロット
C-3: L2BM → (mvp) → PDM            L2BM のデータを PDM に退避（collect）
C-4: PDM → (mvp) → 隣接 L2BM       PDM からシフトして書き戻す（redistribute）
C-5: L2BM → (l2bmb@0) → L1BM      L1B 7 のデータを L1B 0 の L1BM に配信
C-6: L1BM → (l1bmd+1) → PE        前の MAB のスロットを読み出す
C-7: (msr*3)                         PE 内回転で PE 0 に配置
```

### C-3/C-4 の詳細: PDM 経由のデータシフト

**collect（C-3）**: 各セクションの L2BM データを PDM の一時領域に退避する。

```
L2BM(Sec 0) → mvp → PDM $p8192   (512 u64)
L2BM(Sec 1) → mvp → PDM $p8704
...
L2BM(Sec 6) → mvp → PDM $p11264
L2BM(Sec 7) → mvp → PDM $p11776
```

**redistribute（C-4）**: PDM から **隣接セクション** の L2BM に書き戻す（1 つシフト）。

```
PDM $p8192  (Sec 0 のデータ) → mvp → L2BM(Sec 1)
PDM $p8704  (Sec 1 のデータ) → mvp → L2BM(Sec 2)
...
PDM $p11264 (Sec 6 のデータ) → mvp → L2BM(Sec 7)
```

Section 0 は左隣がないので受信しない（境界条件 = 0）。Section 7 のデータは右隣がないので転送しない。

### C-5: l2bmb@0 の役割

redistribute 後、L2BM には前セクションのデータが入っている。このうち **L1B 7 のスロット**（= 前セクションの最後の L1B）のデータを L1B 0 の L1BM に書き込む。

```
L2BM のスロット配置: lc1024 + N*64 が L1B N のデータ
L1B 7 のデータ: lc1024 + 7*64 = lc1472
l2bmb@0 $lc1472 $lb128  →  L1B 0 の L1BM に書き込み
```

## 期待結果

入力: PE[i] = i + 1

| 対象 PE | 結果 | 説明 |
|---------|------|------|
| Sec 0 L1B 0 MAB 0 PE[0-3] | 0, 0, 0, 0 | 左隣なし（境界条件） |
| Sec 1 L1B 0 MAB 0 PE[0-3] | 512, 509, 510, 511 | Sec 0 L1B 7 MAB 15 のデータ |
| Sec 2 L1B 0 MAB 0 PE[0-3] | 1024, 1021, 1022, 1023 | Sec 1 L1B 7 MAB 15 のデータ |
| Sec 0 L1B 1 MAB 0 PE[0-3] | 128, 125, 126, 127 | 同セクション内の L1B 0 MAB 15（正しい） |

Section 0 以外の L1B 0 では、前セクションの L1B 7 の MAB 15 PE 3 のデータが PE 0 に配置される（msr*3 で補正済み）。

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `l2b_crossing.vsm` | 展開済み vsm（各命令に日本語コメント付き） |
| `test_l2b_crossing.c` | 検証プログラム（emu:lib で実行） |
| `build.ninja` | ビルド定義 |

## 実行方法

```bash
ninja build    # assemble3 + host C ビルド
ninja test     # emu:lib で実行
```

## 関連

| example | 内容 |
|---------|------|
| `12-l1b-crossing` | cross L1B（L2BM 経由）の単体テスト |
| `14-stencil1d-debug` | 全階層の中間値を段階的に観察するチュートリアル |
| `05-stencil1d` | 全階層を一括実行する E2E テスト |
