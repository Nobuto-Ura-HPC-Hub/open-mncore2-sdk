# 15-broadcast

broadcast の最小デモ。**host のある 1 つの値 v を全 4096 PE に同じ値として配り**、各 PE が
`out[id] = v + id` を計算して書き戻す（id は `get_global_id`）。配られる v は全 PE 共通で、出力は
id の分だけ PE ごとに変わる。

broadcast は distribute（PE ごとに別の要素を配る）と対になる操作で、全 PE が同じデータを必要とする
計算（nbody など）の土台になる。

## 何をするか

- `v  = broadcast(_bc)`   全 PE 同値の i64（host が仕込む。下記「ホスト側の協力」を必ず読むこと）
- `id = get_global_id(0)`  PE ごとの ID [0..4095]（`@identify`。host が id 表を送る）
- `out[id] = v + id`       各 PE が計算して collect

型は i64（long）。f64 / f32 版の broadcast はまだ無い。

## MN-Core2 の broadcast の仕組みと、ホスト側の協力（重要）

**`@broadcast` は HW だけでは「全 PE に同じ値」にならない。host の協力があって初めて全 PE 同値に
なる。** ここが broadcast を使ううえで一番の注意点。

MN-Core2 の broadcast（HW の l1bmm 命令）は、PDM に置かれた 16 個の u64 のうち **index 12..15 の
4 個**を、MAB 内の 4 個の PE（sub_pe_id 0..3）に **1 個ずつ配り分ける**動作をする。つまり素の HW 動作は
「4 値を sub_pe_id 別に配る」であって、「全 PE 同値」ではない。

- **全 4096 PE を同じ値 v にしたいとき**: host が `PDM[12] = PDM[13] = PDM[14] = PDM[15] = v` にする。
  4 つとも同じ値にすることで、どの sub_pe_id も v を受け取り、結果として全 PE 同値になる。この example
  はこれをやっている（broadcast 入力バッファ全体を v で埋めている）。
- **sub_pe_id ごとに別の値を配りたいとき**: `PDM[12..15]` に 4 つの別々の値を置く（vsm-linker の参照
  example `25-broadcast-reduce-add` がこの demo）。

つまり **「何を配るか」「全 PE 同値にするか」を決めるのは host** であり、backend と HW は「PDM の
どこから配るか」を担うだけ。この責任分担を踏まえずに index 12..15 を埋め忘れると、その sub_pe_id の
1024 個の PE が意図しない値を受け取る（不一致が PE 番号 `i % 4` で規則的に出たら、これを疑う）。

### ホスト側の協力（この example の driver がやっていること）

- **broadcast 入力の index 12..15 を、配りたい値 v で埋める**（この example はバッファ全体を v で埋める
  ので 12..15 も v になる）。
- **転送単位は 64 u64 が最小**（HW の mvb 命令の制約）。broadcast 入力スロットには 64 u64 以上を確保・
  送信する。
- **id 表 [0..4095] も host が送る**。`@identify` は HW 自動生成ではなく、`@distribute` と同じく PDM に
  置いた配列を配る。
- **送信のトリガ順**: broadcast 入力を非トリガ（tag 0）で送り、id 表をトリガ（tag 0x10）で送る。同じ
  待ち tag を 2 度トリガにすると DMA が二重 assert で失敗するため、最後の 1 つだけをトリガにする。

## kernel インターフェース

```c
__kernel void bcast(__global const long* _bc,    // broadcast 入力（host が index 12..15 を v で埋める）
                    __global long* _out)          // out (collect)
{
    long v  = broadcast(_bc);
    long id = get_global_id(0);
    collect(_out, v + id);
}
```

## PDM レイアウト（broadcast.param）

- `slot 8`     = broadcast 入力（`_bc`, arg0）。host が index 12..15 を同値 v で埋める。
- `identify 0` = `get_global_id` 用の id 表 [0..4095]（host が送る）。
- `slot 16`    = collect 出力（`_out`, arg1）。
- slot 番号は param_offset = 8*(argno+1) に対応する（`_bc` は 8、`_out` は 16）。

## ビルドと実行

```bash
ninja build-e2e     # .asm / .idma.dat と host バイナリを生成
ninja test-emu-lib  # host エミュレータで実行し検証
ninja test-device   # 実機で実行
```

## 検証の内容

host が全 4096 要素で `out[i] == v + i` を照合する。v は 32-bit を越える値（`0x0000000700000005`）に
して i64 の両バンク（64-bit）を実際に使う。**不一致が PE 番号 `i % 4` で規則的に出たら、broadcast 入力
index 12..15 の埋め忘れ（sub_pe_id 別分配の罠）を疑う。**
