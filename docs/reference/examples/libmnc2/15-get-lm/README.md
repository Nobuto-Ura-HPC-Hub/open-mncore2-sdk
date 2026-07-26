# 15-get-lm — LM0 collect roundtrip テスト

put_lm で LM0 に distribute → get_lm で LM0 → L1BM → L2BM → PDM に collect。
recv で受信し、送信データと一致することを検証 (4096 要素)。

    ninja && ninja test-emu-lib
