# 20-persistent

`persistent` 修飾子の最小の例。**kernel の実行が終わっても値が PE の中に残る変数**を、2 つの kernel で共有する。

## 何をするか

`persistent.cl` に、変数 1 つと kernel 2 つを書いている。

```c
persistent double acc;

__kernel void init(__global double *seed)                 { acc = distribute(seed); }
__kernel void accum(__global double *in, __global double *out) { acc = acc + distribute(in); collect(out, acc); }
```

- `init` は `acc` に初期値を書く
- `accum` は、前の実行が残した `acc` に入力を足し、結果を返す

ホスト（`test_persistent.c`）は `init` を 1 回、`accum` を 3 回実行する。`accum` の結果は、実行するたびに
`seed + in(1 回目) + in(2 回目) + ...` と累計になる。これが一致すれば PASS。

## 決まり

- 値を共有する kernel は、1 つの `.cl` に書く。別の `.cl` の kernel とは共有しない
- `acc` の初期値は決まっていない。読む前に、どれかの kernel で書いておく（ここでは `init`）
- どの kernel を先に実行するかはホストが決める。コンパイラは順番を調べない

ほかの決まり（使える型など）は `share/mncl/api-reference.md` の「`persistent` 修飾子」の節にある。

## ビルドの流れ

kernel が 2 つあるので、vsmlink の出力先はディレクトリになり、kernel ごとの `.vsm` が出る。

```
persistent.cl
  -> _build/persistent._vsm                    (clang。2 つの kernel を含む)
  -> _build/multikernel/persistent-init.vsm    (vsmlink。kernel ごとに 1 つ)
     _build/multikernel/persistent-accum.vsm
  -> _build/init.idma.dat, _build/accum.idma.dat (assemble3 --loader)
```

`.param` は 1 つを、両方の kernel で使う。kernel 引数の slot 番号は kernel をまたいで重ならないように、
kernel の宣言順に振られる（`seed` が 8、`in` が 16、`out` が 24）。

## 実行

```bash
ninja build-e2e
ninja test-emu-lib    # emu:lib
ninja test-device     # 実機
```

値が kernel の実行をまたいで残るのは emu:lib と実機で、emu:process では残らない。
