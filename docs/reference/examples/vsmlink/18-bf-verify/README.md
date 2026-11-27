# 18-bf-verify: `@boundary_flags` の検証

## やること

`@boundary_flags` で配る境界フラグが正しいかを、`@get_neighbor` の結果を通して確かめる。

`bf-verify._vsm` は、`@boundary_flags` で配ったフラグ（`$lr416`）を、ホストが送った境界フラグのデータ（slot 24）で
上書きしてから `@get_neighbor` を行う。正しいデータを送れば `@get_neighbor` の結果は正しくなり、すべて 0 のデータを
送れば結果が変わる。後者を確かめるのが `test-ng` で、結果が食い違うことを確かめたときに PASS になる。

境界フラグのデータは `examples/data/gen_bf_golden.c` で作る。

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `bf-verify._vsm` | 入力の `_vsm` |
| `bf-verify.param` | 配置パラメタ |
| `expected.vsm` | vsmlink コマンドが生成する `.vsm` の golden |
| `test_bf_override.c` | ホスト側のテスト。境界フラグのデータを送り、`@get_neighbor` の結果を確かめる。`--expect-fail` を付けると、食い違いがあるときに PASS |
| `build.ninja` | ビルド定義 |

## メモリレイアウト (PDM、u64 単位)

| 用途 | slot / ID | `:addr` | 待ちのタグ |
|------|-----------|---------|-----------|
| 入力 | slot 8 | 0 | `:send_wait_tag #x10` |
| 出力 | slot 16 | 4096 | `:recv_wait_tag #x1e` |
| 送る境界フラグのデータ | slot 24 | 8192 | なし |
| `@boundary_flags` | `bf1` | 12288 | なし |
| `@get_neighbor` のバッファ | `nbuf0` | 16384（`:buf_size 4160`）| なし |

## ビルド・実行

```bash
ninja                 # vsmlink コマンドで .vsm を生成
ninja verify          # golden との比較
ninja build-e2e       # assemble3、ホスト側のビルド、境界フラグのデータの生成
ninja test-emu-lib    # emu:lib で、正しいデータ（_build/golden.bin）を送って実行
ninja test-ng         # emu:lib で、すべて 0 のデータ（_build/zero.bin）を送って実行
ninja test-from-17    # emu:lib で、../data/created/57b42dea.bin を送って実行
ninja test-device     # 実機で、正しいデータを送って実行
```
