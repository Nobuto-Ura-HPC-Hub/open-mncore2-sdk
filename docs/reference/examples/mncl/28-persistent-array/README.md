# 28-persistent-array

`persistent` の配列の例。**kernel の実行が終わっても値が PE の中に残る `double` の配列**を、2 つの kernel で共有する。
`persistent` の基本は `20-persistent`。

## 何をするか

`persistent-array.cl` に、配列 1 つと kernel 2 つを書いている。

```c
persistent double tab[8];

__kernel void init(__global double *a, __global double *b)
{
    double4 x = distribute4(a);
    double4 y = distribute4(b);
    tab[0] = x.x; tab[1] = x.y; tab[2] = x.z; tab[3] = x.w;
    tab[4] = y.x; tab[5] = y.y; tab[6] = y.z; tab[7] = y.w;
}

__kernel void step(__global double *out)
{
    double s = 0.0;
    #pragma unroll
    for (int k = 0; k < 8; k++) {
        tab[k] = tab[k] * 2.0;
        s = s + tab[k];
    }
    collect(out, s);
}
```

- `init` は `tab` の 8 個に値を書く
- `step` は、前の実行が残した `tab` の各要素を 2 倍して書き戻し、合計を返す

ホスト（`test_persistent_array.c`）は `init` を 1 回、`step` を 3 回実行する。`step` の結果は、実行するたびに 2 倍になる
（`tab` の初期値の和 × 2、× 4、× 8）。これが一致すれば PASS。

## 配列の決まり

- 型は `double` の 1 次元の配列
- **添字は、コンパイルのときに決まる値だけ**。`tab[3]` や、`#pragma unroll` で展開したループの `tab[k]` は使える。
  実行のときに決まる値の添字や、範囲の外の添字は、コンパイルの誤りで止まる
- 配列は LM0 か LM1 に置く。置ける要素の数には上限があり、超えるとコンパイルの誤りで止まる

ほかの決まり（値を共有する範囲、初期値、kernel を実行する順番、ビルドの流れ）は `20-persistent` と同じ。
詳しくは `share/mncl/api-reference.md` の「`persistent` 修飾子」の節にある。

## 実行

```bash
ninja build-e2e
ninja test-emu-lib    # emu:lib
ninja test-device     # 実機
```

値が kernel の実行をまたいで残るのは emu:lib と実機で、emu:process では残らない。
