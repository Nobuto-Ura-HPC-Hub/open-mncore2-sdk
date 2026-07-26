# libmnc2 examples

リンクは `-lmnc2 -lgpfn3`。backend はリンクされた libgpfn3.so で決まる。

## ディレクトリ構成

```
share/examples/libmnc2/
├── 01-nop/ 〜 55-*/    ← 番号付き examples（各 example に build.ninja + C + VSM）
├── ulib/               ← 共有ヘッダライブラリ（u01/u02 が使用）
├── u01-mmap/           ← 低レベル PIO/DMA/PDM ツール（libgpfn3 直接依存、実機のみ）
└── u02-run-idma/       ← idma.dat 実行ツール（libmnc2 依存）
```

## テスト実行

各 example ディレクトリで:

    ninja test-emu       # emu:process (gpfn3_package_main 経由)
    ninja test-emu-lib   # emu:lib (インプロセスエミュレータ)
    ninja test-device    # 実機

前提: SDK activate 済み（assemble3 が PATH にあること）。
emu:process はさらに gpfn3_package_main が必要。

## ユーティリティツール (u01-mmap / u02-run-idma)

u01-mmap は libgpfn3 を直接使う低レベルツール群。**実機環境でのみビルド可能**。

    cd u01-mmap && ninja && ninja install    # ../bin/ にツールを配置

u02-run-idma は libmnc2 経由で idma.dat を実行するツール。

    cd u02-run-idma && ninja && ninja install    # ../bin/ にツールを配置

`ninja install` でビルド済みバイナリが `bin/` に配置される。
他の examples から `../bin/read-pdm` 等で参照できる。

### ツール一覧

| コマンド | ソース | 依存 | 用途 |
|---|---|---|---|
| pio-read | u01-mmap | libgpfn3 | PIO レジスタ読み出し |
| pio-write | u01-mmap | libgpfn3 | PIO レジスタ書き込み |
| send-dma | u01-mmap | libgpfn3 | DMA 送信 (Host→PDM) |
| read-pdm | u01-mmap | libgpfn3 | PDM mmap → stdout (バイナリ) |
| write-pdm | u01-mmap | libgpfn3 | stdin → PDM mmap |
| run-idma | u02-run-idma | libmnc2 | 任意の idma.dat を実行 |

### ulib/

u01/u02 が共有するヘッダオンリーライブラリ。データパーサ等を提供。
examples から直接使うことはない。

## カーネルデータ

各 example の `data/` には以下が含まれる:

- `.vsm` — カーネルソース
- `.asm` / `.idma.dat` — ビルド済みサンプル (assemble3 なしでもすぐ実行可能)

実際の開発では `.vsm` から `assemble3` でビルドし、生成物を使う:

    assemble3 kernel.vsm --output-file _build/kernel.asm          # テキスト (.asm)
    assemble3 kernel.vsm --output-file _build/kernel --loader     # バイナリ (.idma.dat)

## PE スケールについて

基本方針は **4096 PE 全部にデータを流して検証する** (BLOCK_ELEMS / ELEM_COUNT = 4096)。
ただし以下の examples は **目的的に小規模で問題なし**、もしくは別事情があるので 4096 化の対象外:

### 意図的に小規模 (機能検証、PE スケールの目的なし)

| example | 規模 | 目的 |
|---|---|---|
| 01-nop | 8 doubles | API パラメータ境界検証 (`mnc2_send` / `mnc2_recv` の validate_params) |
| 51-wd-zero | 8 doubles | WD=0 無条件 DMA 開始の検証 1 点だけ |
| 46-debug-write | 4 u64 × 6 region | `mnc2_debug_write` / `mnc2_debug_read` API 単体の roundtrip |
| 47-debug-read-write | 8 PE (MAB0-1 × PE0-3) | `debug_write` + msl カーネル + `debug_read` 統合デモ |
| 27-mv-1a ~ 42-mv-15 (16 個) | 64 要素 | MV 命令パターン (scatter / gather / broadcast / distribute / reduce) の機能検証 |

### 見た目は小さいが実態は全 4096 PE

| example | DMA buffer | 実態 |
|---|---|---|
| 48-reduce-pe | 64 doubles | **縮約命令チェイン `l1bmrdfadd → l2bmrdfadd → mvrdfadd` の動作検証**。`l1bmp` で L1BM の先頭 4 要素を各 PE position に配布 → 全 4096 PE が同じ 1.0 を `$lr0` に持つ (残り 60 要素は未使用)。縮約結果 `1024.0 × 4 PE position = 4096.0` で全 PE が関与したことを **間接的に検証**。各 PE に固有値を入れる bijection テストではない |
| 49-reduce-pe-vec | 64 doubles | 48 の v 付き版。`l1bmp v` で先頭 4 要素を 4 レジスタ (`$lr0, $lr2, $lr4, $lr6`) に配布 → 全 PE が同じ `[1.0, 2.0, 3.0, 4.0]` を持つ。4 cycle × 4 PE position = 16 u64 を recv して縮約命令の動作を検証 |

### 別事情

- **03-vecadd-pfn** (64 u64): PFN 提供の簡易版カーネルを持ち込んだもの。中途半端は既知で放置

## Known XFAIL

- **45-multi-collect** (emu-lib): 同一 `recv_wait_tag` を 2 回再利用する pattern
  を exec する example。 これは API の誤用で、 各 slot に distinct wait_tag を
  割り当てるのが正しい (実機実装、 vsm-linker 生成も同様)。 emu:lib backend は
  同タグ再利用を `double_assert` エラーで弾く。 本 example は誤った pattern を
  exec しているため fail が期待される。 本来は slot 別 tag で書き直すべき。

- **57-pdm-repeat-add** (emu-lib): `libgpfn3.so` の emulator (upstream pfcomp の
  `consume_dt()`) のバグで fail。 `consume_dt()` が `done_flags[mvid].piu_dt` を
  立てる mmode リストから `pdmpdm` (PDM→PDM、 mode 8) が漏れている。 結果、
  PDM→PDM mvp 命令が DDMA event を host に emit せず、 `mnc2_recv` が timeout
  (DDMA_STAT bit が立たず recv rc=-5)。 PFN 修正版が降りるまで暫定で XFAIL。

## broadcast example (l1bmp vs l1bmm)

全 PE に同値を配る broadcast の正は **l1bmp + vector dst + cycle mask** (`l1bmp $lb0 $lr0v/1000`)。
一方 l1bmm は MAB 内 4 PE への分配命令で、 broadcast には流用になる。

| example | 命令 | 内容 |
|---|---|---|
| `60-broadcast-pdm-pe` | l1bmm | **l1bmm の検証**: 周期観察 (全 PE LM0[0] = 周期 4 [12,13,14,15]) |
| `62-broadcast-subpe` | l1bmm | **l1bmm の検証**: sub_pe 別分配 (PDM[12..15] を 4 sub_pe に) |
| `63-broadcast-lm-cycle` | l1bmm | **l1bmm の検証**: 4 cycle 書き込み位置の切り分け |
| `66-broadcast-l1bmp` | l1bmp | **broadcast の正**: l1bmp + v + /1000 で全 4096 PE が同値 (周期なし) |
| `67-l1bmp-2u64` | l1bmp | **l1bmp の 1 u64 版 / 2 u64 版の対応を実測**: L1BM の位置とレジスタの写像。collect で回収するので 3 backend すべてで走る |

**60/62/63 は l1bmm の挙動検証が本題**なので l1bmm のまま維持する。全 PE 同値の broadcast を素直に
示すのは 66 (l1bmp)。 回帰テストは `tests/functional/test_broadcast_l1bmp` (l1bmp + v + /1000)。

`65-nbody-like` は **distribute と broadcast を 1 つの計算の中で対比**する example。
distribute は各 PE に違う 2 値 (x_j, m_j) を配るので実データ 8192 u64 を運ぶ必要があり、
上流 (mvp, l2bmb) の本数が要る。broadcast は 32 粒子の (x_i, m_i) が全 PE で同じ値なので、
l2bmb が 1 回で運ぶ 64 u64 を組で埋めきれば mvb 1 個 + l2bmb 1 個で足りる。
どちらも `v` の 4 cycle を使い切る形にしてあり、host 側の詰め物は無い。

## examples 一覧

**backend 列の凡例** (2026-04-15):
- `✓` = 本質的に対応 (テスト harness / カーネル / 使用 API がその backend で動作可能)
- `✗` = 本質的に非対応 (使用 API がその backend に存在しない等の構造的制約)
- `?` = 要議論 (harness 修正で対応可能かもしれない / pod 実績で未確定)

この列は **動作可否 (ground truth)** を示すもので、現状の test suite 構成とは
必ずしも一致しない。ズレは整理すべき箇所 (`priorities.md` のぬけおち checklist 参照)。

| # | ディレクトリ | 内容 | emu:process | emu:lib | device |
|---|---|---|:-:|:-:|:-:|
| 01 | 01-nop/ | 8 doubles DMA round-trip (nop カーネル) + バリデーション | ✓ | ✓ | ✓ |
| 02 | 02-roundtrip/ | 4096 doubles PDM→PE(LM)→PDM 往復検証 | ✓ | ✓ | ✓ |
| 03 | 03-vecadd-pfn/ | 64 uint64_t 整数加算 (ladd)。PFN 提供の簡易版カーネル | ✓ | ✓ | ✓ |
| 04 | 04-vecadd/ | 4096 doubles 浮動小数点加算 (dvadd)。正式規模 | ✓ | ✓ | ✓ |
| 05 | 05-debug-read/ | `mnc2_debug_read` API で各メモリ階層 (L2BM/L1BM/LM/GRF) の値を検証 | ✗ | ✓ | ✗ |
| 06 | 06-put-get/ | put+get カーネルペアで各メモリ階層の roundtrip 検証 (カーネル間 state 必須) | ✗ | ✓ | ✓ |
| 07 | 07-put-l2bm/ | L2BM distribute: PDM → L2BM (mvp)。device verify は peek_l2bm + read-pdm pipeline で外部から | ✓ | ✓ | ✓ |
| 08 | 08-put-l1bm/ | L1BM distribute: PDM → L2BM → L1BM。device verify は peek_l1bm pipeline | ✓ | ✓ | ✓ |
| 09 | 09-put-lm/ | LM0 distribute: PDM → L2BM → L1BM → LM0。device verify は peek_lm pipeline | ✓ | ✓ | ✓ |
| 10 | 10-put-ln/ | LM1 distribute: PDM → L2BM → L1BM → LM1。device verify は peek_ln pipeline | ✓ | ✓ | ✓ |
| 11 | 11-put-grf0/ | GRF0 distribute: PDM → ... → LM0 → GRF0。device verify は peek_grf pipeline | ✓ | ✓ | ✓ |
| 12 | 12-put-grf1/ | GRF1 distribute: PDM → ... → LM0 → GRF1。device verify は peek_grf1 pipeline | ✓ | ✓ | ✓ |
| 13 | 13-get-l2bm/ | L2BM collect roundtrip: L2BM → PDM (前カーネルの state 必須) | ✗ | ✓ | ✓ |
| 14 | 14-get-l1bm/ | L1BM collect roundtrip: L1BM → L2BM → PDM (同上) | ✗ | ✓ | ✓ |
| 15 | 15-get-lm/ | LM0 collect roundtrip: LM0 → L1BM → L2BM → PDM (同上) | ✗ | ✓ | ✓ |
| 16 | 16-get-ln/ | LM1 collect roundtrip: LM1 → L1BM → L2BM → PDM (同上) | ✗ | ✓ | ✓ |
| 17 | 17-get-grf0/ | GRF0 collect roundtrip: GRF0 → LM0 → ... → PDM (同上) | ✗ | ✓ | ✓ |
| 18 | 18-get-grf1/ | GRF1 collect roundtrip: GRF1 → LM0 → ... → PDM (同上) | ✗ | ✓ | ✓ |
| 19 | 19-t1-msr/ | msr 方向: PE[i] ← PE[i+1] (データ左シフト) | ✓ | ✓ | ✓ |
| 20 | 20-t2-msl/ | msl 方向: PE[i] ← PE[i-1] (データ右シフト) | ✓ | ✓ | ✓ |
| 21 | 21-t3-l1bmd-plus1/ | l1bmd+1: MAB[N-1] から読む | ✓ | ✓ | ✓ |
| 22 | 22-t4-l1bmd-minus1/ | l1bmd-1: MAB[N+1] から読む | ✓ | ✓ | ✓ |
| 23 | 23-t5-maskr-pe0/ | maskr + ipassa: PE[0] のみ元の値を保持 | ✓ | ✓ | ✓ |
| 24 | 24-t6-maskr-pe3/ | maskr + ipassa: PE[3] のみ元の値を保持 | ✓ | ✓ | ✓ |
| 25 | 25-t7-left-neighbor/ | 左隣統合: msr + l1bmd+1 + maskr | ✓ | ✓ | ✓ |
| 26 | 26-t8-right-neighbor/ | 右隣統合: msl + l1bmd-1 + maskr | ✓ | ✓ | ✓ |
| 27 | 27-mv-1a/ | MV PDM→PDM scatter (3x mvp, 3.5.8.8) | ✓ | ✓ | ✓ |
| 28 | 28-mv-1b/ | MV PDM→PDM scatter (mvb+mvp broadcast, 3.5.8.18+10) | ✓ | ✓ | ✓ |
| 29 | 29-mv-2a/ | MV PDM→DRAM scatter (4x mvp, 3.5.8.2) | ✓ | ✓ | ✓ |
| 30 | 30-mv-2b/ | MV PDM→DRAM distribute+collect roundtrip (3.5.8.24+25) | ✓ | ✓ | ✓ |
| 31 | 31-mv-2c/ | MV PDM→L2BM 個別並列 (3.5.8.9) | ✓ | ✓ | ✓ |
| 32 | 32-mv-3a/ | MV PDM@G0~3→DRAM@G0~3 並列 (3.5.8.2) | ✓ | ✓ | ✓ |
| 33 | 33-mv-4a/ | MV PDM@G0~3→PDM@G0 gather (3x mvp, 3.5.8.8) | ✓ | ✓ | ✓ |
| 34 | 34-mv-5a/ | MV DRAM@G0~3→PDM@G0 gather (4x mvp, 3.5.8.3) | ✓ | ✓ | ✓ |
| 35 | 35-mv-5b/ | MV L2BM→PDM グループ間縮約 8× (mvrdfadd, 3.5.8.19) | ✓ | ✓ | ✓ |
| 36 | 36-mv-6a/ | MV DRAM@G0~3→PDM@G0~3 並列 (3.5.8.3) | ✓ | ✓ | ✓ |
| 37 | 37-mv-10/ | MV DRAM→L2BM 個別並列 (3.5.8.11) | ✓ | ✓ | ✓ |
| 38 | 38-mv-11/ | MV DRAM→L2BM グループ内放送 mvb2 (3.5.8.13) | ✓ | ✓ | ✓ |
| 39 | 39-mv-12/ | MV DRAM→L2BM グループ間分配放送 mvb4 (3.5.8.16) | ✓ | ✓ | ✓ |
| 40 | 40-mv-13/ | MV L2BM→DRAM 個別並列 (3.5.8.12) | ✓ | ✓ | ✓ |
| 41 | 41-mv-14/ | MV L2BM→DRAM グループ内縮約 2× (mvr2dfadd, 3.5.8.14) | ✓ | ✓ | ✓ |
| 42 | 42-mv-15/ | MV L2BM→DRAM グループ間結合縮約 4×/n/4 出力 (mvr4dfadd, 3.5.8.17) | ✓ | ✓ | ✓ |
| 43 | 43-imm-collect/ | 即値→LM→collect→PDM→Host。最小 collect パイプライン | ✓ | ✓ | ✓ |
| 44 | 44-peid-collect/ | PE ID pack→collect。各 PE の演算結果が正しく collect されることを検証 | ✓ | ✓ | ✓ |
| 45 | 45-multi-collect/ | collect 2 回、同一 wait タグ再利用 (= API 誤用パターン、 emu:lib XFAIL) | ✓ | ✓ | ✓ |
| 46 | 46-debug-write/ | `mnc2_debug_write`→`mnc2_debug_read` ラウンドトリップ。API そのものの検証 | ✗ | ✓ | ✗ |
| 47 | 47-debug-read-write/ | debug_write + msl カーネル実行 + debug_read 統合テスト | ✗ | ✓ | ✗ |
| 48 | 48-reduce-pe/ | 縮約命令チェイン l1bmrdfadd→l2bmrdfadd→mvrdfadd の動作検証 | ✓ | ✓ | ✓ |
| 49 | 49-reduce-pe-vec/ | 48 の v 付き版。4 レジスタ同時縮約で 16 u64 | ✓ | ✓ | ✓ |
| 50 | 50-tag-interference/ | PE wait と DMA wd の干渉テスト | ✓ | ✓ | ✓ |
| 51 | 51-wd-zero/ | wd=0 無条件 DMA 開始テスト | ✓ | ✓ | ✓ |
| 52 | 52-all-id-collect/ | ID 系固定値入力オペランド ($peid/$l2bid/$l1bid/$mabid/$subpeid) 合成 → u64 collect。ec0..4 の 5 variant あり | ✓ | ✓ | ✓ |
| 53 | 53-f64-imm-collect/ | f64 1.5 を整数即値で設定 → collect | ✓ | ✓ | ✓ |
| 54 | 54-f32-imm-collect/ | f32 1.5 を imm f"1.5" + $aluf → collect | ✓ | ✓ | ✓ |
| 55 | 55-f16-imm-collect/ | f16 1.5 のビットパターンを整数即値で設定 → collect | ✓ | ✓ | ✓ |
| 56 | 56-dram-repeat-add/ | 外部メモリ ↔ PE round-trip bench (DRAM 並列個別 × 2)。state 必須 | ✗ | ✓ | ✓ |
| 57 | 57-pdm-repeat-add/ | 外部メモリ ↔ PE round-trip bench (PDM 並列個別 × 2)。state 必須 | ✗ | ✓ | ✓ |
| 58 | 58-l2bm-repeat-add/ | 外部メモリ ↔ PE round-trip bench (mvd PDM@0 単発)。state 必須 | ✗ | ✓ | ✓ |
| 59 | 59-pdm0-single-add/ | 外部メモリ ↔ PE round-trip bench (mvp 単独個別 × 8)。state 必須 | ✗ | ✓ | ✓ |
| 60 | 60-broadcast-pdm-pe/ | l1bmm の周期観察 (全 PE LM0[0] = 周期 4 [12,13,14,15]) | ✓ | ✓ | ✓ |
| 61 | 61-long-vsm/ | vsm-linker 生成の長い vsm (@distribute / @identify / @boundary_flags / 袖交換 / @collect) | ✓ | ✓ | ✓ |
| 62 | 62-broadcast-subpe/ | l1bmm の sub_pe 別分配 (PDM[12..15] を 4 sub_pe に) | ✓ | ✓ | ✓ |
| 63 | 63-broadcast-lm-cycle/ | l1bmm の 4 cycle 書き込み位置の切り分け (観察型) | ✓ | ✓ | ✓ |
| 64 | 64-lm-bar-shift/ | LM のバレルシフト | ✓ | ✓ | ✓ |
| 65 | 65-nbody-like/ | distribute と broadcast の対比 (f64 nbody like、質量積) | ✓ | ✓ | ✓ |
| 66 | 66-broadcast-l1bmp/ | broadcast の正: l1bmp + v + /1000 で全 4096 PE が同値 | ✓ | ✓ | ✓ |
| 67 | 67-l1bmp-2u64/ | l1bmp の 1 u64 版 / 2 u64 版の L1BM 位置とレジスタの対応を実測 (観察型) | ✓ | ✓ | ✓ |

### 集計 (2026-07-19 現状、 example 67 本)

| backend | ✓ | ✗ | ? |
|---|---|---|---|
| emu:process | 53 | 14 | 0 |
| emu:lib | 67 | 0 | 0 |
| device | 64 | 3 | 0 |

### 分類の根拠

- **emu:process ✗** (14 個): カーネル間 state 保持が必要なテスト、および
  emu:process suite に `.test` が無いテスト。emu:process (`gpfn3_package_main`)
  は kernel 呼び出しごとに独立プロセスで state 引き継ぎが出来ないため、
  連続カーネル実行の検証ができない
  - 06 put-get: put + get ペアで state 必須
  - 13-18 get-X: 前段 (put) の state を引き継いだ上での collect 検証
  - 56-59 bench: repeat-add カーネルの state 累積
  - 05 debug-read, 46 debug-write, 47 debug-read-write: API 自体は emu:process
    でも呼べるが、emu:lib suite にしか `.test` が無いので現状 ✗
- **device ✗** (3 個): `mnc2_debug_read` / `mnc2_debug_write` API をテスト
  対象にしているもの (05, 46, 47)。API 仕様上 HW には対応機構が無い。
  05/46/47 はテスト対象が emu-only API そのものなので「emu-only な test」と
  して残す (= device suite に .test を入れない)
- **07-12 put-X の device 対応** (2026-04-15 追加): 元々は verify に
  `mnc2_debug_read` を使っていて device で動かなかったが、各 example に
  `data/peek_*.vsm` + build.ninja の 3 段 pipeline (run-put-device →
  run-peek-device → peek-check-device → test-device) を追加し、**外部 peek
  kernel + read-pdm 経由**で verify するようにした。ex_put_X.c は device
  backend では debug_read を skip して exit 0 する。07/09/10/11/12 は pod で
  実測 PASS 確認済、08 は未実測だが 07 と同パターン
