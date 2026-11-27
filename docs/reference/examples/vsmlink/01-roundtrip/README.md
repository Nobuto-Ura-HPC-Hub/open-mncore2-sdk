# 01-roundtrip: PDM → PE → PDM の往復

## やること

`@distribute` で PDM から各 PE の LM に配ったデータを、計算せずにそのまま `@collect` で PDM に戻し、
入力と出力が一致することを確かめる。vsmlink コマンドの最小構成の例である。

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `roundtrip._vsm` | 入力: `@distribute` → `nop/24` → `@collect` だけの `_vsm` |
| `roundtrip.param` | 配置パラメタ |
| `expected.vsm` | vsmlink コマンドが生成する `.vsm` の golden |
| `test_roundtrip.c` | ホスト側のテスト。送ったデータと受け取ったデータを比べる |
| `build.ninja` | ビルド定義 |

## メモリレイアウト (PDM、u64 単位)

| 用途 | slot | `:addr` | 待ちのタグ |
|------|------|---------|-----------|
| 入力 | 8 | 0 | `:send_wait_tag #x10` |
| 出力 | 24 | 131072 | `:recv_wait_tag #x1e` |

予約タグは `:reserved_wait_tag #x3f` である。host-dma は byte 単位なので、出力の位置は `131072 * 8` byte になる。

## ビルド・実行

```bash
ninja                # vsmlink コマンドで .vsm を生成
ninja verify         # golden との比較
ninja build-e2e      # assemble3 とホスト側のビルド
ninja test-emu-lib   # emu:lib で実行
ninja test-device    # 実機で実行
```
