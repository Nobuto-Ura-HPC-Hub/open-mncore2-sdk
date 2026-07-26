# mnc2-inspect-kit 0.1.6 API Reference

## mnc2_kernel_inspect.h — カーネルバイナリ検査

.idma.dat (MN-Core 2 命令バイナリ) から DMA 関連タグを抽出する。

### 定数

| 定数 | 値 | 説明 |
|------|-----|------|
| `MNC2_IMODE_FLAT` | 0 | Flat モード (PE x2 + MV x1) |
| `MNC2_IMODE_AUTO` | 1 | Auto Stride モード (PE x3 + MV x1) |
| `MNC2_IMODE_LOOP` | 2 | Loop 展開モード |

### 関数

#### `int mnc2_block_imode(const uint8_t block[128])`

128 バイトの命令ブロックから命令モードを判定する。

- **戻り値**: `MNC2_IMODE_*` 定数。不正なら -1

#### `int mnc2_block_tag_wd(const uint8_t block[128], int pe_idx)`

PE 命令中の待ち合わせタグ (tag_wd) を抽出する。

- **pe_idx**: PE 命令のインデックス (Flat: 0-1, Auto: 0-2)
- **戻り値**: tag_wd 値 (0-255)。範囲外なら -1

#### `int mnc2_block_mvid(const uint8_t block[128])`

MV 命令中の MVID タグを抽出する。

- **戻り値**: MVID 値 (0-255)。エラーなら -1

#### `int mnc2_idma_scan_tags(...)`

.idma.dat 全体から非ゼロの tag_wd / mvid を収集する。

- **data_size**: 128 の倍数であること
- **戻り値**: 0 = 成功, -1 = 引数不正

## mnc2_hw_info.h — HW 制約情報

MN-Core 2 のトポロジ・メモリ容量・DMA 制約を定数として提供する。
全フィールドがコンパイル時定数であり、実行環境に依存しない。

### 構造体 `mnc2_hw_info_t`

| フィールド | 型 | 値 | 説明 |
|-----------|-----|-----|------|
| ngroups | unsigned int | 4 | グループ数 |
| nl2b_per_group | unsigned int | 2 | L2B / group |
| nl1b_per_l2b | unsigned int | 8 | L1B / L2B |
| nmab_per_l1b | unsigned int | 16 | MAB / L1B |
| npe_per_mab | unsigned int | 4 | PE / MAB |
| total_pe | unsigned int | 4096 | 全 PE 数 |
| pdm_size_lw | uint64_t | 524288 | PDM / group (LW) |
| dram_size_lw | uint64_t | 536870912 | DRAM / group (LW) |
| l2bm_size_lw | uint64_t | 32768 | L2BM / L2B (LW) |
| lm_size_lw | unsigned int | 2048 | LM / PE 片面 (LW) |
| grf_size_lw | unsigned int | 256 | GRF / PE 片面 (LW) |
| nlm_per_pe | unsigned int | 2 | LM 数 / PE |
| ngrf_per_pe | unsigned int | 2 | GRF 数 / PE |
| pdm_size_bytes | uint64_t | 4194304 | PDM / group (byte) |
| pdm_align_bytes | unsigned int | 8 | PDM アライメント (byte) |
| ddma_unit_bytes | unsigned int | 4 | DDMA 最小転送単位 (byte) |
| ddma_max_bytes | uint64_t | 4194304 | DDMA 最大転送サイズ (byte) |
| dmaid_max | unsigned int | 63 | dmaid 最大値 |
| wd_max | unsigned int | 63 | wd 最大値 |

### 関数

#### `int mnc2_get_hw_info(mnc2_hw_info_t *info)`

全フィールドに定数を代入する。

- **戻り値**: 0 = 成功, -1 = info が NULL

### メモリ単位

1 LW (Long Word) = 8 bytes。メモリ容量の `*_lw` フィールドは LW 単位。

## 変更履歴

| バージョン | 変更内容 |
|-----------|---------|
| 0.1.6 | sdk-base-kit の最小要件を 0.1.12 に更新 |
| 0.1.5 | アンインストールスクリプトの PREFIX 埋め込み対応。空ディレクトリ掃除追加 |
| 0.1.4 | lm_size_lw, grf_size_lw を長語単位に修正 |
| 0.1.3 | api-reference バージョン展開修正 |
| 0.1.2 | ソースコメント除去。api-reference にバージョン表示追加 |
| 0.1.1 | sdk-examples 対応。パッケージ構造変更（API 変更なし） |
| 0.1.0 | 初版 |
