__kernel void main_kernel0(__global double* _arg_array1, __global double* _arg_array2, __global double* _arg_array3)
{
    double a = distribute(_arg_array1);
    double b = distribute(_arg_array2);

    double c = a + b;

    collect(_arg_array3, c);
}
