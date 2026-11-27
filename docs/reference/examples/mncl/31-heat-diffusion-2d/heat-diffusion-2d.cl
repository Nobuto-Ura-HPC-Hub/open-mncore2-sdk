// 31-heat-diffusion-2d: 2 次元の熱拡散（ヤコビ法。上下左右 4 点の平均を繰り返す）を get_eoshift_boundary で計算する（double）
//
// 格子は 32 x 128（4096 点。1 チップ）。PE の番号 j = y * 32 + x（x が並びの内側）。
//   - x の ±1: get_eoshift_boundary(u, ∓1 / ±1, N = 0, M = 5,  B)（区切りは 1 行 32 点）
//   - y の ±1: get_eoshift_boundary(u, ∓1 / ±1, N = 5, M = 12, B)（ずれ 32、区切りは 4096 点全体）
// 格子の外側は温度 B に固定されているとみなす（固定の境界値）。区切りの外から値を受け取ることになる PE（行の両端、先頭と末尾の行）は、
// 隣の代わりに B を受け取る。そのため、外周の PE かどうかを判定する必要は無く、4096 点をすべて同じ式で更新する。
//
// 1 回の kernel の実行で STEPS 回の更新を行う（#pragma unroll）。get_eoshift_boundary は計算の途中の値を受け取るので、同期は要らない。
#define B     1.0
#define STEPS 4

__kernel void heat_diffusion_2d(__global double* _arg_u, __global double* _out)
{
    double u = distribute(_arg_u);

    #pragma unroll
    for (int s = 0; s < STEPS; s++) {
        double l = get_eoshift_boundary(u, -1, 0, 5,  B);   // u[j - 1]
        double r = get_eoshift_boundary(u,  1, 0, 5,  B);   // u[j + 1]
        double d = get_eoshift_boundary(u, -1, 5, 12, B);   // u[j - 32]
        double t = get_eoshift_boundary(u,  1, 5, 12, B);   // u[j + 32]
        u = (l + r + d + t) * 0.25;
    }
    collect(_out, u);
}
