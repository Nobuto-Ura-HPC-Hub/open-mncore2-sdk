# 22-hypercube-neighbor

超立方体の隣の PE の値を受け取る `get_hypercube_neighbor(v, id, deep)` の例。
PE の番号 j の PE は、番号 `j XOR 2^deep` の PE の `v` を受け取る。

## 何をするか

```c
long id = get_global_id(0);
double a = distribute(_arg_a);

double n0  = get_hypercube_neighbor(a, id, 0);    // 相手は j XOR 1（隣どうしを入れ替える）
double n10 = get_hypercube_neighbor(a, id, 10);   // 相手は j XOR 1024（遠い PE）

collect(_arg_c, a + n0 * 10.0 + n10 * 100.0);
```

`c[j] = a[j] + 10 * a[j ^ 1] + 100 * a[j ^ 1024]` を計算する。相手を取り違えたら値が合わないように、deep ごとに重みを変えて足している。

- `deep` は 0〜11 の定数。`id` には `get_global_id(0)` の値を渡す
- deep 9〜11 は PDM を経由するので、deep 0〜8 より遅く、`.param` に作業領域（`:buffer`）が要る

## `.param`

```
(:identify 0 :place :pdm0 :addr 0 :send_wait_tag #x10)     ; get_global_id の PE の番号
(:buffer nbuf0 :place :pdm0 :addr 16384 :buf_size 4160)    ; deep 9〜11 の作業領域（4096 u64 以上）
```

- `:identify`: ホストが、PE の番号 `[0..4095]`（u64 の配列）を、exec の前にこの番地へ送る。送らないと、全 PE の `get_global_id(0)` が 0 になる
- 端は無いので、境界のフラグ（`:boundary_flags`）は要らない

使い方の決まりは `share/mncl/api-reference.md` の「`get_hypercube_neighbor`」の節。

## 実行

```bash
ninja build-e2e
ninja test-emu-lib    # エミュレータ（emu:lib）
ninja test-device     # 実機
```

ホストは `a[i] = i + 1` を送り、4096 要素をすべて上の式と比べる。一致すれば `PASS 4096/4096` が出る。
