# 21-nop

デバッグ用の待ち `nop(n)` の例。`01-vecadd` の加算の前に `nop(8)` を置く。計算の結果は `01-vecadd` と同じになる。

## 何をするか

```c
double a = distribute(_arg_array1);
double b = distribute(_arg_array2);

nop(8);            // ここに 8 ステップの待ち（アセンブリの nop/8）を 1 行置く

double c = a + b;
collect(_arg_array3, c);
```

- `nop(n)` は、書いた位置に n ステップの待ちを置く。`n` はコンパイルの時点で決まる定数（0 から 65535）
- 正しさのための待ちはコンパイラが入れるので、ふだんは要らない。待ちを足して実機の挙動を調べるときに使う
- 入った待ちは、`_build/nop._vsm` の `nop/8` の行で確かめられる

ほかの決まり（`nop` をまたいで順番が変わらないもの）は `share/mncl/api-reference.md` の「`nop`」の節。
`.param` とホストのプログラムは `01-vecadd` と同じ（`01-vecadd/README.md`）。

## 実行

```bash
ninja build-e2e
ninja test-emu-lib    # エミュレータ（emu:lib）
ninja test-device     # 実機
```

4096 要素がすべて `a[i] + b[i]` と一致すれば `PASS: c[i] == a[i] + b[i] for all 4096 elements` が出る。
