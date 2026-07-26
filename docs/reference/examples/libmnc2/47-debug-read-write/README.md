# 47-debug-read-write — debug_write + カーネル実行 + debug_read 統合テスト

`d set` / `d get` を C API (`mnc2_debug_write` / `mnc2_debug_read`) に置換した msl stencil テスト。

1. `debug_write` で各 PE の LM0[0] に初期値を書き込む
2. `even_msl` カーネルを実行 (msl: PE[i] ← PE[i-1], MAB 内循環)
3. `debug_read` で各 PE の LM0[4] を読み出し、期待値と比較

```
ninja && ninja test-emu        # emu:process
ninja && ninja test-emu-lib    # emu:lib
```
