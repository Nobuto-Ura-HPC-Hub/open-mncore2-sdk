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
| `@boundary_flags` | ホストが PDM に置いた境界フラグを、各 PE のレジスタに配る | 実装済み |
| `@alloc` | レジスタプールの確保 | 実装済み |
| `@free` | レジスタプールの解放 | 実装済み |
| `@get_neighbor` | 隣接 PE の値を取得（全階層: PE/MAB/L1B/L2B） | 実装済み |
| `@get_hypercube_neighbor` | 超立方体の隣（PE の番号 j の相手は j XOR 2^deep）の値を取得（4096 PE） | 実装済み |
| `@get_eoshift` | 位置の差が 2^N の PE の値を、区切り（2^M 個）をまたがずに受け取る（4096 PE） | 実装済み |
| `@reduce` | 全 4096 PE の階層的縮約（L1BM → L2BM → PDM） | 実装済み |
| `@emit_from` | **デバッグ用。** HW PE ID が `<n>` の PE の値を、呼ぶたびに PDM の slot の続きへ書く（`@broadcast` の逆の向き） | 実装済み |
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
| `@boundary_flags_compute` | 境界フラグを PE の上で計算する（デバッグ用） | 廃止。E243 でエラー |

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
| `@emit_from` | PDM へ書く | **その kernel で書いた量**（呼んだ回数 × 64 u64 × `size`） |

`@distribute` と `@collect` は 1 成分あたり 4096 u64（4096 PE かける 1 u64）を使う。
`size` を 2 以上にすると、その倍数だけ連続して占める。

`@broadcast` が使う量は、その slot の `:addr` から、その kernel で消費した量である。同じ slot の
`@broadcast` は、呼ぶたびに続きのデータを読む（「@broadcast」の「同じ slot を繰り返すと続きを読む」の節）。
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
| `@identify` | `mvp`（PDM から L2BM、3.5.8.4）| 64 u64 | 64 の倍数（`.param` の `:identify` の `:addr`）| E255 |
| `@boundary_flags` | `mvp`（PDM から L2BM、3.5.8.4）| 64 u64 | 64 の倍数（`.param` の `:boundary_flags` の `:addr`）| E256 |
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

## slot は `.param` のエントリを指す番号である

`from_param <slot>` / `to_param <slot>` の `<slot>` は `.param` の `:slot` ID であり、
**PDM の番地ではない。** 番地は `.param` がその slot の `:addr` として別に持つ。

vsmlink コマンドは、slot について次の 2 つを行う。

1. **`.param` のエントリを引いて展開する。** directive の `from_param N` / `to_param N` の N で、`.param` の `:slot N` の
   エントリを引く。そこから番地（`:addr`、または `:src-slot` で指した slot の `:addr` + `:addr-offset`）と待ちのタグ
   （`:send_wait_tag` / `:recv_wait_tag`）を得て、directive をその番地と待ちのタグで展開する
2. **`@broadcast` の読み進めた位置を slot ごとに持つ**（「@broadcast」の「同じ slot を繰り返すと続きを読む」の節）

slot の番号は、`.param` と `_vsm` で一致していればよい。次は、kernel の引数 1 つに slot を 1 つ割り当て、`8 * (ArgNo + 1)` で
番号を振った例である（MNCL の振り方）。

```lisp
;; kernel func(double *pos, double *consts, double *vel) に対応する .param
(:slot 8  :place :pdm0 :addr 0     :send_wait_tag #x10)   ; 第 1 引数 pos
(:slot 16 :place :pdm0 :addr 16384 :send_wait_tag #x11)   ; 第 2 引数 consts
(:slot 24 :place :pdm0 :addr 40960)                       ; 第 3 引数 vel
```

### 配列を順に配る

**配列を順に配るときは、同じ slot に対して `@broadcast` を繰り返し書く**（「@broadcast」の「同じ slot を繰り返すと
続きを読む」の節）。要素ごとに slot を増やさない。

配列の途中の位置から配るときは、その位置に新しい slot を割り当て、`.param` に
`:src-slot`（元の slot）と `:addr-offset`（元の `:addr` からのずれ、u64 単位）を書く
（`:addr` は書かない。`param-spec.md` の「`:src-slot` と `:addr-offset`」）。これは要素ごとの
slot ではなく、別の始まりを持つ別の slot である。

**slot 番号に番地のずれを足してはいけない。** slot 番号は離散的な採番であり、番地の
空間ではない。足すと別の正当な slot 番号と偶然一致しうる。

### `@broadcast` の slot の共有

- **`@broadcast` の slot は、ほかの directive と共有してよい。** `@broadcast` に使う slot を、同じ
  `.param` を使うほかの directive（`@distribute` / `@collect` / `@reduce` など）で使っても、エラーにしない。
  共有したとき、同じ kernel の中で `@broadcast` が L1BM に移動した 64 u64 は、その後にほかの directive が
  その slot の PDM に書いても変わらない。`@broadcast` は、L1BM に移動した 64 u64 を使い切るまで PDM を
  読み直さない（「@broadcast」の「同じ slot を繰り返すと続きを読む」の節）
- 共有をエラーにしたいときは、vsmlink コマンドに `--strict-broadcast-slot` を付ける（E257。`api-reference.md` の
  「使い方」）

## 構文

```asm
@distribute             <name> from_param <slot> size <size> <pe_register>
@broadcast              <name> from_param <slot> size <size> <pe_register>
@collect                <name> to_param <slot> size <size> <pe_register>
@alias                  <bf_ID> <register>
@identify               <dim_id> <dest_register> <size>
@access_pattern         <type> <offsets...>
@boundary_flags         <bf_ID> <dim> <register> <flag_names...>
@alloc                  <name> [:grf0 <num...>] [:grf1 <num...>] [:m <num...>] [:n <num...>] [:omr <num...>]
@free                   <name>
@get_neighbor           <neighbor_register> <offset> <src_register> <bf_ID> <buf_id> <alloc_ID> [<端の指定>]
@get_hypercube_neighbor <dest> <deep> <src_register> <id_register> <buf_id> <alloc_ID>
@get_eoshift            <src_register> <buf_id> <alloc_ID> (<dest> <dir> <N> <M> [<端の指定>]) ...
@reduce                 <name> to_param <N> <pe_register> <op>
@emit_from              <debug_name> pe <n> to_param <N> size <size> <pe_register>
@kernel_begin           <kernel 名>
@kernel_end             <kernel 名>
@persistent_info        <任意のトークン列>
@assign                 <bf_ID> <register>                               ; deprecated、@alias を使う
```

`@distribute` / `@broadcast` / `@collect` の `<name>` は、directive の呼び出しに付ける名前である。
利用者が任意に付けられる。**vsmlink コマンドは名前を使わない。** 名前は処理に影響せず、重複の検査もしない
（同じ名前を複数の呼び出しに付けてもエラーにならない）。生成する `.vsm` のコメントとエラーメッセージに、
そのまま書き写す。`@reduce`、`@distribute_aos2soa`、`@collect_soa2aos` の `<name>` も同じである。

## @alloc

レジスタプールを確保する。 `@free` で解放する。

### @free は直前の @alloc と ID が一致していなければならない

`@alloc` は入れ子にできない（E201）。 したがって解放できるのは直前に確保したプールだけ
であり、 `@free` の ID が直前の `@alloc` の ID と違えばエラーになる（E210）。
`@get_neighbor` / `@get_hypercube_neighbor` の `<alloc_ID>` も、確保中の `@alloc` の ID と違えばエラーになる（E222）。

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
粒度は 64 長語なので、PE 単位のインターリーブが原理的に不可能である。転置を含む `@distribute_aos2soa` / `@collect_soa2aos`（次節）を使えば、PE ごとに
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
l1bmd $lb.. $lm0v/1000
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
| `<name>` | 呼び出しに付ける名前（`@distribute` / `@collect` の `<name>` と同じ）|
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
`@distribute` が PE k に配るのと違う。隣の PE の値を使う directive（`@get_neighbor`）は PE の位置で相手を選び、
レジスタの値の出どころを見ないので、組み合わせてもエラーにならず動く。ただし、隣の PE にあるのは番号の隣の
データではない。どのレジスタにどのデータが入っているかは、使う側が管理する。例は
[`data-to-pe-mapping.md`](data-to-pe-mapping.md) を参照する。番号の隣のデータが要る計算には、転置の無い
`@distribute` / `@collect` を使う。

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
  同じ転送。`dmread` の後の待ちは、転送の先頭の `nop/2`（dev manual 3.6.3.9）が兼ねる
- `dmwrite` の次のステップに `dmread` を置く（間に待ちを置かない）。LM0 / LM1 / GRF0 / GRF1 のどの組み合わせでも同じ

## @alias

既存のレジスタに名前を付ける。コードを生成しない。
`@boundary_flags` のレジスタを `@get_neighbor` の `bf_id` として参照するために使用する。

```asm
@alias bf1 $lr416
```

- `bf_id` と `$lr` レジスタの対応を登録する
- `@boundary_flags` が別カーネルで実行済みの場合に、そのレジスタを参照するために使う

## @broadcast

slot の PDM にあるデータを先頭から順に読み、1 回の `@broadcast` で size 個の u64 を **全 4096 PE に同じ size 個の値**として配布する。同じ slot の `@broadcast` を繰り返すと、呼ぶたびに続きのデータを読む（「同じ slot を繰り返すと続きを読む」の節）。 各 PE は `$lr16` のような GRF0 レジスタ起点に size 個を受け取る。 `@distribute` (各 PE が異なる値) とは異なる HW パスを使う。

```asm
@broadcast _arg_a from_param 8 size 4 $lr16
```

- 構文は `@distribute` と同じ (`<name> from_param <slot> size <size> <pe_register>`)
- `<slot>` は `.param` の `:slot` ID。**PDM の番地ではない**
- `<size>` は 1, 2, 4 のいずれか（1 回で配る u64 の個数）。 **size 3 は無い。** 範囲外は error (E227)
  - たとえば OpenCL の `double` なら 1、 `double2` なら 2、 `double4` なら 4 にあたる。 `double3` は配列の中で 1 要素あたり 4 u64 の間隔で並ぶので 4 を使う。 型と size の対応は `_vsm` を書く側（例: MNCL）が決める
  - size 8 / 16 は今後の課題である
- スロットの `:addr` 自体に条件は無い。 1 回の消費（size u64）が 64 u64 の境目をまたぐと error (E228)。 理由は「開始アドレスの境界」の節を参照
- 着地レジスタは +2 偶数刻み。 源の u64[i] が cycle i として `<pe_register>` + 2*i に着地する (size 4・`$lr16` 起点なら u64[0]/[1]/[2]/[3] が `$lr16`/`$lr18`/`$lr20`/`$lr22`)。 GRF0 は偶数アライン必須なので `<pe_register>` は偶数レジスタを指定する
- HW パス: `mvb` (PDM から全 8 L2BM へ放送)、 `l2bmb` (L2BM から全 64 L1BM へ放送)、 `l1bmp` (L1BM から全 64 PE へ 1 u64/cycle 放送。 vector dst + サイクルマスクで size 個ぶん)
- emu:lib で着地レジスタと全 PE 同値を実測済 (`examples/26-broadcast-accumulate-2d` の landing 検証)

### 入力規約 (重要)

`l1bmp` は 1 u64/cycle を MAB 内の全 64 PE に放送する。 `mvb` / `l2bmb` が PDM のデータを全 L2BM / L1BM へ配るため、 全 4096 PE が同じ値を受け取る。 書き込みサイクルマスクにより先頭 size cycle だけがレジスタに書かれる。

ホストは、**配りたい値を配る順に、slot の `:addr` から詰めて置く**。 1 回の `@broadcast` が size u64 を消費し、 次の `@broadcast` はその続きを消費する（「同じ slot を繰り返すと続きを読む」の節）。 L1BM への移動は 64 u64 単位なので、 最後の 64 u64 の中で消費しなかった部分も読まれる（値は使われない）。

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

### 同じ slot を繰り返すと続きを読む

**同じ slot の `@broadcast` を繰り返すと、呼ぶたびに続きのデータを読む。** 同じ slot に対する `@broadcast` は、
次の順に動く。

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
  使わない（「slot は `.param` のエントリを指す番号である」の節）
- **送信完了待ち:** slot に `:send_wait_tag` があれば、その slot の最初の 64 u64 を移動する
  前に 1 回だけ、ホストの送信の完了を待つ（kernel の実行ごとに 1 回）。2 つ目以降の 64 u64 の
  移動では待たない。送信が終われば slot のデータは PDM にそろっているためである。タグが
  無ければ待たない（`param-spec.md` の「送信完了待ち」）
- **kernel の実行ごとに、slot の `:addr` から読み始める。** 読む位置は link 時に決まり、
  固定の命令列として `.vsm` に書き出されるので、ホストから kernel を再実行しても、前回の
  実行の続きを持ち越す仕組みは無い。別の kernel も別に link されるので同じである
- これらの判定はすべて **link 時（vsmlink が `.vsm` を生成する処理の中）に決まる。** device の
  実行時に判定が行われることはない

### `@broadcast` と DRAM のスロット

`.param` の slot が `:place :group-dram`（`param-spec.md` の「`:place :group-dram`」）のとき、
`@broadcast` は PDM の代わりに **DRAM** から読む。`_vsm` の書き方は PDM の slot と同じである。

```asm
@broadcast _pj from_param 8 size 4 $lr16   ; slot 8 が :place :group-dram のとき、DRAM から読む
```

- **読む場所:** 各グループが、自分の DRAM の `:addr` から読む。HW パスは `mvb2`（DRAM から、同じグループの
  2 つの L2BM へ放送。dev manual 3.5.8.13）、`l2bmb`、`l1bmp` である。PDM の slot の `mvb` の代わりに
  `mvb2` を使う
- **各 PE が受け取る値:** 各 PE は、**自分のグループの DRAM の値**を受け取る。全グループの DRAM の同じ番地に
  同じ値が入っていれば、全 4096 PE が同じ値を受け取る（PDM の slot と同じ結果になる）。グループの DRAM の
  中身が違えば、グループごとに違う値を受け取る
- **移動の単位:** PDM の slot と同じく、L1BM への移動は 64 u64 単位で、同じ slot を繰り返すと続きを読む。
  64 u64 の境目をまたぐ消費は E228 になる
- **読む番地の範囲:** 読む DRAM 番地が 512 Mi u64（グループあたり 4 GiB）以上なら E116
- **送信完了待ちは無い:** ホストは DRAM へ送れない。`:send_wait_tag` が書いてあれば E277 で止まる。
  DRAM の中身がそろったことを待つ処理は、DRAM に書く `_vsm` の側（MV 命令とその待ち）で行う

### 読み出し位置を指定する引数は持たせない

`@broadcast` に、読み出し位置（offset など）を指定する引数は持たせない。**任意の位置から
読めるように見えてしまうため**である。読む位置は、その slot をそれまでにどこまで消費したか
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

`_vsm` を書く側（例: MNCL）が、元のプログラムを調べて得たアクセスパターンの宣言。コメントとして出力される。

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
各 PE には、その PE が該当するレベルのうち最も外側の 1 つのビットだけが立つ（注参照）。

| bit | フラグ名 | 意味 |
|-----|---------|------|
| 9 | data_edge_left | データの本当の端（左）。ホストが立てる。`@get_neighbor` では端の PE（「`@get_neighbor` の端の PE」） |
| 8 | data_edge_right | データの本当の端（右）。ホストが立てる。`@get_neighbor` では端の PE |
| 7 | cross_chip_left | cross chip（左）。この MN-Core 2 の端。`@get_neighbor` では端の PE |
| 6 | cross_chip_right | cross chip（右）。この MN-Core 2 の端。`@get_neighbor` では端の PE |
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

**注: cross group のビットは無い**

group の境界に接する PE も、cross L2B と同じ PDM 経由の経路（`mvp`、`:place :pdm0`）で処理する。
経路が同じなので cross group を区別するビットは持たず、その PE には cross_L2B のビットが立つ。

### バイナリの選択

どの PE にどのビットを立てたバイナリを送るかは、ホストが決める。vsmlink コマンドは、ホストが PDM に置いた値を配るだけで、
バイナリの中身を作らず、検査もしない。

| data_edge_left | data_edge_right | バイナリ例（4096 PE） |
|---|---|---|
| 0 | 0 | `57b42dea.bin` (edge00) |
| 0 | 1 | edge01（`gen_bf_golden ok <out> --edge-right` で生成。`examples/data/build.ninja` には登録していない） |
| 1 | 0 | edge10（`gen_bf_golden ok <out> --edge-left` で生成。`examples/data/build.ninja` には登録していない） |
| 1 | 1 | `75c77b0f.bin` (edge11) |

### 動作

- ホスト側で事前計算したフラグデータを PDM → L2BM → L1BM → PE に配布する
- 結果は指定レジスタの LOW 32bit にパックされる
- `.param` に `:boundary_flags <bf_id> :place :pdm0 :addr N` の記述が必要

### 命名規則

- `cross_<レベル>_<方向>`: データ転送が必要な境界（隣接ユニットへの通信）
- `cross_chip_<方向>`: この MN-Core 2 の端。ホストが、データがこの MN-Core 2 の外へ続く（隣に MN-Core 2 がある）ものとして立てる
- `data_edge_<方向>`: データの本当の端。ホストが立てる。cross_chip と排他的で、どの PE にも立てられる（データが 1 つの MN-Core 2 より短いときの端も表せる）
- `@get_neighbor` では、cross_chip と data_edge のどちらが立っている PE も端の PE で、端の指定の対象である。違いは上の層にとっての意味である（「`@get_neighbor` の端の PE」）

## 端の指定（`@get_neighbor` / `@get_eoshift`）

`@get_neighbor` と `@get_eoshift` では、隣の値を受け取らない PE（以下、**端の PE**）がある。端の PE の
`<dest>` に何を入れるかを、末尾の **端の指定** で選ぶ。端の指定は 1 つだけ書ける。2 つ以上書くと E283 になる。

| 端の指定 | 端の PE の `<dest>` |
|---|---|
| （書かない） | 値を約束しない。展開の途中で書いた値が残る |
| `:keep` | 呼ぶ前の `<dest>` の値のまま |
| `:periodic` | 同じ区切りの反対の端の PE の `<src_reg>` の値。`@get_neighbor` では、cross_chip の PE だけ（反対側の cross_chip の PE の値）。data_edge の PE は値を約束しない |
| `:reg <register>` | 端の PE 自身の `<register>` の値。`<register>` は GRF0 / GRF1 / LM0 / LM1（`$lr` / `$ls` / `$lm` / `$ln`）のどれか |

- 端の PE でない PE の `<dest>` は、端の指定によらない
- 知らないキーワード、`:reg` の後にレジスタが無い、`:reg` のレジスタが GRF0 / GRF1 / LM0 / LM1 でない、のどれかは E282 になる
- 端の PE がどれかは、directive ごとに決まっている。`@get_neighbor` は境界のフラグの cross_chip と data_edge で決まる（「`@get_neighbor`」の「端の PE」）。
  `@get_eoshift` は区切り（`<M>`）で決まり、境界のフラグを見ない（「受け取らない PE」の項）

### 端の指定の使い分け

| 用途 | 書き方 | 端の PE の `<dest>` |
|---|---|---|
| 端の値を使わない（端の PE の結果を後で捨てる、マスクで書き込まない） | （書かない） | 値を約束しない。命令は増えない |
| 端の PE だけ、呼ぶ前の値を残す | `:keep` | 呼ぶ前の `<dest>` |
| 周期境界（反対の端とつなぐ） | `:periodic` | 同じ区切りの反対の端の PE の `<src_reg>`（`@get_neighbor` では cross_chip の PE だけ） |
| 端の PE に自分の値を入れる（clamp） | `:reg <src_reg>` | 端の PE 自身の `<src_reg>` |
| 端の PE に、用意しておいた値を入れる（固定の境界値、隣のチャンクの値など） | `:reg <register>` | 端の PE 自身の `<register>` |

例:

```asm
@get_neighbor $lr400 -1 $lm0 bf1 nbuf0 alloc0              ; 端の値を使わない。左の端の PE の $lr400 は約束しない
@get_neighbor $lr400 -1 $lm0 bf1 nbuf0 alloc0 :keep        ; 左の端の PE の $lr400 は、呼ぶ前の $lr400
@get_neighbor $lr400 -1 $lm0 bf1 nbuf0 alloc0 :periodic    ; cross_chip_left の PE の $lr400 は、cross_chip_right の PE の $lm0
@get_neighbor $lr400 -1 $lm0 bf1 nbuf0 alloc0 :reg $lm0    ; 左の端の PE の $lr400 は、自分の $lm0（clamp）
@get_neighbor $lr400 -1 $lm0 bf1 nbuf0 alloc0 :reg $ln8    ; 左の端の PE の $lr400 は、自分の $ln8（呼ぶ前に境界値を置いておく）
```

### `:reg` のレジスタが重なる場合

`:reg <register>` の `<register>` の値は、展開の最後に端の PE の `<dest>` へ書く。`<register>` が、展開の途中で書き換わるレジスタと重なると、
呼ぶ前の値を書けない。そのため、値が重なる `:reg` は、`:reg <src_reg>` だけを通し、それ以外はエラーにする。

| `<register>` | 扱い |
|---|---|
| `<src_reg>` と同じ（ベクタなら同じ範囲） | 通す。`<src_reg>` と `<dest>` が同じレジスタのときは、vsmlink コマンドが展開の最初に `<src_reg>` を `@alloc` のレジスタに写し、端の PE にはその写しを書く（命令が 1 つ増える） |
| `<src_reg>` ではなく、`<dest>` と重なる（`@get_eoshift` では、どの組の `<dest>` でも） | E286 |
| `@alloc` で確保した GRF0 / LM1 のレジスタと重なる | E286 |
| どれとも重ならないが、`<dest>` と同じ LM0 / LM1 にある（`<src_reg>` の場合を含む） | 通す。1 つの命令で同じ LM から読んで同じ LM に書くことはできないので、vsmlink コマンドが展開の最初に `<register>` を `@alloc` のレジスタに写し、端の PE にはその写しを書く（命令が 1 つ増える）。値は重ならないので、端の PE の `<dest>` に入る値は写さない場合と同じ |
| どれとも重ならず、`<dest>` と同じ LM にもない | 通す。写さない |

`:keep` も、呼ぶ前の `<dest>` を展開の最初に `@alloc` のレジスタへ写して使う（命令が 1 つ増える）。

例: 通る書き方。

```asm
@get_eoshift $lm0 nbuf0 hc ($lm16 - 4 8 :reg $lm0)         ; clamp。src の $lm0 と dest の $lm16 が同じ LM0 にあるので、展開の最初に $lm0 を写す
@get_eoshift $lm0 nbuf0 hc ($lm16 - 4 8 :reg $lm4)         ; 境界値を $lm4 に置いておく。$lm4 は dest と同じ LM0 にあるので、展開の最初に写す
@get_neighbor $lm0 -1 $lm0 bf1 nbuf0 alloc0 :reg $lm0      ; src と dest が同じ $lm0。左隣の値で $lm0 を置き換え、左の端の PE には呼ぶ前の自分の $lm0 が入る
@get_eoshift $lm0 nbuf0 hc ($lm0 - 4 8 :reg $lm0)          ; 同じことを @get_eoshift で。区切りの先頭の 16 個の PE の $lm0 には、呼ぶ前の自分の $lm0 が入る
```

例: エラーになる書き方と、その直し方。

```asm
@get_neighbor $lr400 -1 $lm0 bf1 nbuf0 alloc0 :reg $lr400  ; E286: :reg の $lr400 は <dest> と同じで、<src_reg> ではない
                                                          ;   端の PE に呼ぶ前の $lr400 を残すなら :keep と書く
@get_eoshift $lm0 nbuf0 hc ($lm16 - 4 8 :reg $ln8) ($ln8 + 4 8)  ; E286: :reg の $ln8 は、2 つ目の組の <dest> と同じ
                                                          ;   境界値は、どの組の <dest> とも違うレジスタに置く
```

### ベクタ（PE あたり 4 u64）

`<src_reg>` と `<dest>` に `v` を付けて書くと（`$lm0v` のように）、PE あたり 4 u64 を受け取る。4 u64 は、アセンブラの `v` と同じ
並び（`$lm0v` なら `$lm0`、`$lm2`、`$lm4`、`$lm6`）である。

- `<src_reg>` と `<dest>` の `v` の有無が食い違うと E284 になる。`:reg <register>` を書くときは、`<register>` の `v` の有無も
  `<dest>` とそろえる
- 展開は、成分ごとの 1 u64 の展開を 4 つ続けたものになる。`@alloc` で要るレジスタの数は 1 u64 のときと同じである
- `<src_reg>`、`<dest>`（`@get_eoshift` ではすべての組の `<dest>`）、`:reg` の `<register>` のうち 2 つの 4 u64 の範囲が、一部だけ重なると
  E285 になる。範囲が同じ（`$lm0v` と `$lm0v`）なら E285 にならず、上の「`:reg` のレジスタが重なる場合」の規則で扱う
  （`:reg <src_reg>` は通り、それ以外で `<dest>` か `@alloc` のレジスタと重なると E286）

例:

```asm
@get_neighbor $lm0v -1 $lm0v bf1 nbuf0 alloc0 :reg $lm0v   ; 通る。src と dest が同じ $lm0v。端の PE の $lm0v には、呼ぶ前の自分の $lm0v（4 u64）が入る
@get_neighbor $lr302v -1 $lr300v bf1 nbuf0 alloc0          ; E285: $lr300v（$lr300〜$lr306）と $lr302v（$lr302〜$lr308）が一部だけ重なる
@get_neighbor $lm16v -1 $lm0v bf1 nbuf0 alloc0 :reg $lm18v ; E285: :reg の $lm18v と <dest> の $lm16v が一部だけ重なる
@get_neighbor $lm16v -1 $lm0v bf1 nbuf0 alloc0 :reg $lm16v ; E286: :reg の $lm16v は <dest> と同じ範囲で、<src_reg> ではない
```

### ヒント

- **端の PE に自分の `<src_reg>` の値を入れる（clamp）には、`:reg <src_reg>` と書く。** `<src_reg>` と `<dest>` が同じレジスタでも、上の
  「`:reg` のレジスタが重なる場合」のとおり、呼ぶ前の値が入る。

  ```asm
  @get_neighbor $lr400 -1 $lm0 bf1 nbuf0 alloc0 :reg $lm0    ; 左の端の PE の $lr400 には、自分の $lm0 が入る
  @get_eoshift $lm0 nbuf0 hc ($lm16 - 4 8 :reg $lm0)         ; 区切りの先頭の 16 個の PE の $lm16 には、自分の $lm0 が入る
  ```

- データが複数の区切り（たとえば 4096 個を超えるデータを分けたチャンク）にまたがるとき、区切りの端の PE が受け取るべき値は、
  隣の区切りのデータである。この値は directive の外で用意して、`:reg` で渡せる

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

構文: `<dest> <offset> <src_reg> <bf_id> <buf_id> <alloc_id> [<端の指定>]`（6 個の引数と、省略できる端の指定）

| 引数 | 意味 |
|------|------|
| `<dest>` | 結果レジスタ（書き込み先）。GRF0 / GRF1 / LM0 / LM1（`$lr` / `$ls` / `$lm` / `$ln`）のどれか。それ以外は E269 |
| `<offset>` | 隣接方向（-1=左隣、+1=右隣） |
| `<src_reg>` | 読み出し元 PE レジスタ（元データが載っているレジスタ） |
| `<bf_id>` | boundary flags（@boundary_flags/@alias で登録した ID） |
| `<buf_id>` | 袖交換バッファ（.param の `:buffer` で宣言した ID） |
| `<alloc_id>` | リソースプール（@alloc で確保した ID） |
| `<端の指定>` | 省略できる。`:keep`、`:periodic`、`:reg <register>` のどれか（「端の指定」） |

読み出し元 `<src_reg>` と書き込み先 `<dest>` はともに PE レジスタで指定する。どのレジスタを使うかは
呼び出し側（コンパイラのレジスタアロケータ）が決める。`<dest>` に使えるのは GRF0 / GRF1 / LM0 / LM1 で、
T レジスタやマスクレジスタなど、それ以外を指定すると E269 になる。

**1 回の `@get_neighbor` で受け取るのは、PE あたり 1 u64、`v` を付けたときは 4 u64 である**（「端の指定」の「ベクタ」）。
`<src_reg>` の値を、そのまま隣の PE の `<dest>` に運ぶ。値の型は見ないので、1 つの u64 に入っている値（fp64 1 つ、fp32 2 つなど）は、
まとめて運ばれる。

### 端の PE

**端の PE は、`<bf_id>` の境界のフラグ（`@boundary_flags`）で決まる。** `<offset>` が -1 なら cross_chip_left か data_edge_left、+1 なら cross_chip_right か
data_edge_right が立っている PE が、端の PE である。端の PE の `<dest>` は、端の指定で決まる（「端の指定」）。

- cross_chip は、この MN-Core 2 の端である。data_edge は、データの本当の端である。どちらも端の指定の対象で、`:keep`、`:reg <register>`、書かない場合の動作は同じ
- `:periodic` は、cross_chip の PE どうしをつなぐ。cross_chip_left の PE には cross_chip_right の PE の `<src_reg>` の値が、cross_chip_right の PE には
  cross_chip_left の PE の値が入る。data_edge の PE の `:periodic` の値は約束しない（データの本当の端は、周期でつながる相手を持たない。
  データが 1 つの MN-Core 2 より短い周期のデータで、反対側の値が要るなら、上の層がその値を用意して `:reg` で渡す）
- vsmlink コマンドは、境界のフラグの中身を作らず、検査もしない（「`@boundary_flags`」の「バイナリの選択」）。どの PE を端にするかは、境界のフラグを作る上の層が決める

### `<src_reg>` が表すもの

- **`<src_reg>` は、隣の PE から値を受け取る対象のレジスタである。** `@get_neighbor` は、
  展開した時点で `<offset>` だけ離れた PE の `<src_reg>` にある値を `<dest>` に取る。`@distribute` で配った値でも、
  計算の途中の値でもよい。データと PE の対応、および使う側と `@get_neighbor` の責任の範囲は
  [`data-to-pe-mapping.md`](data-to-pe-mapping.md) を参照する

注記（MNCL の `load_neighbor()` として使う場合）: MNCL の `.cl` の API `load_neighbor(p, offset)` は、
「`distribute(p)` が配ったのと同じ配置で、自 PE から offset 離れた位置の値」を返す。この意味で
使うときは、MNCL が、どの kernel 引数をどのレジスタに配ったかを覚えていて、そのレジスタを
`<src_reg>` として渡す。`@distribute` で配った値が `@get_neighbor` の時点まで同じレジスタに
残っていること、同じ段階の中でその配列に書き戻さないことは、`load_neighbor` を使う側（MNCL と利用者）の
約束であり、`@get_neighbor` の制限ではない。MNCL の `get_neighbor(v, offset)` は、変数 `v` が入っている
レジスタを `<src_reg>` として渡し、隣の PE のいまの値を受け取る。

### 展開内容

展開は次の順に命令を出す。0 と 9 は、端の指定があるときだけ出る。

0. **端の値を写す** — `:keep`（呼ぶ前の `<dest>`）と、写しが要る `:reg`（「`:reg` のレジスタが重なる場合」）のときだけ、端の PE に書く値を `@alloc` の GRF0 のレジスタに写す
1. **ゼロレジスタの作成** — `lxor`
2. **入力データを GRF0 へ写す** — `lpassa <src_reg> $lr<n>`（`$lr<n>` は `@alloc` の GRF0 のレジスタ）
3. **cross L2B** — put（PE, L1BM, L2BM, PDM0 の袖交換バッファの順）と get（逆順。1 L2B 分ずらして読む）の後、`l1bmd±1` と `msl` / `msr`。`:periodic` のときは、端の PE の分を、同じ区切りの反対の端の番地から読む
4. **cross L1B** — `l1bmd` で L1BM に出し、`l2bm` と `l2bmb` で隣の L1B に渡し、`l1bmd±1` と `msl` / `msr`
5. **cross MAB** — `l1bmd` で L1BM に出し、`l1bmd±1` と `msl` / `msr`
6. **within MAB** — `<src_reg>` に `msl` / `msr` を直接適用
7. **omr 導出** — `imm` で 10bit ワンホット定数（cross L2B、cross L1B、cross MAB）を 3 本置き、`ixor` で `@boundary_flags` の値との完全一致を判定して omr に入れる
8. **マージ** — within MAB をデフォルトとして `lpassa` で `<dest>` に書き、cross MAB, cross L1B, cross L2B の順に書き込みマスクと `ipassa` で上書きする。書き込みマスクは `<dest>` のレジスタファイルに対して有効にする（`<dest>` が GRF0 なら `maskr`、GRF1 なら `masks`、LM0 なら `maskm`、LM1 なら `maskn`）
9. **端の PE に書く** — 境界のフラグと、向きの側の cross_chip と data_edge の値それぞれとの完全一致（`ixor`）で端の PE を選び、書き込みマスクと `ipassa` で、端の指定の値を `<dest>` に書く

### マージフェーズの実装方針

マージは **compute-all-then-select 方式** で行う: 各階層の shift（`msl`/`msr`/`l1bmd`/`l2bm`/`mvp` 等）を全 PE で無条件に計算し、選択だけをマスクで行う。理由は 2 つある。

- 書き込みマスクは PE メモリへの書き込みを抑制するだけで、命令の実行自体を抑制しない
- data mover 系の命令は隣接 PE の協調を必要とし、「該当する PE だけが実行する」ことができない

選択は次の形で行う。**単一行マスク（`/$imrN`）は使っていない。**

```asm
  mask<c> <omr>
ipassa $lr<n> <dest>
  mask 0
```

`<omr>` と `$lr<n>` は `@alloc` で確保したレジスタである。`<c>` は `<dest>` のレジスタファイルを表す文字（`r` / `s` / `m` / `n`）である。複数行の書き込みマスク
（`mask[r][s][t][m][n][k] <addr>`）は、指定したレジスタファイルへの書き込みにだけ効き、指定の無いレジスタファイルへの
書き込みは抑えない（dev manual 3.6.2.1）。`maskr` に固定すると、`<dest>` が GRF0 以外のときは上書きが全 PE に書き込まれる。

`ipassa` を使うのは、書き込みマスクによる書き込み抑制が効くためである。`lpassa` は書き込みマスクを無視する。

代替案として「data-mover 命令に単一行マスク `/$imrN` を付けて条件実行（predicated-shift）」する方式も考えられるが、以下の理由で採用しない:

- data-mover 系命令（`msl`/`msr`/`l1bmd` 等）に単一行マスクを付けたときの挙動が dev manual に書かれていない（シフト自体がスキップされるのか、書き戻しだけマスクされるのか、無視されるのかが分からない）

### 呼び出し側のマスク状態

**`@get_neighbor` はグローバルマスク状態を変更する。** マージの各上書きが `mask<r|s|m|n> <omr>` で始まり `mask 0` で終わるため、`@get_neighbor` を抜けた時点でマスクは 0 になる。呼び出し前に `maskr` で設定したマスクは保存されない。

したがって `@get_neighbor` を `maskr` 区間の内側に置くことはできない。`examples/15-mask-safety-T4/T4._vsm` も `mask 0` の外側に置いている。

### テンプレートの選択

テンプレートは、`<offset>` の向きと端の指定で決まる。

### リソース数

@alloc で grf0 11 個、omr 4 個、n 1 個が必要。向きと端の指定によらず共通（grf0 の 11 個目は、端の指定の値を写すときに使う）。
同一 @alloc ブロック内で左右 2 回呼んでも、同じレジスタを使うので、追加のレジスタは不要。

## @emit_from

**デバッグ用の directive である。通常の計算には使わない。** 実機では PE の中の途中の値が見えない
（`mnc2_debug_read` が実機で使えない）ので、HW PE ID が `<n>` の PE の値を、呼ぶたびに PDM の slot の続きへ書いていく。
`@broadcast`（PDM の slot の続きを、呼ぶたびに読んで全 PE に配る）の逆の向きにあたる。

```asm
@emit_from em pe 100 to_param 40 size 1 $lm0
@emit_from em pe 100 to_param 40 size 1 $lm0   ; 2 回目は、1 回目の続きに書く
```

構文: `<debug_name> pe <n> to_param <N> size <size> <pe_register>`（`@collect` の並びに `pe <n>` を足した形）

| 引数 | 意味 |
|------|------|
| `<debug_name>` | デバッグ用の名前。利用者が任意に付けられる。vsmlink コマンドは名前を使わず、重複の検査もしない。生成する `.vsm` のコメントに、そのまま書き写す |
| `pe <n>` | 値を取り出す PE の **HW PE ID**（0〜4095。範囲外は E274）。HW PE ID は MN-Core 2 の用語ではなく、vsmlink コマンドの文書が決めた語である。定義と、`$l2bid` / `$l1bid` / `$mabid` / `$subpeid` との対応表は [`hw-pe-id.md`](hw-pe-id.md)。`@identify` で配る PE ID（ホストが PDM に置いた値）とは別のもの |
| `to_param <N>` | 書き先の `.param` の `:slot` ID。`:place :pdm0`、`:addr` は 64 の倍数（E275） |
| `size <size>` | 取り出す成分の数（1〜4。範囲外は E272）。成分は `<pe_register>` から 2 刻みのレジスタ（`@collect` と同じ） |
| `<pe_register>` | 先頭の成分のレジスタ。`v` は付けない |

- **書かれる量。** 1 回の呼び出しで、PDM の slot の続きに **64 u64 × `size`** を書く。成分 c のブロックは続き + 64·c から始まり、
  64 u64 の中身は **`<n>` の属する L1B の 64 PE の値**である。欲しい値は、そのブロックの `<n> % 64` 番目に入る（同じ L1B の
  ほかの 63 PE の値も一緒に残る。書き込みの単位が 64 u64 であるため。dev manual 3.5.8.5）。
  同じ slot の `@emit_from` は、呼ぶたびに続きに書く。続きの番地は、`_vsm` を上から順に展開するときに決まり、命令に埋め込まれる。
  kernel ごとに `:addr` から数え直す。**ホストが同じ kernel を繰り返し実行すると、毎回同じ番地に書く（前の回の値は上書きされる）**
- **範囲の検査は PDM 全体の大きさだけ。** 書く位置が PDM の大きさ（512 Ki u64）を超えると E116。**ほかの slot の領域に
  書き込んでも、エラーにも警告にもならない。** 呼ぶ回数が多いと、その先にある別の slot（kernel 引数の配列や、`@collect` の
  書き先など）の領域に書き込む
- **待ちのタグをサポートしない。** `@emit_from` は、待ちのタグを付けた転送を出さず、タグの完了を待つ処理も出さない。
  slot に `:send_wait_tag` または `:recv_wait_tag` が書かれているときは、無視して W005 を出す（リンクは続く）。同じ kernel の中で
  何回呼んでも、待ちのタグの重複（E120）にならない。書き込みの完了は、ホストにも kernel にも通知されず、kernel の終わりまでに
  書き込みが終わることは保証しない。同期が必要な場合は、`@reduce` など、ほかの directive で行う
- `@alloc` / `@free` は不要で、`@alloc` の中では使えない（E258）。`<pe_register>` は読むだけで、書き換えない
- `_vsm` を書く側は、directive の前後の待ちを考えなくてよい（「_vsm を書く側が置く待ち」）

### 転送の仕組み（L1BM → L2BM → PDM の順に溢れさせる）

呼ぶたびに PDM へ書くと遅いので、まず L1BM の専用の領域に溜め、いっぱいになったら L2BM の専用の領域へ移し、そこもいっぱいに
なったら PDM へ書く。

- 呼ぶたびに、全 PE の値を `l1bmd`（PE → L1BM、dev manual 3.6.8.21）で L1BM の専用の領域の続きに集める
- L1BM の専用の領域（1024 u64 = 16 ブロック）に入らなくなったら、溜めた分を `l2bm@`（L1BM → L2BM、3.6.7.5）で L2BM の専用の
  領域（4096 u64 = 64 ブロック）へ移す
- L2BM の専用の領域に入らなくなったら、溜めた分を `mvp`（L2BM → PDM、3.5.8.5）で PDM へ書く
- **最後の `@emit_from` では、溜まっている分をすべて PDM へ書く。** どの呼び出しが最後かは、`_vsm`（kernel）を読み終えてから決める。
  マルチカーネルの `_vsm` では、kernel ごとに最後を決める
- 専用の領域は、最初の呼び出しで確保し、kernel の終わりまで手放さない。ほかの directive は、この領域を使えない。
  使える L1BM / L2BM が、その分だけ減る（L1BM 1216 u64、L2BM 4096 u64）。確保できないときは E233 になる
- PDM へ書く途中の `mvp` は、L2BM の専用の領域を書き換える前に完了を待つ（vsmlink が予約タグ `:reserved_wait_tags` で待つ。
  `.param` の予約タグが要る）。最後の書き出しは待たない

### 前提と制限

- 対象は 4096 PE（1 チップ）である
- `_vsm` に `if` も `for` も無いので、続きの番地の数え方に例外は無い。残る制限は、上のとおり、ホストが同じ kernel を繰り返し実行する
  と同じ番地に書くことだけである

### ヒント

- 書き込みの完了を待ちたいときは、`@reduce` など、ほかの directive を併用する
- 書き先の slot の先にほかの slot があると、その領域にも書き込まれる

## @get_hypercube_neighbor

超立方体の隣の PE の値を取得する。PE の番号 j（0〜4095）の PE は、PE の番号 `j XOR 2^deep` の PE の
`<src_reg>` にいまある値を `<dest>` に受け取る。バイトニックソートの比較・交換の相手を取るために使う。
`@get_neighbor` と同じく、展開した時点の値を受け取るだけで、その値がどの slot から来たかを知らず、検査もしない
（「データと PE の対応」は [`data-to-pe-mapping.md`](data-to-pe-mapping.md) を参照）。

```asm
@get_hypercube_neighbor $lm16 3 $lm0 $lr8 nbuf0 hc    ; 相手は j XOR 8
```

6 引数構文: `<dest> <deep> <src_reg> <id_reg> <buf_id> <alloc_id>`

| 引数 | 意味 |
|------|------|
| `<dest>` | 結果レジスタ（書き込み先）。GRF0 / GRF1 / LM0 / LM1（`$lr` / `$ls` / `$lm` / `$ln`）のどれか。それ以外は E269 |
| `<deep>` | 整数の定数。0〜11。範囲外は E270。変数は書けない |
| `<src_reg>` | 読み出し元 PE レジスタ |
| `<id_reg>` | 自分の PE の番号 j が入っている PE レジスタ（`@identify` で配ったもの）。deep によっては使わないが、引数としては常に書く |
| `<buf_id>` | PDM 上の一時バッファ（`.param` の `:buffer`）。経路が PDM を通る deep（9〜11）だけが使う。常に書く |
| `<alloc_id>` | リソースプール（`@alloc` で確保した ID）。対応する `@alloc` の中でだけ使える（E222） |

- **対象は 4096 PE（1 チップ、1 次元）である。** 全体の要素数は 2^N でなければならない。`<deep>` は 0〜11 で、12 以上は E270 になる
- **前提: PE j（j は PE の番号）の `<id_reg>` には、値 j が入っている。** ホストが PDM の j 番目に値 j を置き、
  `@identify` で配る。**この前提が満たされないときの結果は保証しない。** vsmlink コマンドは検査しない。エラーにならず、
  誤った PE の値を受け取ることがある
- 1 回の呼び出しで受け取るのは、PE あたり 1 u64 である。複数の u64 にまたがる値は、u64 ごとに呼ぶ
- 端が無いので、`@boundary_flags` は要らない
- `<buf_id>` は、deep 9〜11 のときだけ使う。このとき、`:buffer` は 4096 u64 以上が要る（範囲の外を指すと E142）。`.param` に
  `:reserved_wait_tags` が要る（転送の完了を待つため）。deep 0〜8 では `<buf_id>` を参照せず、存在も検査しない
- 経路が PDM を通る deep（9〜11）は、kernel の中で転送の完了を待つ。deep 0〜8 より遅い
- `@distribute_aos2soa` で配った値との組み合わせは、禁止も検査もしない。データと PE の対応は使う側が管理する
  （[`data-to-pe-mapping.md`](data-to-pe-mapping.md)）
- `_vsm` を書く側は、directive の前後の待ちを考えなくてよい（「_vsm を書く側が置く待ち」）

### deep ごとの経路

| deep | 経路 |
|------|------|
| 0 | MAB 内の 4 PE の循環シフト `msr` / `msl`。`<id_reg>` の bit 0 が 0 の PE は `msr`、1 の PE は `msl` の結果を取る |
| 1 | `msr` を 2 回（4 PE の循環で j+2。4 PE の中で j+2 と j XOR 2 は同じ PE） |
| 2〜4 | `l1bmd` で PE から L1BM へ書き出し、MAB 番号をずらしてディストリビュートする（MAB 番号の bit deep−2 が違う相手）。`<id_reg>` の bit deep が 0 の PE は MAB 番号が 2^(deep−2) 大きい相手、1 の PE は 2^(deep−2) 小さい相手の値を取る |
| 5 | `l1bmd` でコレクトし、MAB 番号を 8 ずらしてディストリビュートする（16 を法として +8 と −8 は同じ。選択は要らない） |
| 6〜8 | `l1bmd` でコレクトし、`l2bmi`（L1BM 間内部マルチキャスト）を 2 回行う。L1B 番号の bit deep−6 が違う L1B へ、片方向ずつ送る。選択は要らない |
| 9〜11 | `l1bmd` でコレクトし、`l2bm@` で各 L2B の L2BM に集め、L2BM → PDM → L2BM の転送で、L2B 番号の bit deep−9 が違う L2B の値を読み戻して、`l2bmb` と `l1bmd` で配る。PDM のバッファ（`<buf_id>`）を 4096 u64 使う。転送の完了は予約タグで待つ |


### リソース数

`@alloc` で、deep 0 は grf0 4 個、n 1 個、omr 1 個。deep 1 は grf0 1 個。deep 2〜4 は grf0 5 個、n 1 個、omr 1 個。deep 5 は grf0 1 個。
複数の deep を続けて呼ぶ場合は、最大に合わせて確保する。`<buf_id>` も、同じものを続けて使ってよい。deep 6〜8 と 9〜11 は grf0 1 個。L1BM は deep 2〜5 が 256 u64、deep 6 以上が 320 u64 を、展開の中で使う（使い終わったら返す）。deep 9〜11 は L2BM の 1024 u64 も使う（使い終わったら返す）。確保できないときは E233 になる。

### ヒント

- 4096 個を超える個数では、呼び出しを要素ごとに分け、deep 12 以上に当たる操作（同じ PE が持つ複数の要素の取り替え）は
  レジスタの写しで書く。例は `examples/37-bitonic-sort-8192`（8192 個のバイトニックソート）である

## @get_eoshift

位置の差が 2^N の PE の値を受け取る。PE の番号 j（0〜4095）の PE は、`<dir>` が `-` なら PE の番号 `j - 2^N` の PE の、`+` なら
`j + 2^N` の PE の `<src_reg>` にいまある値を `<dest>` に受け取る。ただし、**区切り（2^M 個）をまたぐ受け取りはしない。**
位置 j の区切りの番号は `j / 2^M`（切り捨て）で、相手が同じ区切りにある PE だけが受け取る。2 次元か 3 次元か、上か下かは
この directive の外の約束である（この directive は PE の番号の差だけを見る）。

```asm
@get_eoshift $lm0 nbuf0 hc ($lm16 - 4 8) ($lm18 + 4 8)    ; $lm16 は j-16 から、$lm18 は j+16 から。区切りは 256 個
```

構文: `<src_reg> <buf_id> <alloc_id> (<dest> <dir> <N> <M> [<端の指定>]) ...`（3 個の引数と、1 つ以上の組）

| 引数 | 意味 |
|------|------|
| `<src_reg>` | 読み出し元 PE レジスタ |
| `<buf_id>` | PDM 上の一時バッファ（`.param` の `:buffer`）。`<M>` が 10 以上の組（経路が PDM を通る）だけが使う。`<M>` が 9 以下の組は参照しない（存在も検査しない）。常に書く |
| `<alloc_id>` | リソースプール（`@alloc` で確保した ID）。対応する `@alloc` の中でだけ使える（E222） |
| `<dest>` | 結果レジスタ。GRF0 / GRF1 / LM0 / LM1（`$lr` / `$ls` / `$lm` / `$ln`）のどれか。それ以外は E269 |
| `<dir>` | `-` は位置の小さい側（`src[j - 2^N]`）、`+` は大きい側（`src[j + 2^N]`）から受け取る。それ以外は E278 |
| `<N>` | 整数の定数。ずれは 2^N。0〜11。範囲外は E279。変数は書けない |
| `<M>` | 整数の定数。区切りは 2^M 個。`<N> + 1`〜12。範囲外は E280。変数は書けない |
| `<端の指定>` | 省略できる。組ごとに書く。`:keep`、`:periodic`、`:reg <register>` のどれか（「端の指定」） |

- **組は 1 つ以上書く。** 意味は、組ごとに 1 つずつ呼んだ場合と同じである。組の間で、`<dest>` が同じレジスタを指してもよい（後の組が上書きする）
- **受け取らない PE（端の PE）の `<dest>` は、端の指定で決まる。** `<dir>` が `-` なら、区切りの先頭の 2^N 個の PE（`j mod 2^M < 2^N`）、`+` なら区切りの末尾の
  2^N 個の PE（`j mod 2^M >= 2^M - 2^N`）は、区切りの外から値を受け取ることになるので、受け取らない。端の指定を書かなければ、
  そこに何が入るかは決まっていない（「端の指定」）。`:periodic` では、`-` なら `j + 2^M - 2^N`、`+` なら `j - 2^M + 2^N` の PE の値が入る
- **対象は 4096 PE（1 チップ、1 次元）である。** 全体の要素数は 2^12 でなければならない
- **受け付ける `<N>` と `<M>` の組は、`<N>` が 0〜5 で、`<M>` が `<N> + 1`〜12 の場合である。** `<N>` が 6 以上の組は、範囲の中でも E281 になる
- 展開した時点の `<src_reg>` の値を受け取るだけで、その値がどの slot から来たかを知らず、検査もしない
  （「データと PE の対応」は [`data-to-pe-mapping.md`](data-to-pe-mapping.md) を参照）
- 1 回の呼び出しの 1 組で受け取るのは、PE あたり 1 u64、`v` を付けたときは 4 u64 である。**書き込むのは `<dest>` の 1 u64（`v` なら 4 u64）だけで、その外のレジスタは書き換えない**
- `@boundary_flags` は要らない
- `_vsm` を書く側は、directive の前後の待ちを考えなくてよい（「_vsm を書く側が置く待ち」）

### 区切りの大きさごとの経路

位置 j = subpe + 4·mab + 64·l1b + 512·l2b。ずれ 2^N は、N = 2〜5 では MAB 2^(N-2) 個分、N = 0、1 では MAB（4 PE）の内側の 1 個、2 個分にあたる。`<M>` が 9 以下の組は PDM を通らない。
N = 0、1 の組は、先に MAB の内側で 2^N 個回し（PE 間の循環シフト `msl`（`-`）/ `msr`（`+`））、回した値を、N = 2（MAB 1 個分）と同じ形で配る。
MAB の内側で回すと、subpe が `-` なら 2^N 以上（`+` なら 4 - 2^N 未満）の PE は、回した値がそのまま受け取る値になり、それ以外の PE は、1 つ前（後）の MAB の同じ subpe の回した値を受け取る
（MAB 番号 0（`+` は 15）の PE は、前（次）の L1B の値）。これらの選択は書き込みマスクで行い、そのためのマスクが 2 つ要る。以下の表の経路は、N によらない。

| `<M>`（区切り） | 経路 |
|------|------|
| 〜6（64 個以下） | `l1bmd` で PE から L1BM へ書き出し、MAB 番号をずらして（`l1bmd+k` / `l1bmd-k`）ディストリビュートする。受け取る PE の相手は同じ L1B にある |
| 7（128 個 = 2 つの L1B） | 上に加えて、`l2bmi`（L1BM 間内部マルチキャスト）を 1 回行い、区切りの中の隣の L1B の値を写す。MAB 番号が条件（`-` なら `2^(N-2)` 未満、`+` なら `16 - 2^(N-2)` 以上）を満たす PE だけ、写した値を受け取る（書き込みマスク） |
| 8（256 個 = 4 つの L1B） | `l2bmi` を 2 回行い、区切りの中の隣の L1B の値を写す。選択は M = 7 と同じ |
| 9（512 個 = 1 つの L2B） | 区切りの中の隣の L1B の値を写す。一方の向きの組（`-` なら偶数 → 奇数）は `l2bmi` 1 回、もう一方（奇数 → 偶数）は L2BM を通す（`l2bm@` で L2BM に集め、`l2bmb@` で受け取る L1B だけに書く）。選択は M = 7 と同じ |
| 10〜12（1024、2048、4096 個 = 2、4、8 個の L2B） | M = 9 の経路に加えて、L2B をまたぐ 1 行（`-` なら前の L2B の L1B 7 の値を L1B 0 へ、`+` なら次の L2B の L1B 0 の値を L1B 7 へ）だけを PDM 経由で運ぶ。L2B の間に L2BM から L2BM への転送が無いためである。区切りの中で隣の L2B がある L2B の間だけを運ぶ。`<buf_id>` の `:buffer` を 576 u64 使う。転送の完了は予約タグで待つ |

### リソース数

`@alloc` で、`<N>` が 2〜5 の組は、`<M>` が 6 以下なら grf0 1 個、7 以上なら grf0 3 個、n 1 個、omr 1 個。`<N>` が 0、1 の組は、grf0 4 個、omr 2 個（`<M>` が 6 以下でも同じ）。複数の組を続けて呼ぶ場合は、最大に合わせて確保する。
端の指定を書いた組は、少なくとも次が要る（`<M>` が 6 以上の `:periodic` を除く。組の本体と同じレジスタを使い回す）: grf0 3 個、n 1 個、omr 1 個。`:keep`、`:periodic`（`<M>` が 5 以下）、
写しが要る `:reg` は、これに加えて grf0 をもう 1 個使う（`<N>` が 2〜5 なら 4 個目、0、1 なら 5 個目）。
L1BM は、`<M>` が 6 以下の組が 256 u64、7 以上の組が 320 u64 を、展開の中で使う（使い終わったら返す）。L2BM は、`<M>` が 9 の組と、`<M>` が 8 の `:periodic` の組が 512 u64、10 以上の組が 576 u64 を使う（使い終わったら返す）。
確保できないときは E233 になる。`<M>` が 10 以上の組が使う `:buffer` は、576 u64 以上が要る（小さいときは E142、`:buffer` が無いときは E223）。`.param` に `:reserved_wait_tags` が要る（PDM への転送の完了を待つため）。

## @reduce

全 4096 PE の値を階層的に縮約し、結果を PDM に書き込む。
`@collect` と同じ I/F で `.param` の `:slot` を使用する。

```asm
@reduce _result to_param 24 $lr52 dfadd
```

4 引数構文: `<name> to_param <N> <pe_register> <op>`

| 引数 | 意味 |
|------|------|
| `<name>` | 呼び出しに付ける名前（`@distribute` / `@collect` の `<name>` と同じ）|
| `to_param <N>` | `.param` の `:slot` ID |
| `<pe_register>` | 入力レジスタ（`$lr` 系） |
| `<op>` | 縮約オペレータ（rrn_opcode） |

- `@alloc`/`@free` は不要
- `.param` の対応スロットの `:recv_wait_tag` は任意。ホストが結果を `mnc2_recv` で待つときに指定する（省略時はホストは待たない。`param-spec.md` の「`:recv_wait_tag` を省略した場合」）
- スロットの `:addr` は 64 の倍数でなければならない（E246）。「開始アドレスの境界」の節を参照
- ホストは `mnc2_recv(recv_wait_tag=...)` で結果（4 u64）を受け取る
- **PDM には、スロットの `:addr` から 64 u64 を書く。意味があるのは先頭の 4 u64 である。** 残りの 60 u64 に意味は無い。
  PDM への転送（`mvr`）の単位が 64 u64 であるため（dev manual 3.5.8.19）。この 64 u64 は実行のたびに上書きされるので、
  ほかのデータを置かない
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
nop/3

# L1BM → L2BM 縮約（8 L1B → 4 部分和）
l2bmr<op> $lb<N> $lc<M>
nop/2

# L2BM → PDM 縮約（全 8 L2B → 4 部分和、recv_wait_tag 付き）
mvr<op>/n64i<tag> $lc<M> $p<addr>@0
nop/2
nop; wait i<tag>
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
を解釈も検証もしない。 kernel ごとのテキストの切り出しは、 リンク解決より前の段階で行う。

- 出力先の決まり方（`@kernel_begin` の有無で出力がファイルかディレクトリかに変わる）と、
  リンクせずに kernel ごとの `._vsm` を書く `--no-link` は [`api-reference.md`](api-reference.md) を参照
- 切り出しのエラー（kernel 名の重複・不正な文字種・個数の上限超過など E301 から E308）は
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

**`_vsm` を書く側は、directive の前後の待ちを考えなくてよい。** どの directive でも同じである。

- directive の直後に、directive が書いたレジスタを読む命令を置いてよい。待ちは vsmlink が置く
- directive の直前に、directive が読むレジスタへ書く命令を置いてよい。待ちは vsmlink が置く
- directive の直後に、別の directive を置いてよい。待ちは vsmlink が置く

directive の展開は、それだけで完結する。後に何が続いても（利用者の命令でも、別の directive の展開でも）ハザードが
起こらないように、vsmlink が待ちを置く。後続の命令によっては、この待ちが余計になることがある。

`_vsm` を書く側が待ちを置くのは、directive の外の、利用者が書く命令どうしの間だけである。値は dev manual の
3.6.3.9（PE メモリへの書き込みから読み出しまで）による。

| 書き込んだ先 | 読み出すまでに空けるステップ | 条件 |
|---|---|---|
| LM0 / LM1（`$lm` / `$ln`）| 2 | 番地によらず常に要る |
| GRF0 / GRF1（`$lr` / `$ls`）| 1 | 同じ番地を読むときだけ。違う番地なら待たずに読める |

たとえば `@reduce` に渡すために LM の値を `lpassa` で GRF0 へ写した直後は、上の値を空ける。

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
| **cross chip** | この MN-Core 2 の端の PE | `@get_neighbor` では端の PE（「端の指定」）。`:periodic` は反対側の cross chip の PE とつなぐ |
| **data edge** | ホストがデータの本当の端として立てる PE | `@get_neighbor` では端の PE（「端の指定」）。`:periodic` の値は約束しない |

boundary_flags はワンホット。各 PE には上記のうち **1 つだけ** が該当し、対応する 1 ビットだけが立つ。
left/right の方向ごとに独立したビットを持つ（例: cross_MAB_left, cross_MAB_right）。


> **cross group**: `@boundary_flags` の節の「注: cross group のビットは無い」を参照。

### 袖交換（put/get）

`@get_neighbor` の節の「put/get モデル」を参照。
