# 34-soa2aos-roundtrip: 転置つき directive の対で、PDM の並びが元に戻ること

`@distribute_aos2soa` と `@collect_soa2aos` を対で使う最小の例である。PDM に番号の順で
並んだデータ（1 個 = 連続 4 u64）を配り、何も計算せずに書き戻すと、PDM の並びは配る前と
同じになる。この性質により、使うデータが 4096 個より少なくても先頭に連続して収まり、
ホストは並びの式を使わず、`@broadcast` などの読む側は先頭から連続で読める。

仕様は `share/vsmlink/directives-spec.md` の「@distribute_aos2soa / @collect_soa2aos」。
転置そのものと、転置だけでは並びが順列になる理由は `examples/30-soa2aos` と
`30-soa2aos/README.md` にある。

## 2 つの kernel

| kernel | 内容 | 確かめること |
|---|---|---|
| `roundtrip._vsm` | `@distribute_aos2soa` → `@collect_soa2aos` | 出力の全 16384 u64 が入力と一致する（対で元に戻る）|
| `pe_order._vsm` | `@distribute_aos2soa` → 転置の無い `@collect size 4` | 番号 k のデータが PE `4 * (k % 1024) + k / 1024` にある（配る側で PE の対応が変わる）|

どちらも LM0 の `$lm0` から `$lm6` をデータの 4 値、`$lm8` から `$lm14` を転置の作業の場所に使う。

## PDM の並び

| slot | 内容 | 並び |
|---|---|---|
| 8（入力）| 番号 k のデータの 4 値 `1000 + k` `2000 + k` `3000 + k` `4000 + k` | 番号の順。`in[4k .. 4k+3]` |
| 16（出力、roundtrip）| 同じ | 番号の順。入力と一致 |
| 16（出力、pe_order）| 同じ値 | 成分ごとの並び。成分 c の PE j の値が `out[4096c + j]` |

## 使い方

```
source scripts/overlay
ninja -C examples/34-soa2aos-roundtrip                       # vsmlink
ninja -C examples/34-soa2aos-roundtrip verify                # expected.vsm との diff
ninja -C examples/34-soa2aos-roundtrip test-emu-lib          # emu:lib で roundtrip
ninja -C examples/34-soa2aos-roundtrip test-emu-lib-pe-order # emu:lib で pe_order
ninja -C examples/34-soa2aos-roundtrip test-device           # 実機で roundtrip
ninja -C examples/34-soa2aos-roundtrip test-device-pe-order  # 実機で pe_order
```

転置の `dmwrite` から `dmread` までの待ちは 0 である（実機で確かめた値）。

## ファイル構成

| ファイル | 役割 |
|---|---|
| `roundtrip._vsm` | 対で使う kernel |
| `pe_order._vsm` | 配る側だけを通し、PE の順で書く kernel |
| `roundtrip.param` | slot 8（入力）と slot 16（出力）の PDM の番地と wait タグ。両 kernel で共用 |
| `roundtrip.stparam` | `:pe_shape (4096)` `:pe_local (4)` |
| `roundtrip.c` | ホスト。引数なしで roundtrip、`--pe-order` で pe_order を実行して検証する |
| `expected.vsm` / `expected_pe_order.vsm` | vsmlink の出力の期待値（`ninja verify`）|
| `build.ninja` | vsmlink + assemble3 + host ビルド + test target |
