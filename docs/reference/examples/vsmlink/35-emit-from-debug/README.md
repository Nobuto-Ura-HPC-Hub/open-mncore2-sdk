# 35-emit-from-debug: `@emit_from` でデバッグ用に PE の値を書き出す

`@emit_from` は、**デバッグ用の directive** である。実機では PE の中の途中の値が見えない。`@emit_from` を使うと、HW PE ID が `<n>` の
PE の値を、呼ぶたびに PDM の slot の続きへ書き出せる。ホストは、kernel が終わった後で、その列を読む。この example は、その使い方を
実際のプログラムで見せる。仕様は `share/vsmlink/directives-spec.md` の「@emit_from」、HW PE ID の定義と MN-Core 2 の PE の番号との
対応表は `share/vsmlink/hw-pe-id.md` にある。

## このプログラムがしていること

1. **初期値を配る。** PE j（0〜4095）の初期値は `j + 1`（f64）。`@distribute` で全 PE の `$lr8` に配る
2. **`@broadcast` の値を足す。** `@broadcast` の入力は、f64 の `1, 2, 3, ..., 4197`。k 回目（1〜4197）の値 k を、全 PE の `$lr8` に
   `dvadd` で足す。初期値が PE ごとに違うので、足した後の値も PE ごとに違う
3. **足すたびに `@emit_from` で書き出す。**
   - 1〜2048 回目は、**HW PE ID 817** の PE の値を書き出す
   - 2049〜4197 回目は、**HW PE ID 529** の PE の値を書き出す
4. **最後に `@reduce` で全 PE の最終値の合計を取る。** 結果は、sub_pe_id（MAB の中の PE の番号 0〜3）ごとの 4 個の部分和である
5. **ホストが照合する。** `@reduce` の結果と、`@emit_from` が書き出した列を読んで、期待値と比べる

kernel は 1 つである。`@emit_from` が続きに書く番地は、kernel ごとに数え直すので、kernel を分けると、後の kernel が前の
kernel の書き出しを上書きする。

## 同期の取り方

`@emit_from` は、書き込みの完了をホストに知らせない（待ちのタグを付けない）。そのため、この example は、最後の `@reduce` の
`:recv_wait_tag`（`emit_debug.param` の slot 24）をホストが待つことで、kernel の終わりを知る。`@emit_from` の書き込みは、
その後に読む。

## 期待値

- k 回目の後の PE n の値: `(n + 1) + k(k + 1) / 2`
- 書き出しの領域の k 番目（1 から数える）の 64 u64 のブロックの、`n % 64` 番目が、その値である。n は、k が 2048 以下なら 817、
  それ以外は 529。ブロックの残りの 63 個は、同じ L1B（64 PE）のほかの PE の値である
- `@reduce` の sub_pe_id p の部分和: `2095104 + 1024 × (p + 1 + 4197 × 4198 / 2)`

すべて整数で、最大でも約 360 億なので、f64 で誤差なく表せる。照合は完全一致で行う。

## PDM の配置（`emit_debug.param`）

| slot | 用途 | `:addr`（u64） |
|---|---|---|
| 8 | `@distribute` の入力（4096 個） | 0 |
| 16 | `@broadcast` の入力（4197 個） | 4096 |
| 24 | `@reduce` の出力（`:recv_wait_tag` #x06） | 8448 |
| 40 | `@emit_from` の書き先（4197 × 64 = 268608 u64） | 16384 |

`@emit_from` は PDM 全体の範囲しか検査しない。この example では、slot 40 の先にほかの slot を置いていない。

## `_vsm` の作り方

`gen_vsm.py` が、`_vsm` を作る（4197 回の繰り返しなので、手で書かない）。1 回分は次のとおり。

```asm
	@broadcast _b from_param 16 size 1 $lr16
	lpassa $lr16 $ln0
	nop/2
	dvadd $ln0 $lr8 $lr8
	@emit_from em pe 817 to_param 40 size 1 $lr8
```

`lpassa` の後の `nop/2` は、assemble3 のハザードの検査で決めた数である（`$ln0` は LM1 なので、書いた後の読み出しは 2 ステップ空ける）。

## 実行

```bash
ninja              # vsmlink のみ
ninja build-e2e    # + assemble3 + C ビルド
ninja test-emu-lib # emu:lib で実行・検証（4197 回で 1 分半ほど）
ninja test-device  # 実機で実行・検証
ninja verify       # 短い版（8 回、切り替えは 4 回目）の出力と expected.vsm の diff
```

`expected.vsm` は、4197 回の出力が大きくなるので、短い版（8 回）のものである。
