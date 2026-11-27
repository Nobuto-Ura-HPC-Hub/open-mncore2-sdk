# 30-neighbor-edge

隣の PE の値を受け取る関数の、端の扱いの例。端の PE（区切りの外から値を受け取ることになる PE）が受け取る値は、関数で選ぶ。

| 関数 | 端の PE が受け取る値 |
|---|---|
| `load_neighbor` / `get_neighbor` / `get_eoshift`（既定） | 自分の値（clamp） |
| `load_neighbor_periodic` / `get_neighbor_periodic` / `get_eoshift_periodic` | 反対の端の PE の値（周期境界） |
| `load_neighbor_boundary` / `get_neighbor_boundary` / `get_eoshift_boundary` | 最後の引数 `b` の、端の PE 自身の値 |

## 何をするか

1 次元の 3 点の和を 3 つの端の扱いで計算し、あわせて 32 x 128 の格子の上隣を周期境界で受け取る。結果は 4 つの配列に返す。

```c
double a = distribute(_arg_a);

collect(_arg_clamp,    load_neighbor(_arg_a, -1)               + a + load_neighbor(_arg_a, +1));
collect(_arg_periodic, load_neighbor_periodic(_arg_a, -1)      + a + load_neighbor_periodic(_arg_a, +1));
collect(_arg_fixed,    load_neighbor_boundary(_arg_a, -1, 0.0) + a + load_neighbor_boundary(_arg_a, +1, 0.0));
collect(_arg_up,       get_eoshift_periodic(a, -1, 5, 12));    // 格子（j = y * 32 + x）の y の上隣（j - 32）
```

ホストは `a[i] = i + 1` を送る。端の PE（PE の番号 0 と 4095）の期待値は次のとおり。

| 結果 | PE 0 | PE 4095 |
|---|---|---|
| clamp | `a[0] + a[0] + a[1]` | `a[4094] + a[4095] + a[4095]` |
| periodic | `a[4095] + a[0] + a[1]` | `a[4094] + a[4095] + a[0]` |
| fixed（`b` = 0.0） | `0 + a[0] + a[1]` | `a[4094] + a[4095] + 0` |
| up | PE j は `a[j - 32]`。y = 0 の行（PE 0〜31）は、y = 127 の行の値 `a[j + 4064]` を受け取る | |

## `.param`

- `:boundary_flags bf0`: `load_neighbor` 系が使う、境界のフラグの置き場所。ホストは exec の前に、フラグのデータ（`../data/` の `75c77b0f.bin`。build が `_build/collected_flags.bin` に写す）をこの番地へ送る
- `:buffer nbuf0`（`:buf_size 4160`）: `load_neighbor` 系と、`get_eoshift` の `M` が 10 以上の組が使う、PDM の作業領域
- 結果の 4 つの slot は、それぞれ別の受け取りのタグ（`#x1e`〜`#x1b`）を持つ

使い方の決まりは `share/mncl/api-reference.md` の「`load_neighbor` / `get_neighbor`」と「`get_eoshift`」の節。

## 実行

```bash
ninja build-e2e
ninja test-emu-lib    # エミュレータ（emu:lib）
ninja test-device     # 実機
```

4 つの結果を、端の PE を含めた 4096 要素ずつ期待値と比べる。すべて一致すれば `PASS 4 x 4096/4096` が出る。
