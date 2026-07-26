# 13-odd-even-sort-gid

`10-odd-even-sort` の **get_global_id 版**。 奇偶転置ソートの 1 turn を 4096 PE, fp64 で実行する
のは例 10 と同じで、 違いは **「自分がペアの左メンバか」の判定を host が渡す flag ではなく、
PE 自身が `get_global_id` で行う**点だけ。

例 10 は「PE ID を得る組み込み関数が現状の MNCL に無い」ため host flag 方式にしていた
（例 10 README の「even と odd が同じ内容な理由」の最後）。 `get_global_id` が i64 で使える
ようになったので、 その制約が外れたことを示す例。

## 例 10 との差分

例 10 の flag パターンは PE 番号のパリティそのものである。

- even フェーズ: flag = {1,0,1,0,...} = 「PE 番号が偶数なら左メンバ」
- odd  フェーズ: flag = {0,1,0,1,...} = 「PE 番号が奇数なら左メンバ」

したがって kernel の 1 行を置き換えるだけでよい。

```c
// 例 10（host flag 方式）
double is_left = distribute(flag);

// この例（get_global_id 方式）
long id = get_global_id(0);
double is_left = ((id & 1) == 0) ? 1.0 : 0.0;   // even。 odd は ((id & 1) != 0)
```

**kernel 本体（比較・swap・collect）と境界処理（`neighbor` の clamp = boundary_flags）は例 10 と
完全に同一**。 3 kernel 構成（even / odd / reduce）、 データの ping-pong 2 バッファ、 検証の
3 パターン（desc / sorted / dup）も例 10 のまま。 設計の理由は例 10 の README を参照。

## この例が使う整数機能

`is_left = ((id & 1) == 0) ? 1.0 : 0.0` をコンパイルするには、 i64（64-bit 整数）の次の機能が要る。

1. **i64 定数**: `id & 1` の `1` を扱う。
2. **i64 の等値比較（`== 0` / `!= 0`）**: `(id & 1) == 0` を条件分岐・選択のためのマスクに変換する。
   順序比較（`<` `>` など）は現状まだ対応していない。

これらと `get_global_id` の long 対応により、 host が flag を配らなくても PE が左メンバか判定できる。
比較の結果を使った `if` / `else` / 三項演算子は f64 と同じ機構で動く。

## @identify（id 配列の配布）

`get_global_id` は vsmlink の `@identify` に展開され、 **`@distribute` と同じく PDM から PE 番号を
配る**。 そのため host が id 配列 [0..4095] を PDM へ事前送信する必要がある（送らないと全 PE が 0 に
なる）。 `.param` に `(:identify 0 :addr 4096 ...)` を置き、 host が `sendbuf_ids` を送る。
addr 4096 は、 flag を廃止して空いた例 10 の flag スロットを再利用している。

id 配列は even/odd で同じ内容（PE 番号）なので、 例 10 の flag と同じ送信タイミングに乗せている:

- even: id を tag 0 で置いてから data_a を tag #x10 で送る（data が trigger）
- odd : id を tag #x10 で送る（id が trigger。 data_b は device に残っている）

## ビルドと実行

```bash
ninja build-e2e     # 3 kernel の .asm / .idma.dat と host バイナリを生成
ninja test-emu-lib  # host エミュレータで実行
ninja test-device   # 実機で実行
```

## 検証の内容

例 10 と同一。 host は C 側で同じ 2 フェーズを適用した参照配列と device の出力 data を全要素比較し、
swap 回数もフェーズごとに照合する。 入力は desc / sorted / dup の 3 パターン。 とくに dup は
偶数ペアが等値なので、 strict `>` が `>=` に退行していないか（等値ペアを swap しないか）を検査する。
get_global_id 版でも例 10 と同じ結果になる（even/odd の左メンバ判定が flag 版と一致することの確認）。

## 今後の課題（13 / 14 の sort example 共通）

- **入力を stdin から取る設計にする。** 現状 13 も 14（full 版）も入力を host 内で生成している。
  ソートは stdin から u64/double×4096 を読む形にすれば、再コンパイルせずに任意の入力
  （最悪ケースの逆順 4096 や実データ）を流せる。検証は「読んだ入力を host 内で qsort して比較」で同じにできる。
  元の 99-odd-even-sort-full は stdin 入力だったが、14 では内部生成に寄せた（要 stdin 化）。
- **ランダムデータでの検証。** 現状の入力は決め打ち（13 は desc/sorted/dup、14 はブロック逆順）。
  seed 固定のランダム置換も流せるようにすると網羅性が上がる（ただし emu 実行時間は入力の収束 turn 数に依る）。
