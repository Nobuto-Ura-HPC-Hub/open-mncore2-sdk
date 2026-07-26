{% raw %}
# .param 仕様

配置パラメタファイル。スロット ID とメモリ配置を対応づける。S 式形式。`;` 以降は行コメント。

## 基本構文

```lisp
((:version :0.1)
 (:slot 8  :place :pdm0 :addr 0     :send_wait_tag #x10)
 (:slot 16 :place :pdm0 :addr 4096  :send_wait_tag #x10 :recv_wait_tag #x1e)
 (:slot 24 :place :pdm0 :addr 131072 :send_wait_tag #x10))
```

## :slot エントリ

| フィールド | 値 | 説明 | 必須 |
|-----------|-----|------|------|
| `:slot` | 整数 | スロット ID（`from_param N` / `to_param N` の N） | 必須 |
| `:place` | `:pdm0` | 配置先（現状は `:pdm0` のみ） | 任意（デフォルト `:pdm0`） |
| `:addr` | 整数 | 開始アドレス（u64 単位） | 必須 |
| `:send_wait_tag` | `#xNN` または整数 | send 方向 send wait タグ（Host → PDM）。`@distribute` で使用 | `@distribute` 使用時は必須 |
| `:recv_wait_tag` | `#xNN` または整数 | recv 方向 recv wait タグ（PE → PDM）。`@collect` で使用。host は `mnc2_recv(recv_wait_tag=...)` で待つ | `@collect` 使用時は必須 |

`:send_wait_tag` と `:recv_wait_tag` は同一スロットに両方指定可能。

### スロットに大きさは書かない

**`:slot` は開始アドレスだけを持ち、大きさを持たない。** 何 u64 占めるかは、そのスロットを
使う directive で決まる（`@distribute` と `@collect` は 4096 u64、`@broadcast` と
`@reduce` は 64 u64）。

大きさを `.param` に書かせない理由は、**書く側が判断できないため**である。OpenCL の
カーネル引数に大きさが書かれておらず、`@broadcast` は stream なので有効なデータがどこまで
続いているかも分からない。

**代わりに vsmlink が、生成した `.vsm` に使用範囲をメタデータとして出す。**

```
# (:pdm :directive "@reduce" :id "_sum" :slot 11 :place :pdm0 :addr 49152 :size 64 :access :write)
```

ホストはこれと自分の配置を突き合わせて、スロットどうしの重なりや用意漏れを確認できる。
詳細は [directives-spec.md](directives-spec.md) の「PDM の使用範囲とメタデータ」を参照。

## :boundary_flags エントリ

`@boundary_flags` ディレクティブで使用する PDM アドレスと send wait タグを指定する。

```lisp
((:version :0.1)
 (:slot 8  :place :pdm0 :addr 0 :send_wait_tag #x10)
 (:boundary_flags bf1 :place :pdm0 :addr 8192 :send_wait_tag #x10))
```

| フィールド | 値 | 説明 | 必須 |
|-----------|-----|------|------|
| `:boundary_flags` | 文字列 | bf ID（`@boundary_flags` の第 1 引数と一致） | 必須 |
| `:place` | `:pdm0` | 配置先 | 任意 |
| `:addr` | 整数 | 開始アドレス（u64 単位） | 必須 |
| `:send_wait_tag` | `#xNN` または整数 | send wait タグ | PHASE_INIT で使用時は必須 |

## :buffer エントリ

`@get_neighbor` の cross L2B 袖交換で使用する PDM0 上のバッファを指定する。

```lisp
((:version :0.1)
 (:slot 8  :place :pdm0 :addr 0 :send_wait_tag #x10)
 (:buffer nbuf0 :place :pdm0 :addr 16384 :buf_size 4160))
```

| フィールド | 値 | 説明 | 必須 |
|-----------|-----|------|------|
| `:buffer` | 文字列 | バッファ ID（`@get_neighbor` の第 5 引数と一致） | 必須 |
| `:place` | `:pdm0` | 配置先 | 任意 |
| `:addr` | 整数 | 開始アドレス（u64 単位、64 の倍数） | 必須 |
| `:buf_size` | 整数 | バッファサイズ（u64 単位、64 の倍数） | 必須 |

### :buf_size の算出

`:buf_size` はテンプレートの内部レイアウトに依存する。現在の 1D テンプレートでは
**4160 u64** 固定。

`:addr` と `:buf_size` はともに 64 の倍数でなければならない（mvp 転送単位）。

**算出の根拠は `docs/buffer-design.md`（PDM0 バッファの内部設計）にある。** 2D / 3D の場合や
`.stparam` の `:data_size` との関係もそちらを参照。

## :identify エントリ

`@identify` ディレクティブの PDM アドレスと send wait タグを指定する。

```lisp
((:version :0.1)
 (:identify 0 :place :pdm0 :addr 0 :send_wait_tag #x10)
 (:slot 24 :place :pdm0 :addr 131072 :send_wait_tag #x10))
```

| フィールド | 値 | 説明 | 必須 |
|-----------|-----|------|------|
| `:identify` | 整数 | 次元インデックス（0=x, 1=y, 2=z） | 必須 |
| `:place` | `:pdm0` | 配置先 | 任意 |
| `:addr` | 整数 | 開始アドレス（u64 単位） | 必須 |
| `:send_wait_tag` | `#xNN` または整数 | send wait タグ | PHASE_INIT で使用時は必須 |

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

**省略時のデフォルト**: 1 要素 `[#x3f]` + warning W001。

### 制約・検証

vsmlink がリンク時に検査する。違反時はエラー終了（exit code 1）。
該当するエラー ID は E101 から E108。内容は末尾の「エラー・警告一覧」を参照。

### 自然な上限

- 値域 `0x01..0x3F` = 63 値、重複禁止 → 最大 63 要素
- 64 個目を指定すれば必ず E102 か E103 で蹴られる

### `(:range a b)` 構文

将来予定（S 式 reader 整備後）。現状は flat な値列のみ受付。

## 共通事項

| キーワード | 値 | 説明 |
|-----------|-----|------|
| `:version` | `:0.1` | **必須**（先頭エントリ） |

**注意**: `addr` は u64 単位。host-dma の `mnc2_send/recv` に渡す `pdm_offset` はバイト単位なので `addr * 8` の変換が必要。

## エラー・警告一覧

エラーは S 式で stderr に出力し、exit code 1 で終了する。

```lisp
(:error "reserved_wait_tags[0] = #x00 out of range (must be 0x01..0x3f)" :id "E102")
```

ディレクティブが原因のエラーには、`_vsm` の何行目かを示す `:line` が付く。

```lisp
(:error "@broadcast: size must be 1..4" :id "E227" :line 5)
```

### E1xx: 配置パラメタとリンク解決

| ID | 内容 |
|----|------|
| E101 | `:reserved_wait_tag(s)` の任意要素が `:send_wait_tag` / `:recv_wait_tag`（slot / boundary_flags / identify）と衝突 |
| E102 | 範囲外（許容: `0x01..0x3F`、HW `dmaid_max=63`、dev manual 3.6.13 節で `i00` エラー） |
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
| E116 | PDM の offset がバッファの範囲外 |
| E117 | 常駐先を L1BM に確保できない |

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
| E211 | `@boundary_flags_compute`: 引数の形式が違う |
| E212 | `@boundary_flags_compute`: dim が 0 / 1 / 2 以外 |
| E213 | `@boundary_flags_compute`: 対応する `@alloc` の中にない |
| E214 | `@boundary_flags_compute`: テンプレートの placeholder が解決されない |
| E215 | `@boundary_flags`: dim が 0 / 1 / 2 以外 |
| E216 | `@boundary_flags`: フラグ名がない |
| E217 | `@boundary_flags`: フラグ名が期待するものと違う |
| E218 | `@reduce`: 引数の形式が違う |
| E219 | `@reduce`: 指定した slot が `.param` にない |
| E220 | `@reduce`: 未知のオペレータ |
| E221 | `@get_neighbor`: 引数の形式が違う |
| E222 | `@get_neighbor`: 対応する `@alloc` の中にない |
| E223 | `@get_neighbor`: buf_id が `:buffer` にない |
| E224 | `@get_neighbor`: bf_id が見つからない |
| E225 | `@get_neighbor`: テンプレートの placeholder が解決されない |
| E226 | `@broadcast`: 引数の形式が違う |
| E227 | `@broadcast`: size が 1..4 でない |
| E228 | `@broadcast`: PDM 番地から size u64 が区画に収まらない |
| E229 | `@alias`: 引数の形式が違う |
| E230 | `@alias`: レジスタが `$lrN` でない |
| E231 | `@alias`: boundary flags の ID が重複している |
| E232 | `@assign`: boundary flags の ID が重複している |
| E233 | L1BM / L2BM の領域を確保できない |
| E234 | `@broadcast`: PDM の帳簿を用意できない |
| E235 | `@broadcast`: L1BM に区画を載せられない |
| E236 | `@identify`: 指定した dim が `:identify` にない |
| E237 | `@boundary_flags`: 指定した ID が `:boundary_flags` にない |
| E238 | `@distribute`: 指定した slot が `.param` にない |
| E239 | `@broadcast`: 指定した slot が `.param` にない |
| E240 | `@collect`: 指定した slot が `.param` にない |

E233 から E235 は「どのディレクティブのどの行で資源の確保に失敗したか」を示す。
**なぜ失敗したかは E110 / E115 / E116 / E117 が併せて出力する。** 両方を読むこと。

### 警告

警告が出てもリンクは継続する。

- W001: `:reserved_wait_tag(s)` 未指定。default `#x3f` を使用する。
  **この既定値は単一の値であって、ビットマスクではない。** `(:reserved_wait_tags #x3f)` と
  書いたのと同じ意味で、予約されるタグは 1 個だけである。許容範囲 `0x01..0x3F` の
  最大値を選んである
- W002: 出力 vsm の手書き raw 部分に許可セット外の hardcode tag を検出
- W003: 入力 `_vsm` に L1BM / L2BM の番地（`$lb<N>` / `$lc<N>`）が直接書かれている。vsmlink がディレクティブに配る番地と衝突しうる
{% endraw %}
