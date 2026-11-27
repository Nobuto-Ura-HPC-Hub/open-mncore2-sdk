# 03-vecadd-2d: 2 次元のベクタの加算

## やること

`c[i][j] = a[i][j] + b[i][j]` を 64x64 の PE で計算する。カーネル（`_vsm`）と配置パラメタは 02-vecadd と同じもの
（`../02-vecadd/vecadd._vsm`、`../02-vecadd/vecadd.param`）を使う。
PE あたりの演算は 02-vecadd と同じである。

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `expected.vsm` | vsmlink コマンドが生成する `.vsm` の golden |
| `test_vecadd_2d.c` | ホスト側のテスト。`c[i][j] = a[i][j] + b[i][j]` を確かめる |
| `build.ninja` | ビルド定義 |

メモリレイアウトは 02-vecadd と同じである（`../02-vecadd/README.md`）。

## ビルド・実行

```bash
ninja                # vsmlink コマンドで .vsm を生成
ninja verify         # golden との比較
ninja build-e2e      # assemble3 とホスト側のビルド
ninja test-emu-lib   # emu:lib で実行
ninja test-device    # 実機で実行
```
