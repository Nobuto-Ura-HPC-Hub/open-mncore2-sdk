# libmnc2 利用ガイド

## 概要

libmnc2 は MN-Core 2 (GPFN3) 用ホスト側 C ランタイムライブラリ。
DMA データ転送とカーネル実行を統一的な C API で提供する。

このビルドは **emu:process バックエンド専用** であり、
エミュレータ (`gpfn3_package_main`) を外部プロセスとして呼び出す。

## インストール内容

| ファイル | 内容 |
|---------|------|
| `include/mnc2.h` | 公開ヘッダ |
| `lib/libmnc2.a` | 静的ライブラリ (emu:process 専用、外部依存なし) |
| `share/mnc2/usage.md` | このファイル |

## ビルド方法

```bash
cc -I$PREFIX/include test_driver.c -L$PREFIX/lib -lmnc2 -o test_driver
```

libgpfn3 や libgpfn3dma へのリンクは不要。標準 C ライブラリのみに依存する。

ライブラリは `-DMNC2_EMU_PROCESS` 付きでビルドされている。

## 前提条件

- `gpfn3_package_main` が PATH に存在すること（SDK の `activate` で自動設定される）
- 環境変数の設定:

| 環境変数 | 内容 | 例 |
|---------|------|-----|
| `MNC2_EMU_CONFIG` | エミュレータ設定 JSON のパス | `/path/to/config.json` |

## 最小サンプル

```c
#include <mnc2.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    /* 1. デバイスオープン (MNC2_EMU_CONFIG で emu 自動判定) */
    mnc2_device_t* dev = mnc2_open(0);
    if (!dev) { fprintf(stderr, "open failed\n"); return 1; }

    size_t size = 8 * sizeof(double);  /* 8 要素 */
    double* sbuf = mnc2_alloc_host_buffer(dev, size);
    double* rbuf = mnc2_alloc_host_buffer(dev, size);
    memset(rbuf, 0, size);

    /* 2. テストデータ準備 */
    for (int i = 0; i < 8; i++) sbuf[i] = (double)(i + 1);

    /* 3. Host→PDM 転送 (エンディアン変換は自動) */
    mnc2_send(dev, sbuf, /*pdm_offset=*/0, size,
              0x10);  /* カーネルの wait i10 に対応 */

    /* 4. カーネル実行 */
    mnc2_kernel_t* k = mnc2_load_kernel("kernel.idma.dat");
    mnc2_exec_kernel(dev, k);
    mnc2_free_kernel(k);

    /* 5. PDM→Host 転送 (auto-flush + エンディアン変換は自動) */
    mnc2_recv(dev, rbuf, /*pdm_offset=*/0, size,
              0x19 /* nop.vsm: collect → PDM の mvp done flag */);

    /* 6. 結果確認 */
    for (int i = 0; i < 8; i++) printf("%.1f ", rbuf[i]);
    printf("\n");

    /* 7. クリーンアップ */
    mnc2_free_host_buffer(dev, sbuf, size);
    mnc2_free_host_buffer(dev, rbuf, size);
    mnc2_close(dev);
    return 0;
}
```

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

### このビルドの制限

- **emu:process バックエンドのみ対応**
- `MNC2_EMU_CONFIG` 未設定の場合、`mnc2_open()` は NULL を返す
- 実機バックエンドが必要な場合は別途フルビルド版が必要

## API 一覧

詳細は `mnc2.h` のコメントおよび `api-reference.md` を参照。

| 関数 | 概要 |
|------|------|
| `mnc2_open(device_number)` | デバイスオープン |
| `mnc2_close(dev)` | デバイスクローズ |
| `mnc2_alloc_host_buffer(dev, size)` | DMA バッファ確保 |
| `mnc2_free_host_buffer(dev, buf, size)` | DMA バッファ解放 |
| `mnc2_send(dev, buf, pdm_offset, size, send_wait_tag)` | Host → PDM 転送 (エンディアン変換自動) |
| `mnc2_recv(dev, buf, pdm_offset, size, recv_wait_tag)` | PDM → Host 転送 (auto-flush + エンディアン変換自動) |
| `mnc2_set_endian_ctrl(dev, ec)` | エンディアン変換方式設定 (デフォルト: 4) |
| `mnc2_get_endian_ctrl(dev)` | 現在のエンディアン変換方式取得 |
| `mnc2_load_kernel(path)` | カーネルファイル読み込み |
| `mnc2_free_kernel(kernel)` | カーネル解放 |
| `mnc2_exec_kernel(dev, kernel)` | カーネル実行 |
