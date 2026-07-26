# 24-t6-maskr-pe3 — maskr + ipassa PE[3] テスト

maskr で subpe[3] のみ選択し、ipassa で元の値を保持するテスト。
subpe[1,2] は msl (PE[i-1]) の値、subpe[0] は MAB 内 wrap。

    ninja && ninja test-emu-lib
