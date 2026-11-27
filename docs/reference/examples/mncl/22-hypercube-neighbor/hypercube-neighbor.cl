// 22-hypercube-neighbor: 超立方体の隣の PE の値を受け取る get_hypercube_neighbor（4096 PE）
//
// PE の番号 j の PE は、番号 j XOR 2^deep の PE の値を受け取る。deep は 0〜11 の定数。
//   deep 0 : 相手は j XOR 1    （隣どうしを入れ替える）
//   deep 10: 相手は j XOR 1024 （遠い PE。deep 9〜11 は PDM を経由するので、.param に :buffer が要る）
// 第 2 引数 id は自分の PE の番号 j で、get_global_id(0) の値を渡す。
// 相手を取り違えたら値が合わないように、deep ごとに重みを変えて足す。
__kernel void main_kernel0(__global double* _arg_a, __global double* _arg_c)
{
    long id = get_global_id(0);
    double a = distribute(_arg_a);

    double n0  = get_hypercube_neighbor(a, id, 0);
    double n10 = get_hypercube_neighbor(a, id, 10);

    collect(_arg_c, a + n0 * 10.0 + n10 * 100.0);
}
