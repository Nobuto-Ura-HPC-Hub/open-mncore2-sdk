// 15-broadcast: out[id] = v + id  (v は全 4096 PE で同値、broadcast で配布)
//
// broadcast は host のあるデータを全 PE に同じ値として配る（distribute が PE ごとに別の要素を
// 配るのと対になる）。全 PE を同値にするには host が broadcast 入力の index 12..15 を同値で
// 埋める（詳細は README.md と、vsmlink-kit の example 25-broadcast-reduce-add の README）。
//
//   v  = broadcast(_bc)    全 PE 同値の i64
//   id = get_global_id(0)  PE ごとの ID [0..4095]（host が id の表を送る）
//   out[id] = v + id       v は共通、id で PE ごとに変わる
__kernel void bcast(__global const long* _bc, __global long* _out)
{
    long v  = broadcast(_bc);
    long id = get_global_id(0);
    collect(_out, v + id);
}
