# 10-put-ln — LM1 distribute テスト

PDM → L2BM → L1BM → LM1 の distribute パス。
l1bmd の宛先を $ln0v (LM1) にしている点が put_lm との違い。
mnc2_debug_read(MNC2_MEM_LM1) で LM1 の値を検証。

    ninja && ninja test-emu-lib
