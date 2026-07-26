# 46-debug-write — debug_write → debug_read ラウンドトリップ

各メモリ階層 (L2BM/L1BM/LM0/LM1/GRF0/GRF1) に debug_write で書き込み、
debug_read で読み返して一致を確認する。

    ninja && ninja test-emu        # emu:process (nop カーネル経由)
    ninja && ninja test-emu-lib    # emu:lib (カーネル不要)
