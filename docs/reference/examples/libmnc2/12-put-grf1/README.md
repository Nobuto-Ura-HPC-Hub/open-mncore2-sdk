# 12-put-grf1 — GRF1 distribute テスト

PDM → L2BM → L1BM → LM0 → GRF1 の distribute パス。
lpassa $lm0 $ls0 で LM0 → GRF1 ($ls レジスタ) にコピー。
mnc2_debug_read(MNC2_MEM_GRF1) で GRF1 の値を検証。

    ninja && ninja test-emu-lib
