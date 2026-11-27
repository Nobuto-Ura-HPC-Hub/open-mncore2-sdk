# 02-vecadd: ベクタの加算

## やること

`c[i] = a[i] + b[i]` を 4096 PE で計算する。`a` と `b` を `@distribute` で配り、各 PE が `dvadd`（fp64 の加算）を
1 回行い、`c` を `@collect` で戻す。`vecadd._vsm` は LLVM の MN-Core 2 向けの出力である。

03-vecadd-2d、04-vecadd-3d は、この example の `vecadd._vsm` と `vecadd.param` を使う。

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `vecadd._vsm` | 入力の `_vsm` |
| `vecadd.param` | 配置パラメタ |
| `expected.vsm` | vsmlink コマンドが生成する `.vsm` の golden |
| `test_vecadd.c` | ホスト側のテスト。`c[i] = a[i] + b[i]` を確かめる |
| `build.ninja` | ビルド定義 |

## メモリレイアウト (PDM、u64 単位)

| 用途 | slot | `:addr` | 待ちのタグ |
|------|------|---------|-----------|
| `a` | 8 | 0 | `:send_wait_tag #x10` |
| `b` | 16 | 4096 | なし |
| `c` | 24 | 131072 | `:recv_wait_tag #x1e` |

予約タグは `:reserved_wait_tag #x3f` である。

## ビルド・実行

```bash
ninja                # vsmlink コマンドで .vsm を生成
ninja verify         # golden との比較
ninja build-e2e      # assemble3 とホスト側のビルド
ninja test-emu-lib   # emu:lib で実行
ninja test-device    # 実機で実行
```
