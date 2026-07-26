# u02-run-idma: IDMA / IDMA2 関連の実機ツール群

assemble3 --loader で生成した `.idma.dat` を kick する実機ツールと、IDMA2
関連の分解ツールをまとめている。libgpfn3.so を直接リンクするので **実機 pod
でのみビルド・実行可能** (emu では libgpfn3.so が無いのでビルドすら通らない
想定)。

## ビルドとインストール

```bash
ninja              # ビルド (_build/ に生成)
ninja install      # ../bin/ にコピー (他の examples から参照可能にする)
```

前提: SDK activate 済み (`assemble3` が必要)。

## コマンド一覧

| コマンド | 用途 | 備考 |
|---|---|---|
| `_build/run-idma PATH.idma.dat` | 従来の IDMA で kernel を 1 回実行 | 実機で動作 |
| `_build/run-idma2 PATH.idma.dat` | 命令を Device-DRAM に置き、IDMA2 で kick | **kernel module 側の未対応のため pod では EFAULT で失敗** |
| `_build/write-DM [--invalid] [--via-dma] IDX DRAM_ADDR ENTRY_LEN` | DM に IDMA2 descriptor を 1 entry 書く | PIO 経由が既定。`--via-dma` は DDMA 経由の診断用 (pod では失敗見込み) |
| `_build/read-DM IDX [COUNT]` | DM から descriptor を読み出す | `V:0xADDR:0xLEN` 形式で出力 |
| `_build/set-descriptor-regset REGSET DM_INDEX [HEX ...]` | (低レベル) regset に DM 宛アドレスをセット | 単体では何も起きない。DDMA kick が必要 |
| `_build/kick-idma2 DM_INDEX DM_COUNT [--no-wait]` | DM セットアップ済み前提で `gpfn3_kick_inst_dma2` のみ実行 | **kernel module 側の未対応のため pod では EFAULT** |
| `_build/test-idma2-mmap [--dm-index IDX] [--dm-count CNT] [--dev PATH]` | BAR2 mmap で直接 IDMA_ADR/IDMA_KICK に書いて kick | 診断用。kernel ドライバ filter を bypass しようとする実験。pod 環境では mmap 自体が EINVAL で塞がれている |
| `_build/reset` | `gpfn3_reset_device` を呼ぶだけ | DDMA_STAT が stuck したときの復旧用 |
| `_build/set-capture <B\|C\|D\|E> <threshold>` | WDBIT_PCIe capture を arm | 中間シグナル取得の probe |

## descriptor フォーマット (write-DM / read-DM)

DM の 1 entry = u64 で、IDMA2 が読む「Device-DRAM のどこから何 byte fetch
するか」のタプル:

```
 63  62                     24 23         0
[V ] [       addr / 64       ] [ len / 128 ]
  └── valid bit
```

| bit | 意味 | 単位 |
|---|---|---|
| 63 | valid (1=有効, 0=skip) | - |
| 62:24 | device-DRAM アドレス (39 bits) | 64 byte |
| 22:0 | 転送長 (23 bits) | 128 byte |

### write-DM の使い方

```bash
# DM[0] に 「0x100000 番地から 0x80000 byte の命令」と書く
_build/write-DM 0 0x100000 0x80000

# DM[1] を無効化
_build/write-DM --invalid 1 0 0

# DDMA 経由で書く (診断用、pod では regset 設定が pdm=0x1000000000+ で失敗する見込み)
_build/write-DM --via-dma 0 0x100000 0x80000
```

`DRAM_ADDR` は 64 の倍数、`ENTRY_LEN` は 128 の倍数である必要があります (HW
側の boundary)。

### read-DM の出力形式

```bash
_build/read-DM 0 4
# V:0xADDR:0xLEN
# 1:0x100000:0x80000
# 1:0x180000:0x80000
# 0:0x0:0x0
# 0:0x0:0x0
```

## 補足: `gpfn3_set_regset` と device 側アドレス空間

`set-descriptor-regset` の裏にある本体 API。regset (= HPCIA/LPDMA のペア) に
「host 側 buffer の bus address」と「device 側アドレス」をセットする、DDMA
転送の準備用 API:

```c
gpfn3_error_t gpfn3_set_regset(gpfn3_device_id_t dev,
                               unsigned int regset,
                               const void* host,   // host 側 (DMA 登録済み buffer)
                               uint64_t    pdm);   // device 側アドレス
```

4 番目の引数名 `pdm` は「PDM 専用」ではなく、**device 側アドレス空間の
オフセット**を指す (歴史的な命名)。値域によって宛先が決まる:

| `pdm` の値域 | 指す device メモリ |
|---|---|
| `0x00000000+` | PDM (Group 0) |
| `0x1000000000+` | DM (Descriptor Memory) |
| (Device-DRAM のアドレス) | Device-DRAM (CH2/CH3 DDMA と組合せ) |

`gpfn3_set_descriptor_regset(dev, regset, host_table, dm_index)` は
`gpfn3_set_regset(dev, regset, host_table + dm_index, DM_DMA_ADDR + dm_index*8)`
に 1:1 展開される薄いラッパで、`pdm` を自動的に DM 宛 (`0x1000000000+`) に
固定するだけ。

いずれも単独では何も起きず、**直後に `gpfn3_kick_data_dma` を呼んで初めて
DDMA 転送が発火する**。

## テスト用アセンブリ

```
data/imm-f64-collect.vsm
data/fimm-f32-collect.vsm
```

`ninja` で `.idma.dat` まで生成される。

## 使い方

```bash
# u01-mmap で PDM に書き込み → カーネル実行 (IDMA) → u01-mmap で読み出し
../u01-mmap/_build/send-dma f64:1.0 f64:2.0 f64:3.0 f64:4.0
_build/run-idma _build/imm-f64-collect.idma.dat
../u01-mmap/_build/read-pdm 0 32 | xxd
```

## IDMA2 関連ツールの現状 (2026-04-20)

IDMA2 の正規パス (`gpfn3_kick_inst_dma2` → `EXPERIMENTAL_IDMA_KICK` ioctl) は
kernel module 0.15 で IDMA2 未対応のため **pod 環境では全滅**:

```
[mncore2-sdk:u02-run-idma]$ _build/run-idma2 _build/imm-f64-collect.idma.dat
FAIL: kick_inst_dma2:14
[mncore2-sdk:u02-run-idma]$ _build/kick-idma2 0 8
FAIL: kick_inst_dma2:14
```

迂回経路の実験として `test-idma2-mmap` を用意したが、BAR2 mmap も同じ理由で
塞がれている (EINVAL) ため動作しない。原因は次の 2 点:

- IDMA2 の機能分解と実機検証で確認した挙動
- kernel module 0.15 で入った IDMA2 の regression

pod で IDMA2 を使うには kernel module 側のパッチが必要 (PFN linux-driver
チーム対応)。それまでは **IDMA1 (`run-idma`) を使うこと**。

## 環境変数

| 変数 | 説明 | デフォルト |
|---|---|---|
| `GPFN3_DEVICE` | デバイス番号 | `0` |
