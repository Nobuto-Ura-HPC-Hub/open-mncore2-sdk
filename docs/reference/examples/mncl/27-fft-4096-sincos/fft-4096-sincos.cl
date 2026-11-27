// 27-fft-4096-sincos: FFT（4096 点、複素 f64、周波数間引き型、基数 2）
// （26-fft-4096 と同じ計算。26 は w の表をホストが作るが、27 は kernel の中で sin と cos から作る。相手の値は get_hypercube_neighbor で受け取り、複素数の乗算に mad を使う）
//
// PE の番号 i の PE が 1 点 x_i（実部 xr と虚部 xi）を持つ。段 j（j = 11 から 0）で、PE i は相手 i XOR 2^j の値 y を受け取り、
//   i の bit j が 0 の PE（下側）: 新しい値 = x + y
//   i の bit j が 1 の PE（上側）: 新しい値 = (y - x) * w、w = exp(-2πi * (i mod 2^j) / 2^(j+1))
// j = 0 では w = 1 なので、乗算をしない。12 段のあと、PE p が X[bitrev(p)]（出力はビット反転順）を持つ。入力は自然順。
//
// w は kernel の中で作る: w = exp(-2πi * (i mod 2^j) / 2^(j+1)) = cos(a) + i sin(a)、a = -π * (i mod 2^j) / 2^j。
// 26-fft-4096 のホストの表（-2π (i mod 2^j) / 2^(j+1) を double で計算して cos と sin）と、a は同じ double になる
// （π の定数の積と 2 のべきの割り算だけなので、丸めは同じ）。sin と cos は、デバイスの標準関数（f64、最大相対誤差 約 2.2e-16）。
//
// 複素数の乗算 (dr + i di) * (wr + i wi) は、mad（積和。MN-Core 2 の dvfmau / dvfmad）を使う。
//   pr = dr * wr - di * wi = mad(dr, wr, -(di * wi))
//   pi = dr * wi + di * wr = mad(dr, wi, di * wr)
// **mad は、正確に丸めた積和（fma）ではない**（share/mncl/mad.md）。結果は numpy の FFT とビット単位では一致しない。
//
// get_hypercube_neighbor の deep（ここでは j）は定数でなければならない。段ごとに、j を定数で書く（STAGE マクロ）。

#define PI 3.14159265358979323846

#define STAGE(j)                                                        \
    {                                                                   \
        double yr = get_hypercube_neighbor(xr, id, j);                  \
        double yi = get_hypercube_neighbor(xi, id, j);                  \
        double a = (double)(id & ((1 << j) - 1)) * (-PI / (double)(1 << j)); \
        double wr = cos(a);                                             \
        double wi = sin(a);                                             \
        double dr = yr - xr;                                            \
        double di = yi - xi;                                            \
        double qr = mad(dr, wr, -(di * wi));                            \
        double qi = mad(dr, wi, di * wr);                               \
        bool upper = ((id >> j) & 1) != 0;                              \
        double nr = upper ? qr : (xr + yr);                             \
        double ni = upper ? qi : (xi + yi);                             \
        xr = nr;                                                        \
        xi = ni;                                                        \
    }

__kernel void main_kernel0(__global double* _arg_x, __global double* _arg_out)
{
    long id = get_global_id(0);
    double2 x = distribute2(_arg_x);
    double xr = x.x;
    double xi = x.y;

    STAGE(11)
    STAGE(10)
    STAGE(9)
    STAGE(8)
    STAGE(7)
    STAGE(6)
    STAGE(5)
    STAGE(4)
    STAGE(3)
    STAGE(2)
    STAGE(1)

    // 段 0: w = 1 なので、乗算をしない
    {
        double yr = get_hypercube_neighbor(xr, id, 0);
        double yi = get_hypercube_neighbor(xi, id, 0);
        bool upper = (id & 1) != 0;
        double nr = upper ? (yr - xr) : (xr + yr);
        double ni = upper ? (yi - xi) : (xi + yi);
        xr = nr;
        xi = ni;
    }

    collect2(_arg_out, (double2)(xr, xi));
}
