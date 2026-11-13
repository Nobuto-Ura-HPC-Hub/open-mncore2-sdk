# 16-mask-safety-T4-debug

## 概要

15-mask-safety-T4 の debug_read 版。@collect を使わず、debug_read で GRF0 から r16/r18 を直接読み出す。

このテストは emu:lib と emu:process で動かすものである。GRF0 の debug_read は emu:lib と emu:process で使え、実機は未サポート（`MNC2_ERROR_DEVICE`）なので対象外である（根拠: libmnc2 の `share/mnc2/api-reference.md` の「対応メモリ領域」の表）。emu:lib では 2026-10-03 に 4 ケースとも全 PE で期待値と一致することを確認した。debug_read が使えないバックエンド（実機を含む）では、ホストは最初のケースで判定して SKIP（終了コード 0）で終わる。実機で SKIP になっても確認済みにはならない。emu:lib の lit テストは `ALL PASS` を確かめるので、SKIP になれば落ちる。

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
