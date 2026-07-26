// 奇偶転置ソート 1 フェーズ (odd kernel、 get_global_id 版)。
//
// even と同じく、 左メンバ判定を get_global_id で行う。 例 10 の odd フェーズの flag は
// {0,1,0,1,...}（奇数 PE が左メンバ）なので、 「PE 番号が奇数 = 左メンバ」で置き換える。
//   is_left = distribute(flag)  ->  is_left = ((id & 1) != 0) ? 1.0 : 0.0
// ping-pong の向き（data_in / data_out の割り当て）だけ even と逆にする（.param 参照）。
// 両端 (PE 0 と PE 4095) は例 10 と同じく boundary_flags の clamp で self 比較となり swap しない。
//
// data_in:    in  (このフェーズの入力配列。 distribute / neighbor が読む)
// data_out:   out (1 フェーズ後の配列。 collect 先)
// swap_flags: out (PE ごとの swap 有無 0.0/1.0。 reduce kernel が読み直して集計する)
__kernel void odd_even_sort_gid_odd(__global double* data_in,
                                    __global double* data_out,
                                    __global double* swap_flags) {
    long id = get_global_id(0);
    double self  = distribute(data_in);
    double right = neighbor(data_in, 1);
    double left  = neighbor(data_in, -1);
    double is_left = ((id & 1) != 0) ? 1.0 : 0.0;   // 奇数 PE がペアの左メンバ

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
