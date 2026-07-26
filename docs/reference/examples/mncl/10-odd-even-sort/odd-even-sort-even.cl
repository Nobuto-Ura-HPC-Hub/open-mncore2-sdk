// 奇偶転置ソート 1 フェーズ (even kernel)。 swap 判定 + swap 有無の collect のみ。
// reduce_add は独立した reduce kernel に分離した (README.md の「3 kernel 分割の理由」参照)。
//
// ping-pong: data_in を読み、 data_out へ書く。 in-place をやめ入力と出力を別バッファにする。
//   even / odd の kernel 本体は同一。 差は host が渡す flag と、 .param での data_in / data_out の
//   割り当て入れ替えだけ (README.md 参照)。
//
// data_in:    in  (このフェーズの入力配列。 distribute / neighbor が読む)
// data_out:   out (1 フェーズ後の配列。 collect 先)
// flag:       in  (フェーズフラグ。 flag=1.0 の PE が左メンバ)
// swap_flags: out (PE ごとの swap 有無 0.0/1.0。 reduce kernel が読み直して集計する)
//
// flag=1.0 (左側 PE): self > right なら right を取る
// flag=0.0 (右側 PE): left > self なら left を取る
__kernel void odd_even_sort_even(__global double* data_in,
                                 __global double* data_out,
                                 __global double* flag,
                                 __global double* swap_flags) {
    double self  = distribute(data_in);
    double right = neighbor(data_in, 1);
    double left  = neighbor(data_in, -1);
    double is_left = distribute(flag);

    double left_swap;
    if (self > right) { left_swap = 1.0; } else { left_swap = 0.0; }

    double right_swap;
    if (left > self) { right_swap = 1.0; } else { right_swap = 0.0; }

    double do_left  = is_left * left_swap;
    double do_right = (1.0 - is_left) * right_swap;

    double result;
    if (do_left > 0.5) {
        result = right;
    } else {
        result = self;
    }

    double result2;
    if (do_right > 0.5) {
        result2 = left;
    } else {
        result2 = result;
    }

    /* この PE が swap に関与したか (0 or 1)。 reduce kernel が全 PE を集計する */
    double swap_indicator = do_left + do_right;
    collect(swap_flags, swap_indicator);

    collect(data_out, result2);
}
