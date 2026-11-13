# 31-nbody-host-mediated-loop: kernel 分割機能を使った 3D N 体

`examples/28-nbody-3d` (3D N 体、全対全の力計算) を基礎に、vsmlink コマンドに入った
kernel 分割機能 (`@kernel_begin`/`@kernel_end`) を、vsmlink コマンドの単体テスト
の外で、実際のアプリケーションに対して使ってみる example として作った。

## 一見遠回りに見える構成 (意図的)

**この 6 個の kernel はどれも機能的には独立しており、本来なら 28-nbody-3d の
`init._vsm`/`kick._vsm`/`drift._vsm`/`collect_pos._vsm` のように、1 個ずつ別々に
`vsmlink` へかければ済む（`*._vsm` → `*.vsm` の 1 対 1）。kernel 分割は無くても
このアプリケーションは成立する。**

それをあえて「1 本にまとめてから分割する」形にしているのは、**MNCL を見据えてのこと**
である。実際の MNCL は複数 kernel を最初から 1 本の `_vsm` として出力する
(単体テストの入力は実物の MNCL 生成物の例で、1 本のファイルに
`@kernel_begin writer` / `@kernel_begin reader` が両方入っている)。将来 MNCL が
このような複数 kernel 入りの `_vsm` を実際に出すようになったとき、vsmlink の kernel 分割が
小さな検証用の入力だけでなく、**実アプリケーション規模の
入力に対しても正しく動くこと**を、この example で先に確認しておく。

具体的な構成:

- `init._vsm` / `reset_acc._vsm` / `kick._vsm` / `drift._vsm` / `collect_pos._vsm` は
  最初から別ファイルとして手書きしている (28-nbody-3d が kernel ごとに別ファイルで書く
  慣習をそのまま踏襲した)。**kernel 分割が実際に起きるのはビルド時**で、これらのファイルを
  見ているだけでは分からない
1. `gen_nbody31.py` がこの 5 個の手書きファイルと、生成した `accumulate` の中身を
   `@kernel_begin`/`@kernel_end` で包み、**1 本の `_build/nbody31._vsm` に合成する**
   (init / reset_acc / accumulate / kick / drift / collect_pos の 6 kernel が並ぶ。
   `ninja` でビルドした後に `grep kernel _build/nbody31._vsm` で実際に確認できる)
2. `vsmlink` がその 1 本を **6 個の `.vsm` に分割し直す** (`_build/k/nbody31-*.vsm`)

## この example が示すこと (作業を進める中で分かったこと)

もともとの目的は「kernel 分割機能を使う example を作る」ことであり、以下の結論はそれを
目指して見つけたものではなく、作業を進める過程で偶然見つかったものである。

**kernel 数を N に依存させないことと、host が介在しない device 側だけのループは、
同時には成立しない。** これは実装上の工夫が足りないのではなく、MN-Core2 の次の 2 つの
性質から必然的に生じる制約である。

- `@broadcast` が読む PDM 番地は vsmlink の link 時に固定され、実行時に host が番地を
  選び直す手段が無い (番地は `mvb` 命令に即値で
  焼き込まれる)
- MN-Core2 はループを持たない静的スケジュールの装置。データ依存の分岐も、実行時に
  自動で進む番地参照も無い

この 2 点により、1 個の kernel では足りない大きさのデータを device 側だけで処理するには、
必要な数だけ番地の異なる kernel を並べる (28 の方式。kernel 数が N に依存するが host は
介在しない) か、1 個の kernel を使い回してそのたびに host がデータを詰め替える (31 の方式。
kernel 数は N に依存しないが host が介在する) かの二択しかない。kernel 分割は
「1 つの `_vsm` に含まれる別々の名前を持つ kernel を切り出す」機能であり、この二択
そのものを解消する機能ではない。

**検討したが採らなかった案**: PDM へのデータ転送を毎回行う代わりに、assemble3 が生成した
`.idma.dat` に焼き込まれている PDM 番地 (即値) を、host がバイナリを直接パッチして書き換える
案もありうる。これなら PDM 転送そのものを省ける可能性がある。ただしこれは vsmlink /
assemble3 が提供する抽象 (`.param` の slot 経由の番地指定) を素通りして命令エンコーディングの
詳細に host コードが直接依存するハックであり、今回は採らなかった。

## 28 との違い

28 は全対全の力計算 (accumulate) を「粒子 128 個ずつ、分割ごとに別々の `_vsm`/`.param` を
生成して別々に vsmlink を呼ぶ」方式にしている (`gen_accumulate.py` + `gen_chunks_ninja.py`)。
N=4096 では accumulate カーネルが 32 個でき、host は 32 個すべてをロードして順に exec する。
kernel 数が N に依存する。

31 は accumulate を **CHUNK 粒子ずつ固定の PDM 領域を読む 1 個の kernel** にする。host は
「次の区間をその領域へ送信 -> 同じ kernel を exec」を繰り返す。ループが host 側に移るので、
kernel 数は N に依存しない (6 個で固定)。手書き kernel 5 個 + 生成 accumulate kernel を
`@kernel_begin`/`@kernel_end` で 1 本の `_vsm` にまとめ、vsmlink 1 回の呼び出しで
kernel ごとの `.vsm` に分割させる。

同じ kernel を繰り返し exec して都度 PDM から読み直す形自体は 31 が最初ではない。
`examples/26-broadcast-accumulate-2d` に前例があり (送信 -> exec -> 次の送信、を繰り返す)、
31 はこれを 3D 重力・複数ステップの規模に広げたもの。

**速度 (vx,vy,vz) はホストへ回収しない。** device 上では kick が leapfrog の一部として
毎ステップ更新し LM に保持し続けるが (drift が次ステップで使うため)、host が読み出すのは
位置だけにする。したがって物理検証は位置の値照合と束縛 (最大半径) のみで、エネルギー保存の
検証は行わない (KE の計算に速度が要るため)。

`examples/30-soa2aos` (collect の SoA 出力を device 上で AoS に並べ替える) は今回は使わない。
28 と同じく、collect した位置を host が粒子ごとの並びに詰め替えてから次の力の評価に渡す。

処理性能 (実行速度) は今回の評価対象にしていない。CHUNK ごとに send + exec を繰り返す分、
host <-> PDM の往復回数は 28 より大幅に多い。

## kernel 構成 (6 個、N に依存しない)

| kernel 名 | 役割 | 28 での対応 |
|---|---|---|
| `init` | 位置・速度を distribute、定数を broadcast、accumulator を 0 | `init._vsm` |
| `reset_acc` | accumulator ax,ay,az を 0 に戻す | `gen_accumulate.py` の `PREAMBLE` |
| `accumulate` | CHUNK 個の粒子から力を積算 (固定 PDM 領域を読む) | `gen_accumulate.py` の本体 |
| `kick` | `v += a*(dt/2)` (LM 内で完結、host へは回収しない) | `kick._vsm` |
| `drift` | `x += v*dt` | `drift._vsm` |
| `collect_pos` | 更新後の位置を回収 | `collect_pos._vsm` |

28 は「先頭分割だけ accumulator をリセット」だったが、31 は accumulate が全区間で同じ 1 個の
kernel なので、リセットを `reset_acc` として独立させている。

## 実行順序

```
init                                  粒子を distribute (1 回だけ)
[力の評価]                            ループ前の初回ぶん
for step = 0 .. NSTEP-1:              1 step
    kick                              v += a*(dt/2)
    drift                             x += v*dt
    collect_pos -> recv               更新後の位置を host へ (成分ごと)
    host が粒子ごとの並びに詰め替える
    [力の評価]
    kick                              v += a*(dt/2)  (速度は host へ回収しない)
```

`[力の評価]` の中身:

```
reset_acc                                     accumulator を 0 に戻す
for g = 0 .. NCHUNK-1:                        NCHUNK = ceil(N / CHUNK)
    IDMA idle 待ち                             前の exec が PDM を読み終わるのを待つ
    粒子 [g*CHUNK, (g+1)*CHUNK) を PDM0 の固定領域へ送信
    accumulate を exec                        中は CHUNK 個ぶんの broadcast + 計算が並ぶ
```

力の評価は初回ぶんを含めて **NSTEP+1 回**。既定の `nstep=3` なら 4 回、`accumulate` の
exec 回数は全体で `(NSTEP+1) * NCHUNK` 回になる。

粒子を渡す順序は `0, 1, ..., N-1` で 28 と完全に一致する。浮動小数点の加算順序が同じなので、
値は **bit 一致する**はず (N=32 での確認結果は「検証」節を参照)。M が CHUNK の倍数でない
場合、末尾の区間は質量 0 の粒子で埋める (力への寄与が 0 になるので無害)。

IDMA idle 待ちを毎回入れるのは、同じ PDM 領域を書き換えながら回すため。前の exec が
読み終わる前に上書きすると壊れる。28 は全 N を 1 回置くだけで済んでいたので待ちが要らず、
IDMA queue depth 31 の管理 (`exec_q`) をしていたが、31 は毎回完全に drain するので不要。

## PDM レイアウトと slot 番号

PDM の番地は 28 と同じ (u64 単位)。違うのは **`.param` が 1 本になる**点で、slot 番号は
全 kernel を通じて重複しない値にしている (28 は kernel ごとに `.param` が別だったので
slot 8 を使い回せた)。

| slot | 番地 (u64) | 内容 | 使う kernel |
|---|---|---|---|
| 8 | 0 | 位置 (x,y,z,m) distribute | init |
| 16 | 16384 | 定数 broadcast | init |
| 24 | 40960 | 速度 distribute | init |
| 32 | 131072 | 更新後の位置 collect | collect_pos |
| 40 | 20480 | 他粒子 broadcast (固定領域。CHUNK 個のブロックが同じ slot の続きを読む) | accumulate |

速度 (slot 24) は init が distribute するだけで、collect する kernel は無い (回収しない
ため)。collect の slot は 1 つだけなので (pos のみ)、`:recv_wait_tag` を複数値に分ける
必要は無い (該当が 1 個しかない)。

## CHUNK は 16

`@broadcast` は、slot の 1 回目で PDM から 64 u64 を L1BM に移動し、同じ slot の 2 回目以降は
その続きを消費する。64 u64 を使い切ったら次の 64 u64 を移動する
(`share/vsmlink/directives-spec.md` の「@broadcast」)。粒子 1 個は 4 u64 なので、
**1 回の移動 = 粒子 16 個**。

- CHUNK=16 なら accumulate が出す PDM 転送 (`mvb`) はちょうど 1 回
- 送信完了待ち (`wait i10`) は、kernel の中で slot 40 を最初に読む前の 1 回である
  (`share/vsmlink/param-spec.md` の「送信完了待ち」)。host の 1 回の送信の完了を待つので、
  CHUNK を 32 以上にして転送が 2 回以上になっても、2 回目以降の転送が読むデータは送り終えている
- CHUNK を 16 の倍数にしないと、最後に移動する 64 u64 の一部を使わずに終わる (結果は変わらない)
- N=32 / 128 / 1024 / 4096 はすべて 16 の倍数

`build.ninja` の `chunk` 変数で調整できる。

## ファイル構成

| ファイル | 役割 |
|---------|------|
| `init._vsm` `reset_acc._vsm` `kick._vsm` `drift._vsm` `collect_pos._vsm` | 手書き kernel の本体 (28 からの複製 + slot 番号の付け替え) |
| `gen_nbody31.py` | 手書き kernel 5 個 + 生成 accumulate kernel を `@kernel_begin`/`@kernel_end` で束ね、1 本の `_vsm` と `.param` を出力する |
| `nbody31.stparam` | 28 の `nbody3d.stparam` と同一内容 |
| `gen_ic.py` | CI 用の束縛系を struct 形バイナリで生成 (28 と同一) |
| `test_nbody31.c` | 入力を mmap で読み、init + 初回力評価 + (kick+drift+collect_pos+書き戻し+力評価+kick) x NSTEP、`_result/` へ位置スナップショット出力、host 倍精度の leapfrog と位置を照合し、束縛 (最大半径) を見る |
| `build.ninja` | 生成・vsmlink 分割・assemble3・host C ビルド・test target |
| `expected-init.vsm` | `verify` 用の golden (init kernel の展開結果) |

## ビルド・実行

```
source scripts/overlay
ninja -C examples/31-nbody-host-mediated-loop              # 生成 + vsmlink (kernel 分割)
ninja -C examples/31-nbody-host-mediated-loop verify        # expected-init.vsm と diff
ninja -C examples/31-nbody-host-mediated-loop build-e2e     # + assemble3 + C ビルド
ninja -C examples/31-nbody-host-mediated-loop test-emu-lib  # emu:lib で実行・照合 (CI は N=32)
```

### device で大きい N

```
../../libmnc2-0.4.1-rc2/u02-run-idma/_build/reset
ninja -C examples/31-nbody-host-mediated-loop test-device-n4096   # または -n128 / -n1024
```

N=128/1024/4096 の実機実行、および 28 との実機照合は未実行 (別途判断)。

## 検証状況

- **vsmlink の kernel 分割**: `ninja` で 6 個の `.vsm` を生成、`ninja verify` で init kernel の
  golden 一致を確認済み。`accumulate` kernel は CHUNK=16 で、PDM 転送 (`mvb`) と
  送信完了待ち (`wait i10`) がそれぞれ 1 回だけ生成されることを確認済み
- **assemble3 以降 (build-e2e / test-emu-lib、N=32)**: `ninja build-e2e` で 6 kernel の
  assemble3 と host C のビルドが通ることを確認済み。`ninja test-emu-lib` で emulator 実行・
  host 倍精度 leapfrog との値照合まで PASS (`max_rel=1.457e-14`)。
  (過去の版のこの節には「SDK (`_mncore2-sdk-v1`) 自体が無く `assemble3` が使えないため
  未実行」と書かれていたが誤りだった。実際の SDK は `_mncore2-sdk-v0.6` に存在し、
  `_mncore2-sdk-v1` という存在しないディレクトリ名を探していたことが原因だった。)
- **実機 (N=128/1024/4096)**: 未実行 (別途判断)
