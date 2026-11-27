# 23-identify: `@identify` directive サンプル

`@identify` directive で PE ID `[0..4095]` を各 PE に配布し、 偶奇判定によって偶数 PE → 0、 奇数 PE → ID を `@collect` する最小サンプル。

## ファイル構成

| ファイル | 内容 |
|---------|------|
| `test1_identify._vsm` | カーネル (`@identify` + maskr による偶奇分岐 + `@collect`) |
| `identify.param` | 配置パラメタ (PDM slot / wait tag) |
| `test1_identify.c` | host driver (`mnc2_send` → `mnc2_exec_kernel` → `mnc2_recv` + 期待値検証) |
| `build.ninja` | ビルド定義 |

## ビルド・実行

```bash
# SDK を有効にし、この example のディレクトリで実行する（examples の README.md の「動かし方」）

# vsmlink のみ
ninja

# + assemble3 + C ビルド
ninja build-e2e

# + emu:lib で実行・検証 → "test1_identify PASS"
ninja test-emu-lib

# + 実機で実行・検証 (実機 pod のみ)
ninja test-device
```

## メモ

- 期待値: IDs `[0..4095]` 入力に対して `[0, 1, 0, 3, ..., 0, 4095]` が PDM に書き戻される
- driver 内で全 4096 要素を期待値と比較し、 一致すれば `test1_identify PASS` を stdout に出力
- `@identify` は、PDM の slot 8 から ID を読んで各 PE に配る
- `@distribute` + maskr による同等パターンの全バリエーション（複数 slot、 マルチカーネル、 順次実行 等）は vsmlink コマンドの開発用のテストで確かめている

## 関連

- `examples/22-identify-reduce-add/` — `@identify` + `@reduce :liadd` の合成パイプライン
