# 15-mask-safety-T4

## 概要

@stencil を含むネスト条件分岐のテスト。flag1, flag2 の組み合わせで r16/r18 の値を設定する。

## パターン: 初期処理 + 例外上書き

このテストは **正しい if-then-else ではない**。「初期処理 + 例外上書き」（select）パターンを使用している。

```
maskr 15 (flag1):            r16 = 1   ← 初期値
maskr 7  (flag1 AND flag2):  r16 = 5   ← 上書き
maskr 6  (flag1 AND !flag2): r16 = 9   ← 上書き
maskr 8  (!flag1):           r16 = 2   ← 初期値
maskr 5  (!flag1 AND flag2): r16 = 6   ← 上書き
maskr 4  (!flag1 AND !flag2):r16 = 10  ← 上書き
```

flag1=true の PE は maskr 15 で r16=1 に設定された後、maskr 7 または maskr 6 で上書きされる。
1 つのレジスタに複数回書き込まれる。

## T4 で問題にならない理由

- 全操作が `ipassa`（副作用なし）
- `$r24` を毎回 `imm` で再設定
- ブランチ間にデータ依存がない

l1bmd/msr 等の副作用を持つ命令や、前の分岐結果に依存する処理がある場合は、この方式は使えない。

## 期待値

| flag1 | flag2 | r16 | r18 |
|-------|-------|-----|-----|
| T     | T     | 5   | 1   |
| T     | F     | 9   | 0   |
| F     | T     | 6   | 0   |
| F     | F     | 10  | 0   |

## 依存

boundary flags データとして `../17-boundary-collect/_build/collected_flags.bin` を使用する。
事前に 17-boundary-collect のビルド・テストが必要:
```
ninja -C ../17-boundary-collect build-e2e && ninja -C ../17-boundary-collect test
```
