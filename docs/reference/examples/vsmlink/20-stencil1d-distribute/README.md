# 20-stencil1d-distribute: 境界フラグを PE の上で計算する 1 次元ステンシル

## やること

05-stencil1d と同じ 1 次元の 3 点の和 `output[i] = left[i] + center[i] + right[i]` を、4096 PE で計算する。
左隣と右隣の値は `@get_neighbor` で受け取る。

05-stencil1d との違いは境界フラグの作り方である。05-stencil1d は `@boundary_flags` でホストが用意したフラグを配る。
この example は、各 PE が自分の位置のレジスタ（`$subpeid` など）からフラグを計算する。
そのため、境界フラグのための PDM の領域とホストからの送信が無い。計算の命令列は、廃止した
`@boundary_flags_compute` が展開していたものを、`_vsm` にそのまま書いてある。

## 名前の「distribute」について

この example は、入力データを `@distribute` で PDM から 4096 PE に配っている。名前の「distribute」は
もともと、境界フラグもホストから配っていた（`@boundary_flags` で配る）ころに付けたものである。
2026-03-27 に境界フラグを PE の上で計算する形に変えたので、いま配っているのは
入力データだけである。

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `stencil1d._vsm` | 入力の `_vsm` |
| `stencil1d.param` | 配置パラメタ |
| `expected.vsm` | vsmlink コマンドが生成する `.vsm` の golden |
| `test_stencil1d_distribute.c` | ホスト側のテスト |
| `build.ninja` | ビルド定義 |

## メモリレイアウト (PDM、u64 単位)

| 用途 | slot / ID | `:addr` | 待ちのタグ |
|------|-----------|---------|-----------|
| 入力 | slot 8 | 0 | `:send_wait_tag #x10` |
| 出力 | slot 16 | 4096 | `:recv_wait_tag #x1e` |
| `@get_neighbor` のバッファ | `nbuf0` | 16384（`:buf_size 4160`）| なし |

## ビルド・実行

```bash
ninja                # vsmlink コマンドで .vsm を生成
ninja verify         # golden との比較
ninja build-e2e      # assemble3 とホスト側のビルド
ninja test-emu-lib   # emu:lib で実行
ninja test-device    # 実機で実行
```
