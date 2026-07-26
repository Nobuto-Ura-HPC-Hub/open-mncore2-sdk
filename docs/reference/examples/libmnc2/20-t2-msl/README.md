# 20-t2-msl — msl 方向確認テスト

msl 命令の実際の動作を確認する stencil 基本テスト。

実測結果: msl は PE[i] ← PE[i-1] (データが右にシフト)。
subpe[0] は MAB 内 wrap で subpe[3] の値を取得。

    ninja && ninja test-emu-lib
