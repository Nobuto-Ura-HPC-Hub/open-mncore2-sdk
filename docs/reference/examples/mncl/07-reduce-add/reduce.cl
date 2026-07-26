// reduce_add sample: 全 4096 PE の値を合計し、 4 partial sums として PDM に出力
__kernel void main_kernel0(__global double* _arg_a, __global double* _arg_c)
{
    double val = distribute(_arg_a);
    reduce_add(_arg_c, val);
}
