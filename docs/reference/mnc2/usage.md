# libmnc2 利用ガイド

## 概要

libmnc2 は MN-Core 2 (GPFN3) 用ホスト側 C ランタイムライブラリ。
DMA データ転送とカーネル実行を統一的な C API で提供する。

バックエンドは 3 つ (実機、emu:process、emu:lib) あり、`libmnc2.a` は 1 本で共通。どのバックエンドで動くかは、
実行時に読み込まれる `libgpfn3.so` で決まる (`LD_LIBRARY_PATH` で切り替える)。アプリケーションのコードは
バックエンドの違いを意識しない。詳細は `api-reference.md` の「3 つのバックエンド」。

## インストール内容

| ファイル | 内容 |
|---------|------|
| `include/mnc2.h` | 公開ヘッダ |
| `lib/libmnc2.a` | 静的ライブラリ (3 バックエンド共通) |
| `lib/emu-process/libgpfn3.so.0` | emu:process 用の libgpfn3 (エミュレータ `gpfn3_package_main` を別プロセスで起動する) |
| `lib/emu-lib/libgpfn3.so.0` | emu:lib 用の libgpfn3 (エミュレータをプロセス内で動かす) |
| `bin/` | 実機用のコマンドラインツール (`bin-tools.md`) |
| `share/mnc2/` | このファイル、`api-reference.md`、`bin-tools.md` |
| `share/examples/libmnc2/` | example 一式。各ディレクトリで `ninja` でビルドし、`ninja test-emu` / `test-emu-lib` / `test-device` で実行する |

実機用の `libgpfn3.so` は PFN 提供のもので、この kit には含まれない。

## ビルド方法

```bash
gcc -Wall -I$PREFIX/include -c app.c -o app.o
g++ -L$PREFIX/lib/emu-lib -L$PREFIX/lib app.o $PREFIX/lib/libmnc2.a -o app -lrt -lm -fopenmp -lgpfn3 -lmnc2
```

リンクに要るのは `-lmnc2 -lgpfn3 -lstdc++ -lrt -lm -fopenmp` (g++ でリンクすれば `-lstdc++` は不要)。
`-lgpfn3` はリンク時に記号を解決するためのもので、実行時にどの `libgpfn3.so` を読むかは `LD_LIBRARY_PATH` で決める。
`share/examples/scripts/examples.ninja` が同じ手順の実例。

カーネルは `assemble3 kernel.vsm --output-file kernel --loader` で `kernel.idma.dat` にする (SDK の `activate` で PATH に入る)。

## 実行方法 (バックエンドの切り替え)

| バックエンド | 実行時の設定 |
|---|---|
| emu:process | `MNC2_EMU_CONFIG=<エミュレータ設定 JSON> LD_LIBRARY_PATH=$PREFIX/lib/emu-process ./app`。`gpfn3_package_main` が PATH にあること (SDK の `activate` で入る) |
| emu:lib | `LD_LIBRARY_PATH=$PREFIX/lib/emu-lib ./app` |
| 実機 | `LD_LIBRARY_PATH=$PREFIX/lib ./app` (PFN 提供の `libgpfn3.so` と `/dev/gpfn3_X` が要る) |

エミュレータ設定 JSON の例は `share/examples/libmnc2/01-nop/data/config_pdm0.json`。`MNC2_EMU_CONFIG` を読むのは
emu:process の `libgpfn3.so` だけで、emu:lib と実機では使わない。

## 最小サンプル

```c
#include <mnc2.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* 1. デバイスオープン (バックエンドは読み込まれた libgpfn3.so で決まる) */
    mnc2_device_t dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "open failed\n"); return 1; }
    printf("backend: %s\n", mnc2_get_backend_name(dev));

    size_t size = 8 * sizeof(double);  /* 8 要素 */
    double* sbuf = mnc2_alloc_host_buffer(dev, size);
    double* rbuf = mnc2_alloc_host_buffer(dev, size);
    memset(rbuf, 0, size);

    /* 2. テストデータ準備 */
    for (int i = 0; i < 8; i++) sbuf[i] = (double)(i + 1);

    /* 3. Host→PDM 転送 (エンディアン変換は自動)。tag 0x10 はカーネルの wait i10 が消費する */
    mnc2_send(dev, sbuf, /*pdm_offset=*/0, size, 0x10);

    /* 4. カーネル実行 (完了は待たない。キューに空きが無ければ MNC2_ERROR_BUSY) */
    mnc2_kernel_t k = mnc2_load_kernel(dev, "kernel.idma.dat");
    if (mnc2_exec_kernel(k) != MNC2_SUCCESS) { fprintf(stderr, "exec failed\n"); return 1; }

    /* 5. PDM→Host 転送。tag 0x19 はカーネルの最後の mvp (collect → PDM) が立てる */
    mnc2_recv(dev, rbuf, /*pdm_offset=*/0, size, 0x19);

    /* 6. 結果確認 */
    for (int i = 0; i < 8; i++) printf("%.1f ", rbuf[i]);
    printf("\n");

    /* 7. クリーンアップ (free_kernel は IDMA が idle になるのを待ってから解放する) */
    mnc2_free_kernel(k);
    mnc2_free_host_buffer(dev, sbuf, size);
    mnc2_free_host_buffer(dev, rbuf, size);
    mnc2_close(dev);
    return 0;
}
```

send のタグはカーネルの `wait` が、recv のタグはカーネルのタグ付き MV が対応する。立てた flag はプログラムが
終わる前にすべて消費すること。規則は `api-reference.md` の「タグの規則」。

## 注意事項

### エンディアン変換

MN-Core 2 は big-endian、ホスト (x86) は little-endian。
`mnc2_send` / `mnc2_recv` は内部で自動的にエンディアン変換を行う。
呼び出し側での手動変換は不要。

変換方式は `mnc2_set_endian_ctrl` で変更できる（デフォルト: ENDIAN_CTRL=4、double に対応）。
通常はデフォルトのまま使用すること。

```c
// デフォルト (ENDIAN_CTRL=4) のまま使用
mnc2_send(dev, buf, offset, size, send_wait_tag);
mnc2_recv(dev, buf, offset, size, recv_wait_tag);

// float など異なる要素サイズが必要な場合は endian_ctrl を変更
mnc2_set_endian_ctrl(dev, 3);  /* ENDIAN_CTRL=3: 32bit 単位 swap */
mnc2_send(dev, buf, offset, size, send_wait_tag);
mnc2_set_endian_ctrl(dev, 4);  /* 元に戻す */
```

### size の制約

`mnc2_send` / `mnc2_recv` の `size` 引数は **8 の倍数**であること。
u64 (8 バイト) 単位の DMA 転送に由来する制約。

### バックエンドごとの違い

- emu:process は `mnc2_recv` のタグを待たず、PDM の写しを即座に読む。`mnc2_debug_read` の PDM は既知の不具合で使えない
- emu:lib はタグを待ち、消費前に同じタグを 2 回立てるとエラーで止まる
- 実機だけに `mnc2_read_pio` / `mnc2_write_pio` と `bin/` のツールが使える。`mnc2_debug_read` / `mnc2_debug_write` はエミュレータ専用

詳細は `api-reference.md` の各 API の記述を参照。

## API 一覧

全 API の signature、制約、戻り値は `api-reference.md` にある。主なもの:

| 関数 | 概要 |
|------|------|
| `mnc2_open(device_number)` / `mnc2_close(dev)` | デバイスのオープンとクローズ |
| `mnc2_get_backend_name(dev)` | 動いているバックエンドの名前 (`"device"` / `"emu:process"` / `"emu:lib"`) |
| `mnc2_alloc_host_buffer(dev, size)` / `mnc2_free_host_buffer(dev, buf, size)` | DMA バッファの確保と解放 |
| `mnc2_send(dev, buf, pdm_offset, size, send_wait_tag)` | Host → PDM 転送 (完了まで待つ。エンディアン変換自動) |
| `mnc2_recv(dev, buf, pdm_offset, size, recv_wait_tag)` | PDM → Host 転送 (完了まで待つ。エンディアン変換自動) |
| `mnc2_set_endian_ctrl(dev, ec)` / `mnc2_get_endian_ctrl(dev)` | エンディアン変換方式の設定と取得 (デフォルト: 4) |
| `mnc2_load_kernel(dev, path)` / `mnc2_free_kernel(kernel)` | カーネルファイルの読み込みと解放 |
| `mnc2_exec_kernel(kernel)` | カーネル実行 (待たない。空きが無ければ `MNC2_ERROR_BUSY`) |
| `mnc2_exec_kernel_blocking(kernel, limit)` | カーネル実行 (キューの残りが `limit` 以下になるまで待つ) |
| `mnc2_wait_idma_idle(dev)` / `mnc2_wait_ddma_idle(dev)` | 命令 DMA / データ DMA が idle になるまで待つ |
| `mnc2_reset(dev)` | デバイスのリセット (DMA キューとタグの flag を消す。PDM の中身は残る) |
