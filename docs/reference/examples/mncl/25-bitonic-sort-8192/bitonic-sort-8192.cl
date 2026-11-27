// 25-bitonic-sort-8192: バイトニックソート（8192 個、double）。4096 個を超える個数でも get_hypercube_neighbor が使える
//
// 1 チップの PE は 4096 個なので、各 PE が 2 つの値を持つ。要素の番号を i = j + 4096 * r とする（j は PE の番号、r は 0 か 1）。
//   a: 要素 j          （distribute2 の成分 0。入力の前半の 4096 個）
//   b: 要素 j + 4096   （distribute2 の成分 1。入力の後半の 4096 個）
// get_hypercube_neighbor の第 2 引数には、要素の番号を渡す（a には id、b には id + 4096）。
//
// 段 s（1〜13）、ステップ j（s-1 から 0）。要素 i は、相手 i XOR 2^j と比べ、i の bit j と bit s が等しければ小さい方、
// そうでなければ大きい方を新しい値にする（全体は 91 ステップで昇順になる）。
//   - j が 0〜11 のとき、相手は同じ r の別の PE にある。a と b のそれぞれに get_hypercube_neighbor を 1 回ずつ呼ぶ
//   - j が 12 のとき（段 13 の最初の 1 回だけ）、相手は同じ PE のもう一方の値である。PE の間の移動は要らない。deep に 12 は書けないので、
//     a と b を比べて、a に小さい方、b に大きい方を入れる
//   - bit s は要素の番号の bit s なので、段 12 では b（bit 12 が 1）は降順になる。b の向きを id（下位 12 ビットだけ）で決めると、
//     段 12 で a と b が同じ向きに並び、段 13 のバイトニックマージの前提が成り立たない
//
// get_hypercube_neighbor の deep（ここでは j）は定数でなければならない。#pragma unroll で、s と j のループを完全に展開して定数にする。
__kernel void main_kernel0(__global double* _arg_a, __global double* _arg_c)
{
    long id = get_global_id(0);
    long id_b = id + 4096;
    double2 ab = distribute2(_arg_a);
    double a = ab.x;
    double b = ab.y;

    #pragma unroll
    for (int s = 1; s <= 13; s++) {
        #pragma unroll
        for (int j = s - 1; j >= 0; j--) {
            if (j == 12) {
                double lo = (a < b) ? a : b;
                double hi = (a < b) ? b : a;
                a = lo;
                b = hi;
            } else {
                double other_a = get_hypercube_neighbor(a, id, j);
                double lo_a = (a < other_a) ? a : other_a;
                double hi_a = (a < other_a) ? other_a : a;
                bool take_min_a = (((id >> j) ^ (id >> s)) & 1) == 0;

                double other_b = get_hypercube_neighbor(b, id_b, j);
                double lo_b = (b < other_b) ? b : other_b;
                double hi_b = (b < other_b) ? other_b : b;
                bool take_min_b = (((id_b >> j) ^ (id_b >> s)) & 1) == 0;

                a = take_min_a ? lo_a : hi_a;
                b = take_min_b ? lo_b : hi_b;
            }
        }
    }

    collect2(_arg_c, (double2)(a, b));
}
