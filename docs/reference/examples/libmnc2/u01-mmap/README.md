# u01-mmap: 低レベル gpfn3 ツール群

libgpfn3.so を直接使い、PIO レジスタ / DMA / PDM mmap を操作するツール群。
mnc2 ライブラリは使わない。**実機環境（libgpfn3 が必要）でのみビルド可能。**

## ビルドとインストール

```bash
ninja              # ビルド（_build/ に生成）
ninja install      # ../bin/ にコピー（他の examples から参照可能にする）
```

前提: libgpfn3.so が `-lgpfn3` でリンクできること（実機 Pod 上）。

## コマンド

| コマンド | 用途 |
|---|---|
| `_build/pio-read ADDR` | PIO レジスタ読み出し |
| `_build/pio-write ADDR VALUE` | PIO レジスタ書き込み |
| `_build/pio ADDR [VALUE]` | read/write 統合版。VALUE 省略で read、指定で write |
| `_build/send-dma [--channel NAME] ADDR [DATA...]` | DMA 送信 (Host→Device)。channel 省略時は PDM (CH0) |
| `_build/recv-dma [--channel NAME] ADDR LEN` | DMA 受信 (Device→Host)。channel 省略時は PDM |
| `_build/read-pdm ADDR [LEN]` | PDM mmap → stdout (バイナリ) |
| `_build/write-pdm ADDR [DATA...]` | PDM mmap 書き込み。DATA 形式は send-dma と同じ。省略時は stdin |

### `--channel NAME` で指定できる送信先

| NAME | チャネル | 宛先 |
|---|---|---|
| `pdm` (default) | CH0 | PDM |
| `dram` | CH3 | Group 0 Device-DRAM |

`gpfn3_tool <command>` でも実行可能。

## 使用例

```bash
# ENDIAN_CTRL=0 で double 1.0 を DMA 送信 → mmap で観測
_build/pio-write 0x40 0x0
_build/send-dma f64:1.0
_build/read-pdm 0 8 | xxd

# ENDIAN_CTRL=1 で同じテスト
_build/pio-write 0x40 0x1
_build/send-dma f64:1.0
_build/read-pdm 0 8 | xxd

# PDM に直接書き込み
printf '\x3f\xf0\x00\x00\x00\x00\x00\x00' | _build/write-pdm 0
_build/read-pdm 0 8 | xxd
```

## send-dma のデータ形式

```bash
send-dma 0x3f 0xf0 0x00 ...    # バイト列
send-dma 0x3c00                 # 16bit LE (桁数で自動判定)
send-dma 0x3f800000             # 32bit LE
send-dma 0x3ff0000000000000     # 64bit LE
send-dma f64:1.0                # double
send-dma f32:1.0                # float
send-dma bf16:1.0               # bfloat16
echo -ne '\x...' | send-dma    # stdin からバイナリ
send-dma -o 1024 f64:1.0       # PDM offset 指定
```

## トラブルシュート

### mnc2_send が MNC2_ERROR_TIMEOUT (-5) で失敗する

`ddma_wait` が PIO 0x038 (DDMA_STAT) のポーリングでタイムアウトしている。

```bash
# PIO 0x038 の確認
_build/pio-read 0x038
# → 0x0000000000000000 なら正常
# → 0x0000000001010000 等、非ゼロなら DMA ステータスが残っている
```

復旧手順:

```bash
gpfn3-smi reset mnc2p42s0    # デバイス名は gpfn3-smi list で確認
_build/pio-read 0x038        # 0x0000000000000000 になることを確認
```

きっかけは `mnc2_send` をカーネル実行なしで単独実行したこと（2026-04-10）。`ddma_wait` がタイムアウトして関数は -5 を返すが、デバイス側の DMA は完了せずステータスが残る。以降の全 DMA 操作が `ddma_wait` でタイムアウトするようになる。`mnc2_open` は `gpfn3_reset_device` をコメントアウトしているため、再オープンしても自動クリアされない。

## 設計方針

- pio-read/write 以外は stdout にテキストを出さない（パイプ動作前提）
- エラーは stderr
- デバイス番号: 環境変数 `GPFN3_DEVICE` (デフォルト 0)
