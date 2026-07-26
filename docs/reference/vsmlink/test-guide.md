# vsm-linker テストガイド

vsmlink が生成した `.vsm` が正しいことをどう確かめるかをまとめる。

## 前提

- vsmlink kit を install 済みで、`vsmlink` に PATH が通っていること
- assemble3 に PATH が通っていること（アセンブル以降を確かめる場合）
- エミュレータで実行する場合は `gpfn3_package_main` に PATH が通っていること

CLI は 4 引数すべてが必須である。

```bash
vsmlink <input._vsm> <input.param> <input.stparam> <output.vsm>
```

---

## 検証の 3 層

vsmlink の検証は 3 つの層に分かれる。すべての層を毎回通す必要はなく、
確かめたい内容に応じて層を選ぶ。

| 層 | 何を見るか | 使うもの |
|----|----------|---------|
| 展開結果 | ディレクティブが期待どおりの命令列に展開されたか | `vsmlink` の出力と期待値の diff |
| vsm 単体 | 展開した vsm が実際に動き、正しい値を出すか | `gpfn3_package_main` |
| ホスト連携 | host-dma 経由の送受信を含めて一連の流れが動くか | C ホスト + `mnc2.h`（emu:lib） |

**選び方**:

- ディレクティブの展開結果だけを見たい場合は展開結果の層で足りる。ここが一番速い
- 展開した vsm が動くかを見たいが DMA が要らない場合は `gpfn3_package_main` を使う
- host-dma との連携や、生成物の中身を厳密に確かめたい場合は C ホストを使う

新しい機能を確かめるときは、展開結果、vsm 単体、ホスト連携の順に上げていく。

---

## テストの走らせ方

まず examples を作業用の場所に展開する。`sdk-examples` が
`<展開先>/vsmlink-<バージョン>/` というディレクトリを作る。

```bash
sdk-examples vsmlink ~        # ~/vsmlink-<バージョン>/ ができる
cd ~/vsmlink-<バージョン>
```

lit テストは目的別に 3 つに分かれている。

```bash
lit -j 1 lit/test-verify      # vsmlink の生成物が期待値と一致するか
lit -j 1 lit/test-emu-lib     # エミュレータで正しい結果が出るか
lit -j 1 lit/test-device      # 実機で正しい結果が出るか
```

`test-device` は実機がないと動かない。エミュレータだけで確かめるなら
`test-verify` と `test-emu-lib` の 2 つを走らせる。

**`-j 1` は必須である。** テストデータの生成が内部で ninja を呼ぶため、
並列に走らせると同じディレクトリで ninja が競合して不定に失敗する。

examples は 3 段階の検証を持つ。段階ごとに切り分けられるので、
どこで壊れたかが分かる。

```bash
cd ~/vsmlink-<バージョン>/<例の名前>
ninja              # 1. vsmlink が期待どおりの vsm を生成する
ninja build-e2e    # 2. assemble3 が通る
ninja test         # 3. エミュレータで正しい結果が得られる
```

---

## 展開結果を見る

vsmlink を走らせ、出力を期待値と比べる。

```bash
vsmlink input._vsm input.param input.stparam /tmp/out.vsm
diff /tmp/out.vsm expected.vsm
```

ディレクティブごとに、展開後の vsm で何を見れば正しいと言えるかを挙げる。

| ディレクティブ | 確認すること |
|--------------|------------|
| `@alloc` | 指定したレジスタ番号が使われている。番号は偶数であること |
| `@distribute` | PDM から L1BM を経て PE へ届く転送が並ぶ。サイクルマスクで 1 個だけ書く |
| `@collect` | `@distribute` の逆向きの転送が並ぶ |
| `@alias` | 別名が元のバッファと同じ番地に解決される |
| `@broadcast` | 放送命令が出る。size に応じたサイクルマスクが付く（size 1 なら `/1000`、2 なら `/1100`、3 なら `/1110`、4 ならマスク無し）。着地先は指定レジスタから 2 ずつ進む |
| `@identify` | 次元ごとの識別値が PE に配られる |
| `@access_pattern` | 指定したパターンに対応するテンプレートが選ばれている |
| `@boundary_flags` | 境界の判定に使うビット列が配られる。ビットレイアウトは `directives-spec.md` を参照 |
| `@get_neighbor` | 隣接 PE からの読み出しが出る。第 3 引数は読み出し元の PE レジスタである |
| `@reduce` | 縮約が L1BM、L2BM、PDM の順に段を追って進む |

L1BM と L2BM の番地は vsmlink が割り当てる。**番地を直接書いてはいけない。**
テンプレートでは `{{lb+N}}` / `{{lc+N}}` を使う。直接書くと E114 で弾かれ、
入力 `_vsm` に書いた場合は W003 で警告される。

割り当ての結果、同じディレクティブでも並べ方によって番地が変わる。
番地そのものを期待値に書くと、無関係な変更で壊れるテストになる。

---

## vsm 単体で動かす

C コードを書かずに、エミュレータで vsm を動かして値を確かめる。

対話的に操作するのではなく、**値の設定と取得を `.vsm` に `d set` / `d get` として
書いておく**。アセンブルして実行すると、`d get` の結果が標準出力に出る。

```bash
assemble3 kernel.vsm --output-file kernel.asm
gpfn3_package_main -i kernel.asm --offchip-memory-init zero
```

- `d set` / `d get` が扱えるのは **LM である。PDM は対象外**
- PDM に初期値を置くには `--load-memory-file` と config JSON を使う（バイトオーダの変換が要る）
- PDM の内容を見るには `--save-memory-file` で書き出して hexdump する
- `--offchip-memory-init` を省略すると DRAM と PDM が 0 ではない値で初期化されるので、
  ゼロ初期化を前提にするなら `zero` を明示する

---

## ホスト連携を見る

C ホストから `mnc2.h` の API を使い、送信、実行、受信の一連の流れを確かめる。
host-dma との連携、複数カーネルの実行、debug read/write による中身の確認が要る場合に使う。

`share/examples/vsmlink/` の各例が実際の書き方になっている。

---

## エラーの読み方

エラーはすべて S 式で出力し、ID を持つ。上位のツールから種別を判別できる。

```lisp
(:error "reserved_wait_tags[0] = #x00 out of range (must be 0x01..0x3f)" :id "E102")
(:error "@broadcast: size must be 1..4" :id "E227" :line 5)
```

配置パラメタとリンク解決のエラーが E1xx、ディレクティブのエラーが E2xx である。
E2xx には `_vsm` の何行目かを示す `:line` が付く。

資源の確保に失敗した場合は 2 つ出る。なぜ失敗したか（空きが足りない等）を E110 系が、
どの行のどのディレクティブで失敗したかを E233 から E235 が示す。両方を読むこと。

ID の一覧は `param-spec.md` の「エラー・警告一覧」にある。
警告が出た場合、リンク自体は継続して出力が作られる。警告を見落とさないこと。

---

## 参考: .vsm の読み方

生成された `.vsm` に出てくる主な命令。

| 命令 | 意味 |
|------|------|
| `mvp/n512 $pN@G $lcM@L.H` | PDM から L2BM へ。512 u64 を PDM 番地 N、group G から転送する |
| `mvp/n512iXX ...` | 同上に加えて wait tag XX を発火する |
| `l2bmb@N $lcM $lbK` | L2BM から L1BM へ。L2B 内の MAB N に対して転送する |
| `l1bmd $lbK $lmNv` | L1BM から PE へ。`v` はベクトルモード |
| `l2bm@N $lbK $lcM` | L1BM から L2BM へ（逆方向） |
| `l1bmp ...` | L1BM から PE へ放送する。`@broadcast` が使う |
| `l1bmd-1 $lbK $dst` | 1 つ左の MAB の L1BM から読む |
| `l1bmd+1 $lbK $dst` | 1 つ右の MAB の L1BM から読む |
| `msr $src $dst` | PE 間の右シフト。MAB 内 4 PE を循環する |
| `msl $src $dst` | PE 間の左シフト |
| `maskr N` | 以降の命令を特定の PE のみ条件実行する |
| `mask 0` | 条件実行を解除して全 PE 実行に戻す |
| `ipassa $src $dst` | パススルー。`maskr` の下では条件付きの上書きになる |
