# 32-nbody-runtime-link: host が実行時に vsmlink を呼び直す 3D N 体

**実装状況: 実装済み。emu:lib での実行・値照合まで確認済み（N=32）。** 実機（N=128/1024/4096）
での実行は未確認（別途判断）。

`examples/28-nbody-3d`・`examples/31-nbody-host-mediated-loop` と同じ 3D 重力の
全対全 N 体計算を題材に、28（ビルド時に必要な数だけ kernel を生成する）・31（1 個の
kernel を使い回し、host がデータを送り直す）とは異なる第三の構成を検証する。**host が
実行中に `vsmlink`（+ `assemble3`）を呼び直す**点が 28・31 との違いであり、名称の
由来になっている。

## 28・31 との位置づけ

| | kernel 数 | host が繰り返す操作 |
|---|---|---|
| 28-nbody-3d | N に依存する（分割ごとに別 kernel を事前生成） | 生成済み kernel を順に exec するだけ |
| 31-nbody-host-mediated-loop | N に依存しない（固定 1 個を使い回す） | 同じ kernel へ毎回データを送信してから exec |
| 32-nbody-runtime-link | N に依存しない（固定 1 個の雛形を使い回す） | 雛形を毎回 link し直してから exec |

31 は「送るデータ」を毎回作り直す。32 は「実行する kernel」を毎回作り直す。どちらも
kernel 数を N に依存させない点は同じだが、host が繰り返す作業の種類が異なる。

## パイプライン全体

入力は「1 粒子ぶんの計算式 + 粒子数」。28・31 と同じく重力の力の式（softening 付き
逆二乗則、ニュートン補正、leapfrog 積分）を対象にする。**この生成器は 1 つの決まった
式専用に作る（がちがちに固定でよい）。** 任意の式を受け付ける汎用コンパイラにはしない。
「1 粒子の計算式 + 粒子数を入力に取る」という入力の形を説明しているだけで、入力の
中身によって生成器の動作を変える必要はない。

```
1粒子の計算式 + 粒子数
        |
        v
  Python 生成器 (疑似コンパイル)
        |
   経路A ─┴─ 経路B
```

**「疑似コンパイル」と呼ぶが、構文解析も AST も不要。** 既存の `gen_accumulate.py`
(28) / `gen_nbody31.py` (31) と同じ、Python の文字列テンプレート（`.format()` に
よる埋め込みと繰り返し）で足りる。

### 経路 A: 1 本にまとめてから `vsmlink --split-only` で分割する（本 example で採用）

`gen_nbody32.py` が `init` / `reset_acc` / `accumulate_PH` / `kick` / `drift` /
`collect_pos` の 6 kernel を `@kernel_begin`/`@kernel_end` で 1 本の `_vsm` に
まとめ、`vsmlink --split-only` で kernel ごとに分割する（31 と同じ合成の仕方）。

31 との違いは、**合成した `_vsm` の中身が全体として文法的に正しくない**ことである。
`accumulate_PH` kernel の `@broadcast` 行（16 個、CHUNK=16 の unroll ぶん）は、
`from_param` に渡すスロット番号の位置に、`PH` という整数ではない目印を残す（16 行とも
同じ目印。chunk の 16 粒子を、1 つのスロットの続きとして順に読むため）。通常の `vsmlink` 呼び出し（`link()` を伴う）はこの行を展開しようとして失敗する
（実際に `E226` で失敗することを確認済み）ため、**この経路では通常の分割ではなく
`--split-only` を使う**。`--split-only` は kernel 境界の分割だけを行い、`@broadcast`
を含む他の directive のエラーチェック・展開を一切行わないため、この不完全な行が
残っていても分割自体は成功する。31 が通常の分割で足りたのは、31 の合成 `_vsm` が
最初から全体として文法的に正しいためである。

**`--split-only` の出力にはひと手間必要。** `--split-only` は分割の組み立てルール上、
各 kernel の出力に `@kernel_begin <name>` / `@kernel_end <name>` の行をそのまま残す。
この状態のファイルをそのまま通常の `vsmlink` に渡すと、入力にまた `@kernel_begin` が
あると判定され、出力がファイルではなくディレクトリになってしまう（1 kernel だけの
ディレクトリが 1 個できる、という意図しない結果になることを確認済み）。そのため
`build.ninja` では、この 2 行を取り除いてから通常の `vsmlink` に渡す（`strip_kernel_markers`
rule）。この点は `--split-only` 自体の一般的な注意点であり、本 example の設計判断ではない。

### 経路 B: 最初から複数ファイルで生成する（分割不要、本 example では未実装）

`vsmlink` の kernel 分割を経由せず、生成器が `init._vsm` / `kick._vsm` / … を
最初から個別ファイルとして書き出す案。`accumulate` に対応するファイルだけ、経路 A と
同じ理由で `from_param` の引数位置に整数ではない文字列が残る。経路 A・B のどちらを
選んでも、この先に渡る 2 種類のファイルの中身は同じになる（`vsmlink` を経由したかどうかの
違いだけ）。本 example では経路 A のみ実装した。

## 出力される 2 種類のファイル

### 文法どおりの `_vsm`

`init._vsm` / `reset_acc._vsm` / `kick._vsm` / `drift._vsm` / `collect_pos._vsm`
の 5 個は、経路 A・B どちらでも、生成された時点で directive の文法が完成している。
これは既存の確立した用語である「`_vsm`」そのものであり、新しい呼び方は要らない。
28・31 と同様、ビルド時に `.param`/`.stparam` を添えて `vsmlink` へ通し、通常どおり
`.vsm` にしておける。

### `@broadcast` のスロット番号がまだ決まっていないファイル

`accumulate_PH` に対応する 1 個だけは、16 個ある `@broadcast _pj from_param <ここ>
size 4 $lr16` の `<ここ>` に、`PH` という、整数でも `from_param` の有効な書き方でもない
目印が残ったままになる（16 個とも同じ目印）。この状態の `_vsm` を `vsmlink` に渡しても、`from_param` の引数が
解釈できずエラーになる（directive としての文法に通らない。実際に確認したエラー:
`(:error "@broadcast: expected <id> from_param <N> size <S> <pe_reg>" :id "E226")`）。
ビルド時に `.vsm` へできるのはここまでで、この 1 個だけは host の実行時まで link を
持ち越す。

このファイルには、生成した時点で分かる名前として `accumulate_PH._vsm` を付ける。
経路 A では `@kernel_begin accumulate_PH` として kernel 名にこの目印を含めておくことで、
`--split-only` の出力ファイル名（`<入力ファイル名の stem>-accumulate_PH._vsm`）にも
そのまま反映される。

**`_PH` という目印は、このファイルの名前としてのみ扱う。** 本 README の説明文の
中で「`_PH` ファイルは…」のように一般的な用語として使うことはしない。上で書いた
とおり、初出時は都度、何が・なぜ未確定かを具体的に説明する。以降このファイルに
繰り返し言及する必要があるときは、`accumulate_PH._vsm` という具体的なファイル名を
そのまま書く（説明を毎回書き直す代わりに、読み手がこのファイル名に意味を結びつける
ことを期待する）。**`_PH` という命名は、この example に限った 1 回限りの取り決めで
あり、他の example や vsmlink チーム全体の命名規則ではない。**

## host が実行時に行うこと

**力の評価 1 回につき、PDM への送信は 1 回だけ。** 28 と同様、全粒子（CHUNK の倍数に
満たない端数は質量 0 で埋める）を 1 回の `mnc2_send` で PDM へ送り、送信完了を
`mnc2_wait_idma_idle` で待つ。31 のように chunk ごとにデータを送り直すことはしない。

その後、host が chunk ごとに次の手順を繰り返す（テンプレートは build.ninja が
ビルド時に `--split-only` + marker 除去まで済ませておいた
`_build/k/stripped/nbody32_n32-accumulate_PH._vsm` を使う）。

1. テンプレート中の目印 `PH`（`from_param` の引数）を、その chunk のスロット番号へ
   文字列置換し、文法どおりの `_vsm` を作る。16 個の `@broadcast` は同じスロットを使い、
   その続き（chunk の 16 粒子）を順に読む
2. `vsmlink` を呼び、`.vsm` を得る
3. `assemble3 --loader` を呼び、`.idma.dat` を得る
4. `mnc2_load_kernel` で device へ load し、`mnc2_exec_kernel` で exec し、
   `mnc2_free_kernel` で解放する

スロット番号の埋め込みは単純な文字列置換で行う（`gen_accumulate.py` が `{slot}` を
埋めるのと同じ操作を、ビルド時ではなく host の実行時に行うだけ）。`vsmlink` /
`assemble3` の呼び出しは `fork`/`execvp` によるサブプロセス実行。

**`.param` はビルド時に生成した 1 個を最後まで使い回す。** 32 では 28 と同様に、
全 N 粒子を PDM 上の決まった並びへ置き、host が選ぶのは「どのスロット番号を使うか」
だけにする。`.param` には chunk ごとに 1 つのスロット（スロット `40 + g*8`、`:addr` は
chunk g の先頭粒子）をあらかじめ列挙してあるので、host がスロット番号を選ぶ限り、
`.param` 自体を実行時に書き換える必要はない。

## 検討したが採らなかった案

`_vsm` は常に文法どおりのまま保ち、`.param` の `:addr` だけを chunk ごとに host が
書き換えて `vsmlink` を呼び直す案も検討した。`@broadcast` の番地は元々 `.param` の
`:slot N :addr X` で決まる仕組みなので、技術的にはこれでも同じ動作を実現できる。
**この案は採らない。** `.param`（配置パラメタ）は、ハードウェア上の配置をビルド時に
決めておくものという意味論を持つ。host が実行時に `.param` の番地そのものを自由に
書き換える案は、動きさえすればよいという発想で、この意味論を壊してしまう。処理が
うまくいくかどうかとは別に、`.param` が何を表すものかという前提を崩す変更なので
採らない。32 では `.param` はビルド時に生成した 1 個を最後まで使い回し、host が
実行時に選ぶのは（`.param` の番地ではなく）`accumulate_PH._vsm` に埋め込むスロット
番号だけにする。

## trade-off について（未検証）

31 は chunk ごとに PDM へのデータ送信（DMA）が発生する。32 はデータの送信が（力の評価
1 回につき 1 回で）要らない代わりに、chunk ごとに `vsmlink` と `assemble3` という
2 個の外部プロセス呼び出しが発生する。プロセス起動と link・アセンブルにかかる時間は、
通常 DMA 転送よりずっと大きいと見込まれるが、実行速度の比較は未検証（emu:lib での
動作・値照合のみ確認済み）。

## ファイル構成

| ファイル | 役割 |
|---------|------|
| `gen_nbody32.py` | 6 kernel を 1 本の `_vsm` に合成する生成器（経路 A）。`.param` も出力する |
| `init._vsm` `reset_acc._vsm` `kick._vsm` `drift._vsm` `collect_pos._vsm` | 文法どおりの `_vsm`（31 からの複製） |
| `nbody32.stparam` | 31 の `nbody31.stparam` と同一内容 |
| `gen_ic.py` | 31 と同一（初期条件生成） |
| `test_nbody32.c` | host 側。テンプレートの読み込み・chunk ごとのスロット番号埋め込み・`vsmlink`/`assemble3` のサブプロセス呼び出し・load・exec を行う |
| `build.ninja` | 生成 → `vsmlink --split-only` → marker 除去 → 確定済み 5 kernel の link・assemble3 → host C ビルド。`accumulate_PH._vsm` は host が実行時に link するためビルド時の link・assemble3 の対象外 |
| `expected-init.vsm` | `ninja verify` 用の golden（init kernel の展開結果） |

## 検証状況

- **vsmlink の kernel 分割 + host 実行時 link**: `ninja` で `--split-only` による
  6 kernel への分割、`ninja verify` で init kernel の golden 一致を確認済み。通常の
  `vsmlink` が `accumulate_PH._vsm` を E226（`from_param` の引数が整数でない）で
  拒否し、host が目印 `PH` を chunk のスロット番号に書き換えた後は link に成功する
  ことを確認済み。実行時に link した accumulate の PDM からの移動（`mvb`）は 1 回
- **assemble3 以降（build-e2e / test-emu-lib、N=32）**: `ninja build-e2e` で 5 kernel
  の assemble3 と host C のビルドが通ることを確認済み。`ninja test-emu-lib` で
  emulator 実行・host 倍精度 leapfrog との値照合まで PASS（`max_rel=1.457e-14`）
- **実機（N=128/1024/4096）**: 未実行（別途判断）
