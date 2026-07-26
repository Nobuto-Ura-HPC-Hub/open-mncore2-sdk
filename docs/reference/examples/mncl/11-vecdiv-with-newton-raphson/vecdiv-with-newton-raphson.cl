// vecdiv (Newton-Raphson): c[i] = a[i] / b[i] を full double 精度で求める
//
// HW の fdiv は drsqrt 5 bit ベースで最大相対誤差 ~5.6%。
// Newton-Raphson 反復 5 回で full double 精度 (~52 bit) に到達する。
//
// 反復式: x_{n+1} = x_n * (2.0 - b * x_n)
// 初期値: x_0 = 1.0 / b  (HW fdiv の近似値、 ~5 bit)

inline double mncore2_div_double(double a, double b)
{
    double x = 1.0 / b;             // 初期値 (HW fdiv 近似、 ~5 bit)
    x = x * (2.0 - b * x);          // 反復 1: ~10 bit
    x = x * (2.0 - b * x);          // 反復 2: ~20 bit
    x = x * (2.0 - b * x);          // 反復 3: ~40 bit
    x = x * (2.0 - b * x);          // 反復 4: ~52 bit (full double)
    x = x * (2.0 - b * x);          // 反復 5: 安全余裕
    return a * x;
}

__kernel void main_kernel0(__global double* _arg_a,
                           __global double* _arg_b,
                           __global double* _arg_c)
{
    double a = distribute(_arg_a);
    double b = distribute(_arg_b);

    double c = mncore2_div_double(a, b);

    collect(_arg_c, c);
}
