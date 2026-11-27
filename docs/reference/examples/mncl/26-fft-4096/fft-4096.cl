// 26-fft-4096: FFT（4096 点、複素 f64、周波数間引き型、基数 2）
// （vsmlink の 40-fft-4096 に当たる。相手の値は get_hypercube_neighbor で受け取り、複素数の乗算に mad を使う）
//
// PE の番号 i の PE が 1 点 x_i（実部 xr と虚部 xi）を持つ。段 j（j = 11 から 0）で、PE i は相手 i XOR 2^j の値 y を受け取り、
//   i の bit j が 0 の PE（下側）: 新しい値 = x + y
//   i の bit j が 1 の PE（上側）: 新しい値 = (y - x) * w、w = exp(-2πi * (i mod 2^j) / 2^(j+1))
// j = 0 では w = 1 なので、乗算をしない。12 段のあと、PE p が X[bitrev(p)]（出力はビット反転順）を持つ。入力は自然順。
//
// w の表（j = 1〜11）は、ホストが cos と sin で作り、段ごとの kernel 引数（_arg_tw1〜_arg_tw11）で渡す。distribute2 で、
// 実部（wr）が成分 x、虚部（wi）が成分 y に届く。kernel の中で w を計算しない（f64 の cos と sin は使えない）。
//
// 複素数の乗算 (dr + i di) * (wr + i wi) は、mad（積和。MN-Core 2 の dvfmau / dvfmad）を使う。
//   pr = dr * wr - di * wi = mad(dr, wr, -(di * wi))
//   pi = dr * wi + di * wr = mad(dr, wi, di * wr)
// **mad は、正確に丸めた積和（fma）ではない**（share/mncl/mad.md）。結果は numpy の FFT とビット単位では一致しない。
//
// get_hypercube_neighbor の deep（ここでは j）は定数でなければならない。段ごとに、j を定数で書く（STAGE マクロ）。

#define STAGE(j, tw)                                                    \
    {                                                                   \
        double yr = get_hypercube_neighbor(xr, id, j);                  \
        double yi = get_hypercube_neighbor(xi, id, j);                  \
        double2 w = distribute2(tw);                                    \
        double dr = yr - xr;                                            \
        double di = yi - xi;                                            \
        double qr = mad(dr, w.x, -(di * w.y));                          \
        double qi = mad(dr, w.y, di * w.x);                             \
        bool upper = ((id >> j) & 1) != 0;                              \
        double nr = upper ? qr : (xr + yr);                             \
        double ni = upper ? qi : (xi + yi);                             \
        xr = nr;                                                        \
        xi = ni;                                                        \
    }

__kernel void main_kernel0(__global double* _arg_x,
                           __global double* _arg_tw1, __global double* _arg_tw2, __global double* _arg_tw3,
                           __global double* _arg_tw4, __global double* _arg_tw5, __global double* _arg_tw6,
                           __global double* _arg_tw7, __global double* _arg_tw8, __global double* _arg_tw9,
                           __global double* _arg_tw10, __global double* _arg_tw11,
                           __global double* _arg_out)
{
    long id = get_global_id(0);
    double2 x = distribute2(_arg_x);
    double xr = x.x;
    double xi = x.y;

    STAGE(11, _arg_tw11)
    STAGE(10, _arg_tw10)
    STAGE(9, _arg_tw9)
    STAGE(8, _arg_tw8)
    STAGE(7, _arg_tw7)
    STAGE(6, _arg_tw6)
    STAGE(5, _arg_tw5)
    STAGE(4, _arg_tw4)
    STAGE(3, _arg_tw3)
    STAGE(2, _arg_tw2)
    STAGE(1, _arg_tw1)

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
