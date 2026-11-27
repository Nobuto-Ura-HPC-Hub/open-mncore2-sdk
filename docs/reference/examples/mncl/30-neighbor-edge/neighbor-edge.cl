// 30-neighbor-edge: 隣の PE の値を受け取る関数の、端の扱い（double）
//
// 端の PE（区切りの外から値を受け取ることになる PE）が受け取る値は、関数で選ぶ。
//   load_neighbor / get_neighbor / get_eoshift                         : 自分の値（clamp。既定）
//   load_neighbor_periodic / get_neighbor_periodic / get_eoshift_periodic : 反対の端の PE の値（周期境界）
//   load_neighbor_boundary / get_neighbor_boundary / get_eoshift_boundary : 最後の引数 b の、端の PE 自身の値
//
// 1 次元の 3 点の和を、3 つの端の扱いで計算する。あわせて、32 x 128 の格子（j = y * 32 + x）の y の上隣を、
// get_eoshift_periodic で上下につないで受け取る。
__kernel void neighbor_edge(__global double* _arg_a, __global double* _arg_clamp, __global double* _arg_periodic,
                            __global double* _arg_fixed, __global double* _arg_up)
{
    double a = distribute(_arg_a);

    // 既定（clamp）: 端の PE は、隣の代わりに自分の値を足す
    collect(_arg_clamp, load_neighbor(_arg_a, -1) + a + load_neighbor(_arg_a, +1));

    // 周期境界: 端の PE は、反対の端の PE の値を足す
    collect(_arg_periodic, load_neighbor_periodic(_arg_a, -1) + a + load_neighbor_periodic(_arg_a, +1));

    // 固定の境界値（ディリクレ境界）: 端の PE は、隣の代わりに 0 を足す
    collect(_arg_fixed, load_neighbor_boundary(_arg_a, -1, 0.0) + a + load_neighbor_boundary(_arg_a, +1, 0.0));

    // 32 x 128 の格子の y の上隣（j - 32）。y = 0 の行は、y = 127 の行の値を受け取る
    collect(_arg_up, get_eoshift_periodic(a, -1, 5, 12));
}
