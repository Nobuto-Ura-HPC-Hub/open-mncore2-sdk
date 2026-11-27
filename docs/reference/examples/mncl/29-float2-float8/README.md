# 29-float2-float8

f32 を u64 に 2 つずつ詰めて計算する例。`float`（スカラ）は u64 に 1 つしか置かないので、多くの値を計算するときは `float2` / `float8` で書く
（コンパイラが `float` を自動でまとめることはしない）。

| 型 | PE の中の置き場所 | 受け渡し |
|---|---|---|
| `float2` | u64 1 つ。`.x` が上位 32 ビット、`.y` が下位 32 ビット | `distribute` / `collect`（`float2 *`）、`broadcast`、`get_eoshift`、`get_neighbor` |
| `float8` | u64 4 つ。要素 2c が u64 c の上位、2c + 1 が下位。演算は 1 命令で 8 要素 | `distribute4` / `collect4`（`float2 *`） |

```c
float2 x = distribute(_a);
float8 v = distribute4(_b);
collect(_out2, x * 2.0f + (float2)(x.y, x.x));   // スカラ倍、要素の入れ替え
collect4(_out8, v * v - v);                      // 1 命令で 8 要素
```

## ホストの並び

**ホストの C の `float2`（`cl_float2`、`float[2]`）をリトルエンディアンのまま u64 として送ると、`.x` と `.y` が入れ替わる。** ホストで MN-Core 2 の並びに詰める。

```c
uint64_t w = ((uint64_t)bits(x) << 32) | bits(y);   // x が .x
```

`distribute4` / `collect4` の PDM の並びは `double` の `distribute4` と同じ（u64 c を 4096 PE 分並べたものを c = 0〜3 の順に置く）。

## 確かめ方

`ninja build-e2e && ninja test-emu-lib`。値は、積が正確に表せるもの（2 進の仮数部が短いもの）にしてあるので、期待値を C の float の演算でそのまま作れる
（MN-Core 2 の fp32 の積は、仮数部の下位 5 ビット同士の部分積を省くので、一般の値では正確に丸めた積と最後のビットが違うことがある。dev manual 4.3）。
