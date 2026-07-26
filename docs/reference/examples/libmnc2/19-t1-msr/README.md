# 19-t1-msr — msr 方向確認テスト

msr 命令の実際の動作を確認する stencil 基本テスト。

実測結果: msr は PE[i] ← PE[i+1] (データが左にシフト)。
subpe[3] は MAB 内 wrap で subpe[0] の値を取得。

    ninja && ninja test-emu-lib
