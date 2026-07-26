# 11-odd-even-sort — Odd-Even Transposition Sort on 4096 PEs

MN-Core 2 の PE 配列上で Odd-Even Transposition Sort を実行する。
`@stencil` による隣接 PE 通信と条件分岐を組み合わせた、古典的な並列ソート。

**注: cross_chip は未対応。** cross chip の PE の結果は不定（0 になる）。

> **注: cross_group 廃止。** 本 README は cross_group と cross_L2B を区別して記述しているが、
> cross_group は PDM0 固定方針により廃止され cross_L2B に統合された。
> 本文中の cross_group への言及は歴史的経緯として残す。実際の boundary_flags は 10 ビット。

> **注: data/collected_flags.bin の検証。** このバイナリは歴史的経緯があり、現仕様と乖離している可能性がある。
> `python3 ../../examples/17-boundary-collect/dump_flags.py data/collected_flags.bin --human` で内容を確認すること。

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

### 終了条件: swap フラグの OR リダクション

各 PE が「このフェーズで swap したか」を 0/1 で保持し、グループ内で OR リダクションして C 側に返す。

- 1 つでも swap した PE があれば → そのグループの値 = 1
- 4 グループすべてが 0 → どの PE も swap しなかった → ソート完了

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
    if (all_zero(collect_swap_flags())) break;
    exec(sort_odd);   // slot B → slot A
    if (all_zero(collect_swap_flags())) break;
}
```

### 隣接 PE データ取得の階層構造

左隣・右隣の値を取得する際、PE の位置によって異なる HW 経路を使う
（詳細は [docs/boundary-selection.md](../../docs/boundary-selection.md) を参照）:

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
違いは @boundary_flags のビット分類のみ（cross_L2B と cross_group は ワンホット で排他的に立つ。
実際にどちらの経路を使うかは `@stencil` がデータの `:place` を見て判断する）。

最小化版 (step4_minimal 等) では L1B7 の 64 u64 だけを collect/redistribute する (n64)。

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

この構造を @stencil ディレクティブとして実現した場合、以下の副作用が発生する:

1. **レジスタの破壊** — 事前計算の中間結果で GRF レジスタ、omr（マスクレジスタ）、GRF1 ($ln0 等) が上書きされる
2. **PDM 領域の使用** — cross_L2B / cross_group の mvp で PDM の一時領域を消費する。

特に PDM 領域の使用は注意を要する。@stencil の元データ領域を流用するのが望ましい（distribute 完了後は PE に複製済みで、PDM 上では未使用の領域であるため）。

ステンシル計算のデータと PDM、L2BM、L1BM、PE のデータの関係を整理すると、
実際のステンシル計算のデータ本体は PE にある。
PDM、L2BM、L1BM は中間保管庫であり、データの永続的な格納場所ではない。
特に PDM は HOST へのデータ回収にも使われる保管庫であるため、
そのセマンティクスを壊さないオフセット設定が望ましい。

**PDM の使い方に関する注意（11-odd-even-sort と @get_neighbor の違い）**

11-odd-even-sort の step4/step4_minimal では、cross L2B の PDM を一時バッファとして
詰めて使っている。各 L2B の L1B7 データを 64 u64 ずつ隙間なく配置し、
redistribute で宛先を 1 つずらして読む。PDM アドレスと PE 番号の対応はない。

@get_neighbor ではこれと異なり、put/get モデルに基づいて PDM を使う。
PE N のデータが PDM の addr N（+ ベース）に対応し、get(-1) は addr N-1 を読む。
詳細は `examples/21-get-neighbor-left/README.md` を参照。

**cross_group について**

cross_group と cross_L2B は同じ mvp PDM メカニズムで処理される（94 行参照）。
boundary flags では cross_group と cross_L2B は ワンホット で排他的に立つ（`$l2bid` の偶奇で区別）。
実際にどちらの転送経路を使うかは `@stencil` がデータの `:place` を見て判断する。

現在の 1D の例（`:place :pdm0`）では cross_group と cross_L2B は同じ mvp PDM 経路で処理される。
`@stencil` が cross_group の PE を cross_L2B と同じ扱いにするため、
結果選択では cross_L2B の maskr が cross_group の PE もカバーする。

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

step3 までは方式 1 で忠実に if/then/else を組んでいる。
step4 以降は使用するマスクレジスタが少ないことから方式 2 を選択。

なお、マスクレジスタを 2 つ程度に減らしつつ処理速度も落とさない方式がありえる。今後の課題。

## vsm の Step 一覧

各 step は個別の vsm で、操作を段階的に積み上げて odd-even sort を構成する。

### Step 0: 比較命令の仕様確認

- `isub` / `icmp` 系の比較命令を確認
- 比較結果を omr に入れる方法を確定
- `step0_subpeid.vsm`: $subpeid + 1 を LM8 に書いて d get で確認

---

### Step 1: 2値比較 → max/min 選択

- `@distribute` で A, B を各 PE に配布
- `if (A > B): out = A  else: out = B` → max(A, B) を collect
- 確認: `out[i] = max(A[i], B[i])` が全 PE で正しいこと

| ファイル | 内容 |
|---------|------|
| `step1_even_msl.vsm` | msl MAB 内左シフト、d set で入力 |
| `step1_subpeid.vsm` | msl (MAB内左シフト)、$subpeid で入力生成 |

---

### Step 2: 奇数偶数交換用 / 左隣取得 / cross MAB / 必ず swap

- 全 PE が左隣の値を取る。PE0 は cross MAB で前 MAB の PE3 を取得
- boundary flags で PE0 を判定し、maskr で選択

| ファイル | 内容 | テストターゲット |
|---------|------|---------------|
| `step2.vsm` | 手動 boundary flags、cross MAB (l1bmd+1 + msl) | `test-step2` |
| `step2_bf._vsm` + `step2_bf.param` | @boundary_flags distribute 版 | `test-step2-bf` |
| `step2_bf_compute._vsm` + `step2_bf.param` | @boundary_flags_compute 版 | `test-step2-bf-compute` |

---

### Step 3: 左隣取得 / cross MAB + cross L1B / 必ず swap

- 全 PE が左隣の値を取る。cross MAB + cross L1B 対応
- step2 に cross L1Bを追加

| ファイル | 内容 | テストターゲット |
|---------|------|---------------|
| `step3.vsm` | 手動 omr、cross MAB + cross L1B | `test-step3` |
| `step3_bf._vsm` + `step3_bf.param` | @boundary_flags distribute 版 | `test-step3-bf` |
| `step3_only_l1b.vsm` | cross L1B単体テスト（旧版、参考） | — |

---

### Step 4: 左隣取得 / cross MAB + L1B + L2B

全 PE が左隣の値を取る。cross_L2B まで対応。

| ファイル | 内容 | テストターゲット |
|---------|------|---------------|
| `step4.vsm` | 手動 omr、全 L2BM collect (n512) | `test-step4` |
| `step4_minimal.vsm` | 手動 omr、collect 最小化 (n64) | `test-step4-minimal` |
| `step4_bf._vsm` + `step4_bf.param` | @boundary_flags + collect 最小化 | `test-step4-bf` |

`step4.vsm` では cross_L2B の処理で全 L2BM データを PDM 経由で袖交換しており、
下位レベル（cross_L1B、cross_MAB）の個別処理が効率化されない冗長な構造になっている。
これは処理の正しさを確認するためにあえてそうしている。
`step4_minimal.vsm` が cross_L2B の転送量を L1B7 の 64 u64 に抑えたバージョンであり、
下位レベルの袖交換が意味を持つ効率的な構造になっている。

---

### Step 5: 右隣取得 / cross MAB + L1B + L2B

- step4 の逆方向版。全 PE が右隣の値を取る
- cross_L2B まで対応。
| ファイル | 内容 | テストターゲット |
|---------|------|---------------|
| `step5.vsm` | 手動 omr、cross MAB + L1B + L2B | `test-step5` |

---

### Step 6: step4 + lmax + swap 検出

step4（左隣取得）の結果に対して lmax で max(my, left_neighbor) を計算し、
値が変わったかどうかを swap_flag として記録（swap_flag = result != my）。
左隣取得は step4 と同じ。step6 で追加されるのは lmax と swap_flag の 2 つ。
cross_L2B まで対応。

| ファイル | 内容 | テストターゲット |
|---------|------|---------------|
| `step6.vsm` | cross MAB + L1B + L2B | `test-step6` |
| `step6_without_cross.vsm` | cross なし（偶数奇数交換用、MAB 内で完結） | `test-step6-without-cross` |
| `step6_only_cross_MAB.vsm` | cross MAB のみ | — |

---

### Step 7: step5 + lmin + swap 検出

step6 の逆方向版。step5（右隣取得）の結果に対して lmin で min(my, right_neighbor) を計算し、
swap_flag を記録。cross_L2B まで対応。

| ファイル | 内容 | テストターゲット |
|---------|------|---------------|
| `step7.vsm` | cross MAB + L1B + L2B | `test-step7` |
| `step7_without_cross.vsm` | cross なし（偶数奇数交換用、MAB 内で完結） | `test-step7-without-cross` |
| `step7_only_cross_MAB.vsm` | cross MAB のみ | — |

---

### Step 8: step6 + step7 を 1 カーネルに統合

step6（左隣 + lmax）と step7（右隣 + lmin）を subpeid の偶奇で切り替えて
1 カーネルで実行。reduce はしない。
cross_L2B まで対応。

| ファイル | 内容 | テストターゲット |
|---------|------|---------------|
| `step8.vsm` | cross MAB + L1B + L2B | `test-step8` |
| `step8_without_cross.vsm` | cross なし（MAB 内で完結） | `test-step8-without-cross` |
| `step8_only_cross_MAB.vsm` | cross MAB のみ | — |

## C ホストテスト (c_step) 一覧

C ホストプログラムによる E2E テスト。debug_write/debug_read または mnc2 API を使用。
CI では `build test:` ターゲットがあるものが自動実行される。

### vsm の種類

| 拡張子 | ビルドパイプライン | 境界判定 |
|--------|------------------|---------|
| `.vsm` | `assemble3` で直接アセンブル | HW レジスタ直接 or 境界判定なし |
| `._vsm` + `.param` | `vsmlink` → `.vsm` → `assemble3` | `@boundary_flags` distribute |

c_step0〜1, c_step9 は cross を使わないため `.vsm` のまま。
c_step2〜8 は単一カーネルで `_vsm` + `.param` に変換済み。
c_step10〜13 は複数カーネル構成で、cross を使うカーネルのみ `_vsm` に変換。

### 一覧

| ディレクトリ | 内容 | vsm | 階層 | I/O 方式 |
|------------|------|-----|------|---------|
| `c_step0` | subpeid+1 を LM8 に書いて検証 | `.vsm` | — | debug_read |
| `c_step1` | msl MAB 内左シフト | `.vsm` | MAB 内 | debug_write/read |
| `c_step2` | 左隣取得 (cross MAB) | `._vsm` | cross_MAB | debug_write/read |
| `c_step3` | 左隣取得 (cross MAB + L1B) | `._vsm` | cross_MAB + L1B | debug_write/read |
| `c_step4` | 左隣取得 (全階層) | `._vsm` | cross_MAB + L1B + L2B | debug_write/read |
| `c_step5` | 右隣取得 (全階層) | `._vsm` | cross_MAB + L1B + L2B | debug_write/read |
| `c_step6` | 左隣 + lmax + swap 検出 | `._vsm` | cross_MAB + L1B + L2B | debug_write/read |
| `c_step7` | 右隣 + lmin + swap 検出 | `._vsm` | cross_MAB + L1B + L2B | debug_write/read |
| `c_step8` | 偶奇統合 1 フェーズ (左右統合 + swap) | `._vsm` | cross_MAB + L1B + L2B | debug_write/read |
| `c_step9` | l1bmrlbor 縮約 単体テスト | `.vsm` | L1B 内 | debug_write/read |
| `c_step10` | 偶奇統合 + reduce iadd | `._vsm` | cross_MAB + L1B + L2B | debug_write/read |
| `c_step11` | 偶奇統合 + reduce (mnc2_send 入力) | `._vsm` | cross_MAB + L1B + L2B | mnc2_send + debug_read | **注: cross L2B が minimal と非 minimal の混在** |
| `c_step12` | マルチカーネル: init + exec + reduce | `exec_odd._vsm` | cross_MAB + L1B + L2B | mnc2_send + debug_read | **注: cross L2B が非 minimal（全 L1B collect, n512）。c_step4〜10 の minimal 最適化が失われている** |
| `c_step13` | Odd-Even Sort 完全版 | `exec_odd._vsm` | cross_MAB + L1B + L2B | mnc2_send + debug_read | **注: c_step12 と同様、非 minimal** |

### c_step2〜10: boundary flags の PDM 配置

`@boundary_flags` は PDM addr 0 に配置。ホストが `collected_flags.bin` を `mnc2_send` で PDM に送り、カーネル実行時に `@boundary_flags` ディレクティブが PDM → PE に distribute する。

### c_step11〜13: データと boundary flags の PDM 共存

データ自体も PDM 経由で送る c_step11 以降では、PDM アドレスが競合しないよう配置を分ける。
sort_host.c と同じパターンで、boundary flags は **非トリガー (dmaid=0x00)** で先に送り、データは **トリガー (dmaid=0x10)** で後から送る。

| PDM 領域 | addr | 用途 | dmaid |
|----------|------|------|-------|
| `$p0`〜`$p4095` | 0 | データ | `0x10` (トリガー) |
| `$p4096`〜`$p8191` | 4096 | boundary flags | `0x00` (非トリガー) |

c_step12〜13 のマルチカーネル構成では、`exec_odd` カーネル実行前に boundary flags を `dmaid=0x10` で再送し、`@boundary_flags` 展開内の `wait i10` のトリガーとする。

### c_step12〜13: カーネル構成

| カーネル | vsm | cross | 説明 |
|---------|-----|---------|------|
| `init.vsm` | `.vsm` | なし | PDM→LM0 配布 + PE ID 計算 |
| `exec_even.vsm` | `.vsm` | なし | 偶数フェーズ (MAB 内ペア) |
| `exec_odd._vsm` | `._vsm` | あり | 奇数フェーズ (cross MAB/L1B/L2B) |
| `reduce.vsm` | `.vsm` | なし | l1bmrliadd で swap_flag 合計 |
| `exec_swap0.vsm` | `.vsm` | なし | 全 PE swap_flag=0 (テスト用) |
| `exec_swap1.vsm` | `.vsm` | なし | 全 PE swap_flag=1 (テスト用) |

`exec_even` は MAB 内の (PE0,PE1), (PE2,PE3) ペアのみで cross 不要。
`exec_odd` だけが PE3↔PE0 ペアで cross MAB/L1B/L2B が発生するため `._vsm` + `@boundary_flags` が必要。

### c_step13: Odd-Even Sort

```
Usage: ./ex_step13 <in-data.bin> <out-data.bin> [step]
```

- `in-data.bin`: 4096 個の fp64 (little-endian, 32768 bytes)
- `out-data.bin`: ソート結果の出力先
- `step` (省略可): この番号以降のパスで中間データを `out-data-step-N.bin` に出力

フロー: init → (even, odd, reduce)* → swap_count == 0 で収束 → debug_read で結果出力 → 昇順検証。

### テストデータ

`data/` にコミット済みの固定テストデータを使用する:

| ファイル | 内容 | 収束パス数 |
|---------|------|-----------|
| `data/sorted.bin` | 昇順 [0, 1, ..., 4095] | 0 |
| `data/reversed.bin` | 降順 [4095, ..., 0] | ~2048 (worst case) |
| `data/nearly_sorted.bin` | 隣接 50 要素内シャッフル (seed=42) | ~50 |
| `data/random.bin` | 完全シャッフル (seed=123) | ~4096 |

CI では `data/nearly_sorted.bin` を使用（テストとして意味があり、かつ現実的な時間で終わる）。

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

## スタンドアロンテスト

### step2.vsm

cross MAB（offset -1）のテスト。`@boundary_flags compute` の簡易版（PE_left_edge のみ）を使い、PE0 は l1bmd+1、PE1-3 は msl で左隣データを取得する。

```bash
source ../../_mncore2-sdk-v1/bin/activate
assemble3 step2.vsm --output-file _build/step2.asm
gpfn3_package_main -i _build/step2.asm --offchip-memory-init zero
```

入力（LM0）:
```
MAB0: PE0=10(0xA), PE1=20(0x14), PE2=30(0x1E), PE3=40(0x28)
MAB1: PE0=50(0x32), PE1=60(0x3C), PE2=70(0x46), PE3=80(0x50)
MAB2: PE0=90(0x5A), PE1=100(0x64), PE2=110(0x6E), PE3=120(0x78)
```

期待出力（LM8）:
```
MAB0: PE0=0x0(wrap), PE1=0xA, PE2=0x14, PE3=0x1E
MAB1: PE0=0x28,      PE1=0x32, PE2=0x3C, PE3=0x46
MAB2: PE0=0x50,      PE1=0x5A, PE2=0x64, PE3=0x6E
```

## 状態

step4 まで実装・テスト済み（step3_bf, step4/minimal/bf 全 PASS）。
