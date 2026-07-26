# 25-broadcast-reduce-add: `@broadcast` (l1bmp) の全 PE 同値放送 + reduce 検証

`@broadcast` directive が全 PE に同じ 1 値を放送すること、 それを `@reduce` で確認する example。

## `@broadcast` directive とは

「PDM のあるアドレスから全 PE に 1 u64 を届ける」 directive。 放送命令 `l1bmp`
(1 u64 を全 64 PE に放送) に展開される。 host は broadcast 領域を配りたい値 v で埋めて送り、
全 PE が同じ v を受け取る。

```asm
@broadcast _C from_param 16 size 1 $lr16
```

- `_C` — directive 内 ID
- `from_param 16` — `.param` の `:slot 16` を入力に使う
- `size 1` — 1 u64 broadcast
- `$lr16` — 配布先 PE レジスタ

## vsmlink がこれを vsm にどう展開するか

`src/templates/broadcast.vsm` で 3 段の転送に展開される (要点):

```asm
mvb/n64<tag> <pdm> $lc<lc>    # PDM から全 8 L2BM へ
l2bmb $lc<lc> $lb<lb>         # L2BM から全 64 L1BM へ
l1bmp $lb<lb> $lr16           # L1BM から全 64 PE へ放送
```

PDM の 1 値が全 4096 PE の `$lr16` に届く。 実際の展開は `_build/broadcast_reduce.vsm` を参照。

## 本 example の動作

各 PE で:

- `id` = PE ID (0..4095)   `@identify` で配布
- `C` = 全 PE 同値の定数    `@broadcast` (l1bmp) で放送
- `id + C` を計算 (`iadd`)
- `@reduce :liadd` で全 PE 合計

`@reduce` は MAB 内 4 PE 単位を保って縮約するので、 出力は 4 u64 (sub_pe_id 別の 4 部分和)。

## 期待値

sub_pe_id `p` (0..3) の PE 集合は `{i | i = 4k+p, k = 0..1023}` (1024 個ずつ)。 全 PE 同値 C なので:

```
sum_p = Σ_{k=0..1023} (4k + p + C)
      = 4·(0+1+...+1023) + 1024·p + 1024·C
      = 2095104 + 1024·p + 1024·C
```

host は C=100 を送る。 期待値は次のとおりで、 emu:lib で全 4 値の一致を確認済み。

| sub_pe_id | 期待値 |
|-----------|--------|
| 0 | 2,197,504 |
| 1 | 2,198,528 |
| 2 | 2,199,552 |
| 3 | 2,200,576 |

## ファイル構成

| ファイル | 内容 |
|---------|------|
| `broadcast_reduce._vsm` | カーネル本体 |
| `broadcast_reduce.param` | 配置パラメタ |
| `broadcast_reduce.stparam` | 構造パラメタ (1D, 4096 PE) |
| `test_broadcast_reduce.c` | host driver |
| `build.ninja` | ビルド定義 |

## ビルド・実行

```bash
source scripts/overlay
ninja -C examples/25-broadcast-reduce-add              # vsmlink のみ
ninja -C examples/25-broadcast-reduce-add build-e2e    # + assemble3 + C ビルド
ninja -C examples/25-broadcast-reduce-add test-emu-lib # + emu:lib で実行・検証
ninja -C examples/25-broadcast-reduce-add test-device  # + 実機で実行
```

## 関連

- `examples/22-identify-reduce-add` — `@identify` + `@reduce` のシンプル版
- `examples/24-odd-even-sort-reduce` — `[--view] <in.bin> <out.bin>` の参考元
