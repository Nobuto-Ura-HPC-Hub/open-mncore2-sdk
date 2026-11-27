{% raw %}
# .param 仕様

配置パラメタファイル。スロット ID とメモリ配置を対応づける。S 式形式。`;` 以降は行コメント。

## 基本構文

```lisp
((:version :0.1)
 (:slot 8  :place :pdm0 :addr 0     :send_wait_tag #x10)
 (:slot 16 :place :pdm0 :addr 4096  :send_wait_tag #x11 :recv_wait_tag #x1e)
 (:slot 24 :place :pdm0 :addr 131072 :send_wait_tag #x12))
```

## :slot エントリ

| フィールド | 値 | 説明 | 必須 |
|-----------|-----|------|------|
| `:slot` | 整数 | スロット ID（`from_param N` / `to_param N` の N）。`.param` のエントリを引くための番号。番地ではない。0 以上 8192 未満で、1 つの `.param` の中で一意（下記）| 必須 |
| `:place` | `:pdm0` / `:group-dram` | 配置先。`:pdm0` は **グループ 0 の PDM**（dev manual の `$p<addr>@0`。host-dma の `mnc2_send` / `mnc2_recv` が読み書きする PDM0 と同じ）、`:group-dram` は各グループの DRAM（下記）。`:group-dram` を使える directive は `@broadcast` だけである | 任意（デフォルト `:pdm0`） |
| `:addr` | 整数 | 開始アドレス（u64 単位）。境界の条件はスロットを使う directive で決まる（[directives-spec.md](directives-spec.md) の「開始アドレスの境界」）。`:src-slot` があるときは書けない（E121）| `:src-slot` が無いとき必須 |
| `:send_wait_tag` | `#xNN` または整数 | send 方向 send wait タグ（Host → PDM）。`@distribute` / `@broadcast` で使用。あれば、そのスロットの PDM を最初に読む前に送信の完了を待つ（「送信完了待ち」の節）| 任意 |
| `:recv_wait_tag` | `#xNN` または整数 | recv 方向 recv wait タグ（PE → PDM）。`@collect` / `@reduce` で使用。host は `mnc2_recv(recv_wait_tag=...)` で待つ。省略するとホストはその転送の完了を待たない（下記）| 任意 |
| `:src-slot` | 整数 | 番地を決める元のスロットのスロット ID。あれば、番地は元のスロットの `:addr` + `:addr-offset` で決まる（下記）| 任意 |
| `:addr-offset` | 整数 | 元のスロット（`:src-slot`）の `:addr` からのずれ。単位は `:addr` と同じ u64 単位（× 8 で byte）で、要素の数ではない（下記）| `:src-slot` があるとき必須 |

表に無いキーワード、またはキーワードでないキーを書くと E129 で止まる。

`:send_wait_tag` と `:recv_wait_tag` は同一スロットに両方指定可能（ただし値は別にする。「待ちのタグの規則」）。

### `:recv_wait_tag` を省略した場合

`@collect` / `@reduce` が PDM に書く転送のうち、最後の転送に `:recv_wait_tag` を付け、直後に
kernel 自身がそのタグで `wait` する。ホストは同じタグで `mnc2_recv` を呼んで、結果が PDM に
届いたことを待つ。

**`:recv_wait_tag` は省略してよい。** 省略すると、ホストはその転送の完了を待たない（待つための
タグが無い）。kernel の中の転送の完了の同期（最後の転送と直後の `wait`）は、vsmlink が予約タグ
の先頭の値で行う。送信の「`:send_wait_tag` が無ければ待たない」と同じ考え方である。

### `:place :group-dram` — 各グループの DRAM にあるデータ

`:place :group-dram` のスロットは、`:addr` が **DRAM の番地**（u64 単位）を指す。

```lisp
(:slot 8 :place :group-dram :addr 0)
```

- **どのグループも、同じ番地を読む。** グループを付けない `$d<addr>`（dev manual 3.5.2.2）にあたる。
  DRAM はグループごとに別のメモリなので、読む値はグループごとの DRAM の中身で決まる
- **番地の範囲は、DRAM の大きさ（グループあたり 4 GiB = 512 Mi u64）の内側である。** 読む番地が
  512 Mi u64 以上なら E116 で止まる
- **ホストは DRAM に値を送れない。** `mnc2_send` が書くのは PDM0 だけである。DRAM の中身は、`_vsm` に
  書いた MV 命令（PDM から DRAM への転送、dev manual 3.5.8.2 など）で置く。vsmlink コマンドは、
  DRAM に値を置く命令を作らない
- **`:send_wait_tag` は書けない。** ホストが DRAM へ送ることは無いので、待つ相手が無い。書いてあれば、
  `@broadcast` が E277 で止まる。`:recv_wait_tag` は DRAM のスロットでは意味が無い（使える directive が無い）
- **`:group-dram` のスロットを使えるのは `@broadcast` だけである。** `@broadcast` 以外の directive が
  使うと、E268 または E276 で止まる。`@broadcast` の動作は
  [directives-spec.md](directives-spec.md) の「`@broadcast` と DRAM のスロット」を参照

### スロット ID は番号であり、番地ではない

スロット ID は、`.param` と `_vsm` で一致していればよい（例: MNCL は kernel の引数の並び順から `8 * (ArgNo + 1)` で
振っている）。**離散的な採番であって、
番地の空間ではない。スロット ID に番地のずれを足してはいけない**（足すと別の正当な
スロット ID と偶然一致しうる）。

**配列を順に配るときは、要素ごとにスロットを増やさない**（同じスロットの `@broadcast` を繰り返すと続きを読む）。
配列の途中の位置から配るときは、その位置に新しいスロットを割り当てる（下の「`:src-slot` と `:addr-offset`」）。

詳細は [directives-spec.md](directives-spec.md) の「slot は `.param` のエントリを指す番号である」「`@broadcast` の
slot の共有」を参照。

### スロット ID の範囲と一意性

**スロット ID は 0 以上 8192 未満の整数で、1 つの `.param` の中で一意でなければならない。**

- 同じスロット ID のエントリが 2 回現れたら E118 で止まる。どちらの `:addr` を使うかを
  vsmlink は決めない
- 範囲外の値（負の値を含む）は E119 で止まる

**上限 8192 に技術的な根拠は無い。** スロットがそれほど多く必要になることは無いという
見込みによる。配列を順に配るために要素ごとにスロットを増やす誤った形（「スロット ID は
番号であり、番地ではない」の節）で書かれた `.param` を、E119 で止めて見つける役割も
持つ。

### `:src-slot` と `:addr-offset` — 別のスロットの途中から始まるスロット

配列の途中の位置から配るとき（例: MNCL の `broadcast(&buf[1024])`）は、その位置に新しいスロットを割り当てる
（「スロット ID は番号であり、番地ではない」の節）。このスロットは、番地
`:addr` ではなく、元のスロット（`:src-slot`）と、その `:addr` からのずれ（`:addr-offset`）で書く。
vsmlink は、番地を「元のスロットの `:addr` + `:addr-offset`」で決める。

```lisp
(:slot 40 :place :pdm0 :addr 20480 :send_wait_tag #x10)  ; 引数そのもの (buf)
(:slot 48 :src-slot 40 :addr-offset 4096)                 ; buf の途中から始まる別のスロット
```

- **`:addr-offset` の単位は `:addr` と同じ u64 単位**（× 8 で byte）で、要素の数ではない。例えば `double4`
  （1 要素 4 u64）の配列で `&buf[1024]` なら、`:addr-offset` は 1024 × 4 = 4096 になる
- `:src-slot` のあるスロットには `:addr` を書かない（E121）。`:addr-offset` は必須（E122）。`:src-slot` の
  無いスロットに `:addr-offset` は書けない（E123）。`:addr-offset` は 0 以上（E127）
- 元のスロットは、`.param` の中でそれより前に書く（`&buf[1024]` は `buf` があってこそ書ける）。前に無ければ
  E124
- 元のスロット自身が `:src-slot` を持つことはできない（E125）。元にできるのは、`:addr` を持つスロットだけで
  ある
- `:place` は元のスロットから引き継ぐ。書く場合は元のスロットと同じでなければならない（E126）
- `:send_wait_tag` / `:recv_wait_tag` は引き継がない。そのスロットに書いたものが使われる
- 決めた番地に対しても、スロットを使う directive の境界の条件（「開始アドレスの境界」）と範囲の検査が
  そのまま働く

## 仕様に無いキーワード

- `:slot` エントリと `:buffer` エントリに仕様に無いキーワードを書くと、エラーで止まる（E129 / E134）
- `:identify` エントリと `:boundary_flags` エントリでは、仕様に無いキーワードと値の組は読み飛ばされる。エラーにならない

### スロットに大きさは書かない

**`:slot` は開始アドレスだけを持ち、大きさを持たない。** 何 u64 占めるかは、そのスロットを
使う directive で決まる（`@distribute` と `@collect` は 4096 u64、`@reduce` は 64 u64、
`@broadcast` はその kernel で消費した量。詳細は [directives-spec.md](directives-spec.md)
の「PDM の使用範囲」）。

各スロットのエントリに、そのスロットが PDM 上で占める大きさを書かせない。理由は、
**`.param` を書く側（例: OpenCL のカーネルをコンパイルする MNCL）が、その大きさを判断
できないため**である。

- OpenCL のカーネル引数はポインタだけで、指す先の配列の大きさを持たない
- 同じスロットの `@broadcast` を繰り返すと続きのデータを読むので、そのスロットの番地から有効なデータがどこまで続くかが、
  カーネルのソースからは分からない

大きさを知っているのは、PDM にデータを置くホスト（C 側）である。

OpenCL C では、カーネル引数を `__global double buf[4096]` のように配列の形で書いても、型は
`__global double *buf` に読み替えられ、4096 は型に残らない。`.param` を書く側（例: MNCL）が
大きさを知るには、この
書き方に標準の OpenCL C に無い意味を持たせる必要がある。**この形は採らない。** 仕様が複雑に
なるためである。

## 送信完了待ち（`:send_wait_tag`）

`:send_wait_tag` は、ホストが `mnc2_send` でそのエントリの PDM の領域にデータを送るときの
タグである。kernel は、そのタグでホストの送信の完了を待つ（`wait`）。

- PDM を読む directive（`@distribute` / `@identify` / `@broadcast` / `@boundary_flags`）は、使うエントリに
  `:send_wait_tag` があれば、その PDM を最初に読む前にそのタグで `wait` を出す。無ければ待ちを出さない
- libmnc2 の `mnc2_send` は、転送が終わってから戻る（libmnc2 の `share/mnc2/api-reference.md` の「sync model」）。
  kernel が読むデータを、exec の前に `mnc2_send` で送り終えているなら、`.param` に送信のタグを付けなくてよい。
  **このときホストはタグ 0 で送る**（下の「待ちのタグの規則」）
- **ホストがタグ（0 以外）を付けて送ったら、どこかの kernel がそのタグを待つ。** どの kernel もそのタグを一度も
  待たないと、そのタグは使えなくなる。待つのは、`:send_wait_tag` によって vsmlink が出す `wait` でも、`_vsm` や
  手書きの `.vsm` に直接書いた `wait` でもよい
- exec の後に送るデータ（kernel が動いている間に届くもの）と、非同期の `mnc2_async_*` で送るデータ（libmnc2 の
  `share/mnc2/api-reference.md` の「非同期 DMA」）は、タグを付けて kernel に待たせる
- 前の kernel が PDM に書いた結果は、その kernel の `@collect` / `@reduce` の `wait` で完了してから、その kernel が終わる

## 待ちのタグの規則

タグには 2 種類あり、扱いが違う。

- **使う側が `.param` に書く待ちのタグ**（送信 `:send_wait_tag`・受信 `:recv_wait_tag`）:
  **1 つの kernel の中で、同じタグを 2 か所以上に使ってはいけない**（E120）。vsmlink は link の
  ときに、その kernel の directive が使うエントリ（`:slot`、`:boundary_flags`、`:identify`）の
  タグのうち、その directive が使う向きのタグだけを数える。PDM を読む directive
  （`@distribute`・`@broadcast`・`@identify`・`@boundary_flags`）なら送信のタグ、PDM に書く
  directive（`@collect`・`@reduce`）なら受信のタグである。同じタグが複数あると、使う側の意図（どの待ちがどの転送に対応するか）が分からなくなるため
  である。使う側の設計の誤りとして、vsmlink はエラーを出す
  - 範囲を kernel 単位にしている理由: 意図が分からなくなる曖昧さは、1 つの kernel の実行の中で
    しか起きない。また、マルチカーネル `_vsm`（`@kernel_begin`）では 1 つの `.param` を複数の kernel で
    共有するので、`.param` 単位の検査では、別々の kernel が順に同じタグを使う正しい入力まで
    エラーになる
  - kernel をまたいで同じタグを使うのは認める（タグは有限なので使い回しは必要）
  - 使わない向きのタグを数えない理由: マルチカーネル `_vsm` では、ある kernel が読むだけのエントリを、別の
    kernel が書くことがある。そのエントリの受信のタグは、読む kernel から見ると使わない向きだが、
    書く kernel にとっては正しいタグである。1 つの kernel を link している vsmlink には、それが
    正しいかどうかが分からない
  - 使わない向きのタグや、どの kernel も使わないエントリに付いた誤ったタグ（ホストがそのタグで
    送って（受け取って）いないタグ）は、vsmlink には判定できない。ホストのコードと照らし合わせる
    必要がある
- **vsmlink が自動的に使うタグ**（予約タグ。`:reserved_wait_tag(s)`）: vsmlink が kernel の中の
  転送を待つのに使う。タグは有限なので、vsmlink はこれを使い回す。使う側の待ちのタグと同じ値を
  付けるとエラーになる（E101）。予約タグの数は、いま分かっている範囲で最小にする（ふつうは 1 つ）。
  `.param` には必ず書く（省略すると E128）

**タグの値は `0x01`〜`0x3F` の 63 個である**（dev manual 3.6.13 でタグ 0 は `wait` のエラー、
HW の `dmaid_max=63`）。予約・送信・受信のどのタグも、この範囲外は E102 になる。1 つの kernel の
中では使い回せないので、1 つの kernel が使う送信・受信のタグと予約タグの合計は 63 個までである。


## :boundary_flags エントリ

`@boundary_flags` ディレクティブで使用する PDM アドレスと send wait タグを指定する。

```lisp
((:version :0.1)
 (:slot 8  :place :pdm0 :addr 0 :send_wait_tag #x10)
 (:boundary_flags bf1 :place :pdm0 :addr 8192 :send_wait_tag #x11))
```

| フィールド | 値 | 説明 | 必須 |
|-----------|-----|------|------|
| `:boundary_flags` | 文字列 | bf ID（`@boundary_flags` の第 1 引数と一致） | 必須 |
| `:place` | `:pdm0` | 配置先 | 任意 |
| `:addr` | 整数 | 開始アドレス（u64 単位） | 必須 |
| `:send_wait_tag` | `#xNN` または整数 | send wait タグ。あれば、その PDM を最初に読む前に送信の完了を待つ（「送信完了待ち」の節）| 任意 |

## :buffer エントリ

`@get_neighbor` の cross L2B 袖交換と、`@get_eoshift` の `<M>` が 10 以上の組で使用する PDM0 上のバッファを指定する。

```lisp
((:version :0.1)
 (:slot 8  :place :pdm0 :addr 0 :send_wait_tag #x10)
 (:buffer nbuf0 :place :pdm0 :addr 16384 :buf_size 4160))
```

| フィールド | 値 | 説明 | 必須 |
|-----------|-----|------|------|
| `:buffer` | 文字列 | バッファ ID（`@get_neighbor` の第 5 引数、`@get_eoshift` の第 2 引数と一致） | 必須 |
| `:place` | `:pdm0` | 配置先 | 任意 |
| `:addr` | 整数 | 開始アドレス（u64 単位、64 の倍数） | 必須 |
| `:buf_size` | 整数 | バッファサイズ（u64 単位、64 の倍数） | 必須 |

### :buf_size の算出

`@get_neighbor` が使う `:buffer` は **4160 u64** 以上が要る。`@get_eoshift` は 576 u64 以上が要る（小さいときは E142）。

`:addr` と `:buf_size` はともに 64 の倍数でなければならない（mvp 転送単位）。

**算出の根拠。** L2B の境界をまたぐ受け渡しは、1 回あたり 64 u64 の単位で行い、PDM の上では L2B ごとに
512 u64 の間隔で置く（残りの 448 u64 は使わない）。L2B は 8 個あるので 512 × 8 = 4096 u64 になる。これに、読み出す
ときに 1 L2B 分ずらして読むための先頭の余白 64 u64 を足して、4096 + 64 = **4160 u64** である。

## :identify エントリ

`@identify` ディレクティブの PDM アドレスと send wait タグを指定する。

```lisp
((:version :0.1)
 (:identify 0 :place :pdm0 :addr 0 :send_wait_tag #x10)
 (:slot 24 :place :pdm0 :addr 131072 :send_wait_tag #x11))
```

| フィールド | 値 | 説明 | 必須 |
|-----------|-----|------|------|
| `:identify` | 整数 | 次元インデックス（0=x, 1=y, 2=z） | 必須 |
| `:place` | `:pdm0` | 配置先 | 任意 |
| `:addr` | 整数 | 開始アドレス（u64 単位） | 必須 |
| `:send_wait_tag` | `#xNN` または整数 | send wait タグ。あれば、その PDM を最初に読む前に送信の完了を待つ（「送信完了待ち」の節）| 任意 |

## :reserved_wait_tag / :reserved_wait_tags エントリ（top-level）

vsmlink 内部の `mvp/mvr` 完了待ちで使う wait タグ。`@distribute` / `@collect` / `@get_neighbor` 等のディレクティブ展開や template の `{{wait_tag}}` placeholder で参照される。`.param` 全体に対して 1 エントリ（`:slot` 等とは別レベル）。

```lisp
((:version :0.1)
 (:reserved_wait_tag #x3f)                              ; 単一値
 (:slot 8 :place :pdm0 :addr 0 :send_wait_tag #x10))

((:version :0.1)
 (:reserved_wait_tags #x23 #x24 #x25 #x26)              ; 複数値 (array)
 (:slot 8 :place :pdm0 :addr 0 :send_wait_tag #x10))
```

| エントリ | 値 | 説明 |
|---------|-----|------|
| `:reserved_wait_tag` | `#xNN` | **単一値**。等価表現として `(:reserved_wait_tags #xNN)` |
| `:reserved_wait_tags` | `#xNN ...` | **1 個以上**の値の列。template の `{{wait_tag <N>}}` で N=0..count-1 をインデックスする |

**このエントリは必ず書く。省略すると E128 で止まる。** vsmlink は、内部の転送の完了を待つための予約タグが
無いと動けない。どの番号を予約タグにするかは、使う側が自分の使うタグと重ならないように決める。迷ったら、
許容範囲 `0x01..0x3F` の最大値の `(:reserved_wait_tag #x3f)` を書く。

### 制約・検証

vsmlink がリンク時に検査する。違反時はエラー終了（exit code 1）。
該当するエラー ID は E101 から E108。内容は末尾の「エラー・警告一覧」を参照。

### 自然な上限

- 値域 `0x01..0x3F` = 63 値、重複禁止 → 最大 63 要素
- 64 個目を指定すれば必ず E102 か E103 で蹴られる

## 共通事項

| キーワード | 値 | 説明 |
|-----------|-----|------|
| `:version` | `:0.1` | **必須**（先頭エントリ） |

**注意**: `addr` は u64 単位。host-dma の `mnc2_send/recv` に渡す `pdm_offset` はバイト単位なので `addr * 8` の変換が必要。

## エラー・警告一覧

エラーは S 式で stderr に出力し、exit code 1 で終了する。

```lisp
(:error "reserved_wait_tags[0] = #x00 out of range (must be 0x01..0x3f)" :id "E102" :file "kernel.param")
```

`:file` は誤りのある入力ファイルである。ディレクティブが原因のエラーには、`_vsm` の何行目かを示す `:line` が付く。

```lisp
(:error "@broadcast: size は 1, 2, 4 のいずれかでなければならない" :id "E227" :file "kernel._vsm" :line 5)
```

### E0xx: CLI

入力ファイルの中身を読む前の段階の誤り。`:file` は、読めなかった入力ファイル、または書けなかった出力ファイル。

| ID | 内容 |
|----|------|
| E001 | 出力ファイルが入力ファイルと同じ |
| E002 | 入力ファイルを読めない |
| E003 | 出力ファイルを書けない |
| E004 | 出力ディレクトリ（マルチカーネル `_vsm` の出力先）を作れない |
| E005 | `_vsm` が UTF-8 でない |
| E006 | 位置引数の数が違う |

### E1xx: 配置パラメタとリンク解決

| ID | 内容 |
|----|------|
| E101 | `:reserved_wait_tag(s)` の任意要素が `:send_wait_tag` / `:recv_wait_tag`（slot / boundary_flags / identify）と衝突 |
| E102 | タグの値が範囲外（予約・送信・受信のすべて。許容: `0x01..0x3F`、HW `dmaid_max=63`、dev manual 3.6.13 節で `i00` エラー） |
| E103 | array 内に同じ値が複数（重複） |
| E104 | template 中の `{{wait_tag <N>}}` で N が array 長以上 |
| E105 | 空配列（要素 0 個） |
| E106 | `:reserved_wait_tag`（単数形）に複数値を指定 |
| E107 | `{{wait_tag <N>}}` の構文不正（数字以外 / 余分文字） |
| E108 | 予約 wait tag が 1 個も無い状態で `{{wait_tag}}` / `{{recv_wait}}` の展開に到達した（内部整合性エラー） |
| E110 | L1BM / L2BM の領域を確保できない（空きが足りない、または同時に生存できる領域数を超えた） |
| E111 | `{{lb+N}}` / `{{lc+N}}` の N が確保した領域の外を指している |
| E112 | 生存していない L1BM / L2BM 領域を返そうとした（二重解放） |
| E113 | L1BM / L2BM の領域が返されずに残っている |
| E114 | テンプレートに L1BM / L2BM の番地が直接書かれている |
| E115 | 同じバッファ ID に対して前と食い違う PDM の指定をした |
| E116 | PDM の offset がバッファの範囲外（`@broadcast` では、読む PDM 番地が PDM の大きさ 512 Ki u64 以上、または `:group-dram` のスロットで読む DRAM 番地が DRAM の大きさ 512 Mi u64 以上。`@emit_from` では、書く範囲が PDM の大きさ 512 Ki u64 を超える）|
| E117 | 常駐先を L1BM に確保できない |
| E118 | `.param` に同じスロット ID のエントリが 2 回以上現れた |
| E119 | スロット ID が範囲外（許容: 0 以上 8192 未満）|
| E120 | 1 つの kernel の中で、使う側の待ちのタグ（送信・受信）が 2 か所以上に現れた（その kernel の directive が使うエントリの、使う向きのタグ（読むなら送信、書くなら受信）だけを数える）|
| E121 | `:src-slot` のあるスロットに `:addr` を書いた |
| E122 | `:src-slot` のあるスロットに `:addr-offset` が無い |
| E123 | `:src-slot` の無いスロットに `:addr-offset` を書いた |
| E124 | `:src-slot` が指すスロットが、`.param` の中でそれより前に書かれていない |
| E125 | `:src-slot` が指すスロット自身が `:src-slot` を持つ |
| E126 | `:src-slot` のあるスロットの `:place` が、元のスロットと食い違う |
| E127 | `:addr-offset` が負 |
| E128 | `.param` に `:reserved_wait_tag(s)` が無い |
| E129 | `:slot` エントリに知らないキーワード、またはキーワードでないキーを書いた |
| E130 | `:buffer` のバッファ ID が空 |
| E131 | `:buffer` の `:place` が `:pdm0` でない |
| E132 | `:buffer` の `:addr` が負 |
| E133 | `:buffer` の `:buf_size` が負 |
| E134 | `:buffer` に知らないキーワードを書いた |
| E135 | `:buffer` に `:place` が無い |
| E136 | `:buffer` に `:addr` が無い |
| E137 | `:buffer` に `:buf_size` が無い、または 0 |
| E138 | `:buffer` の `:addr` が 64 の倍数でない |
| E139 | `:buffer` の `:buf_size` が 64 の倍数でない |
| E140 | `:buffer` のバッファ ID が重複している |
| E141 | テンプレートが見つからない、または読めない |
| E142 | `@get_neighbor` / `@get_hypercube_neighbor` のテンプレートの `{{pdm+N}}` が、`:buffer` の範囲の外を指している。`@get_eoshift` は、`<M>` が 10 以上の組で `:buffer` が 576 u64 より小さい |
| E143 | `.param` の最初のエントリが `(:version :0.1)` でない（`.param` が空のときを含む） |
| E144 | 空のエントリ `()` |
| E145 | 値の形が違う（リストのはずがリストでない、整数のはずが整数でない、名前のはずが名前でない） |
| E146 | `:slot` エントリに `:slot` が無い |
| E147 | エントリの ID などの引数、またはキーワードの値が無い |
| E148 | `:slot` / `:identify` / `:boundary_flags` の `:place` が `:pdm0` / `:group-pdm` / `:group-dram` のどれでもない |

E109 は欠番。

### E2xx: ディレクティブ

いずれも `:line` が付く。

| ID | 内容 |
|----|------|
| E201 | `@alloc` が入れ子になっている |
| E202 | `@alloc`: ID がない |
| E203 | `@alloc`: 未知のキーワード |
| E204 | `@alloc`: キーワードより前に数値がある |
| E205 | `@alloc`: レジスタ番号が不正 |
| E206 | `@alloc`: プールに指定したレジスタが多すぎる |
| E207 | `@alloc`: PE メモリのレジスタ番号が偶数でない |
| E208 | `@alloc`: レジスタのキーワードが 1 つもない |
| E209 | `@free`: ID がない |
| E210 | `@free`: ID が対応する `@alloc` と一致しない |
| E211 | 欠番（`@boundary_flags_compute` の廃止による） |
| E212 | 欠番（`@boundary_flags_compute` の廃止による） |
| E213 | 欠番（`@boundary_flags_compute` の廃止による） |
| E214 | 欠番（`@boundary_flags_compute` の廃止による） |
| E215 | `@boundary_flags`: dim が 0 / 1 / 2 以外 |
| E216 | `@boundary_flags`: フラグ名がない |
| E217 | `@boundary_flags`: フラグ名が期待するものと違う |
| E218 | `@reduce`: 引数の形式が違う |
| E219 | `@reduce`: 指定した slot が `.param` にない |
| E220 | `@reduce`: 未知のオペレータ |
| E221 | `@get_neighbor` / `@get_hypercube_neighbor`: 引数の形式が違う |
| E222 | `@get_neighbor` / `@get_hypercube_neighbor`: 対応する `@alloc` の中にない |
| E223 | `@get_neighbor` / `@get_hypercube_neighbor` / `@get_eoshift`: buf_id が `:buffer` にない（`@get_hypercube_neighbor` は deep 9〜11、`@get_eoshift` は `<M>` が 10 以上のとき） |
| E224 | `@get_neighbor`: bf_id が見つからない |
| E225 | `@get_neighbor` / `@get_hypercube_neighbor`: テンプレートの placeholder が解決されない |
| E226 | `@broadcast`: 引数の形式が違う |
| E227 | `@broadcast`: size が 1, 2, 4 のいずれでもない |
| E228 | `@broadcast`: 1 回の消費（size u64）が 64 u64 の境目をまたぐ |
| E229 | `@alias`: 引数の形式が違う |
| E230 | `@alias`: レジスタが `$lrN` でない |
| E231 | `@alias`: boundary flags の ID が重複している |
| E232 | `@assign`: boundary flags の ID が重複している |
| E233 | L1BM / L2BM の領域を確保できない |
| E234 | `@broadcast`: PDM の帳簿を用意できない |
| E235 | `@broadcast`: L1BM に 64 u64 を移動する領域を確保できない |
| E236 | `@identify`: 指定した dim が `:identify` にない |
| E237 | `@boundary_flags`: 指定した ID が `:boundary_flags` にない |
| E238 | `@distribute`: 指定した slot が `.param` にない |
| E239 | `@broadcast`: 指定した slot が `.param` にない |
| E240 | `@collect`: 指定した slot が `.param` にない |
| E241 | `@collect`: size が 1 から 4 でない |
| E242 | `@distribute`: size が 1 から 4 でない |
| E243 | 廃止済みのディレクティブを使用した |
| E244 | `@distribute`: スロットの `:addr` が `mvp` の単位動作（64 u64）の倍数でない |
| E245 | `@collect`: スロットの `:addr` が `mvp` の単位動作（64 u64）の倍数でない |
| E246 | `@reduce`: スロットの `:addr` が `mvr` の単位動作（64 u64）の倍数でない |
| E247 | `@collect_soa2aos`: 引数の形式が違う（レジスタが `$lm` `$ln` `$lr` `$ls` の偶数番号でない場合を含む）|
| E248 | `@collect_soa2aos`: 指定した slot が `.param` にない |
| E249 | `@collect_soa2aos`: スロットの `:addr` が `mvp` の単位動作（64 u64）の倍数でない |
| E250 | `@collect_soa2aos`: データのレジスタと作業の場所の 4 本が重なる |
| E251 | `@distribute_aos2soa`: 引数の形式が違う（レジスタが `$lm` `$ln` `$lr` `$ls` の偶数番号でない場合を含む）|
| E252 | `@distribute_aos2soa`: 指定した slot が `.param` にない |
| E253 | `@distribute_aos2soa`: スロットの `:addr` が `mvp` の単位動作（64 u64）の倍数でない |
| E254 | `@distribute_aos2soa`: データのレジスタと作業の場所の 4 本が重なる |
| E255 | `@identify`: `:identify` エントリの `:addr` が `mvp` の単位動作（64 u64）の倍数でない |
| E256 | `@boundary_flags`: `:boundary_flags` エントリの `:addr` が `mvp` の単位動作（64 u64）の倍数でない |
| E257 | `--strict-broadcast-slot` を付けたとき、`@broadcast` の slot をほかの directive も使っている |
| E258 | `@alloc` の区間の中に、プールを使う directive 以外の directive、または PE 命令を書いた |
| E259 | `@assign` の引数が足りない（`<id> <register>`） |
| E260 | `@assign` のレジスタが `$lrN` でない |
| E261 | `@boundary_flags` の引数が足りない（`<bf_id> <dim> <register> <flag names...>`） |
| E262 | `@boundary_flags` のレジスタが `$lrN` でない |
| E263 | `@boundary_flags` のフラグ名の後ろに余分な語がある |
| E264 | 欠番（`@boundary_flags_compute` の廃止による） |
| E265 | 欠番（`@boundary_flags_compute` の廃止による） |
| E266 | `@collect` の構文の誤り（`<name> to_param <N> size <S> <pe_reg>`） |
| E267 | `@distribute` の構文の誤り（`<name> from_param <N> size <S> <pe_reg>`） |
| E268 | `:place` が `:group-pdm` のスロットを、`@broadcast` / `@distribute` / `@collect` / `@reduce` / `@distribute_aos2soa` / `@collect_soa2aos` に使った。または `:place` が `:group-dram` のスロットを、`@broadcast` 以外のこれらに使った。`:identify` / `:boundary_flags` の `:place` が `:pdm0` 以外のとき、`@identify` / `@boundary_flags` に使った（いずれも対応していない） |
| E269 | `@get_neighbor` / `@get_hypercube_neighbor` / `@get_eoshift` の `<dest>` が GRF0 / GRF1 / LM0 / LM1 のレジスタ（`$lr` / `$ls` / `$lm` / `$ln`）でない |
| E270 | `@get_hypercube_neighbor` の `<deep>` が 0〜11 の範囲の外 |
| E271 | `@emit_from`: 引数の形式が違う |
| E272 | `@emit_from`: `size` が 1〜4 の範囲の外 |
| E273 | `@emit_from`: `to_param` の slot が `.param` にない |
| E274 | `@emit_from`: `pe`（HW PE ID）が 0〜4095 の範囲の外 |
| E275 | `@emit_from`: slot の `:addr` が `mvp` の単位動作（64 u64）の倍数でない |
| E276 | `@emit_from`: slot の `:place` が `:group-pdm` / `:group-dram`（未対応） |
| E277 | `@broadcast`: `:place :group-dram` の slot に `:send_wait_tag` が書いてある（ホストは DRAM へ送れない） |
| E278 | `@get_eoshift`: 引数の形式が違う（`<src_reg> <buf_id> <alloc_id> (<dest> <dir> <N> <M>) ...`）、または `<dir>` が `-` / `+` でない |
| E279 | `@get_eoshift`: `<N>` が 0〜11 の範囲の外 |
| E280 | `@get_eoshift`: `<M>` が `<N> + 1`〜12 の範囲の外 |
| E281 | `@get_eoshift`: `<N>` と `<M>` の組を受け付けない（`<N>` が 0〜5 で、`<M>` が `<N> + 1`〜12 の組だけを受け付ける） |
| E282 | `@get_neighbor` / `@get_eoshift`: 端の指定の誤り（知らないキーワード、`:reg` の後にレジスタが無い、`:reg` のレジスタが GRF0 / GRF1 / LM0 / LM1 でない） |
| E283 | `@get_neighbor` / `@get_eoshift`: 端の指定を 2 つ以上書いた |
| E284 | `@get_neighbor` / `@get_eoshift`: `<src_reg>`、`<dest>`、`:reg` のレジスタの `v` の有無が食い違う |
| E285 | `@get_neighbor` / `@get_eoshift`: `v` を付けたレジスタの 4 u64 の範囲が一部だけ重なる（`<src_reg>`、`<dest>`、`:reg` のうちの 2 つ） |
| E286 | `@get_neighbor` / `@get_eoshift`: `:reg` のレジスタが、`<src_reg>` でないのに `<dest>`（`@get_eoshift` ではどの組の `<dest>` でも）と重なるか、`@alloc` で確保した GRF0 / LM1 のレジスタと重なる |

E233 から E235 は「どのディレクティブのどの行で資源の確保に失敗したか」を示す。
**なぜ失敗したかは E110 / E115 / E116 / E117 が併せて出力する。** 両方を読むこと。

E243 は廃止済みのディレクティブを検出する。対象は `@bind` `@omr_alloc` `@omr_free`
`@stencil` の 4 つで、いずれも名前をディスパッチ表に残したまま、使われたらエラーで
止める。名前を表から消すと未知の `@` 行として素通しし、廃止したことが伝わらないまま
`.vsm` に混入するためである。代替があるものはメッセージで案内する。

| 廃止したディレクティブ | 代替 |
|---|---|
| `@bind` | 無し（0.9.0 で機能ごと廃止）|
| `@omr_alloc` | `@alloc <name> :omr <num...>` |
| `@omr_free` | `@free <name>` |
| `@stencil` | `@get_neighbor` |

### E3xx: マルチカーネル `_vsm`

`@kernel_begin <name>` / `@kernel_end <name>` で区切られた `_vsm`（マルチカーネル `_vsm`）から
kernel ごとのテキストを切り出す処理のエラー。いずれも `:line` が付く。

| ID | 内容 |
|----|------|
| E301 | `@kernel_begin` の入れ子（閉じる前に次の `@kernel_begin` が来た） |
| E302 | `@kernel_begin` に kernel 名が無い |
| E303 | `@kernel_end` の名前が対応する `@kernel_begin` と不一致 |
| E304 | 対応する `@kernel_begin` が無い `@kernel_end` |
| E305 | ファイル終端まで閉じられていない `@kernel_begin` |
| E306 | kernel 名の重複（同じ名前の `@kernel_begin` が 2 回現れた） |
| E307 | kernel 名が C 言語の識別子（`[A-Za-z_][A-Za-z0-9_]*`）でない |
| E308 | kernel 数が上限（65536）を超えた |

kernel 名はそのまま出力ファイル名の一部（`<入力名>-<kernel 名>.vsm`）になるため、
E307 でパス区切り・`..`・空白・制御文字を排除している。切り出しは先頭ヘッダを kernel の
数だけ複製し、kernel 1 個につき出力ファイルを 1 つ作るため、必要なメモリもファイル数も
kernel 数に比例して増える。E308 の上限 65536 はヘッダ 4KiB 換算で約 256MiB に収まる
大きさで、壊れた入力（生成器の暴走・手書きのミス）を受けても即座には破綻しない線引きで
ある。超えたときは黙って切り捨てず、エラーで停止する。

### E4xx: 欠番

E401〜E411 は欠番（`.stparam` の廃止による）。

### E9xx: vsmlink コマンド自身の不具合

入力を直しても解決しない。vsmlink チームに報告する。

| ID | 内容 |
|----|------|
| E901 | エラーで終わったのに、どこもエラーを出していない（エラーを出し忘れた経路がある） |

### S 式として読めなかったとき

`.param` を S 式として読めなかったときは、ID の無い `(:error "cannot read as S-expression" :file "<ファイル名>")` を出す。

### 警告

警告が出てもリンクは継続する。

- W001: 欠番（`:reserved_wait_tag(s)` の省略は E128）
- W002: 出力 vsm の手書き raw 部分に、許可セット外の待ちのタグが直接書かれている。見るのは `wait iXX` と、タグを付けた MV 命令（`mvp` / `mvb` / `mvb2` / `mvb4` / `mvd` / `mvr<op>` / `mvr2<op>` / `mvr4<op>` の `/n<size>iXX`）
- W003: 入力 `_vsm` に L1BM / L2BM の番地（`$lb<N>` / `$lc<N>`）が直接書かれている。vsmlink がディレクティブに配る番地と衝突しうる
- W004: `@distribute_aos2soa` / `@collect_soa2aos` の転置で、`dmwrite` から `dmread` までの待ちが決まっていない
  レジスタの組み合わせを使った。いまはすべての組み合わせで待ちが決まっている（待ち 0）ので、この警告は出ない
- W005: `@emit_from` の slot に `:send_wait_tag` / `:recv_wait_tag` が書いてある。`@emit_from` は待ちのタグをサポートしないので、無視する
- W006: 欠番
{% endraw %}
