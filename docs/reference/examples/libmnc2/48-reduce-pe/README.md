# 48-reduce-pe — PE 全段縮約 (l1bmr → l2bmr → mvrdfadd)

全 4096 PE が持つ 1 つの値（$lr0 = 1.0）を、メモリ階層に沿って段階的に縮約する。

## 縮約パイプライン

```
PE (4096 個, 各 $lr0 = 1.0)
  ↓ l1bmrdfadd (16 MAB を加算縮約, PE position は結合)
L1BM (各 L1B に 4 u64)
  ↓ l2bmrdfadd (8 L1B を加算縮約)
L2BM (各 L2B に 4 u64)
  ↓ mvrdfadd (8 L2B バンクを加算縮約)
PDM (4 u64)
```

## 結果

PDM に 4 u64 が格納される。1 値ではなく **PE position (0-3) 別の部分和**:

| PDM[i] | 内容 | 値 |
|--------|------|-----|
| [0] | 全 PE の PE position 0 の合計 | 1024.0 |
| [1] | 全 PE の PE position 1 の合計 | 1024.0 |
| [2] | 全 PE の PE position 2 の合計 | 1024.0 |
| [3] | 全 PE の PE position 3 の合計 | 1024.0 |

ホスト側で 4 値を足せば全 PE の総和: 1024.0 × 4 = 4096.0 = 4096 PEs × 1.0

## なぜ 1 値にならないか

l1bmr は MAB 方向（16 MAB）を**縮約**するが、PE 方向（4 PE/MAB）は**結合**（並置）するだけ。
この PE position の区別は全段を通して消えないため、最終出力は常に 4 u64。
l1bmr → l2bmr → mvrdfadd の各段の動作は MN-Core 2 dev-manual を参照。

## 実行

    ninja && ninja test-emu-lib    # emu:lib
