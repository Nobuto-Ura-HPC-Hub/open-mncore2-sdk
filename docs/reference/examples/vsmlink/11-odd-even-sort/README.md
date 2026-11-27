# 11-odd-even-sort — Odd-Even Transposition Sort on 4096 PEs

MN-Core 2 の PE 配列上で Odd-Even Transposition Sort を実行する。
`@get_neighbor` による隣接 PE 通信と条件分岐を組み合わせた、古典的な並列ソート。

端の PE（PE の番号 0 と 4095）は、`@get_neighbor` の端の指定 `:reg $lm0` で自分の値を受け取る。

> **注: cross_group。** 本 README は cross_group と cross_L2B を区別して記述している。`@boundary_flags` に cross_group の
> ビットは無く、cross_group の PE には cross_L2B のビットが立つ（境界フラグは 10 ビット）。


## アルゴリズム

各 PE が 1 要素を保持し、隣接 PE と比較・交換を繰り返す:

1. **偶数奇数交換**: PE[0]↔PE[1], PE[2]↔PE[3], ... を比較し、逆順なら交換
2. **奇数偶数交換**: PE[1]↔PE[2], PE[3]↔PE[4], ... を比較し、逆順なら交換
3. swap が 1 件も起きなくなったら整列完了（最悪 N = 4096 フェーズ）

計算量: O(N) ステップ、各ステップ O(1)（全 PE 並列）。
ソートネットワークとしての比較器数は N(N-1)/2。

## 実装アーキテクチャ

### 2 つの交換フェーズ

Odd-Even Transposition Sort は 2 つの交換フェーズを交互に実行する。
MN-Core 2 の PE 配列 (subpeid 0-3) を例にとると:

**偶数奇数交換 (even-odd exchange)**:
```
| (0 1) (2 3) |  (0 1) (2 3) |  ...
```
偶数 PE と右隣の奇数 PE がペア。MAB 内で完結し、cross は発生しない。

**奇数偶数交換 (odd-even exchange)**:
```
(3 | 0) (1 2) (3 | 0) (1 2) (3 | 0) ...
```
奇数 PE と右隣の偶数 PE がペア。PE3 と次 MAB の PE0 がペアになるため、
cross MAB / L1B / L2B / group が発生する。

**各 PE の動作:**

| 交換フェーズ | PE の役割 | cross |
|------------|---------|---------|
| 偶数奇数交換 | 偶数 PE: 右隣(奇数)と比較 | なし (MAB 内) |
| 偶数奇数交換 | 奇数 PE: 左隣(偶数)と比較 | なし (MAB 内) |
| 奇数偶数交換 | 奇数 PE (PE3): 右隣(偶数)と比較 | PE3→次MAB PE0 で cross MAB |
| 奇数偶数交換 | 偶数 PE (PE0): 左隣(奇数)と比較 | PE0←前MAB PE3 で cross MAB |

奇数偶数交換では PE3↔PE0 のペアが cross MAB となるため、
左隣取得・右隣取得の**両方向**で cross MAB / L1B / L2B / group が発生する。

**注意**: 偶数奇数交換では cross が発生しないが、これを上位レイヤ（MNCL 等）で
自動検出するのはかなり困難。`neighbor(a, 1)` 単体では全 PE が右隣を取るため
PE3→PE0 の cross MAB が必ず発生する。cross が不要になるのは
`if (subpeid % 2 == 0) { neighbor(a, 1) }` のように条件付きで呼んでいる場合で、
MNCL がこれを最適化するには subpeid の偶奇条件と neighbor の方向を
静的に解析して cross 不要と判定する必要がある。

### 終了条件: swap フラグ

各 PE が「このフェーズで値が変わったか」を 0/1 の swap フラグにし、`@collect` で slot 24 に書く（4096 個）。
ホスト（`sort_host.c`）は、4096 個がすべて 0（どの PE も値が変わらなかった）ならソート完了とする。

### 注意: vsm にループ命令はない

1 カーネル起動 = 1 フェーズ。C ホストが交互にカーネルを起動する:

```
sort_even._vsm  ←→  sort_odd._vsm
  (slot A→B)           (slot B→A)
```

```c
// C ホスト側ループ
while (true) {
    exec(sort_even);  // slot A → slot B
    if (all_zero(recv_swap_flags())) break;
    exec(sort_odd);   // slot B → slot A
    if (all_zero(recv_swap_flags())) break;
}
```

### 隣接 PE データ取得の階層構造

左隣・右隣の値を取得する際、PE の位置によって異なる HW 経路を使う
:

| 交差レベル | 条件 (左隣の例) | HW 経路 |
|-----------|---------------|--------|
| within_MAB | subpeid != 0 | msl (MAB 内シフト) |
| cross_MAB | subpeid == 0, mabid != 0 | L1BM 経由 (l1bmd+1 + msl) |
| cross_L1B | subpeid == 0, mabid == 0, l1bid != 0 | L2BM 経由 (l2bm → l2bmb + l1bmd+1 + msl) |
| cross_L2B | subpeid == 0, mabid == 0, l1bid == 0, 同一 group 内 | mvp PDM 経由 (L2BM → PDM → 隣接セクション L2BM → L1BM) |
| cross_group | subpeid == 0, mabid == 0, l1bid == 0, group 境界 | mvp PDM 経由 (cross_L2B と同一メカニズム) |

cross_L2B と cross_group は同じ mvp PDM メカニズムを使う。
mvp は全セクションを一斉に 1 セクション分シフトするため、
group 内の cross L2B (sec0→sec1) と cross group (sec1→sec2) が同時に処理される。
このため `@boundary_flags` の cross group のビットは廃止し、旧 cross group の PE は cross_L2B のビットに
統合した（`share/vsmlink/directives-spec.md` の「注: cross group のビットは無い」）。

**選択方式: なぜ全レベルを事前計算するか**

隣接 PE データ取得を素直に C で書くと、ネスト if/then/else になる:

```c
if (cross_group) {
    cross_group 処理
} else {
    if (cross_L2B) {
        cross_L2B 処理
    } else {
        if (cross_L1B) {
            cross_L1B 処理
        } else {
            if (cross_MAB) {
                cross_MAB 処理
            } else {
                普通の PE の情報交換
            }
        }
    }
}
```

しかし MN-Core 2 は SIMD であり、この if/then/else を忠実に再現できない:

1. **処理のマスクはできない。代入のマスクだけができる。**
   MN-Core 2 の maskr は GRF への書き込みを抑制するだけで、
   命令の実行自体を抑制する仕組みはない。
   `maskr` で一部の PE の代入を止めても、命令は全 PE で実行され、
   実行ステップを消費する。

2. **データ移動には隣接 PE の協調が必要。**
   例えば cross_L1B の処理には l1bmd+1 が必要だが、
   l1bmd+1 は全 PE が同時にデータを L1BM に出し入れする命令である。
   「cross_L1B の PE だけが l1bmd+1 を実行する」ことはできない。

したがって、if の中に処理を入れるのではなく、
**全レベルの結果を全 PE で事前計算し、最後に maskr で正しい結果を選択する**
という構造を取る。事前計算は GRF への書き込みのみで副作用がないため安全。

```c
// 事前計算（全 PE で実行、GRF 書き込みのみ = 安全）
cross_group 処理 (結果をレジスタ A へ代入)
cross_L2B 処理   (結果をレジスタ B へ代入)
cross_L1B 処理   (結果をレジスタ C へ代入)
cross_MAB 処理   (結果をレジスタ D へ代入)
within_MAB 処理  (結果をレジスタ E へ代入)

// 選択（代入のみ）
if (cross_group) {
    最終レジスタ = レジスタ A
} else {
    if (cross_L2B) {
        最終レジスタ = レジスタ B
    } else {
        if (cross_L1B) {
            最終レジスタ = レジスタ C
        } else {
            if (cross_MAB) {
                最終レジスタ = レジスタ D
            } else {
                最終レジスタ = レジスタ E
            }
        }
    }
}
```

この構造を @get_neighbor ディレクティブとして実現した場合、以下の副作用が発生する:

1. **レジスタの破壊** — 事前計算の中間結果で GRF レジスタ、omr（マスクレジスタ）、GRF1 ($ln0 等) が上書きされる
2. **PDM 領域の使用** — cross_L2B / cross_group の mvp で PDM の一時領域を消費する。

特に PDM 領域の使用は注意を要する。@get_neighbor の元データ領域を流用するのが望ましい（distribute 完了後は PE に複製済みで、PDM 上では未使用の領域であるため）。

ステンシル計算のデータと PDM、L2BM、L1BM、PE のデータの関係を整理すると、
実際のステンシル計算のデータ本体は PE にある。
PDM、L2BM、L1BM は中間保管庫であり、データの永続的な格納場所ではない。
特に PDM は HOST へのデータ回収にも使われる保管庫であるため、
そのセマンティクスを壊さないオフセット設定が望ましい。

**PDM の使い方**

@get_neighbor は、put/get モデルに基づいて PDM を使う。
PE N のデータが PDM の addr N（+ ベース）に対応し、get(-1) は addr N-1 を読む。
詳細は `../21-get-neighbor/README.md` を参照。

**cross_group について**

cross_group と cross_L2B は同じ mvp PDM メカニズムで処理される（上の「交差レベル」の表を参照）。
`:place :pdm0` に固定しているので経路を分ける意味が無く、`@boundary_flags` の cross group のビットは
廃止した。旧 cross group の PE には cross_L2B のビットが立つ。このため結果選択では、cross_L2B の maskr が
旧 cross group の PE もカバーする（`share/vsmlink/directives-spec.md` の「注: cross group のビットは無い」）。

```c
// 事前計算（全 PE で実行、GRF 書き込みのみ = 安全）
cross_L2B 処理   (結果をレジスタ B へ代入)
cross_L1B 処理   (結果をレジスタ C へ代入)
cross_MAB 処理   (結果をレジスタ D へ代入)
within_MAB 処理  (結果をレジスタ E へ代入)

// 選択（方式 2: default + overwrite）
最終レジスタ = レジスタ E              // default: within_MAB を全 PE に設定
maskr omr_cross_MAB
  最終レジスタ = レジスタ D
mask 0
maskr omr_cross_L1B
  最終レジスタ = レジスタ C
mask 0
maskr omr_cross_L2B                    // :place :pdm0 なら cross_group も同じ経路
  最終レジスタ = レジスタ B
mask 0
```

**maskr による実現方式は 2 つある:**

方式 1: ネスト maskr — if/then/else を忠実に再現

各条件と NOT 条件のペアを omr に設定し、ネストした maskr で if/else を構成する。
（例: omr_cross_L1B と omr_not_cross_L1B をフラグのビットから導出）
```
maskr omr_cross_group
  最終レジスタ = レジスタ A
mask 0
maskr omr_not_cross_group
  maskr omr_cross_L2B
    最終レジスタ = レジスタ B
  mask 0
  maskr omr_not_cross_L2B
    ...
  mask 0
mask 0
```

方式 2: default + overwrite — デフォルト値を全 PE に入れ、内側から外側にフラットに上書き
```
最終レジスタ = レジスタ E              // default: within_MAB を全 PE に設定
maskr omr_cross_MAB
  最終レジスタ = レジスタ D
mask 0
maskr omr_cross_L1B
  最終レジスタ = レジスタ C
mask 0
maskr omr_cross_L2B
  最終レジスタ = レジスタ B
mask 0
maskr omr_cross_group
  最終レジスタ = レジスタ A
mask 0
```

`@get_neighbor` の展開は、使うマスクレジスタが少ない方式 2 を使う（`share/vsmlink/directives-spec.md` の `@get_neighbor` の
「マージフェーズの実装方針」）。

## ファイル

| ファイル | 内容 |
|---|---|
| `sort_even._vsm` | 偶数フェーズ（組は (PE0, PE1), (PE2, PE3), ...）。slot 8 を読み、slot 16 に書く。左右の隣の値を `@get_neighbor` で受け取り、swap したかを slot 24 に `@collect` で書く |
| `sort_odd._vsm` | 奇数フェーズ（組は (PE1, PE2), (PE3, PE4), ...）。slot 16 を読み、slot 8 に書く。ほかは `sort_even._vsm` と同じ |
| `sort.param` | 配置パラメタ（`sort_even` と `sort_odd` で共用） |
| `sort_host.c` | ホスト。`sort_even` と `sort_odd` を交互に起動し、swap がすべて 0 になったら終え、昇順になったかを照合する |
| `expected_sort_even.vsm` / `expected_sort_odd.vsm` | `ninja verify` が比べる、vsmlink コマンドの出力 |

```bash
# SDK を有効にし、この example のディレクトリで実行する（examples の README.md の「動かし方」）
ninja                # vsmlink コマンドで .vsm を生成
ninja verify         # expected_sort_*.vsm との diff
ninja build-e2e      # assemble3 とホスト側のビルド
ninja test-emu-lib   # emu:lib で実行・照合（整列が収束するまで回すので時間がかかる）
ninja test-device    # 実機で実行・照合
```

## 原典

### Habermann (1972)

> **N. Habermann, "Parallel Neighbor Sort (or the Glory of the Induction Principle),"**
> CMU Computer Science Report, 1972.
> (Technical Report AD-759 248, National Technical Information Service,
> US Department of Commerce, 5285 Port Royal Rd, Springfield VA 22151)

並列プロセッサ上での効率性を示した最初の論文。
帰納法による正当性証明から "the Glory of the Induction Principle" という副題がついている。

### Knuth (1973)

> **D. E. Knuth, *The Art of Computer Programming, Volume 3: Sorting and Searching*,**
> Addison-Wesley, 1973. (2nd edition, 1998)
> Section 5.3.4 "Networks for Sorting"

ソートネットワークの枠組みで Odd-Even Transposition Sort を包括的に記述。
Batcher の Odd-Even Merge Sort（O(n log²n) の深さ）との比較も含む。

## 参考文献

- [Odd-even sort — Wikipedia](https://en.wikipedia.org/wiki/Odd%E2%80%93even_sort)
- [Odd-Even Transposition Sort — Baeldung](https://www.baeldung.com/cs/odd-even-transposition-sort)
- [Sorting Networks — hwlang.de](https://hwlang.de/algorithmen/sortiren/networks/oetsen.htm)
