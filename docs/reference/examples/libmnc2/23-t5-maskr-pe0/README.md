# 23-t5-maskr-pe0 — maskr + ipassa PE[0] テスト

maskr で subpe[0] のみ選択し、ipassa で元の値を保持するテスト。
subpe[1,2] は msr (PE[i+1]) の値、subpe[3] は MAB 内 wrap。

    ninja && ninja test-emu-lib
