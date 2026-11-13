{% raw %}
# _vsm ディレクティブ仕様

vsmlink が展開する全ディレクティブの構文と動作。
用語定義は末尾の「用語」セクションを参照。

## ディレクティブ一覧

| ディレクティブ | 説明 | 状態 |
|---------------|------|------|
| `@distribute` | PDM → L2BM → L1BM → PE（一括展開、 各 PE が異なる値） | 実装済み |
| `@broadcast` | PDM → L2BM → L1BM → PE（全 4096 PE が同じ値） | 実装済み |
| `@collect` | PE → L1BM → L2BM → PDM（一括展開） | 実装済み |
| `@distribute_aos2soa` | PDM（番号の順の並び）→ L2BM → L1BM → PE、転置つき。`@collect_soa2aos` と対 | 実装済み |
| `@collect_soa2aos` | PE → 転置 → L1BM → L2BM → PDM（番号の順の並び）。`@distribute_aos2soa` と対 | 実装済み |
| `@alias` | レジスタに名前を付ける（コード生成なし、境界フラグ参照用） | 実装済み |
| `@identify` | PE に問題空間座標を配る（PDM → L2BM → L1BM → `$lr` に展開） | 実装済み |
| `@access_pattern` | アクセスパターン宣言 | コメント化のみ |
| `@boundary_flags` | 境界フラグ生成（distribute モード） | 実装済み |
| `@boundary_flags_compute` | 境界フラグ生成（HW レジスタから計算。デバッグ用・非公開） | 実装済み |
| `@alloc` | レジスタプールの確保 | 実装済み |
| `@free` | レジスタプールの解放 | 実装済み |
| `@get_neighbor` | 隣接 PE の値を取得（全階層: PE/MAB/L1B/L2B） | 実装済み |
| `@reduce` | 全 4096 PE の階層的縮約（L1BM → L2BM → PDM） | 実装済み |
| `@kernel_begin` | 複数 kernel を含む `_vsm` の kernel 開始位置 | 実装済み |
| `@kernel_end` | kernel 終了位置 | 実装済み |
| `@persistent_info` | kernel をまたいで保持する変数の情報 | コメント化のみ |

**deprecated:**

| ディレクティブ | 説明 | 状態 |
|---------------|------|------|
| `@assign` | `@alias` の旧名（boundary flags 参照） | deprecated（今も動く）|
| `@stencil` | `@get_neighbor` の旧名 | 廃止。E243 でエラー |
| `@bind` | バッファ名とスロットの結び付け | 廃止。E243 でエラー。代替は無い |
| `@omr_alloc` | `@alloc` の `:omr` に統合 | 廃止。E243 でエラー |
| `@omr_free` | `@free` に統合 | 廃止。E243 でエラー |

**廃止したディレクティブも名前は残してある。** 使うと E243 で停止し、代替があれば
メッセージで案内する。 名前ごと消すと未知の `@` 行として素通しし、 廃止したことが
伝わらないまま `.vsm` に混入するためである。

## PDM の使用範囲

**directive は `.param` のスロットの開始アドレスから、決まった量の PDM を使う。**
仕様として受け取る量（`@reduce` の 4 u64 など）と、実際に占める量は別である。

| directive | 向き | 開始アドレスから使う量 |
|---|---|---|
| `@distribute` | PDM から読む | **4096 u64 かける `size`** |
| `@collect` | PDM へ書く | **4096 u64 かける `size`** |
| `@distribute_aos2soa` | PDM から読む | **16384 u64**（常に 4 成分）|
| `@collect_soa2aos` | PDM へ書く | **16384 u64**（常に 4 成分）|
| `@broadcast` | PDM から読む | **その kernel で消費した量**（消費した回数 × `size`。`size` が行ごとに違えばその合計） |
| `@reduce` | PDM へ書く | **64 u64** |

`@distribute` と `@collect` は 1 成分あたり 4096 u64（4096 PE かける 1 u64）を使う。
`size` を 2 以上にすると、その倍数だけ連続して占める。

`@broadcast` が使う量は、その slot の `:addr` から、その kernel で消費した量である。同じ slot の
`@broadcast` は stream として順に消費する（「@broadcast」の「stream として消費する」の節）。
実際に PDM から L1BM へ移動するのは、**転送命令 `mvb` の最小単位**（64 u64、dev manual
3.5.8.18）ごとなので、消費した範囲の前後も読む。しかしそこの値は使わないので、使う量には
含めない。

`@reduce` は結果として意味があるのは先頭 4 u64 だが、**転送命令 `mvr` が 64 の倍数しか
運べないため、必ず 64 u64 書く**（dev manual 3.5.8.19）。減らす手段は無い。

### 開始アドレスの境界

**`:addr` に課す境界の条件は、その directive に対して vsmlink が出す PDM 側の転送命令で
決まる。** `:src-slot` のスロットでは、決めた番地（元のスロットの `:addr` + `:addr-offset`）に
この条件を課す。 dev manual 3.5.3 に「単位動作が 64 語にアクセスするなら、そのオペランドの番地は
64 語の倍数でなければならない」とある。

| directive | PDM 側の命令 | 単位動作 | `:addr` の条件 | 違反時 |
|---|---|---|---|---|
| `@distribute` | `mvp`（PDM から L2BM、3.5.8.4）| 64 u64 | 64 の倍数 | E244 |
| `@collect` | `mvp`（L2BM から PDM、3.5.8.5）| 64 u64 | 64 の倍数 | E245 |
| `@distribute_aos2soa` | `mvp`（PDM から L2BM、3.5.8.4）| 64 u64 | 64 の倍数 | E253 |
| `@collect_soa2aos` | `mvp`（L2BM から PDM、3.5.8.5）| 64 u64 | 64 の倍数 | E249 |
| `@reduce` | `mvr`（3.5.8.19）| 64 u64 | 64 の倍数 | E246 |
| `@broadcast` | `mvb`（3.5.8.18）| 64 u64 | `:addr` 自体には無い。1 回の消費（size u64）が 64 u64 の境目をまたがないこと | E228 |

`@broadcast` だけ条件の形が違う。MN-Core2 の仕様から、次のように決まる。

- `mvb` は単位動作が 64 u64 なので、PDM 側の番地は 64 の倍数でなければならない（dev manual
  3.5.3 / 3.5.8.18）。vsmlink は `:addr` を含む 64 u64 の先頭（64 の倍数）に切り下げて
  `mvb` を出すので、`:addr` 自体は 64 の倍数でなくてよい
- `l2bmb` の L2BM / L1BM 側の番地は 16 の倍数でなければならない（3.6.7.2）。これは vsmlink が
  選ぶ番地であり、`:addr` には関係しない
- `l1bmp` は L1BM 側の境界合わせの制約が無い（3.6.8.6）。L1BM に移動した 64 u64 のどの位置
  からでも読める

したがって `:addr` 自体にハードウェア上の制約は無い。ただし `l1bmp` は 1 命令で L1BM 上の
連続した位置を読むので、**1 回の消費（size u64）が 64 u64 の境目をまたぐことは禁止する**
（E228）。またぐには、次の 64 u64 が L1BM 上で続いて並んでいる必要があるためである。
同じ slot で size を変えなければ、`:addr` を size の倍数にしておけばまたがない。

vsmlink が出す命令が変われば、この条件も変わる。

## slot は引数の位置を表す

`from_param <slot>` / `to_param <slot>` の `<slot>` は `.param` の `:slot` ID であり、
**PDM の番地ではない。** 番地は `.param` がその slot の `:addr` として別に持つ。

**slot が表すのは kernel の引数の位置である。** `.param` と `_vsm` を書く側が引数の並び順から
採番する（例: MNCL は `8 * (ArgNo + 1)` で機械的に決めている）。

```lisp
;; kernel func(double *pos, double *consts, double *vel) に対応する .param
(:slot 8  :place :pdm0 :addr 0     :send_wait_tag #x10)   ; 第 1 引数 pos
(:slot 16 :place :pdm0 :addr 16384 :send_wait_tag #x11)   ; 第 2 引数 consts
(:slot 24 :place :pdm0 :addr 40960)                       ; 第 3 引数 vel
```

### 配列の引数にも slot は 1 つ

配列は 1 個の引数なので、slot も 1 つである。**配列を順に配るために、要素ごとに slot を
増やしてはいけない。** `@broadcast` で配列を順に配るときは、同じ slot に対して `@broadcast`
を繰り返し書く（「@broadcast」の「stream として消費する」の節）。

上位層が、配列の途中の位置を別の始まりとして使う場合（その位置に何を置いたかを作者が
知っていて、全 PE にそこを読ませたい場合）は、その位置に新しい slot を割り当て、`.param` に
`:src-slot`（元の引数の slot）と `:addr-offset`（元の `:addr` からのずれ、u64 単位）を書く
（`:addr` は書かない。`param-spec.md` の「`:src-slot` と `:addr-offset`」）。これは要素ごとの
slot ではなく、別の始まりを持つ別の slot である。

**slot 番号に番地のずれを足してはいけない。** slot 番号は離散的な採番であり、番地の
空間ではない。足すと別の引数の正当な slot 番号と偶然一致しうる。

### slot とデータの同一性

- **slot が同じなら、データの同一性は保証される。**
- **slot が違えば、それぞれ処理の別のやり取りの中にあり、データの同一性は保証されない。**
  PDM 上で番地が重なっていても、同じデータとはみなさない。
- **`@broadcast` の slot は専用である。** `@broadcast` に使う slot を、同じ `.param` を使うほかの
  directive（`@distribute` / `@collect` / `@reduce` など）で使ったら**エラーとする。** マルチカーネル
  `_vsm` では、`.param` を共有するすべての kernel を通して調べる。link のときに slot の番号だけで
  判定する。前の kernel が書いたものを `@broadcast` で読む場合は、同じ `:addr` を持つ別の slot を
  用意する
- **ホストからの書き込みは検出しない。** `mnc2_send` がいつどこへ送るかは vsmlink から見えない。
  消費の途中に書き換わらないようにするのはホストの責任である
- **未実装**: slot の専用の検出（エラー）は、0.10.2 の vsmlink にはまだ無い。エラー ID（E2xx）は、
  実装するときに帯の末尾から割り当て、`param-spec.md` の一覧に載せる

## 構文

```asm
@distribute             <buffer_id> from_param <slot> size <size> <pe_register>
@broadcast              <buffer_id> from_param <slot> size <size> <pe_register>
@collect                <buffer_id> to_param <slot> size <size> <pe_register>
@alias                  <bf_ID> <register>
@identify               <dim_id> <dest_register> <size>
@access_pattern         <type> <offsets...>
@boundary_flags         <bf_ID> <dim> <register> <flag_names...>
@boundary_flags_compute <bf_ID> <dim> <register> <alloc_ID>
@alloc                  <id> [:grf0 <num...>] [:grf1 <num...>] [:m <num...>] [:n <num...>] [:omr <num...>]
@free                   <id>
@get_neighbor           <neighbor_register> <offset> <src_register> <bf_ID> <buf_id> <alloc_ID>
@reduce                 <id> to_param <N> <pe_register> <op>
@kernel_begin           <kernel 名>
@kernel_end             <kernel 名>
@persistent_info        <任意のトークン列>
@assign                 <bf_ID> <register>                               ; deprecated、@alias を使う
```

`@distribute` / `@broadcast` / `@collect` の `<buffer_id>` は、名前が付いている程度の意味しか
持たない（生成する `.vsm` のコメントに出る）。以前は `@get_neighbor` が
配列を指すために使っていたが、0.9.0 で `@get_neighbor` が読み出し元をレジスタで受け取るように
なって意味を失った（「@get_neighbor」の節）。`@broadcast` は同じ構文をそのまま引き継いだ。

## @alloc

レジスタプールを確保する。 `@free` で解放する。

### @free は直前の @alloc と ID が一致していなければならない

`@alloc` は入れ子にできない（E201）。 したがって解放できるのは直前に確保したプールだけ
であり、 `@free` の ID が直前の `@alloc` の ID と違えばエラーになる（E210）。

### レジスタ番号の制約

**`@alloc` で確保する PE メモリのレジスタ番号は偶数でなければならない。**
imm の halfword ペア（`_hi` / `_lo` 方式）と整合させるためである。

**未検証**: 本制約は過去の開発記録に基づく記述であり、 dev manual / assemble3 での裏取りは未実施。

## @distribute / @collect

PDM と PE の間のデータ転送を MV 命令列に展開する。

```asm
@distribute _arg_a from_param 8 size 1 $lm0
@collect    _arg_c to_param 16 size 1  $ln4088
```

- `from_param N` / `to_param N` の N は `.param` の `:slot` ID
- `size` は **PE 1 個が受け取る（渡す）u64 の個数**。指定できるのは 1 から 4 で、範囲外はエラー
  （`@collect` は E241、`@distribute` は E242）
- スロットの `:addr` は 64 の倍数でなければならない（`@distribute` は E244、`@collect` は
  E245）。理由は「開始アドレスの境界」の節を参照
- `<pe_register>` は先頭の成分を置くレジスタ（例: `$lm0`）。`v` は付けない。
  成分は 2 刻みで着地する（`$lm0` 起点で `size` が 3 なら `$lm0` / `$lm2` / `$lm4`）

`size` の上限が 4 なのは、最終段の `l1bmd` が 4 サイクル動作で、1 命令では 4 成分しか
運べないためである（dev manual 3.6.8.20 分配 / 3.6.8.21 結合）。`@broadcast` の上限と同じ根拠。

### PDM 上の並び

**`@distribute` と `@collect` は同じ並びを使う。** そうでなければ、配ったものを回収して
元に戻らず、対で使えない。

```
size 4:  [成分 0 を 4096 PE 分][成分 1][成分 2][成分 3]
size 1:  [成分 0 を 4096 PE 分]
```

`size` が N なら先頭 N ブロックが使われる。**`size` を変えても、それ以前の成分の番地は
動かない。** `size 1` の並びは `size 4` の前置部分である。

この並びは選べない。`l1bmd` が分配・結合とも `addr_b + cycle * 64` でアクセスするため、
成分（サイクル）が外側、PE が内側になることがハードウェアで決まっている。

**PE ごとに成分を隣接させる並び（OpenCL の `doubleN` バッファの並び）は、この転送経路では
作れない。** `mvp` は連続コピーで並べ替えをせず（dev manual 3.5.8.5）、`l2bm@` が動かせる
粒度は 64 長語なので、PE 単位のインターリーブが原理的に不可能である。並べ替えが必要なら
上位層で行うこと。転置を含む `@distribute_aos2soa` / `@collect_soa2aos`（次節）を使えば、PE ごとに
成分を隣接させた並び（番号の順の並び）を device 上で読み書きできる。

往復が成立することは `examples/27-roundtrip-size4` で検証している（計算せずに
`@distribute` して `@collect` し、全要素の一致を見る）。

### @distribute はサイクルマスクで size 個だけ書く

最終段の `l1bmd` は 4 サイクル動作する命令である
（dev manual 3.6.8.20。`src_addr = addr_b + cycle * 64` で cycle 0 が `addr_b` を
読み、`refer_pemem(dst, cycle)` で cycle 0 が dst の要素 0 に書く）。

vsmlink は展開時に単一行マスク（dev manual 3.6.2.1）を付けて、先頭 `size` サイクルだけ
書き込む。マスクは `size` で決まる。

| `size` | マスク | 書かれるレジスタ（`$lm0` 起点） |
|---|---|---|
| 1 | `/1000` | `$lm0` |
| 2 | `/1100` | `$lm0` `$lm2` |
| 3 | `/1110` | `$lm0` `$lm2` `$lm4` |
| 4 | なし | `$lm0` `$lm2` `$lm4` `$lm6` |

```asm
l1bmd $lb0 $lm0v/1000
```

`size` が 1 なら `$lm0` にだけ payload が入り、第 2 から 4 サイクルが触る `$lm2` / `$lm4` /
`$lm6` への書き込みは抑制される。`$lm0v` の `v` は「4 連続で読み出す」という `l1bmd` の
都合であって、書き込む個数はマスクで決める。

### @collect は運ぶ成分数で絞る

**結合方向はサイクルマスクが効かない。** マスクの適用先は PE メモリだけで、L1BM は
対象外である（dev manual 3.6.2.1）。したがって `l1bmd` は `size` によらず 4 成分を
L1BM に書く。

vsmlink は L1BM から先へ運ぶ成分数を `size` 個に絞ることで対応する。運ばれなかった成分は
L1BM に置かれたまま残るが、領域は確保済みなので他を壊さない。

### @distribute の動作

PDM から L2BM、L1BM を経て PE ローカルメモリへ展開する。

1. スロットに `:send_wait_tag` があれば、そのタグで `wait` を発行（host からの転送完了待ち）。無ければ出さない（`param-spec.md` の「送信完了待ち」）
2. 成分ごとに次を繰り返す（`size` 回）
   1. mvp 命令で PDM から L2BM へ（8 本、全 L2B 分）
   2. l2bmb で L2BM から L1BM へ（8 本）
3. `l1bmd $lb.. $lm0v<マスク>` で L1BM から PE ローカルメモリへ（先頭 `size` サイクルを書く）

`.param` のスロットエントリの `:send_wait_tag` は任意。無ければ、データはすでに PDM にあるものとして待たない。

### @collect の動作

PE ローカルメモリ → L1BM → L2BM → PDM への展開。

1. l1bmd で PE → L1BM（`size` によらず 4 成分が載る）
2. 成分ごとに次を繰り返す（`size` 回）
   1. l2bm で L1BM から L2BM へ（8 本、全 L2B 分）
   2. mvp 命令で L2BM から PDM へ（グループごとに wait タグ付き）

`.param` のスロットエントリの `:recv_wait_tag` は任意。`:recv_wait_tag` は**最後の成分の最後のグループ**の mvp に付与されるタグで、host が `mnc2_recv(recv_wait_tag=...)` で転送完了を検出する。途中の成分と途中のグループは予約 wait タグで内部同期を取る。省略すると、host はその転送の完了を待たず、最後の mvp も予約 wait タグで内部同期を取る（`param-spec.md` の「`:recv_wait_tag` を省略した場合」）。

### .param の例

```lisp
((:version :0.1)
 (:slot 8  :place :pdm0 :addr 0     :send_wait_tag #x10)            ; @distribute 用
 (:slot 16 :place :pdm0 :addr 4096  :send_wait_tag #x11 :recv_wait_tag #x1e) ; @collect 用
 (:boundary_flags bf1 :place :pdm0 :addr 8192 :send_wait_tag #x12))
```

- `:send_wait_tag`: send 方向（Host → PDM）の send wait タグ。`@distribute` / `@broadcast` / `@boundary_flags` / `@identify` で使用。待つ規則は `param-spec.md` の「送信完了待ち」
- `:recv_wait_tag`: recv 方向（PE → PDM）の recv wait タグ。`@collect` で使用。host は `mnc2_recv(recv_wait_tag=...)` で待つ

## @distribute_aos2soa / @collect_soa2aos

PDM の上のデータを「1 個分（4 個の値）ずつ、番号の順」に置いたまま、PE との間で受け渡す
directive の対である。`@distribute` / `@collect` と同じ転送に、MAB 内の転置（`dmwrite` / `dmread`）
を足したもので、転送の経路と番地は `@distribute size 4` / `@collect size 4` と同じである。

```asm
@distribute_aos2soa _p   from_param 8  $lm0 $lm8    ; PDM の番号の順の並び → 各 PE の $lm0,2,4,6
@collect_soa2aos    _out to_param   16 $lm0 $lm8    ; 各 PE の $lm0,2,4,6 → PDM の番号の順の並び
```

| 引数 | 意味 |
|------|------|
| `<id>` | 識別子（`@distribute` / `@collect` と同じ）|
| `from_param N` / `to_param N` | `.param` の `:slot` ID。`:place` `:addr` と、配る側は `:send_wait_tag`、書く側は `:recv_wait_tag` を使う |
| `<dst_register>` / `<src_register>` | データの 4 個の値が 2 刻みで並ぶ連続 4 本の先頭（例: `$lm0` なら `$lm0` `$lm2` `$lm4` `$lm6`）。`v` は付けない |
| `<work_register>` | 作業の場所。転置の途中の値を置く連続 4 本の先頭。中身は壊れる |

- レジスタは LM0 / LM1 / GRF0 / GRF1（`$lm` `$ln` `$lr` `$ls`）のいずれかで、偶数番号。
  データのレジスタと作業の場所の 4 本は重なってはならない（`@collect_soa2aos` は E247 / E250、
  `@distribute_aos2soa` は E251 / E254）
- 値の数は常に 4 である。`size` は書かない。転置が MAB 内の 4 PE x 4 アドレスを動かすため、
  4 未満では番号の順の並びにならない
- 行列レジスタ x 面の行 0 から 3 を使う。中身は壊れる
- **PDM の並び。** slot の `:addr` から 16384 u64 を使う。データ 1 個が連続 4 u64（値 0..3 の順）で、
  番号の順に並ぶ。`:addr` は 64 の倍数でなければならない（`mvp` の単位動作。E253 / E249）
- 待ちのタグと `:send_wait_tag` の送信完了待ちは `@distribute` / `@collect` と同じ

### 転置と、逆向きの directive を設けない理由

- **転置**（`dmwrite` / `dmread`）は、MAB の中で PE の番号とレジスタの番号（4 本のうちの何本目か）を入れ替える操作
  である。2 回行うと元に戻る。つまり、転置の逆の操作は転置そのものである
- そのため、逆向きの directive（`@collect_aos2soa`、`@distribute_soa2aos`）は設けない。作っても、命令列はそれぞれ
  `@collect_soa2aos`、`@distribute_aos2soa` とまったく同じになり、違いはレジスタの値の呼び方だけになる。同じ展開に
  2 つの名前を付けると、別の動作をすると読めてしまう
- 名前の `soa2aos` / `aos2soa` は、転置という操作を表すのではない。「PDM の側は常にデータ 1 個分ずつの並び」という
  2 つの directive の約束のもとで、データの形がどう変わるか（書くときは PE の側の成分ごとから PDM の側のデータ
  1 個分ずつへ、配るときはその逆）を表す
- `@collect_soa2aos` を、転置の無い `@distribute` と組み合わせて単独で使うと、PDM の上ではデータ 1 個分ずつには
  まとまるが、番号の順にはならない。並びは下の「データと PE の対応が変わる」の「書く側だけを使った場合」のとおり

### 対で使うと元に戻る

`@distribute_aos2soa` で配った値を、PE を変えずに（計算の有無にかかわらず）`@collect_soa2aos`
で書くと、PDM の並びは配る前と同じになる。**先頭 N 個に置いたデータは先頭 N 個に戻る。**
使うデータが 4096 個より少なくても、`@broadcast` などの読む側は先頭から連続で読めばよく、
ホストが並びの式を使う必要は無い。vsmlink はデータの個数を扱わない。

対で使う例は `examples/34-soa2aos-roundtrip` にある。

### データと PE の対応が変わる

配る側を通すと、**番号 k のデータは PE `4 * (k % 1024) + k / 1024` に入る**（PE k ではない）。
`@distribute` が PE k に配るのと違う。したがって、隣の PE の値を使う directive（`@get_neighbor`
など）とは組み合わせられない。PE の並びに意味がある計算には、転置の無い `@distribute` /
`@collect` を使う。

**転置の無い `@distribute` / `@collect` で読み書きするデータは PE の順になる。** 同じ kernel で
両方を使うと、片方は番号の順、片方は PE の順で PDM に並ぶ。

書く側だけを使った場合（参考）: PE j の 4 個の値は `:addr + 4 * (1024 * (j % 4) + j / 4)` から
連続 4 u64 に置かれる。ホストがこの領域を直接読む形は想定していない。言い換えると、位置 k（4 u64 を 1 個と
数える）に来るのは PE `4 * (k % 1024) + k / 1024` の値である。

- **位置 k < 1024 では、PE `4k` の値が来る。** 位置 1 に PE 4 の値が来るので、先頭だけを見ても並べ替えの有無は
  区別できる。ただし、先頭 1024 個だけでは単純な 4 刻みに見え、式の `k / 1024` の項は表れない。並べ替えの規則を
  確かめるときは k >= 1024 を含める
- **使う PE が 4096 より少ないと、先頭に収まらない。** PE 0 から N − 1 の値は、位置 0、1024、2048、3072 から始まる
  4 つの区間に分かれて置かれる。対で使えばこの問題は起きない
- `@distribute size 4` で配った値に、MAB 内の転置（`dmwrite` / `dmread`）を手で書いてから `@collect size 4` で書いても、
  同じ並びになる（`examples/30-soa2aos`、`examples/33-broadcast-soa2aos`）

### 展開内容

- `@distribute_aos2soa`: `@distribute size 4` と同じ転送で作業の場所へ配り、`nop/2`（PE メモリの
  書き込みから読み出しまでの待ち。dev manual 3.6.3.9）、`dmwrite <work>v $lx0`、待ち、
  `dmread $lx0 <dst>v` の順。展開の末尾は `dmread` なので、**直後に `<dst_register>` を読む命令を
  置くときの待ちは、`@distribute` の直後と同じく assemble3 のハザード検査に従って置く**
- `@collect_soa2aos`: `dmwrite <src>v $lx0`、待ち、`dmread $lx0 <work>v` の後に `@collect size 4` と
  同じ転送。`dmread` の後の待ちは、転送の先頭の `nop/24` が兼ねる
- `dmwrite` から `dmread` までの待ちは、dev manual に規定が無く、assemble3 も検査しない。実機で測り、
  LM0 / LM1 / GRF0 / GRF1 の全組み合わせで待ち 0 で正しい結果になることを確かめたので、`dmwrite` の次の
  ステップに `dmread` を置く。この値は dev manual の規定ではなく実機で確かめたものである

## @alias

既存のレジスタに名前を付ける。コードを生成しない。
`@boundary_flags` のレジスタを `@get_neighbor` の `bf_id` として参照するために使用する。

```asm
@alias bf1 $lr416
```

- `bf_id` と `$lr` レジスタの対応を登録する
- `@boundary_flags` が別カーネルで実行済みの場合に、そのレジスタを参照するために使う

## @broadcast

slot の PDM にあるデータを stream として先頭から順に消費し、1 回の `@broadcast` で size 個の u64 を **全 4096 PE に同じ size 個の値**として配布する（「stream として消費する」の節）。 各 PE は `$lr16` のような GRF0 レジスタ起点に size 個を受け取る。 `@distribute` (各 PE が異なる値) とは異なる HW パスを使う。

```asm
@broadcast _arg_a from_param 8 size 4 $lr16
```

- 構文は `@distribute` と同じ (`<buffer_id> from_param <slot> size <size> <pe_register>`)
- `<slot>` は `.param` の `:slot` ID（引数の位置）。**PDM の番地ではない**
- `<size>` は 1, 2, 4 のいずれか（1 回で配る u64 の個数）。 **size 3 は無い。** 範囲外は error (E227)
  - たとえば OpenCL の `double` なら 1、 `double2` なら 2、 `double4` なら 4 にあたる。 `double3` は配列の中で 1 要素あたり 4 u64 の間隔で並ぶので 4 を使う。 型と size の対応は `_vsm` を書く側（例: MNCL）が決める
  - size 8 / 16 は今後の課題である
- スロットの `:addr` 自体に条件は無い。 1 回の消費（size u64）が 64 u64 の境目をまたぐと error (E228)。 理由は「開始アドレスの境界」の節を参照
- 着地レジスタは +2 偶数刻み。 源の u64[i] が cycle i として `<pe_register>` + 2*i に着地する (size 4・`$lr16` 起点なら u64[0]/[1]/[2]/[3] が `$lr16`/`$lr18`/`$lr20`/`$lr22`)。 GRF0 は偶数アライン必須なので `<pe_register>` は偶数レジスタを指定する
- HW パス: `mvb` (PDM から全 8 L2BM へ放送)、 `l2bmb` (L2BM から全 64 L1BM へ放送)、 `l1bmp` (L1BM から全 64 PE へ 1 u64/cycle 放送。 vector dst + サイクルマスクで size 個ぶん)
- emu:lib で着地レジスタと全 PE 同値を実測済 (`examples/26-broadcast-accumulate-2d` の landing 検証)

### 入力規約 (重要)

`l1bmp` は 1 u64/cycle を MAB 内の全 64 PE に放送する。 `mvb` / `l2bmb` が PDM のデータを全 L2BM / L1BM へ配るため、 全 4096 PE が同じ値を受け取る。 書き込みサイクルマスクにより先頭 size cycle だけがレジスタに書かれる。

ホストは、**配りたい値を配る順に、slot の `:addr` から詰めて置く**。 1 回の `@broadcast` が size u64 を消費し、 次の `@broadcast` はその続きを消費する（「stream として消費する」の節）。 L1BM への移動は 64 u64 単位なので、 最後の 64 u64 の中で消費しなかった部分も読まれる（値は使われない）。

### L1BM への移動と、PE への配布

- **L1BM への移動の単位は 64 u64 である。** `mvb/n64` で PDM から全 8 個の L2BM へ、
  `l2bmb` で L2BM から全 L1BM へ運ぶ（dev manual 3.5.8.18 / 3.6.7.2）。
- **PE への配布は `l1bmp` 1 命令で行う。** `l1bmp` は 4 サイクル動作で、サイクル i に
  L1BM から 1 u64 を読み、`<pe_register>` の `+2*i` のレジスタに書く（dev manual 3.6.8.6）。
- vsmlink は `l1bmp` に書き込みサイクルマスク（dev manual 3.6.2.1）を付け、先頭 size
  サイクルだけを書く。**`<pe_register>` の `+2*size` 以降のレジスタは書き換わらない。**
  これがマスクを付ける目的である。

| size | マスク | 書くレジスタ（`$lr16` 起点）|
|---|---|---|
| 1 | `/1000` | `$lr16` |
| 2 | `/1100` | `$lr16`、`$lr18` |
| 4 | なし | `$lr16`、`$lr18`、`$lr20`、`$lr22` |

**L1BM / L2BM を使う期間**

- **L1BM:** `@broadcast` が PDM から移動した 64 u64 を置く。移動したときから、その 64 u64 を
  消費し切ったとき（消費し切らずに `_vsm` が終わればその終わり）まで使う。同じ slot の次の
  `@broadcast` が次の 64 u64 を移動するときは、改めて確保する
- **L2BM:** PDM から L1BM へ移動する間だけ使う

### stream として消費する

**`@broadcast` は slot ごとの stream である。** 同じ slot に対する `@broadcast` は、次の順に
動く。

1. その slot の 1 回目の `@broadcast` で、slot の `:addr` から 64 u64 を L1BM に移動し、
   その先頭から size u64 を消費する
2. 同じ slot の 2 回目以降の `@broadcast` では、L1BM への移動をしない。L1BM に移動した
   64 u64 の残りから、続けて size u64 を消費する
3. 64 u64 を使い切ったら、次の 64 u64 を L1BM に移動し、続けて消費する

同じ slot を続けて書くと、配列を先頭から順に配ることになる。

```asm
@broadcast _pj from_param 8 size 4 $lr16   ; :addr から 4 u64
@broadcast _pj from_param 8 size 4 $lr16   ; その続きの 4 u64
@broadcast _pj from_param 8 size 4 $lr16   ; さらに続きの 4 u64
```

size 4 なら 16 回で 64 u64 を使い切り、17 回目で次の 64 u64 を移動する。

- **判断に使うのは、その slot 自身の状態だけである。** 違う slot が L1BM に移動した 64 u64 は
  使わない（「slot とデータの同一性」の節）
- **送信完了待ち:** slot に `:send_wait_tag` があれば、その slot の最初の 64 u64 を移動する
  前に 1 回だけ、ホストの送信の完了を待つ（kernel の実行ごとに 1 回）。2 つ目以降の 64 u64 の
  移動では待たない。送信が終われば slot のデータは PDM にそろっているためである。タグが
  無ければ待たない（`param-spec.md` の「送信完了待ち」）
- **kernel の実行ごとに、slot の `:addr` から読み始める。** 読む位置は link 時に決まり、
  固定の命令列として `.vsm` に書き出されるので、ホストから kernel を再実行しても、前回の
  実行の続きを持ち越す仕組みは無い。別の kernel も別に link されるので同じである
- これらの判定はすべて **link 時（vsmlink が `.vsm` を生成する処理の中）に決まる。** device の
  実行時に判定が行われることはない

### 読み出し位置を指定する引数は持たせない

`@broadcast` に、読み出し位置（offset など）を指定する引数は持たせない。**任意の位置から
読めるように見えてしまうため**である。読む位置は、その slot の stream をどこまで消費したか
だけで決まる。

## @identify

PE に問題空間座標（PE ID）を配布する。PDM → L2BM → L1BM → PE ローカルレジスタ（`$lr`）に直接書き込む。
`@distribute` との違いは L-memory 経由ではなく `$lr` に直接書き込む点。

```asm
@identify 0 $lr8 4096
```

- 第1引数: 次元インデックス（整数。`.param` の `:identify N` と対応。0=x, 1=y, 2=z）
- 第2引数: 書き込み先 PE レジスタ（`$lr` 系）
- 第3引数: 要素数（ビット幅決定等の将来利用のために必須）

`@distribute` と同じ `distribute.vsm` を展開に使うので、最終段は `l1bmd $lb.. $lr8v/1000` に
なり、サイクルマスクで `$lr8` の 1 個だけを書く（`$lr10` / `$lr12` / `$lr14` は書かない）。

## @access_pattern

`_vsm` を書く側（例: MNCL）がソースコード解析で得たアクセスパターンの宣言。コメントとして出力される。

```asm
@access_pattern stencil1d (-1) (1)
→ #@ @access_pattern stencil1d (-1) (1)
```

接頭辞は `#@ ` である。 コメント化して出力するディレクティブ（`@alias` / `@assign` /
`@alloc` など）はすべてこの接頭辞を使い、 元が `_vsm` のディレクティブ行であったことを
示す。

## @boundary_flags

PE の位置を示す境界フラグを 1 レジスタにパックして生成する。omr には触らない。

```asm
@boundary_flags bf1 <dim> $lr416 data_edge_left data_edge_right cross_chip_left cross_chip_right cross_L2B_left cross_L2B_right cross_L1B_left cross_L1B_right cross_MAB_left cross_MAB_right
```

- `<dim>`: 次元番号（0, 1, 2）。必須
- 2D/3D では次元ごとに別の bf_id で発行する

```asm
# 2D 例
@boundary_flags bf_dim0 0 $lr416 data_edge_left ... cross_MAB_right
@boundary_flags bf_dim1 1 $lr418 data_edge_left ... cross_MAB_right
```

### ビットレイアウト (正式仕様)

innermost (PE) が低ビット、outermost が高ビット。
各 PE のフラグは、その PE が該当する全レベルのビットが立つ（注参照）。

| bit | フラグ名 | 意味 |
|-----|---------|------|
| 9 | data_edge_left | 問題空間の端（左）。data_offset == 0 |
| 8 | data_edge_right | 問題空間の端（右）。data_offset + pe_shape == data_size |
| 7 | cross_chip_left | cross chip（左） |
| 6 | cross_chip_right | cross chip（右） |
| 5 | cross_L2B_left | cross L2B（左）。旧 cross group を含む |
| 4 | cross_L2B_right | cross L2B（右）。旧 cross group を含む |
| 3 | cross_L1B_left | cross L1B（左）。mabid == 0 |
| 2 | cross_L1B_right | cross L1B（右）。mabid == 15 |
| 1 | cross_MAB_left | cross MAB（左）。subpeid == 0 |
| 0 | cross_MAB_right | cross MAB（右）。subpeid == 3 |

**注: ビットエンコーディング（ワンホット）**

フラグはワンホット。各 PE には該当するレベルのビットが **1 つだけ** 立つ。
複数のビットが同時に立つことはない。
例: cross_L1B_left の PE には bit3 だけが立ち、cross_MAB_left (bit1) は立たない。
within MAB（どの境界にも接しない PE）はすべてのビットが 0 である。
HW 経路が排他的（MAB 内シフト / L1BM 経由 / L2BM 経由 / PDM 経由）であることに対応する。

**注: cross group 廃止の経緯**

以前は cross_group（bit 7/6）と cross_L2B（bit 5/4）を `$l2bid` の偶奇で排他的に区別していた。
しかし `:place :pdm0` 固定方針により、cross group も cross L2B と同じ mvp PDM 経路で処理される。
区別する意味がなくなったため cross group ビットを廃止し、旧 cross group の PE は cross L2B に統合した。

これにより:
- boundary_flags は 12 ビットから 10 ビットに簡素化
- `@boundary_flags_compute` の omr 消費が 5 個から 4 個に削減
- cross_L2B の判定条件が「omr3 + l2bid 偶奇チェック」から「omr3 のみ」に簡素化

### バイナリの選択基準

ホスト側から送信する boundary flags バイナリは、`.stparam` の `:data_size` / `:data_offset` / `:pe_shape` から決定される `data_edge` の有無で選択する。

判定式:
```
data_edge_left  = (data_offset[0] == 0)
data_edge_right = (data_offset[0] + pe_shape[0] == data_size[0])
```

| data_edge_left | data_edge_right | 意味 | バイナリ例（4096 PE） |
|---|---|---|---|
| 0 | 0 | 中間チャンク | `57b42dea.bin` (edge00) |
| 0 | 1 | 右端チャンク | edge01（`gen_golden --edge-right` で生成） |
| 1 | 0 | 左端チャンク | edge10（`gen_golden --edge-left` で生成） |
| 1 | 1 | 全体が1チャンク | `75c77b0f.bin` (edge11) |

- 4096 PE 以外の場合は `examples/data/gen_golden.c` でバイナリを生成できる
- `@boundary_flags_compute`（デバッグ用）を使えばホスト側のバイナリ送信なしに PE 上で直接計算もできる

### 動作

- ホスト側で事前計算したフラグデータを PDM → L2BM → L1BM → PE に配布する
- 結果は指定レジスタの LOW 32bit にパックされる
- `.param` に `:boundary_flags <bf_id> :place :pdm0 :addr N` の記述が必要
- `data_edge` は `.stparam` の `:data_size` / `:data_offset` / `:pe_shape` から判定

### 命名規則

- `cross_<レベル>_<方向>`: データ転送が必要な境界（隣接ユニットへの通信）
- `data_edge_<方向>`: 問題空間の端。`:boundary` に関係なく常に立つ。cross_chip と排他的。テンプレートが扱いを決定（clamp: self、periodic: ラップアラウンド）

## @boundary_flags_compute（デバッグ用・非公開）

HW レジスタ ($subpeid, $mabid, $l1bid, $l2bid) と `.stparam` の情報から境界フラグを直接計算する。
`.param` の `:boundary_flags` 宣言は不要。`@alloc` ブロック内で使用する。

```asm
@alloc bfcomp :omr 1 2 3 4 :grf0 200 32 34 40 42 :n 0
@boundary_flags_compute bf1 0 $lr416 bfcomp
@free bfcomp
```

4 引数構文: `<bf_id> <dim> <register> <alloc_id>`

| 引数 | 意味 |
|------|------|
| `<bf_id>` | boundary flags の ID |
| `<dim>` | 次元番号（0, 1, 2） |
| `<register>` | 結果レジスタ（`$lr` 偶数） |
| `<alloc_id>` | リソースプール（@alloc で確保した ID） |

- `data_edge` は `.stparam` の `:data_size` / `:data_offset` から判定。未指定の場合は常に 0
- テンプレート `boundary_flags_compute.vsm` から展開
- @alloc で grf0 5 個、omr 4 個、n 1 個が必要

**注意:**
- デバッグ・検証用途。エンドユーザは `@boundary_flags`（distribute 版）を使うこと
- 17-boundary-collect で boundary flags の golden data 生成と検証に使用

## @get_neighbor

隣接 PE の値を取得。`@stencil` の後継。全階層対応（within MAB / cross MAB / cross L1B / cross L2B）。

### put/get モデル

cross L2B の袖交換は PDM0 上の専用バッファを経由して行う。

- **put**: 自 PE のデータをバッファの対応位置に書き込む（PE → L1BM → L2BM → PDM）
- **get**: 隣接 PE のデータをバッファから読み出す（PDM → L2BM → L1BM → PE）

バッファは .param の `:buffer` で宣言した PDM0 上の連続領域であり、
元データの slot（@distribute）とは独立している。

### 構文

```asm
@get_neighbor $lr400 -1 $lm0 bf1 nbuf0 alloc0   ; offset=-1（左隣）
@get_neighbor $lr408 +1 $lm0 bf1 nbuf0 alloc0   ; offset=+1（右隣）
```

6 引数構文: `<dest> <offset> <src_reg> <bf_id> <buf_id> <alloc_id>`

| 引数 | 意味 |
|------|------|
| `<dest>` | 結果レジスタ（書き込み先） |
| `<offset>` | 隣接方向（-1=左隣、+1=右隣） |
| `<src_reg>` | 読み出し元 PE レジスタ（元データが載っているレジスタ）。`{{self}}` に直接使用 |
| `<bf_id>` | boundary flags（@boundary_flags/@alias で登録した ID） |
| `<buf_id>` | 袖交換バッファ（.param の `:buffer` で宣言した ID）。`{{pdm+N}}` の base アドレスに使用 |
| `<alloc_id>` | リソースプール（@alloc で確保した ID） |

読み出し元 `<src_reg>` と書き込み先 `<dest>` はともに PE レジスタで指定する。どのレジスタを使うかは
呼び出し側（コンパイラのレジスタアロケータ）が決める。vsmlink はレジスタと元データの対応を検証しない。

### `<src_reg>` が表すもの

- **`<src_reg>` は、kernel 引数の配列を `@distribute` で配った宛先レジスタである。**
  `@get_neighbor` は、各 PE の `<src_reg>` にある値について、`<offset>` だけ離れた PE の値を
  `<dest>` に取る
- vsmlink は `<src_reg>` にある値の隣を取るだけで、その値がどの配列から来たかは知らない
- **前提: `@distribute` で配った値が、`@get_neighbor` の時点まで同じレジスタに残っていること。
  これを保証するのは `_vsm` を書く側の責任である。** vsmlink はレジスタの中身を追跡せず、
  検査もしない

注記（MNCL との関係）: この仕様は、MNCL の `.cl` の API `neighbor(p, offset)` を考慮している。
`neighbor(p, offset)` は「`distribute(p)` が配ったのと同じ配置で、自 PE から offset 離れた位置の
値」を返すもので、利用者が隣を取れるのは kernel 引数の配列だけである（計算途中の任意の値の
隣は取れない）。MNCL は、どの kernel 引数をどのレジスタに配ったかを覚えていて、そのレジスタを
`<src_reg>` として渡す。

### 展開内容

4 テンプレートとも構成は同じ。`get_neighbor_left_clamp.vsm` を例に取る。

1. **ゼロレジスタの作成** — `lxor`
2. **入力データを GRF0 へ写す** — `lpassa {{self}} $lr{{r0}}`
3. **cross L2B** — put（PE, L1BM, L2BM, PDM0 の袖交換バッファの順）と get（逆順。1 L2B 分ずらして読む）の後、`l1bmd±1` と `msl` / `msr`
4. **cross L1B** — `l1bmd` で L1BM に出し、`l2bm` と `l2bmb` で隣の L1B に渡し、`l1bmd±1` と `msl` / `msr`
5. **cross MAB** — `l1bmd` で L1BM に出し、`l1bmd±1` と `msl` / `msr`
6. **within MAB** — `{{self}}` に `msl` / `msr` を直接適用
7. **omr 導出** — `imm` で 10bit ワンホット定数を 4 本置き、`ixor` で `@boundary_flags` の値との完全一致を判定して omr に入れる
8. **マージ** — within MAB をデフォルトとして `lpassa` で `<dest>` に書き、cross MAB, cross L1B, cross L2B, data_edge の順に `maskr` と `ipassa` で上書きする

data_edge の上書き値はテンプレートによって異なる。clamp は `{{self}}`（自分自身）を書き、periodic は cross L2B の結果（ラップアラウンド済み）を書く。

### マージフェーズの実装方針

マージは **compute-all-then-select 方式** で行う: 各階層の shift（`msl`/`msr`/`l1bmd`/`l2bm`/`mvp` 等）を全 PE で無条件に計算し、選択だけをマスクで行う。理由は 2 つある。

- `maskr` は GRF への書き込みを抑制するだけで、命令の実行自体を抑制しない
- data mover 系の命令は隣接 PE の協調を必要とし、「該当する PE だけが実行する」ことができない

選択は次の形で行う。**単一行マスク（`/$imrN`）は使っていない。**

```asm
  maskr {{omr2}}
ipassa $lr{{r4}} {{neighbor}}
  mask 0
```

`ipassa` を使うのは、`maskr` による書き込み抑制が効くためである。`lpassa` は `maskr` を無視する。

代替案として「data-mover 命令に単一行マスク `/$imrN` を付けて条件実行（predicated-shift）」する方式も考えられるが、以下の理由で採用しない:

- data-mover 系命令（`msl`/`msr`/`l1bmd` 等）に単一行マスクを付けたときの挙動が dev manual で未確認（シフト自体がスキップされるのか、書き戻しだけマスクされるのか、無視されるのかが不明）
- 手書き vsm（例: `examples/11-odd-even-sort/` 配下の `step8` / `c_step13`）も同じ compute-all-then-select 方式を採用しており、動作実績がある

将来 predicated-shift を採用する場合は、先に dev manual での確認または実機検証が必要。

### 呼び出し側のマスク状態

**`@get_neighbor` はグローバルマスク状態を変更する。** マージの各上書きが `maskr <omr>` で始まり `mask 0` で終わるため、`@get_neighbor` を抜けた時点でマスクは 0 になる。呼び出し前に `maskr` で設定したマスクは保存されない。

したがって `@get_neighbor` を `maskr` 区間の内側に置くことはできない。`examples/15-mask-safety-T4/T4._vsm` も `mask 0` の外側に置いている。

### テンプレート一覧

| テンプレート | offset | :boundary | omr 導出 | data_edge の扱い |
|-------------|--------|-----------|---------|----------------|
| `get_neighbor_left_clamp.vsm` | -1 | clamp | ixor | self で上書き |
| `get_neighbor_right_clamp.vsm` | +1 | clamp | ixor | self で上書き |
| `get_neighbor_left_periodic.vsm` | -1 | periodic | ixor | cross_L2B と同じ結果（ラップアラウンド） |
| `get_neighbor_right_periodic.vsm` | +1 | periodic | ixor | cross_L2B と同じ結果（ラップアラウンド） |

テンプレート選択は `.stparam` の `:boundary` で自動決定される。
`:boundary` 未指定または `:clamp` の場合は clamp テンプレート、`:periodic` の場合は periodic テンプレートを使用。

### テンプレート内の placeholder

template ファイル中で使える主な placeholder:

| Placeholder | 展開 |
|------------|------|
| `{{neighbor}}` | dest register |
| `{{self}}` | src_reg（読み出し元レジスタ） |
| `{{bf}}` | boundary flags register |
| `{{pdm+N}}` | `$p<addr+N>@0`（範囲チェック付き） |
| `{{r0}}`, `{{omr0}}`, `{{n0}}` 等 | `@alloc` で確保したレジスタプール由来 |
| `{{wait_tag}}` | `.param` の `:reserved_wait_tags[0]` の値（例: `i3f`） |
| `{{wait_tag <N>}}` | `:reserved_wait_tags[N]` の値、N が範囲外なら E104 |

`{{wait_tag}}` は vsmlink 内部の `mvp/wait` 完了同期に使う。順序性により単一値で機能する設計（複数値は将来の chip 並列化用）。詳細は `param-spec.md` の `:reserved_wait_tag(s)` を参照。

### リソース数

@alloc で grf0 11 個、omr 4 個、n 1 個が必要。全バリアント共通。
同一 @alloc ブロック内で左右 2 回呼んでもプレースホルダは共通なので、追加のレジスタは不要。

## @reduce

全 4096 PE の値を階層的に縮約し、結果を PDM に書き込む。
`@collect` と同じ I/F で `.param` の `:slot` を使用する。

```asm
@reduce _result to_param 24 $lr52 dfadd
```

4 引数構文: `<id> to_param <N> <pe_register> <op>`

| 引数 | 意味 |
|------|------|
| `<id>` | 識別名 |
| `to_param <N>` | `.param` の `:slot` ID |
| `<pe_register>` | 入力レジスタ（`$lr` 系） |
| `<op>` | 縮約オペレータ（rrn_opcode） |

- `@alloc`/`@free` は不要
- `.param` の対応スロットの `:recv_wait_tag` は任意。ホストが結果を `mnc2_recv` で待つときに指定する（省略時はホストは待たない。`param-spec.md` の「`:recv_wait_tag` を省略した場合」）
- スロットの `:addr` は 64 の倍数でなければならない（E246）。「開始アドレスの境界」の節を参照
- ホストは `mnc2_recv(recv_wait_tag=...)` で結果（4 u64）を受け取る
- **`<pe_register>` は `$lr` 系（GRF0）でなければならない。** LM（`$lm` / `$ln`）の値を
  縮約したいときは、`lpassa` で GRF0 へ写してから `@reduce` に渡す。写した直後は
  ハザードが要る（「_vsm を書く側が置く待ち」の節）
- **1 つの kernel に `@reduce` を複数書ける。** それぞれ別の `:slot` を指定すれば、
  独立した縮約として順に展開される。**`:recv_wait_tag` はスロットごとに別の値にする。**
  同じ tag を複数の `@reduce` で共有すると、vsmlink のリンク・assemble3 のアセンブルは
  通るが、実行時（emu:lib で確認）に「DMA waits for double asserted PIU-DT done flag」で
  異常終了する。複数の完了が同じ tag の flag を奪い合い、1 ビットの flag では表現しきれ
  なくなるためと見られる
- **結果 4 u64 の合成方法は `<op>` に従う。** PE ID を 4 で割った余り（sub_pe_id）ごとの
  部分縮約が 4 つ出るだけで、それをどう 1 個にまとめるかは op が決める。加算系
  （`dfadd` / `liadd` 等）なら 4 値の和が全 PE の総和になるが、`dmax` / `dmin` なら
  4 値の最大値・最小値、`lbor` / `lband` なら 4 値の OR・AND になる

### 縮約パイプライン

```
l1bmr<op>  : PE → L1BM    (16 MAB 縮約、v なし)
l2bmr<op>  : L1BM → L2BM  (8 L1B 縮約)
mvr<op>    : L2BM → PDM   (全 8 L2B 縮約)
```

### .param の例

```lisp
((:version :0.1)
 (:slot 8  :place :pdm0 :addr 0     :send_wait_tag #x10)
 (:slot 24 :place :pdm0 :addr 20480 :recv_wait_tag #x06))  ; @reduce 用
```

### 対応オペレータ一覧

**浮動小数点:**

| オペレータ | 型 | 演算 |
|-----------|-----|------|
| `dfadd` | f64 | 加算 |
| `ffadd` | f32 | 加算 |
| `hfadd` | f16 | 加算 |
| `dmax` | f64 | 最大値 |
| `fmax` | f32 | 最大値 |
| `hmax` | f16 | 最大値 |
| `dmin` | f64 | 最小値 |
| `fmin` | f32 | 最小値 |
| `hmin` | f16 | 最小値 |

**整数:**

| オペレータ | 型 | 演算 |
|-----------|-----|------|
| `liadd` | 64bit | 加算 |
| `iiadd` | 32bit | 加算 |
| `siadd` | 16bit | 加算 |
| `lband` | 64bit | ビット AND |
| `iband` | 32bit | ビット AND |
| `sband` | 16bit | ビット AND |
| `land` | 64bit | 論理 AND |
| `iand` | 32bit | 論理 AND |
| `sand` | 16bit | 論理 AND |
| `lbor` | 64bit | ビット OR |
| `ibor` | 32bit | ビット OR |
| `sbor` | 16bit | ビット OR |
| `lor` | 64bit | 論理 OR |
| `ior` | 32bit | 論理 OR |
| `sor` | 16bit | 論理 OR |

### 展開内容

```asm
# PE → L1BM 縮約（16 MAB → 4 部分和）
l1bmr<op> $lr<src> $lb<N>
nop/24

# L1BM → L2BM 縮約（8 L1B → 4 部分和）
l2bmr<op> $lb<N> $lc<M>
nop/24

# L2BM → PDM 縮約（全 8 L2B → 4 部分和、recv_wait_tag 付き）
mvr<op>/n64i<tag> $lc<M> $p<addr>@0
nop/2
nop; wait i<tag>
nop/5
```

3 段の階層的縮約で全 4096 PE の値を PDM に書き込む。
結果は PE position 別の 4 u64 部分縮約。ホスト側で 4 値を op に応じて合成すれば全 PE の
縮約値が得られる（上の「4 u64 の合成方法」参照）。

---

## @kernel_begin / @kernel_end / @persistent_info

`_vsm` を書く側（例: MNCL）は、複数の kernel を 1 つの `_vsm` にまとめて出力できる。 `@kernel_begin <名前>` と
`@kernel_end <名前>` がその区切りである。

```asm
@kernel_begin writer
@distribute _arg_a from_param 8 size 1 $lm0
...
@kernel_end writer
```

vsmlink はこの 3 つをコメント化するだけで、 中身（kernel 名・変数名・サイズ・レジスタ）
を解釈も検証もしない。 kernel 単位のファイルへの分割は、 リンク解決より前の段階で行う。

- 出力先の決まり方（`@kernel_begin` の有無で出力がファイルかディレクトリかに変わる）と、
  分割だけを行う `--split-only` は [`api-reference.md`](api-reference.md) を参照
- 分割時のエラー（kernel 名の重複・不正な文字種・個数の上限超過など E301 から E308）は
  [`param-spec.md`](param-spec.md) を参照

`@persistent_info` は kernel をまたいで保持する変数の情報を伝える。 **構文は定めていない。**
vsmlink は行の中身を読まないので、 任意のトークン列を書ける。

## deprecated

一覧は冒頭の「ディレクティブ一覧」の deprecated 表にある。 廃止したものは E243 で停止し、
代替があればメッセージで案内する。

## _vsm 配置例

```asm
# ABI PFN3_SBY64
@identify 0 $lr0 4096
@access_pattern stencil1d (-1) (1)
@boundary_flags bf1 0 $lr416 data_edge_left data_edge_right cross_chip_left cross_chip_right cross_L2B_left cross_L2B_right cross_L1B_left cross_L1B_right cross_MAB_left cross_MAB_right
@distribute _arg_a from_param 8 size 1 $lm0
lpassa $lm0 $lr8
@alloc alloc0 :omr 5 6 7 8 :grf0 0 32 80 88 402 404 96 86 87 84 85 :n 0
@get_neighbor $lr400 -1 $lm0 bf1 nbuf0 alloc0
@get_neighbor $lr408 +1 $lm0 bf1 nbuf0 alloc0
@free alloc0
; PE 計算
@collect _arg_c to_param 16 size 1 $ln4088
```

## _vsm を書く側が置く待ち

**directive の展開は、それだけで完結する。** 展開の後に何が続いても（利用者の命令でも、別の directive の展開でも）
ハザードが起こらないように、vsmlink が展開の末尾に待ちを置く。`_vsm` を書く側は、directive が書いたレジスタを読む
命令の前に待ちを置かなくてよい。後続の命令によっては、この待ちが余計になることがある。

ただし、現在の実装には、展開の末尾に待ちを置いていない directive がある。それらの直後に、directive が書いたレジスタを
読む命令を置くときは、`_vsm` を書く側が待ちを置く。値は dev manual の 3.6.3.9（PE メモリへの書き込みから読み出しまで）
による。

| 書き込んだ先 | 読み出すまでに空けるステップ | 条件 |
|---|---|---|
| LM0 / LM1（`$lm` / `$ln`）| 2 | 番地によらず常に要る |
| GRF0 / GRF1（`$lr` / `$ls`）| 1 | 同じ番地を読むときだけ。違う番地なら待たずに読める |

- `@distribute` の展開は `l1bmd` で終わる。**assemble3 は `l1bmd` が PE メモリに書いた直後の読み出しを検出しない**
  ので、その直後に配った先を読むときは、上の値を自分で空ける
- `@distribute_aos2soa` の展開は `dmread` で終わる。直後に `<dst_register>` を読むときも上の値を空ける（こちらは
  assemble3 が検出する）
- 上の 2 つ以外の directive が、展開の末尾に待ちを置いているかどうかは、まだ確かめていない

directive の外の、利用者が書く命令どうしの待ちも、同じ値による。たとえば `@reduce` に渡すために LM の値を
`lpassa` で GRF0 へ写した直後も、上の値を空ける。

## if/then/else の実装パターン: interior 方式

MN-Core 2 の SIMD 実行モデルでは、全 PE が同じ命令ストリームを実行する。
袖交換（隣接 PE 値の取得）では通信命令を全 PE が協調実行する必要があるため、
「条件に合う PE だけ実行する」ネスト if/then/else は成立しない。

代わりに **interior 方式** を使う:

1. 全パス（全階層）の結果を事前に計算する
2. boundary flags から **interior omr**（そのレベルより内側の PE で active）を導出する
3. default = 最外層の結果 → maskr で内側の結果を bottom-up に上書き

### 原理

boundary flags (`$lr416`) の各ビットは PE がどの境界に位置するかを示す。
あるレベル以上のビットが **すべて 0** なら、その PE はそのレベルより内側（interior）にある。

```
iand($lr416, mask) == 0  →  interior（そのレベルの境界越え転送は不要）
```

マスク値は対象レベル以上の左ビットの論理和:

| omr | 名前 | マスク | 意味 |
|-----|------|--------|------|
| omr5 | interior_L2B | 0xA0 (bit5\|bit7) | L2B 以上のビットなし → within_MAB / cross_MAB / cross_L1B |
| omr6 | interior_L1B | 0xA8 (bit3\|bit5\|bit7) | L1B 以上のビットなし → within_MAB / cross_MAB |
| omr7 | interior_MAB | 0xAA (bit1\|bit3\|bit5\|bit7) | 全左ビットなし → within_MAB |

### 結果選択（bottom-up overwrite）

default を最外層にし、interior omr で内側の結果を順に上書きする:

```asm
# 事前計算済みの各階層結果:
#   $lr80  = cross_L2B (mvp PDM 経由、旧 cross_group を含む)
#   $lr88  = cross_L1B (L2BM 経由)
#   $lr402 = cross_MAB (L1BM 経由)
#   $lr404 = within_MAB (msl)

# --- omr 導出 ---

# omr5: interior_L2B
lpassa $lr416 $ln0
nop/2
iand $ln0 $lr84 $lr96          # $lr84 = (0, 0xA0)
nop/2
lpassa $lr96 $omr5              # active when interior to L2B

# omr6: interior_L1B
lpassa $lr416 $ln0
nop/2
iand $ln0 $lr86 $lr96          # $lr86 = (0, 0xA8)
nop/2
lpassa $lr96 $omr6              # active when interior to L1B

# omr7: interior_MAB
lpassa $lr416 $ln0
nop/2
iand $ln0 $lr90 $lr96          # $lr90 = (0, 0xAA)
nop/2
lpassa $lr96 $omr7              # active when within_MAB

# --- 結果選択 (bottom-up overwrite) ---

lpassa $lr80 $lr400             # default: cross_L2B（最外層）

nop/2
  maskr 5                       # interior_L2B → cross_L1B で上書き
ipassa $lr88 $lr400
  mask 0

nop/2
  maskr 6                       # interior_L1B → cross_MAB で上書き
ipassa $lr402 $lr400
  mask 0

nop/2
  maskr 7                       # interior_MAB → within_MAB で上書き
ipassa $lr404 $lr400
  mask 0
```

### なぜ bottom-up か

最外層（cross_L2B/group）を default にして内側へ上書きするのは、
maskr が「条件に合う PE だけ書き込む」動作であるため。
interior_L2B の PE は interior_L1B / interior_MAB の PE を含む（包含関係）ので、
外側から内側へ順に上書きすれば、各 PE に最も適切な結果が残る。

```
全 PE:        cross_L2B (default)
  ├ interior_L2B:  cross_L1B で上書き
  │  ├ interior_L1B:  cross_MAB で上書き
  │  │  └ interior_MAB:  within_MAB で上書き
```

### 参照

- `examples/11-odd-even-sort/step4.vsm` — 全階層左隣取得の完全な実装例

---

## 用語

### 境界レベル

MN-Core 2 のメモリ階層は PE → MAB → L1B → L2B → group → chip。
隣接 PE の値を取得する `@get_neighbor` では、PE 間のデータ転送経路が階層によって異なる。
各 PE がどの階層の境界に位置するかを boundary_flags で表現する。

| 用語 | 意味 | 転送経路 |
|------|------|----------|
| **within MAB** | どの境界にも接しない PE。boundary_flags = 0 | MAB 内シフト（msl/msr） |
| **cross MAB** | MAB の境界に接する PE | L1BM 経由 |
| **cross L1B** | L1B の境界に接する PE | L2BM 経由 |
| **cross L2B** | L2B の境界に接する PE（旧 cross group を含む） | PDM 経由 |
| **cross chip** | chip の端に接する PE | 未対応 |
| **data edge** | 問題空間の端に接する PE | `:boundary` に関係なく常に立つ。テンプレートが扱いを決定 |

boundary_flags はワンホット。各 PE には上記のうち **1 つだけ** が該当し、対応する 1 ビットだけが立つ。
left/right の方向ごとに独立したビットを持つ（例: cross_MAB_left, cross_MAB_right）。


> **cross group 廃止**: `@boundary_flags` の節の「注: cross group 廃止の経緯」を参照。

### 袖交換（put/get）

`@get_neighbor` の節の「put/get モデル」を参照。
{% endraw %}
