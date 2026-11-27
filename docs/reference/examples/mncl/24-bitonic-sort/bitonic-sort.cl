// 24-bitonic-sort: バイトニックソート（4096 個、double）。get_hypercube_neighbor で比較・交換の相手を受け取る
//
// 段 s（1〜12）、ステップ j（s-1 から 0）。PE の番号 id の PE は、相手 id XOR 2^j の値を受け取り、
// id の bit j と bit s が等しければ小さい方、そうでなければ大きい方を新しい値にする（全体は 78 ステップで昇順になる）。
//
// get_hypercube_neighbor の deep（ここでは j）は定数でなければならない。#pragma unroll で、s と j のループを完全に展開して定数にする。
__kernel void main_kernel0(__global double* _arg_a, __global double* _arg_c)
{
    long id = get_global_id(0);
    double v = distribute(_arg_a);

    #pragma unroll
    for (int s = 1; s <= 12; s++) {
        #pragma unroll
        for (int j = s - 1; j >= 0; j--) {
            double other = get_hypercube_neighbor(v, id, j);
            double lo = (v < other) ? v : other;
            double hi = (v < other) ? other : v;

            bool take_min = (((id >> j) ^ (id >> s)) & 1) == 0;
            v = take_min ? lo : hi;
        }
    }

    collect(_arg_c, v);
}
