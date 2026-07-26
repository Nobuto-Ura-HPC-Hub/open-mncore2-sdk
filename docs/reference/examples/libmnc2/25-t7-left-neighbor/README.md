# 25-t7-left-neighbor — 左隣統合テスト

msr + l1bmd+1 + maskr の組み合わせによる左隣取得パターン。

subpe[1-3]: msr で右隣 PE の値を取得。
subpe[0]: l1bmd+1 で前 MAB (MAB[N-1]) の PE[0] の値で上書き。
L1B 内 (16 MAB) で wrap。

    ninja && ninja test-emu-lib
