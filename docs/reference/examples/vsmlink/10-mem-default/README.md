# 10-mem-default: `.param` の `:place` を省略したときの往復

## やること

01-roundtrip と同じ往復（`@distribute` で PDM から PE に配り、計算せずに `@collect` で PDM に戻す）を、
`.param` の各 slot の `:place` を省略して行う。`:place` を省略すると `:pdm0` を指定したのと同じになる
（`share/vsmlink/param-spec.md`）。`_vsm` は 01-roundtrip と同じ内容で、`.param` だけが違う。

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `roundtrip._vsm` | 入力: `@distribute` → `nop/24` → `@collect` だけの `_vsm` |
| `roundtrip.param` | 配置パラメタ（`:place` を省略）|
| `expected.vsm` | vsmlink コマンドが生成する `.vsm` の golden |
| `test_roundtrip.c` | ホスト側のテスト。送ったデータと受け取ったデータを比べる |
| `build.ninja` | ビルド定義 |

## メモリレイアウト (PDM、u64 単位)

| 用途 | slot | `:addr` | 待ちのタグ |
|------|------|---------|-----------|
| 入力 | 8 | 0 | `:send_wait_tag #x10` |
| 出力 | 24 | 131072 | `:recv_wait_tag #x1e` |

## ビルド・実行

```bash
ninja                # vsmlink コマンドで .vsm を生成
ninja verify         # golden との比較
ninja build-e2e      # assemble3 とホスト側のビルド
ninja test-emu-lib   # emu:lib で実行
ninja test-device    # 実機で実行
```
