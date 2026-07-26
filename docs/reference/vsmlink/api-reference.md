# vsm-linker リファレンス (0.9.0)

`_vsm` + 配置パラメタ（`.param`）を入力として、リンク解決した `.vsm` を出力する CLI ツール。

## 層構造における位置

```
OpenACC / FORTRAN / C アプリケーション
  ↓
MNCL (_vsm 生成、.param 生成)
  ↓ _vsm + .param [+ .stparam]
vsmlink (本ツール) ← ここ
  ↓ .vsm
assemble3
  ↓ .idma.dat / .asm
libmnc2 (host-dma)
  ↓
libgpfn3 → Linux カーネルドライバ → MN-Core 2
```

## CLI 使い方

```bash
vsmlink <input._vsm> <input.param> <input.stparam> <output.vsm>
```

引数は 4 つすべて必須（positional）。`.stparam` は構造パラメタ（`:ndim` / `:pe_shape` / `:pe_local` / `:data_layout` 等）を含む S 式ファイル。詳細は [`stparam-spec.md`](stparam-spec.md) を参照。

### 典型的なワークフロー

```bash
# リンク解決
vsmlink kernel._vsm kernel.param kernel.stparam kernel.vsm

# アセンブル（assemble3 は別ステップ）
assemble3 kernel.vsm --output-file kernel.idma.dat --loader
```

---

## 入力ファイル形式

| ファイル | 仕様 |
|---------|------|
| `.param`（配置パラメタ） | [`param-spec.md`](param-spec.md) |
| `.stparam`（構造パラメタ） | [`stparam-spec.md`](stparam-spec.md) |

---

## _vsm ディレクティブ

[`directives-spec.md`](directives-spec.md) を参照。

---

## エラー出力

エラーは `stderr` に出力し、exit code 1 で終了する。

| メッセージ | 原因 |
|-----------|------|
| `error: <file>: parse error (_vsm or .param)` | `.param` / `_vsm` の構文エラー |
| `error: <file>: undefined slot reference` | `from_param N` / `to_param N` の N が `.param` にない |
| `error: link failed: ...` | リンク解決失敗 |
| `warning: <file>: :boundary is parsed but not used in link resolution` | `.stparam` の `:boundary` は現在未使用 |

---

## 将来の拡張予定

| 項目 | 説明 |
|------|------|
| 2D/3D @get_neighbor | 十字・箱パターン。PE + MAB + L1B + L2B の多次元展開 |
| @identify 多次元 | 複数次元（x/y/z）の同時展開 |
| @access_pattern 活用 | テンプレート選択の自動化 |

---

## 変更履歴

- 0.9.0: **後方互換なし**。 `@get_neighbor` の第 3 引数をバッファ名から PE レジスタ (読み出し元) に変更。 `@broadcast` を放送方式に変更し、 size 1..4 が有効になった (ホストは配布先 PDM 領域の先頭 N u64 に配りたい値を置く。 従来の「先頭 16 u64 を同値で埋める」規約は廃止)。 `@bind` と `@assign <buffer_id> from_param ...` 形式を廃止 (`@assign <bf_id> <register>` は非推奨として残存)。 `@distribute` / `@identify` / `@boundary_flags` / `@get_neighbor` が、 展開時に対象レジスタ以外の隣接 PE レジスタを破壊しないよう修正。 `@distribute` / `@collect` の size が 1 から 4 で有効になった (成分ごとに PDM 上へ 4096 u64 ずつ並ぶ。 size 1 の出力は従来と同一)。 ディレクティブのエラー出力を S 式に統一し、 エラー ID (E2nn) と行番号を付けた。 `.param` に無い slot を参照したときのエラーに、 ディレクティブ名と slot 番号が付いた。 3D N 体シミュレーションの example (`28-nbody-3d`) を追加 (distribute / broadcast / collect と rsqrt で、 ループの無いハード上の全対全 N 体を unroll して回す)。 2D の総和 example を `26-broadcast-accumulate-2d` に改名。 broadcast と collect を組み合わせた example (`29-broadcast-collect`) と、 collect の SoA 出力を device 上で AoS 配置に並べ替える example (`30-soa2aos`) を追加。
- 0.8.1: `@broadcast` directive 新規追加 (PDM 1 u64 を 16 MAB 放送 + MAB 内 4 PE 分配)。 `.param` の `:send_wait_tag` 省略時は wait 命令を生成しない仕様に緩和、 `:recv_wait_tag` 省略時は内部 wait tag にフォールバック。 example 25 (`@broadcast` の HW 特性 demo) 追加。
- 0.8.0: `examples/23-identify` を `@identify` directive サンプル 1 本にシンプル化。
- 0.7.2: CLI を 4 positional 引数必須に変更（旧 `-s <stparam>` 廃止、 **後方互換なし**）。 `.param` に `:reserved_wait_tag` / `:reserved_wait_tags` を追加。
- 0.7.1: `@reduce` を全段縮約（L1BM→L2BM→PDM）に拡張。`examples/identify/` を `tests/examples/identify/` に移行し自己完結化。`22-identify-reduce-add` 追加（@identify + @reduce :liadd パイプライン）。`vsmlink --version` / `--help` オプション追加。テンプレート `reduce.vsm`・`boundary_flags_compute.vsm` を `src/templates/` に追加。
- 0.7.0: `.param` キーワード `:dmaid` → `:send_wait_tag`、`:mvtag` → `:recv_wait_tag` に改名（SDK host-dma API 追従、**後方互換なし**）。`@reduce` ディレクティブ追加（L1B 内 64 PE 縮約、全 24 rrn_opcode 対応）。`@get_neighbor` を 6 引数構文 + `:buffer` 参照 + ixor テンプレートに刷新。periodic テンプレート追加。cross group 廃止（boundary_flags 12bit → 10bit）。`@boundary_flags_compute` 外部テンプレート化。`@stencil` 廃止。boundary_flags バイナリの一元管理化。`examples/data/` に共有データ集約。
- 0.6.4: `.param` を opaque handle（`vsmlink_parse_param_file`）に移行。`vsmlink_link_ex` → `vsmlink_link` に改名。`:identify` ID を整数に変更（`x`/`y` → `0`/`1`）。`vsmlink_identify_binding_t` から `:range` フィールド削除。`test_link` に bf / identify テスト追加。
- 0.6.3: `--boundary_flags=distribute` 追加。`.param` 形式 v0.1（`:place` / `:version :0.1`）に移行（後方互換なし）。`@omr_alloc` なしの `@stencil` をエラーとした。example 20（`@boundary_flags distribute` + stencil）追加。CLI のみ提供（libvsmlink.a は非公開）。
- 0.6.2: 「方式 A/B」を `--boundary_flags=compute` / `--boundary_flags=distribute` に統一
- 0.6.1: `@identify`、`@access_pattern` 追加。`@stencil` 5 引数構文確定
- 0.6.0: `@boundary_flags`、`@omr_alloc`、`@omr_free` 追加
- 0.5.0: `@stencil` 1D 袖交換実装（cross L2B/L1B/MAB 対応）
- 0.4.0: `@distribute`、`@collect` 実装
- 0.1.0: 初版
