# vsmlink コマンド リファレンス (0.10.3)

vsmlink は、`_vsm`（ディレクティブ入りのアセンブラ）を、配置パラメタ（`.param`）に従って、assemble3 にかけられる
`.vsm` に変換する CLI ツールである。

## 全体の流れ

```
_vsm + .param
  ↓ vsmlink（本ツール）
.vsm
  ↓ assemble3
.idma.dat
  ↓ libmnc2（ホストがロードして実行）
MN-Core 2
```

`_vsm` と `.param` は vsmlink の外で作る（上位のツール、例えば MNCL が出力するか、手で書く）。

## 使い方

```bash
vsmlink <input._vsm> <input.param> <output>
assemble3 <output.vsm> --output-file <kernel.idma.dat> --loader
```

| 引数 | 内容 |
|---|---|
| `<input._vsm>` | 入力。書けるディレクティブは [`directives-spec.md`](directives-spec.md) |
| `<input.param>` | 配置パラメタ（PDM の番地・待ちのタグなど）。形式は [`param-spec.md`](param-spec.md) |
| `<output>` | 出力。`_vsm` に kernel が 1 つならファイル、複数ならディレクトリ（下記） |

3 つの引数はすべて必須である。引数の数が違うと E006 になる。

### 1 つの `_vsm` に複数の kernel を書く場合

`@kernel_begin <name>` / `@kernel_end <name>` で区切ると、1 つの `_vsm` に複数の kernel を書ける。
このような `_vsm` を、本書と関連する仕様書では「マルチカーネル `_vsm`」と呼ぶ。
このとき `<output>` はディレクトリ（無ければ作る）で、kernel ごとに
`<入力ファイル名から ._vsm を除いた部分>-<kernel 名>.vsm` を書き出す。

例: `demo._vsm` に `writer` と `reader` があれば、`demo-writer.vsm` と `demo-reader.vsm`。

- 1 つの `.param` を、すべての kernel に使う
- kernel 名の重複や不正な文字、個数の上限などのエラーは E3xx（[`param-spec.md`](param-spec.md)）

### `--no-link`: kernel ごとの `._vsm` を書くだけ

```bash
vsmlink --no-link <input._vsm> <output>
```

マルチカーネル `_vsm` を kernel ごとの `._vsm` に書くだけで、変換（リンク）はしない。`.param` は要らない。`<output>` はディレクトリで、kernel ごとに
`<入力ファイル名から ._vsm を除いた部分>-<kernel 名>._vsm` を書き出す。拡張子の `._vsm` は、まだ
変換していないことを示す。

- `@kernel_begin` / `@kernel_end` の行は、`#@ ` を付けたコメントにして書く（例: `#@ @kernel_begin writer`）。
  通常の vsmlink がこの 2 行を変換したときと同じ形である
- 出力はそのまま通常の vsmlink の入力に使える。出力は 1 つのファイルになり、その中身は、
  元のマルチカーネル `_vsm` を通常の vsmlink で変換したときの、その kernel の `.vsm` と同じになる
- kernel の区切りの検査（E301〜E308）は行う。ほかのディレクティブの検査はしない
- `@kernel_begin` が無ければ、`<output>` はファイルで、入力をそのまま書く

### `--strict-broadcast-slot`: `@broadcast` の slot の共有をエラーにする

```bash
vsmlink --strict-broadcast-slot <input._vsm> <input.param> <output>
```

- `@broadcast` が `from_param N` で使う slot N を、ほかの directive（`@distribute` / `@collect` / `@reduce` /
  `@distribute_aos2soa` / `@collect_soa2aos`）も使っていると、E257 で止まる。`@broadcast` どうしが同じ slot を使うのは
  共有に数えない
- マルチカーネル `_vsm` では、すべての kernel を通して、slot の番号で判定する。止まったときは、どの kernel の出力も書かない
- 付けなければ、共有しても止まらない（[`directives-spec.md`](directives-spec.md) の「`@broadcast` の slot の共有」）

## エラーと警告

エラーと警告は stderr に S 式で出る。エラーがあれば終了コード 1 で終わる。このとき出力ファイルは
空のまま作られることがあるので、成否は終了コードで分かる。警告だけなら変換を続ける。

```lisp
(:error "@broadcast _a: slot 8 not found in .param" :id "E239" :file "kernel._vsm" :line 3)
(:error ":slot 48: :addr-offset without :src-slot" :id "E123" :file "kernel.param")
(:warning "unknown wait tag i55 in passthrough VSM (not in :reserved_wait_tags / :send_wait_tag / :recv_wait_tag)" :id "W002" :file "kernel._vsm")
```

- `:id` で種類を見分ける。すべての ID の意味は [`param-spec.md`](param-spec.md) の一覧にある
- `:file` は、誤りのある入力ファイル。コマンド行に渡したとおりの名前で出る。`.param` の誤りはそのファイル、
  `_vsm` を変換している間の誤り（テンプレートの展開の誤りを含む）は `_vsm` になる
- `:line` は `_vsm` のファイルの行番号。マルチカーネル `_vsm` でも、kernel の中で数えた番号ではなく、ファイルの行番号である（`.param` のエラーなど、行の無いものもある）
- エラーと警告は、1 つずつ S 式の 1 行で出る。1 つの誤りから、原因と場所を示す複数のエラーが出ることがある。S 式でない行は出ない
- `.param` を S 式として読めなかったときは、ID の無い `(:error "cannot read as S-expression" :file "...")` が出る
- E901 は vsmlink コマンド自身の不具合（エラーを出し忘れた経路）を表す。出たら vsmlink チームに報告する


---

## 使うときに気をつけること

- **`@broadcast` の続きは、kernel を実行するたびに巻き戻る。** 同じスロットの `@broadcast` を繰り返すと、
  続きのデータを読む。ただし、読む番地は vsmlink が link のときに命令に埋め込むので、続きが
  つながるのは 1 つの kernel の中だけである。kernel を実行するたびに、スロットの `:addr`（`:src-slot` の
  スロットでは、元のスロットの `:addr` + `:addr-offset`）から読み直す。kernel を分けて配列の続きを読むには、
  kernel ごとに、その kernel が読む位置を指すスロットを用意する。詳細は [`directives-spec.md`](directives-spec.md)
  の「@broadcast」
- **`@distribute_aos2soa` と `@collect_soa2aos` は対で使う。** PDM の上のデータ 1 個分ずつの並びを、配る側の
  `@distribute_aos2soa` で PE に置き、書く側の `@collect_soa2aos` で同じ並びに戻す。逆向きの directive
  （`@collect_aos2soa`、`@distribute_soa2aos`）は無く、この 2 つで足りる。理由と並びの定義は
  [`directives-spec.md`](directives-spec.md) の「@distribute_aos2soa / @collect_soa2aos」
- **`@get_neighbor` と `@get_eoshift` は、区切りの中の隣だけを扱う。** `@get_neighbor` の区切りは 4096 個（1 チップ）である。
  区切りの外から値を受け取ることになる端の PE の値は、端の指定（`:keep`、`:periodic`、`:reg <register>`）で決まり、指定しなければ
  決まっていない。データの端かどうかは見ない。詳細は [`directives-spec.md`](directives-spec.md) の「端の指定」
- **`@emit_from` は、デバッグ用である。** 呼ぶたびに PDM の slot の続きへ書き、その先にある別の slot（kernel 引数の配列や、
  `@collect` の書き先など）の領域に書き込んでも、エラーにならない。
  書き込みの完了は、ホストにも kernel にも通知されない。
  同じ kernel を繰り返し実行すると、毎回同じ番地に書く。詳細は [`directives-spec.md`](directives-spec.md) の「@emit_from」
- **`@emit_from` の `pe <n>` に書く HW PE ID は、MN-Core 2 の用語ではなく、vsmlink コマンドの文書が決めた語である。**
  MN-Core 2 の `$l2bid` / `$l1bid` / `$mabid` / `$subpeid` との 1 対 1 の対応表は [`hw-pe-id.md`](hw-pe-id.md)
- **`@identify` が配るのは、ホストが PDM に置いた値である。** vsmlink コマンドは PE ID の値を作らない。
  `.param` の `:identify` の `:addr` から 4096 u64 を読み、PE 1 つにつき 1 u64 を配る（1 次元、4096 個の固定）。
  第 3 引数（要素数）は使わない。書き込み先は `$lr` 系と決めているが、vsmlink コマンドは `$lr` かどうかを検査しない。
  詳細は [`directives-spec.md`](directives-spec.md) の「@identify」

---

## 変更履歴

- 0.10.3:
  - CLI の引数を 4 個から 3 個に変えた: `vsmlink <input._vsm> <input.param> <output>`。`.stparam` を取らない。4 個渡すと E006（後方互換なし）
  - `.stparam` を廃止した。`:boundary` などの指定は読まない。E401〜E411 は欠番（後方互換なし）
  - `@get_neighbor` の端の PE の値の決め方を変えた。これまでは `.stparam` の `:boundary` で決まり、書かなければ自分の値だった。これからは端の指定で決まり、書かなければ値は決まっていない。これまでの動作は、`:boundary` が無いか `:clamp` なら `:reg <src_reg>`、`:periodic` なら `:periodic` を書けば同じになる（結果が変わる。後方互換なし）
  - `@get_neighbor` の端の PE を、`@boundary_flags` の `data_edge` に加えて `cross_chip` のビットでも決めるようにした。cross_chip の PE も端の指定の対象になり、`:periodic` は cross_chip の PE どうしをつなぐ。data_edge の PE の `:periodic` の値は約束しない（結果が変わる。後方互換なし）
  - `@get_neighbor` と `@get_eoshift` に、端の指定（`:keep`、`:periodic`、`:reg <register>`）を加えた。端の指定の誤りは E282、2 つ以上書くと E283。`:reg` のレジスタが `<src_reg>` でないのに `<dest>` か `@alloc` のレジスタと重なると E286（`:reg <src_reg>` は、src と dest が同じレジスタでも通る）
  - `@get_neighbor` と `@get_eoshift` で、`v` を付けたレジスタ（`$lm0v` など）で PE あたり 4 u64 を受け取れるようにした。`v` の有無が食い違うと E284、4 u64 の範囲が一部だけ重なるレジスタを書くと E285
  - `.param` のスロット ID の上限を 4096 から 8192 に広げた（0 以上 8192 未満。これまで E119 で止まった 4096〜8191 が通る）
  - `@boundary_flags_compute` を廃止した。使うと E243。E211〜E214、E264、E265 は欠番（後方互換なし）
  - examples の 06-jacobi2d、07-box2d、08-himeno3d（`.stparam` を読めることを確かめるもの）を外した
  - `@get_hypercube_neighbor`（deep 9〜11）、`@get_eoshift`（`<M>` が 10 以上）、`@emit_from` の展開で、L1BM → L2BM 転送から、その L2BM を読む MV 命令までの待ちを 1 ステップから 2 ステップにした（出力の `.vsm` が変わる）
  - `@reduce` の展開の待ちを変えた（PE から L1BM の後を 24 ステップから 3 ステップに、L1BM から L2BM の後を 24 ステップから 2 ステップに。出力の `.vsm` が変わる）
  - `@distribute`、`@identify`、`@broadcast`、`@boundary_flags`、`@collect`、`@collect_soa2aos`、`@reduce`、`@get_neighbor`、`@get_eoshift`、`@get_hypercube_neighbor`、`@emit_from` の展開と、`@distribute` などの直後に置く待ちの nop を減らした（出力の `.vsm` が変わる）
  - `@get_neighbor` の `<dest>` を GRF1 / LM0 / LM1 にしたときの値の誤りを直した。これまではマージの上書きが全 PE に書き込まれ、自分自身の値などになっていた（結果が変わる）
  - `@get_neighbor` の `<dest>` が GRF0 / GRF1 / LM0 / LM1 のレジスタでなければエラーにする（E269）。これまでは受け付けていた（後方互換なし）
  - CLI のオプションの改名: `--split-only` → `--no-link`。`--split-only` は受け付けない（後方互換なし）
  - `.param` の `:reserved_wait_tag(s)` の省略をエラーにする（E128）。これまでは W001 を出して `#x3f` で続けていた（後方互換なし）
  - `.param` の `:slot` エントリに、知らないキーワード（打ち間違いを含む）やキーワードでないキーを書くとエラーにする（E129）。これまでは読み飛ばしていた（後方互換なし）
  - `_vsm` が UTF-8 でなければエラーにする（E005）。これまでは置換文字に置き換えて続けていた（後方互換なし）
  - `@identify` / `@boundary_flags` が使う `.param` の `:addr` が 64 の倍数でなければエラーにする（E255 / E256）
  - `_vsm` とテンプレートの、4095 バイトを超える行を切り詰めない。そうした行があると、出力の `.vsm` が変わる（後方互換なし）
  - `--no-link` の出力で、`@kernel_begin` / `@kernel_end` の行を `#@ ` を付けたコメントにする。出力をそのまま vsmlink に渡すと、1 つのファイルが出力される（後方互換なし）
  - エラーと警告を、すべて S 式で出す。これまで `error: ...` の 1 行で出ていたものに ID を付けた（CLI は E001〜E006、`.param` の `:buffer` は E131〜E140、テンプレートは E141 / E142）（後方互換なし）
  - これまで何も出さずに止まっていた誤りに、S 式のエラーを出す（`.param` は E143〜E148、directive は E258〜E268、vsmlink コマンド自身の不具合は E901。`.param` を S 式として読めないときは ID の無いエラー）
  - エラーのときに最後に出していた `error: link failed: ...` / `error: <file>: parse error ...` の 1 行を出さない（後方互換なし）
  - エラーと警告の S 式に、誤りのある入力ファイルを示す `:file` を足した（`:id` と `:line` の間）（後方互換なし）
  - マルチカーネル `_vsm` のエラーの `:line` を、kernel の中で数えた番号から、`_vsm` のファイルの行番号に変えた。E257 のメッセージの `line N` も同じ（後方互換なし）
  - 位置引数の数が違うときは、使い方の文を出さずに E006 を出す。`--help` の文は stdout に出す（これまでは stderr）
  - `.param` の `()` を、空のリストとして扱う（空の `.param` は E143、空のエントリは E144 になる）
  - E207 のエラーの文を `must be even (longword number)` から `must be even (u64 aligned)` に変えた
  - W002 が、`mvp` だけでなく、タグを付けたすべての MV 命令（`mvb` / `mvd` / `mvr<op>` など）の待ちのタグを見る。警告の文の `unknown mvp wait tag` を `unknown MV wait tag` に変えた
  - `directives-spec.md` に、`_vsm` を書く側は directive の前後の待ちを考えなくてよいことを明記した。これまでの仕様書は、`@distribute` / `@distribute_aos2soa` の直後に書く側が待ちを置くと書いていた
  - `--strict-broadcast-slot` の追加。付けると、`@broadcast` の slot をほかの directive と共有しているときに E257 で止まる
  - `@broadcast` の slot を、ほかの directive（`@distribute` / `@collect` / `@reduce` など）と共有してよい。0.10.2 の仕様書の「エラーとする」を取り消す
  - `@get_hypercube_neighbor` の追加。PE の番号 j の相手が j XOR 2^deep の PE（deep 0〜11）の値を受け取る。`<deep>` が範囲外なら E270
  - `@get_eoshift` の追加。位置の差が 2^N（N = 0〜5）の PE の値を、2^M 個ごとの区切り（M = N+1〜12）をまたがずに受け取る。`(<dest> <dir> <N> <M>)` の組を 1 回の呼び出しに複数書ける。範囲の外は E278〜E281。`<M>` が 10 以上の組は `.param` の `:buffer`（576 u64 以上）を使う
  - `@emit_from` の追加（デバッグ用）。HW PE ID が `<n>` の PE の値を、呼ぶたびに PDM の slot の続きへ書く（定義と対応表は [`hw-pe-id.md`](hw-pe-id.md)）。ホストと同期しない
  - `.param` の `:slot` の `:place :group-dram` の追加。`@broadcast` が DRAM から読める（各 PE は自分のグループの DRAM の値を受け取る）。`:group-dram` の slot を `@broadcast` 以外の directive に使うと E268。`:group-dram` の slot に `:send_wait_tag` を書くと E277
  - `param-spec.md` に、`:place :pdm0` がグループ 0 の PDM（dev manual の `$p<addr>@0`）であることを明記した
  - `:place` が `:pdm0` でない slot（`:identify`、`:boundary_flags` を含む）を `@reduce` / `@identify` / `@boundary_flags` に使うと E268 で止める。これまでは何も出さずに DRAM の番地の形で展開していた（後方互換なし）
- 0.10.2:
  - `@distribute_aos2soa` / `@collect_soa2aos` の追加。転置（`dmwrite` / `dmread`）を含む `@distribute` /
    `@collect` の対で、PDM の上でデータ 1 個分（4 個の値）ずつ番号の順に並んだものを読み書きする
  - `@broadcast` を、スロットごとの stream として正しく実装した。このバージョンから使える
  - スロット ID の範囲と一意性の検査（E118 / E119）
  - `@distribute` / `@collect` / `@reduce` の `:addr` のバウンダリの検査（E244 / E245 / E246）
  - `@broadcast` の size を 1, 2, 4 に限定（E227）
  - 1 つの kernel の中での `:send_wait_tag` / `:recv_wait_tag` の重複の検査（E120）
  - `:send_wait_tag` / `:recv_wait_tag` の値の範囲の検査（E102）
  - `.param` の `:send_wait_tag` / `:recv_wait_tag` の意味の変更（後方互換なし）
  - `.param` の `:src-slot` のスロットの位置を、`:addr` ではなく `:addr-offset` で書く形に変更（後方互換なし）
- 0.10.1:
  - `.param` の `:src-slot` の導入
  - 廃止済みの `@bind` / `@omr_alloc` / `@omr_free` / `@stencil` を E243 でエラーにした
- 0.10.0:
  - vsmlink を Rust で最初から作り直した
  - kernel 分割: `@kernel_begin` / `@kernel_end` の導入
  - `--split-only` の導入
  - `lib/libvsmlink.a` の配布をやめた（CLI だけを提供する）
- 0.9.0:
  - `@get_neighbor` の第 3 引数を、バッファ名から PE レジスタに変更（後方互換なし）
  - `@broadcast` を全 PE に同じ値を配る形に変更し、ホストが PDM に置くデータの並びを変更（後方互換なし）
  - `@bind` と `@assign <buffer_id> from_param ...` の廃止（後方互換なし）
  - `@distribute` / `@collect` の size 1〜4 の導入
  - `@distribute` / `@identify` / `@boundary_flags` / `@get_neighbor` が、対象以外の隣接 PE レジスタを壊す不具合の修正
  - ディレクティブのエラー出力を S 式に変更
- 0.8.1:
  - `@broadcast` の導入
  - `:send_wait_tag` / `:recv_wait_tag` を省略したときの動作の変更
  - example 25 の追加
- 0.8.0:
  - `examples/23-identify` を `@identify` の例 1 本にした
- 0.7.2:
  - CLI を 4 つの位置引数に変更。`-s <stparam>` の廃止（後方互換なし）
  - `.param` の `:reserved_wait_tag` / `:reserved_wait_tags` の導入
- 0.7.1:
  - `@reduce` を PDM まで縮約する形に変更
  - `vsmlink --version` / `--help` の導入
  - example 22 の追加
- 0.7.0:
  - `.param` のキーワードの改名: `:dmaid` → `:send_wait_tag`、`:mvtag` → `:recv_wait_tag`（後方互換なし）
  - `@get_neighbor` の構文の変更（後方互換なし）
  - `@stencil` の廃止（後方互換なし）
  - cross group の廃止。boundary_flags を 12 bit から 10 bit に変更（後方互換なし）
  - `@reduce` の導入
- 0.6.4:
  - C API の変更（`vsmlink_parse_param_file`、`vsmlink_link`）
  - `.param` の `:identify` の ID を整数に変更
- 0.6.3:
  - `.param` の形式を v0.1 に変更（後方互換なし）
  - `--boundary_flags=distribute` の追加
  - `@omr_alloc` の無い `@stencil` をエラーにした
  - example 20 の追加
- 0.6.2: 「方式 A/B」を `--boundary_flags=compute` / `--boundary_flags=distribute` に統一
- 0.6.1: `@identify`、`@access_pattern` 追加。`@stencil` 5 引数構文確定
- 0.6.0: `@boundary_flags`、`@omr_alloc`、`@omr_free` 追加
- 0.5.0: `@stencil` 1D 袖交換実装（cross L2B/L1B/MAB 対応）
- 0.4.0: `@distribute`、`@collect` 実装
- 0.1.0: 初版
