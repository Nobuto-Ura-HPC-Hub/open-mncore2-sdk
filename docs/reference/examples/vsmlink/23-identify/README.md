# 23-identify: `@identify` directive サンプル

`@identify` directive で PE ID `[0..4095]` を各 PE に配布し、 偶奇判定によって偶数 PE → 0、 奇数 PE → ID を `@collect` する最小サンプル。

## ファイル構成

| ファイル | 内容 |
|---------|------|
| `test1_identify._vsm` | カーネル (`@identify` + maskr による偶奇分岐 + `@collect`) |
| `identify.param` | 配置パラメタ (PDM slot / wait tag) |
| `identify.stparam` | 構造パラメタ (1D, 4096 PE) |
| `test1_identify.c` | host driver (`mnc2_send` → `mnc2_exec_kernel` → `mnc2_recv` + 期待値検証) |
| `build.ninja` | ビルド定義 |

## ビルド・実行

```bash
source scripts/overlay   # vsm-linker/ 直下から

# vsmlink のみ
ninja -C examples/23-identify

# + assemble3 + C ビルド
ninja -C examples/23-identify build-e2e

# + emu:lib で実行・検証 → "test1_identify PASS"
ninja -C examples/23-identify test-emu-lib

# + 実機で実行・検証 (実機 pod のみ)
ninja -C examples/23-identify test-device
```

## メモ

- 期待値: IDs `[0..4095]` 入力に対して `[0, 1, 0, 3, ..., 0, 4095]` が PDM に書き戻される
- driver 内で全 4096 要素を期待値と比較し、 一致すれば `test1_identify PASS` を stdout に出力
- `@identify` は現状 `@distribute` の別名として実装されている（PDM slot 8 から ID を読む）。 本格実装（`.stparam` の `ndim` / `topology` を参照して PE 座標を意味的に扱う）は TODO
- `@distribute` + maskr による同等パターンの全バリエーション（複数 slot、 マルチカーネル、 順次実行 等）は `tests/e2e/identify/` に集約（device 含めて 14 + 14 個）

## 関連

- `tests/e2e/identify/` — `@distribute` + maskr の全バリエーション + golden cmp による回帰検証
- `examples/22-identify-reduce-add/` — `@identify` + `@reduce :liadd` の合成パイプライン
- `docs/test-add-guide.md` — テスト追加ガイド（テスト層・lit 組み込み・集約原則）
