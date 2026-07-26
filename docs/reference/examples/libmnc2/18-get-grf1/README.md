# 18-get-grf1 — GRF1 collect roundtrip テスト

put_grf1 で GRF1 に distribute → get_grf1 で GRF1 → LM0 → L1BM → L2BM → PDM に collect。
recv で受信し、送信データと一致することを検証 (4096 要素)。

    ninja && ninja test-emu-lib
