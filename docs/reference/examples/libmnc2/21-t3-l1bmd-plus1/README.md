# 21-t3-l1bmd-plus1 — l1bmd+1 方向確認テスト

l1bmd+1 が隣接 MAB のどちら側から読むかを確認する観察テスト。

実測結果: l1bmd+1 は MAB[N-1] (前の MAB) の同位置 PE から読む。
L1B 内 (16 MAB = 64 PE) で wrap する。

    ninja && ninja test-emu-lib
