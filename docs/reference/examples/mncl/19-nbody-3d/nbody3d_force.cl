// 19-nbody-3d 力フェーズ: 各 PE が担当粒子 i の加速度 a_i を積算する（f64, double3）。
//
// 2 カーネル構成の 1 本目。もう 1 本は nbody3d_integ.cl（semi-implicit Euler の積分）。
// 加速度の積算値 a_i = sum_k m_k (r_k - r_i) / |r|^3 を PDM 常駐の _acc に貯める。
// **質量 m_i で割らないので、積算値は加速度そのもの**（重力加速度は m_i に依らない）。
//
// **PDM 上の並びは「成分が外側、PE が内側」である。** x が先頭 4096 個、その後ろに y が 4096 個、
// さらに z が 4096 個。OpenCL の double3 配列（PE ごとに x,y,z が隣接）とは違う。l1bmd が
// addr_b + cycle*64 でアクセスするため HW で決まっており選べない。並べるのはホストの責務であり、
// 引数が double* なのはそのため（値のほうは double3 でよい）。
//
// ホスト loop で k=0..N-1 の粒子を broadcast してカーネルを起動し、各 PE が寄与を積算する。
// 積算値はカーネルをまたいで PDM に常駐する（17-nbody-2d と同じ「collect した値を次の
// distribute で読み直す」パターン）。1 タイムステップの力フェーズの頭で host が _acc を 0 埋めする。
//   r2 = |d|^2 + eps^2、 1/r = rsqrt(r2) を Newton-Raphson で精緻化、 a += m_k * d / r^3
//
// **スカラ × ベクタ（splat）を使う。** m_k * ir3 はスカラで、それをベクタ d に掛ける。
// HW が splat を直接持つので（v 無しレジスタ）、余計なコピーは出ない。
//
// **double3 の 4 レーン目（padding）は計算されるが読まれない。** r2 は x,y,z の 3 レーンだけを
// 取り出して足し、collect3 も 3 成分だけ書き戻す。4 レーン目の空振りは無害である。
#define EPS2 0.0625   /* 2^-4 softening^2 */

__kernel void nbody3d_force(__global const double* _pos,
                            __global const double3* _bpos,
                            __global const double* _bmass,
                            __global double* _acc)
{
    /* 自粒子の位置 (x, y, z) を 1 回の distribute で受け取る（PDM 常駐、積分で更新される） */
    double3 pi = distribute3(_pos);

    /* 粒子 k の位置と質量を broadcast で配る。
     * broadcast は 1 個の値を全 PE に配るだけなので、並びの問題が無く型で overload できる。
     *
     * **位置と質量を 1 本の double4 にまとめない。** そうすると位置の double3 取り出しが
     * extract_subvector に畳まれ、backend が未対応で落ちる。型を分けて渡せば済む。 */
    double3 pk = broadcast(_bpos);
    double  mk = broadcast(_bmass);

    /* このステップの積算値（PDM 常駐、頭で host が 0 埋め）を 1 回で読む */
    double3 a = distribute3(_acc);

    /* ここから最後までベクタのまま計算する */
    double3 d = pk - pi;
    double3 dd = d * d;
    double r2 = dd.x + dd.y + dd.z + EPS2;

    double ir = rsqrt(r2);
    ir = ir * (1.5 - 0.5 * r2 * ir * ir);   /* Newton-Raphson 1 */
    ir = ir * (1.5 - 0.5 * r2 * ir * ir);   /* Newton-Raphson 2 */
    ir = ir * (1.5 - 0.5 * r2 * ir * ir);   /* Newton-Raphson 3 */
    double ir3 = ir * ir * ir;

    a = a + (mk * ir3) * d;        /* 積算。スカラ (mk*ir3) を splat で d に掛ける */

    collect3(_acc, a);             /* 同一スロットに書き戻す */
}
