// 21-nop: デバッグ用の待ち nop(n) の例（mncl-kit 0.3.5）。c[i] = a[i] + b[i] の加算の前に、8 ステップの待ちを置く。
//
// nop(n) は、その位置に n ステップの待ち（アセンブリの nop/n）を置く。n はコンパイルの時点で決まる定数（0 から 65535）。
// 正しさのための待ちはコンパイラが入れるので、ふだんは要らない。待ちを足して実機の挙動を調べるときに使う。
// 計算の結果は、待ちの有無で変わらない（01-vecadd と同じ値になる）。
__kernel void main_kernel0(__global double* _arg_array1, __global double* _arg_array2, __global double* _arg_array3)
{
    double a = distribute(_arg_array1);
    double b = distribute(_arg_array2);

    nop(8);

    double c = a + b;

    collect(_arg_array3, c);
}
