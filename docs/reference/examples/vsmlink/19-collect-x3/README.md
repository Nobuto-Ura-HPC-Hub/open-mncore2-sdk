# 19-collect-x3: `@collect` を 3 回使って PE の位置を回収する

## やること

各 PE の `$subpeid`（MAB の中の PE 番号、0〜3）、`$mabid`（0〜15）、`$l1bid`（0〜7）を、3 回の `@collect` で
1 つずつホストに回収する。directive は `@collect` だけを使い、値は `lpassa` で LM に写してから回収する。

17-boundary-collect は境界フラグを 1 回の `@collect` で回収する。こちらは位置そのものを 3 回に分けて回収する。

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `boundary-collect._vsm` | 入力の `_vsm` |
| `boundary-collect.param` | 配置パラメタ |
| `expected.vsm` | vsmlink コマンドが生成する `.vsm` の golden |
| `test_boundary_collect.c` | ホスト側のテスト。3 つの出力を PDM から読み出す |
| `build.ninja` | ビルド定義 |

## メモリレイアウト (PDM、u64 単位)

| 用途 | slot | `:addr` | 待ちのタグ |
|------|------|---------|-----------|
| `$subpeid`（1 回目の `@collect`）| 8 | 0 | `:recv_wait_tag #x1e` |
| `$mabid`（2 回目）| 16 | 4096 | `:recv_wait_tag #x1f` |
| `$l1bid`（3 回目）| 24 | 8192 | `:recv_wait_tag #x20` |

3 つの `@collect` は、それぞれ別の受信のタグを使う。

## ビルド・実行

```bash
ninja                # vsmlink コマンドで .vsm を生成
ninja verify         # golden との比較
ninja build-e2e      # assemble3 とホスト側のビルド
ninja test-emu-lib   # emu:lib で実行
ninja test-device    # 実機で実行
```
