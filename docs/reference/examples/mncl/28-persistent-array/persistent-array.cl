// 28-persistent-array: persistent の double の配列 tab を、2 つの kernel（init、step）で共有する。
//
// init は tab[0..7] に値を書く。step は、前の実行が残した tab の各要素を 2 倍して書き戻し、その合計を返す。
// 配列の添字は、コンパイルのときに決まる値だけ（#pragma unroll で展開したループの k は使える）。
persistent double tab[8];

__kernel void init(__global double *a, __global double *b)
{
    double4 x = distribute4(a);
    double4 y = distribute4(b);
    tab[0] = x.x; tab[1] = x.y; tab[2] = x.z; tab[3] = x.w;
    tab[4] = y.x; tab[5] = y.y; tab[6] = y.z; tab[7] = y.w;
}

__kernel void step(__global double *out)
{
    double s = 0.0;
    #pragma unroll
    for (int k = 0; k < 8; k++) {
        tab[k] = tab[k] * 2.0;
        s = s + tab[k];
    }
    collect(out, s);
}
