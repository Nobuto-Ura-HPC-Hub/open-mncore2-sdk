__kernel void main_kernel0(__global double* _arg_a, __global double* _arg_c) {
    double self  = distribute(_arg_a);
    double left  = neighbor(_arg_a, -1);
    double right = neighbor(_arg_a, +1);

    double result = left + self + right;

    collect(_arg_c, result);
}
