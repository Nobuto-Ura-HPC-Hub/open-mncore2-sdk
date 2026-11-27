// 23-emit-from-debug: emit_from でデバッグ用に PE の値を書き出す
//
// emit_from は、デバッグ用の API である。実機では PE の中の途中の値が見えない。emit_from(name, pe, p, v) を使うと、HW PE ID が pe の PE の
// 値 v を、呼ぶたびに PDM の p の続きへ書き出せる。ホストは、kernel が終わった後で、その列を読む（vsmlink の `35-emit-from-debug` に当たる）。
//
// この kernel がしていること:
//   1. PE j（0〜4095）の初期値 j + 1 を distribute で配る
//   2. broadcast の値（1, 2, 3, ..., 100）を、呼ぶたびに全 PE に足す。k 回目（1〜100）の値 k を足す。初期値が PE ごとに違うので、足した後の値も PE ごとに違う
//   3. 足すたびに emit_from で書き出す。1〜60 回目は HW PE ID 817、61〜100 回目は HW PE ID 529 の PE を書き出す
//   4. 最後に collect で全 PE の最終値を返す（ホストは、この完了を待って、kernel の終わりを知る）
//
// pe は、コンパイルの時点で決まる定数でなければならない。#pragma unroll で、k のループを完全に展開して、(k < 60) ? 817 : 529 を定数にする。
// name（"em"）は文字列リテラルで、生成する .vsm のコメントに書かれる。vsmlink の <debug_name> の規則を参照すること。
__kernel void main_kernel0(__global double* _arg_a, __global double* _arg_b, __global double* _arg_d, __global double* _arg_c)
{
    double v = distribute(_arg_a);

    #pragma unroll
    for (int k = 0; k < 100; k++) {
        v = v + broadcast(_arg_b);
        emit_from("em", (k < 60) ? 817 : 529, _arg_d, v);
    }

    collect(_arg_c, v);
}
