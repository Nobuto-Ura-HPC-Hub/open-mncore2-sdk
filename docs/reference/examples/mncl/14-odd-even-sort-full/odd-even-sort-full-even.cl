// 奇偶転置ソート full 版 (even kernel)。
//
// 13-odd-even-sort-gid の even kernel（get_global_id で左メンバ判定）をベースに、swap 有無を
// 別 kernel の reduce ではなく **kernel 内で inline reduce_add** して swap_count に直接出す
// （99 の inline reduce_add 方式。reduce の PDM 往復を省き、host の収束ループを速くする）。
// host はこの swap_count を毎フェーズ受け取り、even+odd の 1 turn で 0 になったら収束と判定する。
//
// even フェーズ: PE 番号が偶数の PE がペアの左メンバ（is_left = ((id & 1) == 0)）。
// ping-pong: data_in を読み data_out へ書く（odd は逆向き）。境界は boundary_flags の clamp。
//
// data_in:    in  (このフェーズの入力配列。 distribute / neighbor が読む)
// data_out:   out (1 フェーズ後の配列。 collect 先)
// swap_count: out (全 PE の swap 有無を reduce_add した 4 partial sum。 host が合計して収束判定)
__kernel void odd_even_sort_full_even(__global double* data_in,
                                      __global double* data_out,
                                      __global double* swap_count) {
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

    /* この PE が swap に関与したか (0 or 1) を全 PE で集計。host が合計 0 なら収束 */
    double swap_indicator = do_left + do_right;
    reduce_add(swap_count, swap_indicator);

    collect(data_out, result2);
}
