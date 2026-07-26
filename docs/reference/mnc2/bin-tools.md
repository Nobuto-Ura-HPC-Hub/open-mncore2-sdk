# libmnc2-kit 付属コマンドラインツール

libmnc2-kit に同梱される CLI ツール群 (`$PREFIX/bin/`) のリファレンス。
すべて **実機 (pod) 専用**で、`libgpfn3.so` に動的リンクしている。
SDK の `activate` を実行すると `$PREFIX/bin` が `PATH` に入るため、
ターミナルから直接起動できる。

デバイス番号の指定は環境変数 `GPFN3_DEVICE` (default=0) で行う。

## 一覧

| コマンド | 役割 |
|---|---|
| `pio-read`  | BAR2 (PIO レジスタ) から 1 ワード読み出し |
| `pio-write` | BAR2 (PIO レジスタ) に 1 ワード書き込み |
| `send-dma`  | Host → PDM へ少量データを DMA 送信 |
| `read-pdm`  | PDM 領域を mmap 読み出し → stdout (バイナリ) |
| `write-pdm` | stdin から PDM 領域に mmap 書き込み |
| `run-idma`  | `assemble3 --loader` が生成した `.idma.dat` を実行 |

すべて引数を省略、または不正な引数を渡すと usage を stderr に出力して終了する。

## pio-read

```
pio-read ADDR
```

BAR2 の 1 ワード (u32) を読み出して 16 進で stdout に出力。
`ADDR` は 16 進 (`0x70`) でも 10 進でも可。

例:

```sh
pio-read 0x70    # WDBIT_PCIe 状態を確認
```

## pio-write

```
pio-write ADDR VALUE
```

BAR2 の 1 ワードに `VALUE` を書き込む。
`ADDR` / `VALUE` は 16 進 / 10 進どちらでも可。

例:

```sh
pio-write 0x00 0x1       # デバイス reset (HW 仕様に依存)
```

## send-dma

```
send-dma ADDR [DATA...]
```

Host から PDM (`ADDR` バイトオフセット) へ少量データを DMA 送信する。
`DATA` が無ければ stdin から binary を読む。`DATA` に指定できる形式:

- `f64:1.0` — IEEE 754 double を 8 byte に encode
- `f32:1.0` — IEEE 754 float を 4 byte に encode
- `bf16:1.0` — brain-float 16 を 2 byte に encode
- `0xHH...` — hex dump (バイト列)

例:

```sh
# f64 1.0 を PDM offset 0 に書き込む
send-dma 0 f64:1.0

# stdin から 1 KiB のバイナリを投入
dd if=/dev/urandom bs=1024 count=1 | send-dma 0x1000
```

## read-pdm

```
read-pdm ADDR [LEN]
```

PDM の `ADDR` バイトオフセットから `LEN` バイトを mmap して stdout にバイナリ出力。
`LEN` を省略すると 8 byte 読む。

例:

```sh
# PDM 先頭 64 バイトを hex dump
read-pdm 0 64 | xxd
```

## write-pdm

```
write-pdm ADDR
```

stdin から読んだバイナリを PDM `ADDR` バイトオフセットに mmap 書き込み。
書き込みサイズは stdin の EOF までの全バイト。

例:

```sh
# f64 1.0 をファイル経由で書き込み
printf '\x00\x00\x00\x00\x00\x00\xf0\x3f' | write-pdm 0
```

## run-idma

```
run-idma PATH.idma.dat [--no-reset] [--timeout SEC] [--capture X:N]* [WD ...]
```

`assemble3 --loader` で生成した `.idma.dat` をそのまま実機に流して実行する。

- `PATH.idma.dat` — assemble3 --loader 生成ファイル
- `--no-reset` — device open 直後の `gpfn3_reset_device` を省略
- `--timeout SEC` — busy-poll に SIGALRM ベースの timeout を仕掛ける (default 10 秒、0 で無限)
- `--capture X:N` — Capture X (`B`/`C`/`D`/`E`) を threshold N で arm (複数指定可)。
  終端で `# Capture X snapshot:` として snapshot を出力
- `WD ...` — WD 番号 (0..63) を列挙すると kick 後に BAR2 0x70 (WDBIT_PCIe) を
  busy-poll して、各 WD の立ち下がり時刻 (`wall_ns` + chip 命令 counter) を記録・表示

例:

```sh
# 01-nop/_build/nop.idma.dat を実行、デフォルト timeout 10 秒
run-idma _build/nop.idma.dat

# Capture B を arm し、WD2 (SEND_TAG) / WD6 (RECV_TAG) の遷移を観測
run-idma _build/add1.idma.dat --capture B:2 2 6
```

## 補助コマンド (`set-capture` / `reset`)

`run-idma` と同じディレクトリで build される `set-capture` と `reset` は
手動デバッグ用の単機能ツール。配布 tarball には含めない (dev で使う)。
`run-idma` に `--capture` と reset が統合されているため、ほとんどの場合はこちらで足りる。
