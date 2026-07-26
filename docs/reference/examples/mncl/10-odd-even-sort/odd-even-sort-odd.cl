// 奇偶転置ソート 1 フェーズ (odd kernel)。 kernel 本体は even と同一 (kernel 名のみ相違)。
// 偶数 / 奇数フェーズの差は host が渡す flag と、 .param での data_in / data_out の割り当て
// 入れ替えだけで表現する。 MNCL では even の cross 経路省略が再現できないため、 even と同じ
// 内容になる (README.md「even と odd が同じ内容な理由」参照)。
//
// data_in:    in  (このフェーズの入力配列。 distribute / neighbor が読む)
// data_out:   out (1 フェーズ後の配列。 collect 先)
// flag:       in  (フェーズフラグ。 flag=1.0 の PE が左メンバ)
// swap_flags: out (PE ごとの swap 有無 0.0/1.0。 reduce kernel が読み直して集計する)
__kernel void odd_even_sort_odd(__global double* data_in,
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
