# 31-heat-diffusion-2d

2 次元の熱拡散（ヤコビ法。上下左右の 4 点の平均で更新することを繰り返す）を、`get_eoshift_boundary` で計算する例（`double`）。
格子の外側は温度 `B` = 1.0 に固定されているとみなす。

## 何をするか

格子は 32 x 128（4096 点。1 チップ）。点 (x, y) を、PE の番号 `j = y * 32 + x` の PE が受け持つ。

```c
double u = distribute(_arg_u);

#pragma unroll
for (int s = 0; s < STEPS; s++) {                      // STEPS = 4
    double l = get_eoshift_boundary(u, -1, 0, 5,  B);   // u[j - 1]（x - 1）
    double r = get_eoshift_boundary(u,  1, 0, 5,  B);   // u[j + 1]（x + 1）
    double d = get_eoshift_boundary(u, -1, 5, 12, B);   // u[j - 32]（y - 1）
    double t = get_eoshift_boundary(u,  1, 5, 12, B);   // u[j + 32]（y + 1）
    u = (l + r + d + t) * 0.25;
}
collect(_out, u);
```

- x の ±1 は `N = 0`、`M = 5`（ずれ 1、区切りは 1 行 32 点）。y の ±1 は `N = 5`、`M = 12`（ずれ 32、区切りは 4096 点全体）
- 区切りの外から値を受け取ることになる PE（行の両端と、先頭と末尾の行）は、隣の代わりに `B` を受け取る。
  そのため、外周の PE かどうかを判定せずに、4096 点をすべて同じ式で更新できる
- `get_eoshift_boundary` は計算の途中の値を受け取るので、1 回の kernel の実行の中で 4 回更新できる
- 既定の `get_eoshift`（端の PE は自分の値を受け取る）にすると、外周から熱が出入りしない計算になる

## `.param`

- `:buffer nbuf0`（`:buf_size 4160`）: y の ±1（`M = 12`）は PDM を経由するので、作業領域が要る（576 u64 以上）

使い方の決まりは `share/mncl/api-reference.md` の「`get_eoshift`」の節。

## 照合

ホストは、同じ計算を C で行って期待値を作り、4096 点すべてを**ビット単位で**比べる（許容誤差を設けない）。

- 期待値は、`get_eoshift_boundary` の定義（区切りの外は `B`）どおりに上下左右の値を取り、同じ式で 4 回更新して作る
- 初期値は `k / 16`（k は 0〜255）で、2 進で正確に表せる。演算は足し算と `* 0.25` だけなので、4 回の更新の後も丸めが起きず、
  MN-Core 2 とホストの値が足す順番によらず一致する。そのためビット単位で比べられる
- 初期値は、隣を取り違えると値が変わるように選んである。外周（端の PE）も含めて比べる

## 実行

```bash
ninja build-e2e
ninja test-emu-lib    # エミュレータ（emu:lib）
ninja test-device     # 実機
```

すべて一致すれば `PASS 4096/4096` が出る。
