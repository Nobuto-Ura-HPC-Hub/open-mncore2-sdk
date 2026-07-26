// 奇偶転置ソートの swap 有無を集計する reduce kernel。
// even / odd kernel が collect した swap_flags (PE ごとの 0.0/1.0) を PDM から distribute で
// 読み直し、 reduce_add で全 PE を集計して swap_count (4 partial sum) を書き戻す。
//
// even / odd に reduce_add をインライン維持すれば PDM 往復を 1 回減らせるが、 あえて分離した。
// reduce 部分を最小・独立に保つ設計判断による (README.md「3 kernel 分割の理由」参照)。
//
// swap_flags: in  (even / odd が collect した PE ごとの swap 有無)
// swap_count: out (4 partial sum。 PE position 別の swap カウント。 host が合計する)
__kernel void odd_even_sort_reduce(__global double* swap_flags,
                                   __global double* swap_count) {
    double sf = distribute(swap_flags);
    reduce_add(swap_count, sf);
}
