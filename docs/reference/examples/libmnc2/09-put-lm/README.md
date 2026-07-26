# 09-put-lm — LM0 distribute テスト

PDM → L2BM → L1BM → LM0 の full distribute パス (mvp + l2bmb + l1bmd)。
mnc2_debug_read(MNC2_MEM_LM0) で LM0 の値を検証。

    ninja && ninja test-emu-lib
