// 29-float2-float8: f32 を u64 に 2 つずつ詰めて計算する（float2 は u64 1 つ、float8 は u64 4 つ）
//
// float2 の .x は u64 の上位 32 ビット、.y は下位 32 ビット。float8 の要素 2c は u64 c の上位、2c + 1 は下位。
// ホストは、この並びに合わせて詰める（test_float2_float8.c の pack）。
// float8 の演算は 1 命令で 8 要素を計算する。
__kernel void main_kernel0(__global float2* _a, __global float2* _b,
                           __global float2* _out2, __global float2* _out8)
{
    float2 x = distribute(_a);          // PE あたり u64 1 つ
    float8 v = distribute4(_b);         // PE あたり u64 4 つ

    collect(_out2, x * 2.0f + (float2)(x.y, x.x));   // スカラ倍、要素の入れ替え
    collect4(_out8, v * v - v);                      // 1 命令で 8 要素
}
