// 12-vecadd-i64: c[i] = a[i] + b[i] (4096 PE, i64 整数加算)
//
// i64 の整数演算が普通の CPU として動くことのデモ。add は XREG(i64) の ladd に落ちる。
// distribute/collect は i64(long) 版（opencl-c.h に long overload あり）。

__kernel void main_kernel0(__global long* _arg_array1, __global long* _arg_array2, __global long* _arg_array3)
{
    long a = distribute(_arg_array1);
    long b = distribute(_arg_array2);

    long c = a + b;

    collect(_arg_array3, c);
}
