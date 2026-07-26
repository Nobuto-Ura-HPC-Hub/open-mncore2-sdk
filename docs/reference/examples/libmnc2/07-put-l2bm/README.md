# 07-put-l2bm — L2BM distribute テスト

PDM にデータを send し、put_l2bm カーネルで L2BM に distribute。
mnc2_debug_read(MNC2_MEM_L2BM) で値を読み出し、非ゼロを確認する。

    ninja && ninja test-emu-lib
