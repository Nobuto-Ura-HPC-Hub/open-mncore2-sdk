# 27-roundtrip-size4: @distribute と @collect の往復

## やること

`@distribute` で PDM から PE に配ったデータを、計算せずにそのまま `@collect` で PDM に戻し、
入力と出力が全要素で一致することを確かめる。検証したいのは、PDM 上の並びが `@distribute` と
`@collect` で一致していることである。

成分数（size）が 2 / 3 / 4 の 3 通りを回す。size 4 は `l1bmd` にマスクが付かない経路、
size 2 / 3 はマスク（`/1100` / `/1110`）が付く経路である。

## ファイル構成

| ファイル | 説明 |
|---------|------|
| `roundtrip{2,3,4}._vsm` | 入力: `@distribute` → `nop/24` → `@collect` だけの `_vsm` |
| `roundtrip{2,3,4}.param` | 配置パラメタ（3 つとも同じ内容） |
| `expected_roundtrip{2,3,4}.vsm` | vsmlink コマンドが生成する `.vsm` の golden |
| `test_roundtrip4.c` | ホスト側のテスト。size を引数で受け取る |
| `build.ninja` | ビルド定義 |

## メモリレイアウト (PDM、u64 単位)

| 用途 | slot | `:addr` | 待ちのタグ |
|------|------|---------|-----------|
| 入力 | 8 | 0 | `:send_wait_tag #x10` |
| 出力 | 24 | 131072 | `:recv_wait_tag #x1e` |

予約タグは `:reserved_wait_tag #x3f` である。

入力と出力の並びはどちらも `[成分 0 を 4096 PE 分][成分 1]…[成分 size-1]` である。
成分 k の PE p には `1000*(k+1) + p` を入れる。成分ごとに値域が違うので、並びが入れ替わると
必ず不一致になる。

host-dma は byte 単位なので、出力の位置は `131072 * 8` byte になる。

## ビルド・実行

```bash
ninja                # vsmlink コマンドで .vsm を生成
ninja verify         # golden との比較
ninja build-e2e      # assemble3 とホスト側のビルド
ninja test-emu-lib   # emu:lib で size 2 / 3 / 4 を実行
ninja test-device    # 実機で size 2 / 3 / 4 を実行
```

実機は 1 台を共有するので、`test-device` は深さ 1 の pool で直列に実行する。並列に起動すると、
実機の DMA が衝突して exec / recv が非決定的に失敗する。
