# 22-identify-reduce-add

@identify → @reduce :liadd の最小パイプライン。

PE ID [0..4095] を各 PE に配布し、全 PE の整数加算 reduce を行う。
reduce は 4 部分和（PE position 0〜3）を返し、それぞれ個別に検証する。

## 期待値

| PE position | 属する PE ID | 部分和 |
|-------------|-------------|--------|
| 0 | 0, 4, 8, …, 4092 | 2,095,104 |
| 1 | 1, 5, 9, …, 4093 | 2,096,128 |
| 2 | 2, 6, 10, …, 4094 | 2,097,152 |
| 3 | 3, 7, 11, …, 4095 | 2,098,176 |
| **合計** | | **8,386,560** |

## ビルド・実行

```bash
ninja           # vsmlink のみ
ninja build-e2e # + assemble3 + C ビルド
ninja test      # emu:lib で実行・検証
```
