# 16-get-ln — LM1 collect roundtrip テスト

put_ln で LM1 に distribute → get_ln で LM1 → L1BM → L2BM → PDM に collect。
recv で受信し、送信データと一致することを検証 (4096 要素)。

    ninja && ninja test-emu-lib
