# 13-get-l2bm — L2BM collect roundtrip テスト

put_l2bm で L2BM に distribute → get_l2bm で L2BM → PDM に collect。
recv で受信し、送信データと一致することを検証 (4096 要素)。

    ninja && ninja test-emu-lib
