# 07 の debug_read を外部シミュレーションする手順 (pod 手作業用)

目的: `mnc2_debug_read(MNC2_MEM_L2BM, addr=0, count=4)` が device で動かない
ため、put_l2bm を実行した後に **peek カーネル + read-pdm** で同じ 4 u64 が
取れるか手作業で確認する。

これは情報収集フェーズであり、ビルドシステムには組み込まれていない
(build.ninja は変更しない)。実機 pod 上で手作業で実行する想定。

## HW 制約メモ

- `mvp` の転送数 `nw` は **64 の倍数**である必要あり (`n4` は不可、`nw must
  be aligned to the multiple of 64` エラー)
- そのため peek_l2bm.vsm は 64 u64 を一括 L2BM → PDM に転送する。host 側では
  先頭 4 u64 = 32 バイトだけ見ればよい (残りはゴミ値)
- **PDM の mmap 範囲**: `gpfn3_map_pdm` は先頭 1 MiB しか mmap しないため、`read-pdm`
  は byte offset 0x100000 以降を読めない (garbage が返る)。peek の書き戻し先は
  必ず 1 MiB 内 (word < 131072) に取ること。ここでは `$p0@0` を使い、put 済
  の input data を上書きする

## 前提

- pod 上で SDK activate 済み (`assemble3`, `run-idma`, `read-pdm` が PATH)
- host-dma から tarball install 済み、または host-dma 直下で checkout 利用
- カレントディレクトリ: `share/examples/libmnc2/07-put-l2bm/` (= この README の場所)

## 手順

### 1. put_l2bm のビルド (既存、いつもどおり)

```sh
ninja
```

`_build/put_l2bm.idma.dat` が生成される。

### 2. peek_l2bm のアセンブル (手作業、ninja 管轄外)

```sh
assemble3 data/peek_l2bm.vsm --output-file _build/peek_l2bm --loader
```

`_build/peek_l2bm.idma.dat` が生成される。

### 3. 入力データを PDM[0..4095] に投入

`put_l2bm.idma.dat` は `wait i10` で host send を待つので、run-idma 単独
では動かない。代わりに `ex_put_l2bm` を通常どおり走らせる:

```sh
ninja test-device
# 失敗する (mnc2_debug_read が -1 を返すので FAIL exit)
# ただし put_l2bm カーネルは実行済で、L2BM にデータが入った状態で終了する
```

exit 1 で終わるが device 状態は保持される (mnc2_close は HW reset しない)。

### 4. peek カーネル実行 (L2BM → PDM[131072] に 4 u64 書く)

```sh
run-idma _build/peek_l2bm.idma.dat --no-reset
```

`--no-reset` は **必須**。省略すると gpfn3_reset_device が走って L2BM の
状態が消える。

### 5. PDM[0] から 4 u64 = 32 バイトを読み戻す

```sh
read-pdm 0 32 | xxd
```

期待出力 (バイト列、little-endian u64):

```
00000000: 0000 0000 0000 f83f 0000 0000 0000 0840  .......?......@
00000010: 0000 0000 0000 1240 0000 0000 0000 1840  .......@......@
```

(= 1.5, 3.0, 4.5, 6.0 の IEEE 754 double ビットパターン)

u64 として表示するなら:

```sh
read-pdm 0 32 | od -An -v -tx8 --endian=little
```

期待出力:

```
 3ff8000000000000 4008000000000000
 4012000000000000 4018000000000000
```

### 6. 比較元 (emu:lib での debug_read 値)

dev 環境で既に確認済み:

```
L2BM[0..3]: 0x3ff8000000000000 0x4008000000000000 0x4012000000000000 0x4018000000000000
```

### 7. 結果判定

手順 5 の出力が手順 6 と一致すれば **外部シミュレーション成立**。
一致しない場合、以下を疑う:

- endian 変換の有無 (mnc2_recv は ENDIAN_CTRL 経由で swap するが read-pdm は
  raw bytes を返す。emu の debug_read は内部値をそのまま出す)
- L2BM の state が peek 実行前に reset で消えている (--no-reset 付け忘れ)
- run-idma の default timeout で kernel が途中終了している
- ninja test-device が実は put_l2bm までたどり着かず、L2BM に何も入っていない
  (例: mnc2_open の DMA キュー残留警告で send が失敗)

## 確認してほしいこと

この手順でバイト列が取れたら、「07 と同じ観測が `vsm + run-idma + read-pdm` で
できる」ことが確定する。これが確認できたら、次フェーズで libmnc2 側の
`mnc2_debug_read` に device 実装を追加できる (内部でこの 3 ステップを踏む)。

取れなかった場合、どのステップで失敗したか (手順 3/4/5 のどこ) を教えてください。
