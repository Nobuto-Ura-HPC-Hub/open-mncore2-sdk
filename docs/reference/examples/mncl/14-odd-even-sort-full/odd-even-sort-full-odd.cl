// 奇偶転置ソート full 版 (odd kernel)。even と同じく get_global_id で左メンバ判定し、
// swap 有無を inline reduce_add で swap_count に直接出す。
//
// odd フェーズ: PE 番号が奇数の PE がペアの左メンバ（is_left = ((id & 1) != 0)）。
// ping-pong の向きだけ even と逆（data_in=data_b、data_out=data_a）。境界は boundary_flags の clamp。
//
// data_in:    in  (このフェーズの入力配列)
// data_out:   out (1 フェーズ後の配列)
// swap_count: out (全 PE の swap 有無を reduce_add した 4 partial sum)
__kernel void odd_even_sort_full_odd(__global double* data_in,
                                     __global double* data_out,
                                     __global double* swap_count) {
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

    double swap_indicator = do_left + do_right;
    reduce_add(swap_count, swap_indicator);

    collect(data_out, result2);
}
