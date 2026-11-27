// 20-persistent: persistent の変数を 2 つの kernel で共有する
//
// persistent を付けた変数は、kernel の実行が終わっても値が PE の中に残る。
// 値を共有する kernel は、この例のように 1 つの .cl に書く。
//
//   init:  acc = seed           acc に初期値を書く（読まない）
//   accum: acc = acc + in       前の実行が残した acc に足し、結果を out に返す
//
// ホストは init を 1 回、accum を 3 回実行する。accum の out は、実行するたびに
// seed + in(1 回目) + in(2 回目) + ... と累計になる。
persistent double acc;

__kernel void init(__global double *seed)
{
    acc = distribute(seed);
}

__kernel void accum(__global double *in, __global double *out)
{
    acc = acc + distribute(in);
    collect(out, acc);
}
