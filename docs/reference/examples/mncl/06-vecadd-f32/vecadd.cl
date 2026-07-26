__kernel void main_kernel0(__global float* _arg_array1, __global float* _arg_array2, __global float* _arg_array3)
{
    float a = distribute(_arg_array1);
    float b = distribute(_arg_array2);

    float c = a + b;

    collect(_arg_array3, c);
}
