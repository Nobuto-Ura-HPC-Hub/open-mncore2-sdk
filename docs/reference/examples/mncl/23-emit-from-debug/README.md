# 23-emit-from-debug: `emit_from` でデバッグ用に PE の値を書き出す

`emit_from` は、**デバッグ用の API** である。実機では、PE の中の途中の値が見えない。`emit_from(name, pe, p, v)` を使うと、HW PE ID が `pe` の PE の値 `v` を、
呼ぶたびに PDM の `p` の続きへ書き出せる。ホストは、kernel が終わった後で、その列を読む。この example は、その使い方を、実際のプログラムで見せる
（vsmlink の `35-emit-from-debug` に当たる）。使い方の決まりは `share/mncl/api-reference.md` の「`emit_from`」の節にある。

## この kernel がしていること

1. **初期値を配る。** PE j（0〜4095）の初期値は `j + 1`（double）。`distribute` で全 PE に配る
2. **`broadcast` の値を足す。** `broadcast` の入力は、double の `1, 2, 3, ..., 100`。k 回目（1〜100）の値 k を、全 PE の `v` に足す。初期値が PE ごとに違うので、
   足した後の値も PE ごとに違う
3. **足すたびに `emit_from` で書き出す。** 1〜60 回目は **HW PE ID 817**、61〜100 回目は **HW PE ID 529** の PE を書き出す
4. **最後に `collect` で全 PE の最終値を返す。** ホストは、この完了を待って、kernel の終わりを知る

`pe` は、コンパイルの時点で決まる定数でなければならない。`#pragma unroll` で `k` のループを完全に展開して、`(k < 60) ? 817 : 529` を定数にする。

## 同期の取り方

`emit_from` は、書き込みの完了をホストに知らせない（待ちのタグを付けない）。そのため、この example は、最後の `collect` の完了（`.param` の slot 32 の
`:recv_wait_tag`）をホストが待つことで、kernel の終わりを知る。`emit_from` の書き込みは、その後に読む。

## 期待値

- k 回目の後の PE n の値: `(n + 1) + k(k + 1) / 2`
- 書き出しの領域の k 番目（1 から数える）の 64 u64 のブロックの i 番目が、L1B の先頭の PE（817 は 768、529 は 512）+ i の PE の値である。
  欲しい値（`pe % 64` 番目）のほかに、**同じ L1B の 63 PE の値も、一緒に残る**（書き込みの単位が 64 u64 であるため）
- 100 回 > 64 ブロックなので、L1BM（16 ブロック）と L2BM（64 ブロック）の専用の領域が溢れて、PDM へ書き出される経路も通る

すべて整数で、最大でも約 4 千なので、double で誤差なく表せる。照合は完全一致で行う。

## PDM の配置（`emit-from-debug.param`）

| slot | 用途 | `:addr`（u64） |
|---|---|---|
| 8 | `distribute` の入力（4096 個。`:send_wait_tag` #x10） | 0 |
| 16 | `broadcast` の入力（1〜100。ホストが exec の前に送り終えるので、タグは付けない） | 4096 |
| 32 | `collect` の出力（`:recv_wait_tag` #x1e） | 8192 |
| 24 | `emit_from` の書き先（100 × 64 = 6400 u64。待ちのタグは付けない） | 16384 |

`emit_from` は PDM 全体の範囲しか検査しない。slot 24 の先に、ほかの slot を置かないこと。

## 実行

```bash
ninja              # .cl -> ._vsm -> .vsm
ninja build-e2e    # + assemble3 + ホストの C
ninja test-emu-lib # emu:lib で実行
ninja test-device  # 実機で実行（実機で PASS を確認）
```
