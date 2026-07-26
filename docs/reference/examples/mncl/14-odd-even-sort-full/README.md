# 14-odd-even-sort-full

奇偶転置ソートの **full 版**。 `13-odd-even-sort-gid`（1 turn デモ）を土台に、**host が even/odd
フェーズを収束するまでループして配列を完全にソートする**。左メンバ判定は host が渡す flag ではなく
`get_global_id`（kernel 内の `(id & 1)`）で PE 自身が行う。

- `10`（host flag 版）・`13`（get_global_id 版の 1 turn デモ）は変更しない。この 14 は「13 を回し切って
  完全ソートを達成する」版。
- kernel は **even / odd の 2 本**。swap 有無は別 reduce kernel ではなく **kernel 内で inline
  `reduce_add`** して swap_count に直接出す（reduce の PDM 往復を省き、収束ループを速くする）。

## 何をするか

odd-even transposition sort を収束まで実行する。1 turn = 偶数フェーズ + 奇数フェーズ。host は各フェーズの
swap_count（swap した PE 数）を受け取り、**1 turn の合計が 0 になったら収束**（完全ソート済）と判定して
早期終了する。収束後の配列を host 内の `qsort`（昇順）と全要素比較し、昇順であることも直接確認する。

## host のループ（データはデバイス常駐）

SDK の vsmlink 例 `24-odd-even-sort-reduce` と同じ骨格:

- **データは ping-pong 2 バッファ（data_a / data_b）でデバイスに常駐**。even は a→b、odd は b→a。
  turn 間でデータを再送しない（kernel をまたいでも PDM 上に残る）。
- **kernel 起動トリガ**: データを送らないので、代わりに **id 配列を tag 0x10 で再送**して待ち合わせを発火させる
  （id は `@identify` の入力でもあり、値は毎回同じ [0..4095]）。
- **収束判定**: 各フェーズ後に swap_count（4 partial sum）を受信して合計。even+odd の 1 turn で 0 なら break。
- **安全上限 MAX_PASS**（= N = 4096 turn。理論最悪は N/2 = 2048 turn）。通常は早期 break で終わる。

各フェーズは data_out と swap_count を host が受信して done flag を落とす（落とさないと次の collect /
reduce が二重になる。13 と同じ作法）。

## 収束とテスト入力

odd-even sort は **最悪 N フェーズ（= N/2 turn）で必ずソート完了**する（N = 要素数 = 4096）。最悪は逆順入力で
約 2048 turn かかり、エミュレータでは重すぎる。**host のループ自体は任意入力を収束させる**（正しさは入力に
依らない）ので、このテストは実行時間が現実的な入力を使う:

- 入力 = 大域ソート済 [1..4096] を **幅 `BLOCK` ごとに逆順**にしたもの。各ブロック内の逆転をソートするのに
  約 `BLOCK` フェーズかかるので、収束は `BLOCK/2` turn 程度で終わる。
- 既定は `BLOCK = 64`（約 33 turn、emu:lib で数十秒）。`BLOCK` を大きくすると収束 turn が増え、より強い
  多ターン収束の実証になる（が emu 時間も増える）。逆順 4096 全体を試すと最悪の 2048 turn を踏む。

## kernel インターフェース

```c
// even / odd (本体は判定の偶奇のみ相違)
__kernel void odd_even_sort_full_even(__global double* data_in,     // in  (distribute / neighbor)
                                      __global double* data_out,    // out (collect)
                                      __global double* swap_count); // out (inline reduce_add, 4 partial sum)
```

## ビルドと実行

```bash
ninja build-e2e     # even / odd の .asm / .idma.dat と host バイナリを生成
ninja test-emu-lib  # host エミュレータで収束まで実行し、qsort と照合
ninja test-device   # 実機で実行
```

## 検証の内容

収束後の配列（最後の odd フェーズの出力 = data_a）を、host が `qsort` した参照配列と全 4096 要素比較し、
かつ隣接要素が昇順であることを確認する。swap_count が turn を追って 0 に落ちる過程も表示する。
入力は互いに異なる値なので、残留や部分ソートがあれば必ず不一致で捕まる。
