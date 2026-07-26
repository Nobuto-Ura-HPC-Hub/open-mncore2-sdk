# 26-t8-right-neighbor — 右隣統合テスト

msl + l1bmd-1 + maskr の組み合わせによる右隣取得パターン。

subpe[0-2]: msl で左隣 PE の値を取得。
subpe[3]: l1bmd-1 で次 MAB (MAB[N+1]) の PE[3] の値で上書き。
L1B 内 (16 MAB) で wrap。

    ninja && ninja test-emu-lib
