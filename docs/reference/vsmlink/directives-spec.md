# _vsm ディレクティブ仕様

vsmlink が展開する全ディレクティブの構文と動作。
用語定義は末尾の「用語」セクションを参照。

## ディレクティブ一覧

| ディレクティブ | 説明 | 状態 |
|---------------|------|------|
| `@distribute` | PDM → L2BM → L1BM → PE（一括展開、 各 PE が異なる値） | 実装済み |
| `@broadcast` | PDM → L2BM → L1BM → PE（全 4096 PE が同じ値） | 実装済み |
| `@collect` | PE → L1BM → L2BM → PDM（一括展開） | 実装済み |
| `@alias` | レジスタに名前を付ける（コード生成なし、境界フラグ参照用） | 実装済み |
| `@identify` | PE に問題空間座標を配る（PDM → L2BM → L1BM → `$lr` に展開） | 実装済み |
| `@access_pattern` | アクセスパターン宣言 | コメント化のみ |
| `@boundary_flags` | 境界フラグ生成（distribute モード） | 実装済み |
| `@boundary_flags_compute` | 境界フラグ生成（HW レジスタから計算。デバッグ用・非公開） | 実装済み |
| `@alloc` | レジスタプールの確保 | 実装済み |
| `@free` | レジスタプールの解放 | 実装済み |
| `@get_neighbor` | 隣接 PE の値を取得（全階層: PE/MAB/L1B/L2B） | 実装済み |
| `@reduce` | 全 4096 PE の階層的縮約（L1BM → L2BM → PDM） | 実装済み |

**deprecated:**

| ディレクティブ | 説明 | 状態 |
|---------------|------|------|
| `@assign` | `@alias` の旧名（boundary flags 参照） | deprecated |
| `@stencil` | `@get_neighbor` の旧名 | 廃止（`@get_neighbor` を使用すること） |

## PDM の使用範囲とメタデータ

**directive は `.param` のスロットの開始アドレスから、決まった量の PDM を使う。**
仕様として受け取る量（`@reduce` の 4 u64 など）と、実際に占める量は別である。

| directive | 向き | 開始アドレスから使う量 |
|---|---|---|
| `@distribute` | PDM から読む | **4096 u64 かける `size`** |
| `@collect` | PDM へ書く | **4096 u64 かける `size`** |
| `@broadcast` | PDM から読む | **64 u64**（`size` によらない） |
| `@reduce` | PDM へ書く | **64 u64** |

`@distribute` と `@collect` は 1 成分あたり 4096 u64（4096 PE かける 1 u64）を使う。
`size` を 2 以上にすると、その倍数だけ連続して占める。

`@broadcast` が `size` によらず 64 u64 なのは、**転送命令 `mvb` の最小単位が 64 u64
だから**である（dev manual 3.5.8.18）。先頭 `size` 個だけを読むが、確保は 64 u64 要る。

`@reduce` は結果として意味があるのは先頭 4 u64 だが、**転送命令 `mvr` が 64 の倍数しか
運べないため、必ず 64 u64 書く**（dev manual 3.5.8.19）。減らす手段は無い。

### vsmlink はメタデータを出す。使うかどうかはホストが決める

**vsmlink は PDM の配置の是非を判断しない。検査もしないし、拒否もしない。やったことを
出すだけである。** PDM をどう使ってどう並べるかはホスト側の問題であり、vsmlink は判断
材料を持たない。

生成した `.vsm` に、directive ごとに 1 行の S 式が入る。

```
# @reduce _result to_param 24 $lr52 dfadd
# === @reduce _result: $lr52 → L1BM → L2BM → PDM $p20000 (dfadd) ===
# (:pdm :directive "@reduce" :id "_result" :slot 24 :place :pdm0 :addr 20000 :size 64 :access :write)
```

| フィールド | 意味 |
|---|---|
| `:directive` | directive 名 |
| `:id` | `_vsm` に書いた識別名 |
| `:slot` | `.param` のスロット ID |
| `:place` | 配置先（`:pdm0` / `:group_pdm` / `:group_dram`） |
| `:addr` | 開始アドレス（u64 単位） |
| `:size` | 開始アドレスから使う量（u64 単位） |
| `:access` | `:read` または `:write` |

`:access` の意味はホスト側から見て次のとおり。

- **`:write`** — `:addr` から `:size` u64 に**他のものを置かないこと**。置いていたら壊れる
- **`:read`** — `:addr` から `:size` u64 に**有効なデータを置いておくこと**。足りなければ
  ごみを読む

### 読み方

1 行が独立した S 式なので、`# (:pdm ` で始まる行を集めて先頭の `# ` を外せば、そのまま
S 式として読める。

```sh
grep '^# (:pdm ' out.vsm | sed 's/^# //'
```

ホストは自分の PDM の配置とこれを突き合わせて、用意漏れと破壊を確認できる。
**確認するかどうかはホストの判断である。**

`@broadcast` は同じ区画が L1BM に載っていれば PDM からの転送を出さないが、**メタデータは
`@broadcast` の行ごとに必ず 1 行出る。**

### `@identify` / `@boundary_flags` / `@get_neighbor` は対象外

これらも PDM に触るが、`:slot` ではなく `:identify` / `:boundary_flags` / `:buffer` という
別のバインディングを使う。**現時点ではメタデータを出していない。**

## 構文

```asm
@distribute             <buffer_id> from_param <offset> size <size> <pe_register>
@broadcast              <buffer_id> from_param <offset> size <size> <pe_register>
@collect                <buffer_id> to_param <offset> size <size> <pe_register>
@alias                  <bf_ID> <register>
@identify               <dim_id> <dest_register> <size>
@access_pattern         <type> <offsets...>
@boundary_flags         <bf_ID> <dim> <register> <flag_names...>
@boundary_flags_compute <bf_ID> <dim> <register> <alloc_ID>
@alloc                  <id> [:grf0 <num...>] [:grf1 <num...>] [:m <num...>] [:n <num...>] [:omr <num...>]
@free                   <id>
@get_neighbor           <neighbor_register> <offset> <src_register> <bf_ID> <buf_id> <alloc_ID>
@reduce                 <id> to_param <N> <pe_register> <op>
@assign                 <bf_ID> <register>                               ; deprecated、@alias を使う
```

## @alloc

レジスタプールを確保する。 `@free` で解放する。

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
上位層で行うこと。

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

1. PHASE_INIT の場合、`.param` の `:send_wait_tag` で指定された send wait タグで `wait` を発行（host からの転送完了待ち）
2. 成分ごとに次を繰り返す（`size` 回）
   1. mvp 命令で PDM から L2BM へ（8 本、全 L2B 分）
   2. l2bmb で L2BM から L1BM へ（8 本）
3. `l1bmd $lb.. $lm0v<マスク>` で L1BM から PE ローカルメモリへ（先頭 `size` サイクルを書く）

`.param` のスロットエントリに `:send_wait_tag` が必須。未指定はエラー。

### @collect の動作

PE ローカルメモリ → L1BM → L2BM → PDM への展開。

1. l1bmd で PE → L1BM（`size` によらず 4 成分が載る）
2. 成分ごとに次を繰り返す（`size` 回）
   1. l2bm で L1BM から L2BM へ（8 本、全 L2B 分）
   2. mvp 命令で L2BM から PDM へ（グループごとに wait タグ付き）

`.param` のスロットエントリに `:recv_wait_tag` が必須。`:recv_wait_tag` は**最後の成分の最後のグループ**の mvp に付与されるタグで、host が `mnc2_recv(recv_wait_tag=...)` で転送完了を検出する。途中の成分と途中のグループは予約 wait タグで内部同期を取る。未指定はエラー。

### .param の例

```lisp
((:version :0.1)
 (:slot 8  :place :pdm0 :addr 0     :send_wait_tag #x10)            ; @distribute 用
 (:slot 16 :place :pdm0 :addr 4096  :send_wait_tag #x10 :recv_wait_tag #x1e) ; @collect 用
 (:boundary_flags bf1 :place :pdm0 :addr 8192 :send_wait_tag #x10))
```

- `:send_wait_tag`: send 方向（Host → PDM）の send wait タグ。`@distribute` / `@boundary_flags` / `@identify` で使用
- `:recv_wait_tag`: recv 方向（PE → PDM）の recv wait タグ。`@collect` で使用。host は `mnc2_recv(recv_wait_tag=...)` で待つ

## @alias

既存のレジスタに名前を付ける。コードを生成しない。
`@boundary_flags` のレジスタを `@get_neighbor` の `bf_id` として参照するために使用する。

```asm
@alias bf1 $lr416
```

- `bf_id` と `$lr` レジスタの対応を登録する
- `@boundary_flags` が別カーネルで実行済みの場合に、そのレジスタを参照するために使う

## @broadcast

PDM 上の先頭 size 個の u64 を、 **全 4096 PE に同じ size 個の値**として配布する。 各 PE は `$lr16` のような GRF0 レジスタ起点に size 個を受け取る。 `@distribute` (各 PE が異なる値) とは異なる HW パスを使う。

```asm
@broadcast _arg_a from_param 8 size 3 $lr16
```

- 構文は `@distribute` と同じ (`<buffer_id> from_param <N> size <S> <pe_register>`)
- `<size>` は 1 から 4。 各 PE は放送領域の先頭 size 個の u64 を double として受け取る。 範囲外は error (E227)
- 着地レジスタは +2 偶数刻み。 源の u64[i] が cycle i として `<pe_register>` + 2*i に着地する (size 3・`$lr16` 起点なら u64[0]/[1]/[2] が `$lr16`/`$lr18`/`$lr20`)。 GRF0 は偶数アライン必須なので `<pe_register>` は偶数レジスタを指定する
- HW パス: `mvb` (PDM から全 8 L2BM へ放送)、 `l2bmb` (L2BM から全 64 L1BM へ放送)、 `l1bmp` (L1BM から全 64 PE へ 1 u64/cycle 放送。 vector dst + サイクルマスクで size 個ぶん)
- emu:lib で着地レジスタと全 PE 同値を実測済 (`examples/26-nbody-2d` の landing 検証)

### 入力規約 (重要)

`l1bmp` は 1 u64/cycle を MAB 内の全 64 PE に放送する。 `mvb` / `l2bmb` が放送領域を全 L2BM / L1BM へ配るため、 全 4096 PE が同じ値を受け取る。 size N なら **配りたい N 個の値を放送領域の先頭 N u64 に置く** (源 u64[i] が cycle i)。 書き込みサイクルマスクにより先頭 N cycle だけがレジスタに書かれる。

`mvb` の最小単位は 64 u64 (= 512 byte) なので、 PDM 領域は 64 u64 分確保する (先頭 N u64 以外はサイクルマスクで読まれないためゴミでよい)。 ホスト側は先頭 N u64 を配りたい値で埋めて `mnc2_send` で送る。

詳細は `docs/broadcast-design.md` を参照。

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

MNCL がソースコード解析で得たアクセスパターンの宣言。コメントとして出力される。

```asm
@access_pattern stencil1d (-1) (1)
→ # @access_pattern stencil1d (-1) (1)
```

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
設計経緯は [docs/buffer-rationale.md](buffer-rationale.md) を参照。

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

マージは **compute-all-then-select 方式** で行う: 各階層の shift（`msl`/`msr`/`l1bmd`/`l2bm`/`mvp` 等）を全 PE で無条件に計算し、選択だけをマスクで行う。理由は 2 つある（`docs/boundary-selection.md` 参照）。

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
- `.param` の対応スロットに `:recv_wait_tag` が必須
- ホストは `mnc2_recv(recv_wait_tag=...)` で結果（4 u64）を受け取る
- 結果は PE position 別の部分和（[0]+[1]+[2]+[3] で全 PE の総和）

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
 (:slot 24 :place :pdm0 :addr 20000 :recv_wait_tag #x06))  ; @reduce 用
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
結果は PE position 別の 4 u64 部分和。ホスト側で 4 値を合算すれば全 PE の総和が得られる。

---

## deprecated

`@stencil` は廃止。`@get_neighbor` を使用すること。

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

## if/then/else の実装パターン: interior 方式

> 結果選択の設計背景、代替方式の検討については [docs/boundary-selection.md](boundary-selection.md) を参照。

MN-Core 2 の SIMD 実行モデルでは、全 PE が同じ命令ストリームを実行する。
袖交換（隣接 PE 値の取得）では通信命令を全 PE が協調実行する必要があるため、
「条件に合う PE だけ実行する」ネスト if/then/else は成立しない（詳細は `if-then-else-history.md`）。

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
- `docs/if-then-else-history.md` — ネスト方式が不適切である理由の経緯

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

設計の背景、結果選択の方式については [docs/boundary-selection.md](boundary-selection.md) を参照。

> **cross group 廃止**: `@boundary_flags` の節の「注: cross group 廃止の経緯」を参照。

### 袖交換（put/get）

`@get_neighbor` の節の「put/get モデル」を参照。
