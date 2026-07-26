// 奇偶転置ソート 1 フェーズ (even kernel、 get_global_id 版)。
//
// 10-odd-even-sort と同じ内容だが、 「自分がペアの左メンバか」の判定を host が渡す flag ではなく
// get_global_id で PE 自身が行う点だけが違う。 例 10 の flag パターン {1,0,1,0,...} は PE 番号の
// パリティそのものなので、 even フェーズでは「PE 番号が偶数 = 左メンバ」で置き換えられる。
//   is_left = distribute(flag)  ->  is_left = ((id & 1) == 0) ? 1.0 : 0.0
// これ以外の kernel 本体（比較・swap・collect）と境界処理（neighbor の clamp）は例 10 と同一。
//
// data_in:    in  (このフェーズの入力配列。 distribute / neighbor が読む)
// data_out:   out (1 フェーズ後の配列。 collect 先)
// swap_flags: out (PE ごとの swap 有無 0.0/1.0。 reduce kernel が読み直して集計する)
__kernel void odd_even_sort_gid_even(__global double* data_in,
                                     __global double* data_out,
                                     __global double* swap_flags) {
    long id = get_global_id(0);
    double self  = distribute(data_in);
    double right = neighbor(data_in, 1);
    double left  = neighbor(data_in, -1);
    double is_left = ((id & 1) == 0) ? 1.0 : 0.0;   // 偶数 PE がペアの左メンバ

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
