# data/ — テスト用 golden データ管理

## 管理方針

golden バイナリは **ジェネレータ (C プログラム or Python スクリプト) で生成**し、git には入れない。
ファイル名は内容の sha256 先頭 8 文字 (例: `57b42dea.bin`) とし、内容が変わると別ファイル名になる。

生成ロジックを `examples/data/` に置くことで、誰がいつ実行しても同じ bin が再現できることを保証する。

## ディレクトリ構成

```
examples/data/
├── README.md
├── build.ninja              ← 全 bin の生成ルール
├── gen_bf_golden.c          ← boundary_flags golden ジェネレータ
├── gen_identify_golden.c    ← identify golden ジェネレータ
├── gen_input.py             ← odd-even-sort / get-neighbor 入力データジェネレータ
├── _build/                  ← 中間生成物 (.o / 実行ファイル、gitignore)
└── created/                 ← 生成された bin (gitignore)
```

## 使い方

```bash
ninja           # default = all、14 個の bin を created/ に生成
ninja all       # 同上
ninja verify    # 全 14 bin の sha256 検証
```

各 example の `build.ninja` は `fetch_data` ルールで `../data/created/<sha>.bin` を `_build/` にコピーする。

## ファイル一覧 (14 bin、すべて created/ に生成)

### boundary_flags golden (gen_bf_golden)

| ファイル | 内容 |
|---------|------|
| `57b42dea.bin` | 10bit, data_edge なし |
| `75c77b0f.bin` | 10bit, data_edge あり (4096 PE, offset=0) |

### identify golden (gen_identify_golden)

| ファイル | 内容 |
|---------|------|
| `ab1449ae.bin` | IDs [0..4095] 奇数 PE → ID, 偶数 PE → 0 |
| `d202bcfb.bin` | IDs [4096..8191] 奇数 ID → ID, 偶数 ID → 0 |
| `8f8ea7e3.bin` | `ab1449ae` ‖ `d202bcfb` の連結 (64 KiB) |
| `01becdee.bin` | IDs [0..4095] 偶数 PE → ID, 奇数 PE → 0 |
| `e4d40321.bin` | IDs [4096..8191] 偶数 ID → ID, 奇数 ID → 0 |

### odd-even-sort 入力データ (gen_input.py)

| ファイル | type | 内容 |
|---------|------|------|
| `d5575075.bin` | `sorted` | 4096 fp64, `[0, 1, ..., 4095]` |
| `75c24047.bin` | `reversed` | 4096 fp64, `[4095, ..., 0]` |
| `7353c2c6.bin` | `negated` | 4096 fp64, `[0, -1, ..., -4095]` |
| `1d5f9c66.bin` | `group_reversed` | 4096 fp64, 256 要素単位で逆順 |
| `1720baa2.bin` | `random-123` | 4096 fp64, `[0..4095]` を Python `random.seed(123)` で完全シャッフル |
| `d382fbf4.bin` | `nearly_sorted-42` | 4096 fp64, sorted を `random.seed(42)` で 50 要素 chunk shuffle |

### get-neighbor 入力データ (gen_input.py)

| ファイル | type | 内容 |
|---------|------|------|
| `08063881.bin` | `sequential` | 16384 u64, `[0, 1, ..., 16383]` |

## ツール

| ファイル | 内容 |
|---------|------|
| `gen_bf_golden.c` | boundary_flags golden ジェネレータ |
| `gen_identify_golden.c` | identify golden ジェネレータ (エミュレータ不要) |
| `gen_input.py` | odd-even-sort / get-neighbor 入力データジェネレータ |
| `dump_flags.py` | バイナリのビット分布表示 (`--human` で詳細) |
| `mk_random.py` | 完全 random fp64 を sha256 名で保存 (新規 random data 作成用) |

## 検証

`ninja verify` で全 14 bin の sha256 が `expected` と一致することを確認する。
sha256 値は `build.ninja` 内の各 `verify-<sha>` edge の `expected = ...` に記載。

## 新しい入力データを追加するときは

1. `gen_input.py` に新 type を追加 (or 既存ジェネレータを拡張)
2. 一度生成して sha256 を確認 (例: `python3 gen_input.py <type> /tmp/x.bin && sha256sum /tmp/x.bin`)
3. `build.ninja` に build edge と verify edge を追加 (sha256 値を `expected` に書く)
4. `all` / `verify` の phony 依存に追加
5. `ninja verify` で sha256 検証

「ジェネレータを置かずに bin だけ commit」 は禁止 (再現性が失われる)。
