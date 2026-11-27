# data/ — @get_neighbor テスト用の入力

## ファイル

| ファイル | 内容 |
|---------|------|
| `get_left._vsm` | 左隣（offset -1）を受け取る kernel |
| `get_right._vsm` | 右隣（offset +1）を受け取る kernel |
| `get_left.param` | 配置パラメタ（`get_left` と `get_right` で共用）|
| `edge_test.vsm` | 実験用の手書きの `.vsm`（vsmlink を通さず assemble3 に直接渡す）|
| `config_pdm0_sequential.json` | `edge_test.vsm` を `gpfn3_package_main` で実行するときの PDM の初期化の設定 |

## boundary_flags のデータ

boundary_flags の 10bit ワンホットフラグ（4096 PE × 8 バイト、uint64_t）は、このディレクトリには置かない。
`examples/data/` で作り、`../build.ninja` の `fetch_data` で `_build/75c77b0f.bin` にコピーして使う。

- 生成元: `examples/data/gen_bf_golden.c`（`--edge-left --edge-right`。data_edge あり、data_size=4096、data_offset=0）
- 出力: `examples/data/created/75c77b0f.bin`
- sha256: `75c77b0f06bb635fb67d50f8b0ee5cf035723d9429a231a5409d6a587c0fe5ac`（`examples/data/build.ninja` の `verify-75c77b0f` で検証）
- ビット分布の表示: `examples/data/dump_flags.py`（`--human` で詳細）

詳細は `examples/data/README.md` を参照。
