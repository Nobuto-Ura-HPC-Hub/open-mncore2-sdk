// 17-nbody-2d: 2D 重力多体（f64）。段階的に育てる example。
//
// 【Step 5】位置と力を double2 にまとめ、ベクタのまま計算する。
//
// x / y と fx / fy は 2 次元ベクトルなので double2 で扱う。distribute が 4 本から 2 本、
// collect が 2 本から 1 本に減る。**転送の回数そのものが減る。**
//
// **PDM 上の並びは「成分が外側、PE が内側」である。** つまり x が先頭 4096 個、その後ろに
// y が 4096 個。OpenCL の double2 配列（PE ごとに x,y が隣接）とは違う。この並びを作るのは
// ホストの責務であり、引数が double* なのはそのため（値のほうは double2 でよい）。
//
// ホスト loop で k=0..N-1 の粒子を broadcast してカーネルを起動し、各 PE が力を積算する。
// 積算値はカーネルをまたいで PDM に常駐する（10-odd-even-sort と同じ「collect した値を次の
// distribute で読み直す」パターン）。
//   r2 = |d|^2 + eps^2、 1/r = rsqrt(r2) を Newton-Raphson で精緻化、 f += m_k * d / r^3
//
// **スカラ × ベクタ（splat）を使う。** m_k * ir3 はスカラで、それをベクタ d に掛ける。
// HW が splat を直接持つので（v 無しレジスタ）、余計なコピーは出ない。
#define EPS2 0.0625   /* 2^-4 softening^2 */

__kernel void nbody2d(__global const double* _pos,
                      __global const double2* _bpos,
                      __global const double* _bmass,
                      __global double* _f)
{
    /* 位置 (x, y) を 1 回の distribute で受け取る */
    double2 pi = distribute2(_pos);

    /* 粒子 k の位置と質量を broadcast で配る（size N）。
     * broadcast は 1 個の値を全 PE に配るだけなので、並びの問題が無く型で overload できる。
     *
     * **位置と質量を 1 本の double3 にまとめない。** そうすると (double2)(bk.x, bk.y) が
     * extract_subvector に畳まれ、backend が未対応で落ちる。型を分けて渡せば済む。 */
    double2 pk = broadcast(_bpos);
    double mk = broadcast(_bmass);

    /* 前回までの積算値（PDM 常駐）を 1 回で読む */
    double2 f = distribute2(_f);

    /* ここから最後までベクタのまま計算する */
    double2 d = pk - pi;
    double2 dd = d * d;
    double r2 = dd.x + dd.y + EPS2;

    double ir = rsqrt(r2);
    ir = ir * (1.5 - 0.5 * r2 * ir * ir);   /* Newton-Raphson 1 */
    ir = ir * (1.5 - 0.5 * r2 * ir * ir);   /* Newton-Raphson 2 */
    ir = ir * (1.5 - 0.5 * r2 * ir * ir);   /* Newton-Raphson 3 */
    double ir3 = ir * ir * ir;

    f = f + (mk * ir3) * d;        /* 積算。スカラ (mk*ir3) を splat で d に掛ける */

    collect2(_f, f);               /* 同一スロットに書き戻す */
}
