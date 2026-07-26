// 16-broadcast-f64: out[i] = v + w + x[i]
//
// broadcast の f64 版の確認。1 カーネルに broadcast を 2 本置いて、複数 broadcast が同一カーネルで
// 動くことも確認する（nbody は 3 本使うため）。
//   v = broadcast(_bc)   全 PE 同値（host が bc の index 12..15 を V で埋める）
//   w = broadcast(_bc2)  全 PE 同値（同一カーネルで 2 本目の broadcast）
//   x = distribute(_x)   PE ごとの値
//   out[i] = v + w + x[i]
__kernel void bcast_f64(__global const double* _bc,
                        __global const double* _bc2,
                        __global const double* _x,
                        __global double* _out)
{
    double v = broadcast(_bc);
    double w = broadcast(_bc2);
    double x = distribute(_x);
    collect(_out, v + w + x);
}
