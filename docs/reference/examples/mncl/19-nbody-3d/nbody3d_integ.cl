// 19-nbody-3d 積分フェーズ: semi-implicit Euler（Euler-Cromer）の 1 ステップ（f64, double3）。
//
// 2 カーネル構成の 2 本目。力フェーズ（nbody3d_force.cl）が _acc に加速度 a を積んだ後、host が
// 1 回だけこのカーネルを起動する。全 PE が並列に自粒子の状態を進める（粒子間相互作用は無い）。
//
//   v <- v + dt * a      速度を先に更新する
//   x <- x + dt * v      **更新後の速度**で位置を更新する（これが Euler-Cromer の肝。explicit
//                        Euler は古い v を使う。semi-implicit Euler は概ね symplectic で
//                        エネルギーが有界振動する）
//
// **このカーネルは triggerless（起動トリガの送信が不要）。** 入力（pos/vel/acc/dt）はすべて PDM
// 常駐で、host から新しく送るデータが無い。.param に send_wait_tag を付けないと vsm-linker は先頭の
// 起動ゲート wait を出さず、mnc2_exec_kernel 単独でカーネルが走る（ISS-213。10-odd-even-sort の reduce
// カーネルと同じ正攻法）。順序は host の sync recv で担保する。
//
// **dt は broadcast で受けるが、host が init で 1 回だけ送る常駐値である**（毎ステップは送らない）。
// runtime 値なので f64 即値制約（下位 32bit=0）も回避できる。
//
// **dt * a / dt * v はスカラ × ベクタ（splat）**。dt は全レーン共通なので v 無しレジスタで複製される。
// double3 の 4 レーン目（padding）は空振りで書かれるが、collect3 は 3 成分しか書き戻さないので無害。

__kernel void nbody3d_integ(__global double* _pos,
                            __global double* _vel,
                            __global const double* _acc,
                            __global const double* _dt)
{
    double3 x = distribute3(_pos);
    double3 v = distribute3(_vel);
    double3 a = distribute3(_acc);
    double  dt = broadcast(_dt);

    v = v + dt * a;    /* 速度を先に更新（splat 乗算） */
    x = x + dt * v;    /* 更新後の速度で位置を更新（Euler-Cromer, splat 乗算） */

    collect3(_pos, x);
    collect3(_vel, v);
}
