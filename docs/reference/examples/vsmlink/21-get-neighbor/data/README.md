# data/ — @get_neighbor テスト用バイナリデータ

## collected_flags.bin

boundary_flags の 10bit ワンホットフラグ。4096 PE × 8 バイト (uint64_t)。
data_edge あり（data_size=4096, data_offset=0）。

### 生成

```bash
ninja          # gen_golden.c からビルド + sha256 検証
ninja dump-human  # ビット分布を表示
```

- 生成元: `gen_golden.c`（`examples/17-boundary-collect/gen_golden.c` の複製）
- レイアウト: 10bit (cross_group 廃止後)
- sha256: `75c77b0f06bb635fb67d50f8b0ee5cf035723d9429a231a5409d6a587c0fe5ac`

### ファイル

| ファイル | 内容 |
|---------|------|
| `gen_golden.c` | boundary_flags の golden データ生成プログラム |
| `dump_flags.py` | バイナリのビット分布表示 (`--human` で詳細) |
| `build.ninja` | ビルド + sha256 検証 |
