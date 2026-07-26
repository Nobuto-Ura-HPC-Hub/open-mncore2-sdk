# 11-put-grf0 — GRF0 distribute テスト

PDM → L2BM → L1BM → LM0 → GRF0 の distribute パス。
full distribute 後に lpassa $lm0 $lr0 で LM0 → GRF0 にコピー。
mnc2_debug_read(MNC2_MEM_GRF0) で GRF0 の値を検証。

    ninja && ninja test-emu-lib
