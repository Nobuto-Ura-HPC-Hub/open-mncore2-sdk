# 01-vecadd

最小の例。4096 個の PE が、それぞれ 1 要素ずつ `c[i] = a[i] + b[i]`（`double`）を計算する。
kernel を動かすのに要る 3 つのファイル（`.cl`、`.param`、ホストのプログラム）がそろっている。

## 3 つのファイル

| ファイル | 役目 |
|---|---|
| `vecadd.cl` | kernel。`distribute` で PDM の `a` と `b` から自分の要素を受け取り、足して、`collect` で `c` に返す |
| `vecadd.param` | kernel 引数の配列を PDM のどこに置くか（slot 8 が `a`、16 が `b`、24 が `c`）と、転送の完了を待つタグ |
| `test_vecadd.c` | ホストのプログラム（libmnc2）。`a` と `b` を PDM に送り、kernel を読み込んで実行し、`c` を受け取って照合する |

```c
__kernel void main_kernel0(__global double* _arg_array1, __global double* _arg_array2, __global double* _arg_array3)
{
    double a = distribute(_arg_array1);   // PE i は a[i] を受け取る
    double b = distribute(_arg_array2);
    double c = a + b;
    collect(_arg_array3, c);              // PE i の c が c[i] に入る
}
```

kernel 引数の slot の番号は、宣言の順に 8、16、24 である。`vecadd.param` の `:addr`（u64 単位の番地）と、
ホストの `mnc2_send` / `mnc2_recv` の番地（バイト単位。`:addr` の 8 倍）をそろえる。

`.param` の待ちのタグ:

- `:send_wait_tag #x10`（slot 8）: kernel は、ホストがタグ `0x10` で送った `a` の到着を待ってから読む。`b` はタグ 0 で先に送り終えているので待たない
- `:recv_wait_tag #x1e`（slot 24）: `collect` の書き込みが終わると立つタグ。ホストの `mnc2_recv` は、このタグを待ってから `c` を読む
- `:reserved_wait_tag #x3f`: vsmlink が kernel の中の転送の完了を待つのに使う。どの kernel にも要る

書き方の決まりは `share/vsmlink/param-spec.md`、ホストの API は `share/mnc2/api-reference.md`。

## 実行

```bash
ninja                 # kernel の build（clang、vsmlink、assemble3）
ninja build-e2e       # ホストのプログラムも build する
ninja test-emu-lib    # エミュレータ（emu:lib）で実行して照合する
ninja test-device     # 実機で実行して照合する
```

4096 要素がすべて一致すれば、次の行が出る。

```
  PASS: c[i] == a[i] + b[i] for all 4096 elements
```
