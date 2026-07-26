// abs(x) = x > 0 ? x : -x
// if/else を使った fp64 absolute value kernel
__kernel void test_abs(__global double* a, __global double* c) {
    double x = distribute(a);
    double result;
    if (x > 0.0) {
        result = x;
    } else {
        result = -x;
    }
    collect(c, result);
}
