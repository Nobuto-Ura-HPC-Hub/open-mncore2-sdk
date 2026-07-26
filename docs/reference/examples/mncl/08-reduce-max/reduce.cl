// reduce_max sample: 全 4096 PE の値の最大を求め、 4 partial maxes として PDM に出力
__kernel void main_kernel0(__global double* _arg_a, __global double* _arg_c)
{
    double val = distribute(_arg_a);
    reduce_max(_arg_c, val);
}
