# vsmlink コマンド リファレンス (0.10.2)

vsmlink は、`_vsm`（ディレクティブ入りのアセンブラ）を、配置パラメタ（`.param`）と構造パラメタ
（`.stparam`）に従って、assemble3 にかけられる `.vsm` に変換する CLI ツールである。

## 全体の流れ

```
_vsm + .param + .stparam
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
vsmlink <input._vsm> <input.param> <input.stparam> <output>
assemble3 <output.vsm> --output-file <kernel.idma.dat> --loader
```

| 引数 | 内容 |
|---|---|
| `<input._vsm>` | 入力。書けるディレクティブは [`directives-spec.md`](directives-spec.md) |
| `<input.param>` | 配置パラメタ（PDM の番地・待ちのタグなど）。形式は [`param-spec.md`](param-spec.md) |
| `<input.stparam>` | 構造パラメタ（次元・PE の形など）。形式は [`stparam-spec.md`](stparam-spec.md) |
| `<output>` | 出力。`_vsm` に kernel が 1 つならファイル、複数ならディレクトリ（下記） |

4 つの引数はすべて必須である。

### 1 つの `_vsm` に複数の kernel を書く場合

`@kernel_begin <name>` / `@kernel_end <name>` で区切ると、1 つの `_vsm` に複数の kernel を書ける。
このとき `<output>` はディレクトリ（無ければ作る）で、kernel ごとに
`<入力ファイル名から ._vsm を除いた部分>-<kernel 名>.vsm` を書き出す。

例: `demo._vsm` に `writer` と `reader` があれば、`demo-writer.vsm` と `demo-reader.vsm`。

- 1 つの `.param` を、すべての kernel に使う
- kernel 名の重複や不正な文字、個数の上限などのエラーは E3xx（[`param-spec.md`](param-spec.md)）

### `--split-only`: kernel ごとに分けるだけ

```bash
vsmlink --split-only <input._vsm> <output>
```

kernel ごとに分けるだけで、変換はしない。`.param` と `.stparam` は要らない。出力は、まだ変換して
いないことを示す `._vsm` で、そのまま通常の vsmlink の入力に使える。

- kernel の区切りの検査（E301〜E308）は行う。ほかのディレクティブの検査はしない
- `@kernel_begin` が無ければ、`<output>` はファイルで、入力をそのまま書く

## エラーと警告

エラーと警告は stderr に S 式で出る。エラーがあれば終了コード 1 で終わる。このとき出力ファイルは
空のまま作られることがあるので、成否は終了コードで判定すること。警告だけなら変換を続ける。

```lisp
(:error "@broadcast _a: slot 8 not found in .param" :id "E239" :line 3)
(:warning "no :reserved_wait_tag(s) specified, using default #x3f" :id "W001")
```

- `:id` で種類を見分ける。すべての ID の意味は [`param-spec.md`](param-spec.md) の一覧にある
- `:line` は `_vsm` の行番号（`.param` のエラーなど、行の無いものもある）
- エラーのときは、最後に `error: link failed: ...` の 1 行が続く
- `.stparam` に `:boundary` を書くと、`warning: <file>: :boundary is parsed but not used in link resolution`
  が出る（`:boundary` は読むが、変換には使わない）


---

## 使うときに気をつけること

- **`@broadcast` の続きは、kernel を実行するたびに巻き戻る。** 同じスロットの `@broadcast` を繰り返すと、
  続きのデータを読む（stream）。ただし、読む番地は vsmlink が link のときに命令に埋め込むので、続きが
  つながるのは 1 つの kernel の中だけである。kernel を実行するたびに、スロットの `:addr`（`:src-slot` の
  スロットでは、元のスロットの `:addr` + `:addr-offset`）から読み直す。kernel を分けて配列の続きを読むには、
  kernel ごとに、その kernel が読む位置を指すスロットを用意する。詳細は [`directives-spec.md`](directives-spec.md)
  の「@broadcast」
- **`@distribute_aos2soa` と `@collect_soa2aos` は対で使う。** PDM の上のデータ 1 個分ずつの並びを、配る側の
  `@distribute_aos2soa` で PE に置き、書く側の `@collect_soa2aos` で同じ並びに戻す。逆向きの directive
  （`@collect_aos2soa`、`@distribute_soa2aos`）は無く、この 2 つで足りる。理由と並びの定義は
  [`directives-spec.md`](directives-spec.md) の「@distribute_aos2soa / @collect_soa2aos」

---

## 将来の拡張予定

| 項目 | 説明 |
|------|------|
| 2D/3D @get_neighbor | 十字・箱パターン。PE + MAB + L1B + L2B の多次元展開 |
| @identify 多次元 | 複数次元（x/y/z）の同時展開 |
| @access_pattern 活用 | テンプレート選択の自動化 |

---

## 変更履歴

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
