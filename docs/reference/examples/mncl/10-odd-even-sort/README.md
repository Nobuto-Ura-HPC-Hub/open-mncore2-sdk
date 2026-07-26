# 10-odd-even-sort

奇偶転置ソート (odd-even transposition sort) の 1 turn を 4096 PE, fp64 で実行し、
swap が起きた回数を集計する例。 `neighbor` と `if` / `else` (レーン述語実行) を使う。

このディレクトリは kernel を **even / odd / reduce の 3 つ**に分け、 データを
**ping-pong の 2 バッファ**でやり取りする構成になっている。 分けた理由とバッファ構成の
理由を以下に記す。 実装した理由が一番重要なので、 動くようになった後もこの説明は残す。

## 何をするか

奇偶転置ソートの 1 turn、 すなわち偶数フェーズと奇数フェーズを 1 回ずつ実行する。
収束まで turn を繰り返す full なソートはまだで、 host 側のループが要る (最後の節を参照)。

- 偶数フェーズ: ペア (0,1),(2,3),... を比較交換。 flag = {1,0,1,0,...}
- 奇数フェーズ: ペア (1,2),(3,4),... を比較交換。 flag = {0,1,0,1,...}。 両端 (PE 0 と PE 4095) は
  boundary_flags の clamp で self 比較となり swap しない

各フェーズごとに、 交換後の配列と「PE ごとの swap 有無」を得る。 swap 有無は reduce kernel が
全 PE ぶん足し合わせ、 swap 回数 (収束判定に使える 1 値) にする。

## ファイル構成

| ファイル | 役割 |
|---|---|
| `odd-even-sort-even.cl` / `odd-even-sort-even.param`     | even kernel。 偶数フェーズの比較交換と swap 有無の書き出し |
| `odd-even-sort-odd.cl`  / `odd-even-sort-odd.param`      | odd kernel。 kernel 本体は even と同一 (下記) |
| `odd-even-sort-reduce.cl` / `odd-even-sort-reduce.param` | reduce kernel。 swap 有無を全 PE 集計する |
| `odd-even-sort.stparam`  | PE 形状 (4096 PE 1D)。 3 kernel 共通 |
| `test_odd-even-sort.c`   | host driver |

## kernel を even / odd / reduce に分けた理由

### kernel をまたいでレジスタは残らないので、 値の受け渡しは PDM 経由になる

kernel (`.idma.dat` 単位で実行される一まとまり) は、 実行が終わると PE のレジスタ状態を
保持し続ける保証がない。 次の kernel を呼ぶとき、 PE のレジスタに「前の kernel がここに
置いた値が生きている」という情報を伝える手立てが無い。

そのため、 kernel をまたいで値を受け渡すには、 PDM (あるいは DRAM) という、 kernel 実行の
外側にある永続的な場所を経由するしかない。 これは MN-Core2 の kernel 実行モデルそのものから
来る制約であり、 実装の工夫で回避できるものではない。

even kernel が計算した「PE ごとの swap 有無」を reduce kernel に渡す際も、 一度 collect で
PDM に書き出し、 reduce kernel が distribute で PDM から読み直す、 という PDM 経由の往復に
なる。 kernel 起動も 1 turn あたり 2 回 (even, odd) から 4 回 (even, reduce, odd, reduce) に増える。
これらのコストは kernel 分割に伴うものとして受け入れている。

この「kernel をまたいでレジスタが生きている情報を伝えられない」制約そのものを無くしたい場合、
それは kernel 実行モデルへの大きな変更になる。 ここではその制約を前提として受け入れている。

### reduce をあえて even / odd に含めず分離した理由

reduce の集計 (`reduce_add`) を even / odd kernel の中に置けば、 PDM 往復を 1 回減らせる。
even が PE 内で集計まで済ませ、 1 回の collect で swap 回数を書けるからである。 それを承知の
うえで、 独立した reduce kernel に分けている。 PDM 往復のコストを払ってでも、 reduce 部分を
単純かつ独立した最小構成 (1 入力を distribute して reduce_add するだけ) に保つ、 という
設計上の判断による。 性能上の最適解ではない。

### even と odd の kernel 本体が同じ内容な理由

偶数フェーズか奇数フェーズかは、 host が渡す flag (どの PE がペアの左メンバか) だけで決まる。
kernel のコードはフェーズに依存しない。 したがって even と odd の `.cl` は kernel 名以外は
同一で、 フェーズの差は次の 2 つだけで表現している。

- host が渡す flag のパターン ({1,0,1,0,...} か {0,1,0,1,...} か)
- `.param` での data_in / data_out バッファの割り当ての向き (下記 ping-pong)

自分が even 側か odd 側かを PE 自身に計算させる (PE ID を得る) 組み込み関数は現状の MNCL に
無いので、 判定は host が flag を distribute で渡す方式にしている。 PE ID を自前計算する
init 専用 kernel は導入していない。

## データの ping-pong (2 バッファ)

data を 1 つの in/out 共用バッファにせず、 2 つのバッファ (data_a / data_b) に分けている。

- even: data_a を読み、 data_b へ書く
- odd : data_b を読み、 data_a へ書く (even と逆向き)

### なぜ分けるか

in/out を共用しても結果は正しい。 kernel は distribute / neighbor の読み取りが collect の
書き込みより前にすべて済むため、 隣接読み (`neighbor`) は上書き前の元の値を見る。 1 フェーズ
内で読みと書きが競合することはない。 だから正しさのためではない。

分けるのはデバッグのためである。 in/out を共用すると、 実行後に PDM をダンプしても
「見えている値が更新前か後か」が分からない。 入力と出力を別バッファにすれば、 そのフェーズの
入力バッファは実行中は不変で、 出力側だけが変化するので、 host から両方をダンプして直接
突き合わせられる。 コストは PDM 上のバッファが 1 つ増えるだけで小さい。

## PDM 配置

`.param` の `addr` は 8 byte (1 長語 = double 1 個) 単位。

| 内容 | addr (長語) | byte offset | 向き |
|---|---|---|---|
| data_a     | 0      | 0        | even の入力 / odd の出力 |
| flag       | 4096   | 32768    | in |
| boundary_flags | 8192 | 65536  | in |
| neighbor buffer | 16384 | 131072 | 内部スクラッチ |
| data_b     | 24576  | 196608   | even の出力 / odd の入力 |
| swap_flags | 32768  | 262144   | even/odd が書き、 reduce が読む |
| swap_count | 131072 | 1048576  | reduce の出力 (4 partial sum) |

**even / odd が swap_flags を書き出す addr と、 reduce が swap_flags を読む addr は一致させる
必要がある** (どちらも 32768)。 一致していないと reduce が別の場所を読んでしまう。

## host 呼び出し順

1 turn につき、 次の順で呼ぶ。

```
even   : flag と data を送る  ->  even 実行  ->  data_b から出力を受信、 swap_flags を受信
reduce : reduce 実行          ->  swap_count を受信 (even の swap 回数)
odd    : flag を送る          ->  odd 実行   ->  data_a から出力を受信、 swap_flags を受信
reduce : reduce 実行          ->  swap_count を受信 (odd の swap 回数)
```

- data は初回に host が data_a へ送る。 even 後は data_b、 odd 後は data_a から受信する
  (ping-pong の向きに合わせる)。 フェーズ間で data は device 上に残るので、 途中の再送は要らない。
- swap_flags は even / odd が collect するたびに host が受信して done flag を落とす
  (落とさないと次の collect が二重になる)。 この受信は、 reduce が読む前に collect が
  完了していることも保証する。 値そのものは reduce が PDM から読むので host 側では使わない。
- reduce は host から送るものが無い。 直前の even / odd が PDM に残した swap_flags を
  distribute で読むだけなので、 reduce の実行前に送信 (trigger) は要らない。

## kernel インターフェース

```c
// even / odd (本体は同一、 kernel 名のみ相違)
__kernel void odd_even_sort_even(__global double* data_in,     // in  (distribute / neighbor が読む)
                                 __global double* data_out,     // out (collect 先)
                                 __global double* flag,         // in  (ペアの左メンバか)
                                 __global double* swap_flags);  // out (PE ごとの swap 有無 0/1)

// reduce
__kernel void odd_even_sort_reduce(__global double* swap_flags, // in  (even/odd が書いたもの)
                                   __global double* swap_count); // out (4 partial sum)
```

## ビルドと実行

```bash
ninja build-e2e     # 3 kernel の .asm / .idma.dat と host バイナリを生成
ninja test-emu-lib  # host エミュレータで実行
ninja test-device   # 実機で実行
```

## 検証の内容

host は C 側で同じ 2 フェーズを適用した参照配列と、 device の出力 data を全要素比較する。
swap 回数もフェーズごとに C の計算値と照合する (期待値はハードコードしない)。 入力は 3 パターン:

- `desc`   降順 [4096..1]。 両フェーズとも全ペアが swap する
- `sorted` 昇順。 swap が 1 度も起きない (収束状態) 経路を通す
- `dup`    偶数ペアが等値、 奇数ペアは非等値。 strict `>` の検査を狙う。 等値ペアは swap して
           はならないので、 `>` が `>=` に退行すると偶数フェーズの swap 数が増えてここで落ちる

3 パターンの値域は互いに重ならないので、 前のテストの data が device 上に残ったまま次が
PASS する事故は data 比較で必ず捕まる。

## full なソートにするには (今後)

ここは 1 turn だけを実行する。 収束まで回す full なソートには host 側のループが要る
(最大 `ceil(4096 / 1)` turn。 各 turn 後の swap_count が全 0 になったら早期終了)。
data は in / out 共用アドレスで device に残せるので、 turn ごとの host 往復は要らない。
