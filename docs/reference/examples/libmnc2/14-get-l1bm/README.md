# 14-get-l1bm — L1BM collect roundtrip テスト

put_l1bm で L1BM に distribute → get_l1bm で L1BM → L2BM → PDM に collect。
recv で受信し、送信データと一致することを検証 (4096 要素)。

    ninja && ninja test-emu-lib
