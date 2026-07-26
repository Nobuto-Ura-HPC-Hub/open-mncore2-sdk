# 24-odd-even-sort-reduce

Odd-Even Transposition Sort（4096 PE）を `@reduce :liadd` を使って on-device で
収束判定する版。`11-odd-even-sort` の冗長な host ↔ device DMA を排除している。

## 11-odd-even-sort との違い

| 項目 | 11 | 24 |
|------|----|----|
| 各 pass の収束判定 | swap_flags (32 KB) を host に recv → all_zero 判定 | reduce kernel で 4 u64 (32 byte) に集約 → host で sum |
| カーネル間の data round-trip | あり (slot A/B の re-recv → re-send で wait i10 をトリガー) | なし (init 後はデータが LM0 に常駐) |
| カーネル数 | 2 (sort_even, sort_odd) | 4 (init, exec_even, exec_odd, reduce) |
| 結果回収 | 最終 slot を recv | 全 PE の LM0[0] を `mnc2_debug_read` で読む |

11 はチュートリアル的に send/recv の往復を見せる版として残し、24 は性能寄りの参考実装。

## ビルドと実行

```bash
ninja              # kernel + host C ビルド
ninja test-emu-lib # emu:lib で実行
ninja test-device  # 実機で実行
```

## ファイル

| ファイル | 用途 |
|---------|------|
| `init.vsm`       | PE ID 計算 + データ配布 (raw vsm) |
| `exec_even.vsm`  | 偶数フェーズ swap (raw vsm) |
| `exec_odd._vsm` + `exec_odd.param` | 奇数フェーズ swap (`@boundary_flags` 使用、vsmlink) |
| `reduce._vsm` + `reduce.param` | swap_count を 4 u64 に集約 (`@reduce :liadd` 使用、vsmlink) |
| `sort.c`         | host driver |
| `data/`          | 入力データ + boundary flags |

## アルゴリズムフロー

```
host                                device
====                                ======
mnc2_send(input, ..)
mnc2_send(boundary_flags, ..)
                                    init.idma.dat
                                      → 各 PE の LM0[0] に対応する値を配置
loop:
                                    exec_even.idma.dat
                                      → 偶数 swap、swap が起きた PE は LM0[1] に 1
mnc2_send(boundary_flags, trigger)
                                    exec_odd.idma.dat
                                      → 奇数 swap、同じく LM0[1] に 1
                                    reduce.idma.dat
                                      → 全 PE の LM0[1] を chip ごとに合計、PDM[32768..] に書き込み
mnc2_recv(swap_count[4]) → sum
sum == 0 で break

mnc2_debug_read で LM0[0] 全 PE 回収
```
