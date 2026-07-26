// 18-broadcast-vec: out[i] = v.x + v.y * 2 + v.z * 4 + x[i]
//
// broadcast の size N の確認。1 回の broadcast で double3 を配る。
//
//   v = broadcast(_bc)   double3。全 PE が同じ 3 個を受け取る
//   x = distribute(_x)   PE ごとの値
//
// **レーンごとに係数を変えてある。** v.x / v.y / v.z が入れ替わったり、同じ値が 3 つ
// 届いたりしたら結果が合わなくなる。全部同じ係数だと、レーンの取り違えを見逃す。
//
// host は放送領域の**先頭 3 u64** に配りたい値を置く（源 u64[i] が cycle i として着地する）。
// 16-broadcast-f64 はバッファ全体を同じ値で埋めていたので、先頭 N 個という規約を
// 検証できていなかった。ここではレーンごとに別の値を置く。
__kernel void bcast_vec(__global const double3* _bc,
                        __global const double* _x,
                        __global double* _out)
{
    double3 v = broadcast(_bc);
    double x = distribute(_x);
    collect(_out, v.x + v.y * 2.0 + v.z * 4.0 + x);
}
