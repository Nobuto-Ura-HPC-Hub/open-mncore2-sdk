# proto-8pt: FFT の 8 点の試作

`40-fft-4096` の親ディレクトリのスクリプト（`../gen_vsm.py` など）を、点数 m = 3（8 点、3 段）で使う。
4096 点に広げる前に、小さい点数で値と手順を確かめるために残す。点数を変えて試すときは、`build.ninja` の `m = 3` を変える
（2〜12。PE の番号 0 から N−1 だけを使い、残りの PE の入力は 0 である）。

```bash
ninja test-emu-lib   # 8 点を emu:lib で実行し、numpy の FFT とノルム誤差で比べる
```

別の入力で試すときは、`../README.md` の「入力データ」の手順で `input.bin` と `tw.bin` を作り、`_build/fft_host` に渡す。
