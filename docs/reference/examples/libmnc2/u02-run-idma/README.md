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
| `_build/repeat-idma PATH.idma.dat MAX_COUNT` | 同じ命令列を連続 kick して IDMA のあふれを観測 | 下記「repeat-idma でのあふれの観測」 |

## run-idma の仕組み

```
run-idma PATH.idma.dat [--no-reset] [--timeout SEC] [--capture X:N]* [WD ...]
```

### 実行の流れ (`run_idma.c`)

1. (`--no-reset` 省略時) `gpfn3_reset_device` でデバイスをリセット
2. `--timeout` に基づき SIGALRM ベースのタイムアウトを仕掛ける (busy-poll の各 iteration で check)
3. `--capture X:N` が指定された分だけ、`gpfn3_capture_set_inst(dev, sel, threshold)` で Capture B/C/D/E を threshold で arm (kick より前)
4. `now_stamp()` で `wall_ns`/`inst`/`all_nop` を記録 (`event_load`、`.idma.dat` ロード直前)
5. `.idma.dat` をロード
6. `now_stamp()` で記録 (`event_kick`、kick 直前)
7. `gpfn3_kick_inst_dma` で IDMA kick
8. IDMA_STAT (PIO 0x018) を busy-poll し、kick 反映 (`n_dma`/`inst_in_ibuf` > 0) → 完了 (bits[20:0]=0) を待つ
9. WD 番号が指定されていれば、BAR2 0x70 (WDBIT_PCIe) を busy-poll し、各 WD が 1→0 に落ちるたびに `now_stamp()` で記録
10. arm した Capture の snapshot を `gpfn3_capture_read` で読み出す (`close_device` の前)
11. デバイスを close し、記録した内容を stdout に出力 (`0xLL`=load、`0xKK`=kick、`0xNN`=WD N 解除の順)

### 命令カウントの仕組み

`now_stamp()` は `inst`/`all_nop` を、次の 2 回の**独立した** PIO 呼び出しで取る。

```c
gpfn3_capture_get_inst(dev, inst);       /* 覗き 1 回目 */
gpfn3_capture_get_all_nop(dev, all_nop); /* 覗き 2 回目 (別の PIO 呼び出し) */
```

同時に 1 回で取れているわけではなく、1 行目を読んでから 2 行目を読むまでの間にも chip 側のカウンタは進み続ける。`inst`/`all_nop` とも、何かを初期化・開始させる操作ではなく、既に動いているカウンタの現在値を読むだけ (「即時のカウンタ取得」)。

### 分かっていないこと (実機で実測、確認依頼中)

- `inst`/`all_nop` がいつからカウントし始めるか。`gpfn3_reset_device` がこのカウンタを 0 に戻す保証はコードから読み取れない
- `all_nop` は kick 前後のアイドル区間でも一定速度で増え続けており、このカーネル固有の nop 数ではなく chip 側の背景カウンタに見える (実測では絶対値が数十億〜百億のオーダー)
- Capture B/C/D/E の threshold と実際の発火に対応が見えない。複数同時に arm すると異なるスロットが同一の値を返すことがある
- snapshot 内の `tag[N]` (N=1..255) が WD 番号と対応していない (同じ N でも全く別のカーネル実行で同じ値が出る)
- `mnc2_capture_set_trigger` の threshold は、arm 時点で既に累積 `inst` が超えている場合にどう振る舞うか未検証 (このカウンタ自体が reset で 0 に戻らない可能性があるため)

実行例は実機 (pod) が必要なため今後の課題。

## repeat-idma でのあふれの観測

```
repeat-idma PATH.idma.dat MAX_COUNT [--full-reset] [--no-reset]
```

同じ命令列を連続で kick し、毎回 IDMA_STAT (PIO 0x018) を読んで overflow (bit 21) が
立つまで続ける。何個目で立つか、そのとき `n_dma` と `inst_in_ibuf` がいくつかを見るための
ツール。`MAX_COUNT` は overflow が立たないまま回り続けないための上限。

### なぜ要るか

host 側が IDMA にできることは「キューがあふれないように要求を出す」ことだけだが、
何個で、どちらがあふれるのかが分かっていない。資料の記述も揃っていない。

- `n_dma` は 5 bit なので上限 31、`inst_in_ibuf` は 13 bit なので上限 8191
- bit 21 の overflow が、キューのあふれと命令バッファのあふれのどちらで立つのかが
  資料によって食い違っている

### リセットについて

**既定では `gpfn3_reset_device` を使わない。** このツールは意図的にキューを埋めるので、
DMA キューに残留がある状態を作る。`gpfn3_reset_device` はその状態で上限のない命令
カウンタ待ちに入り、戻ってこないことがある (`reset --explain` の case (b))。

代わりに SPI-A リセット (PIO 0x050 の `SPIA_MODULE | DMAQ_CLEAR | FRB_RESET`) を、
開始時と終了時の両方で打つ。PIO 書き込みだけなので必ず戻り、`DMAQ_CLEAR` がキューを掃除する。
終了時にも打つのは、overflow の状態とキューの残留を次の実行に持ち越さないため。

`--full-reset` を付けると `gpfn3_reset_device` を使うが、上記の理由で戻らなくなる場合がある。

### 出力

1 kick 1 行。`#` で始まる行はヘッダと集計。

```
# kernel: _build/nop_x8192.idma.dat (1398144 bytes)
# max_count: 200
# reset: SPI-A (PIO 0x050)
# idx kick_rc n_dma inst_in_ibuf overflow
0 0 1 0 0
1 0 2 0 0
...
# kicked: 38
# stopped: overflow at idx=37
# max n_dma: 31
# max inst_in_ibuf: 8191
# idle wait: idle (IDMA_STAT=0x0000000000000000)
# final reset: OK
```

kick の戻り値も毎行出している。overflow が立った後に kick がエラーを返すのか、黙って
受けるのかが分かっていないため。

### 命令列の用意

`gen_nop.py` で `nop` を任意の個数並べた `.vsm` を作れる。生成結果は `data/` にコミット済みで、
ビルドはそれを assemble するだけ。別の大きさを試すときにスクリプトを回して、出力を
コミットし直す。

```
python3 gen_nop.py 8192 data/nop_x8192.vsm
ninja
```

コミット済みの 3 サイズと、assemble 後の実測サイズ:

| vsm | nop 行数 | `.idma.dat` |
|---|---|---|
| `data/nop_x64.vsm` | 64 | 11008 B |
| `data/nop_x1024.vsm` | 1024 | 174848 B |
| `data/nop_x8192.vsm` | 8192 | 1398144 B |

1 行あたり約 170.7 B。

### 使い方

IDMA_STAT をツールの外から確認したいときは `u01-mmap` の `pio-read` を使う。

```
ninja -C ../u01-mmap install         # pio-read / pio-write / pio を ../bin に
ninja                                # repeat-idma をビルド

./_build/repeat-idma _build/nop_x8192.idma.dat 200
./_build/repeat-idma _build/nop_x1024.idma.dat 200
./_build/repeat-idma _build/nop_x64.idma.dat   200

../bin/pio-read 0x018                # IDMA_STAT を独立に確認
```

実機専用。エミュレータは IDMA のキューも命令バッファもモデル化しておらず、kick した命令を
その場で実行するだけなので、あふれは再現しない。

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
