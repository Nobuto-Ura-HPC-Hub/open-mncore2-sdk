# data/ — テスト用 golden データ管理

## 管理方針

golden バイナリは **ジェネレータ (C プログラム) で生成** し、 git には入れない。
ファイル名は内容の sha256 先頭 8 文字 (例: `57b42dea.bin`) とし、 内容が変わると別ファイル名になる。

生成ロジックを `data/` に置くことで、 誰がいつ実行しても同じ bin が再現できることを保証する。

## ディレクトリ構成

```
data/
├── README.md             ← 本ファイル
├── build.ninja           ← 全 bin の生成・検証ルール
├── gen_bf_golden.c       ← boundary_flags golden ジェネレータ
├── _build/               ← 中間生成物 (.o / 実行ファイル、 gitignore)
└── created/              ← 生成された bin (gitignore)
```

## 使い方

```bash
ninja           # default = all、 2 個の bin を created/ に生成
ninja all       # 同上
ninja verify    # 全 2 bin の sha256 検証
```

各 example の `build.ninja` は `fetch_data` ルールで `../data/created/<sha>.bin` を `_build/` にコピーして使う。

## ファイル一覧 (2 bin、 すべて created/ に生成)

### boundary_flags golden (gen_bf_golden)

| ファイル | 内容 |
|---|---|
| `57b42dea.bin` | 10bit, data_edge なし (中間チャンク) |
| `75c77b0f.bin` | 10bit, data_edge あり (全体が 1 チャンク、 4096 PE) |

`@boundary_flags` ディレクティブの distribute モードで PDM に送る golden。
`.stparam` の `:data_size` / `:data_offset` / `:pe_shape` から判定される data_edge 状態に応じて選択する。

| data_edge_left | data_edge_right | 用途 | バイナリ |
|---|---|---|---|
| 0 | 0 | 中間チャンク | `57b42dea.bin` |
| 1 | 1 | 全体が 1 チャンク (4096 PE) | `75c77b0f.bin` |

判定ロジックの仕様は vsm-linker チーム回答による (`gen_bf_golden.c` の `expected_flags()` 実装を参照)。

## ツール

| ファイル | 内容 |
|---|---|
| `gen_bf_golden.c` | boundary_flags golden ジェネレータ。 `--edge-left` / `--edge-right` で data_edge を制御 |

注: vsm-linker 由来 (vsmlink-kit `share/examples/vsmlink/data/gen_bf_golden.c` と同一実装)。 `@boundary_flags_compute` のテンプレート選択ロジックを host 側で再現したもの。

## 検証

`ninja verify` で全 2 bin の sha256 が `expected` と一致することを確認する。
sha256 値は `build.ninja` 内の各 `verify-<sha>` edge の `expected = ...` に記載。

## 新しい入力データを追加するときは

1. ジェネレータ (`gen_*.c`) を追加 or 既存を拡張
2. 一度生成して sha256 を確認 (例: `./gen_foo /tmp/x.bin && sha256sum /tmp/x.bin`)
3. `build.ninja` に build edge と verify edge を追加 (sha256 値を `expected` に書く)
4. `all` / `verify` の phony 依存に追加
5. `ninja verify` で sha256 検証

「ジェネレータを置かずに bin だけ commit」 は禁止 (再現性が失われる)。

## 関連

- vsm-linker / MNCL 共通の data/ 管理規約 (sha256 集約 + ジェネレータ + .gitignore)
- `@boundary_flags` のバイナリ選択基準 (vsm-linker チーム提供の仕様)
