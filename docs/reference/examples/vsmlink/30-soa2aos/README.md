# 30-soa2aos: collect の SoA 出力を device 上で AoS に並べ替える

`@collect` の出力は成分ごと (SoA) `[x×4096][y×4096][z×4096][m×4096]` に固定される。一方
`@broadcast size 4` は 1 命令 (v で 4 サイクル) で 1 粒子の struct を配れて効率が良いが、源が
**AoS (1 粒子の x,y,z,m が連続 4 u64)** でないと読めない。この並びの差を、**device 上で**埋める。

このカーネルは SoA を AoS の `struct {x,y,z,m}[]` に並べ替える。ホストは出力をそのまま struct
配列として読める。設計とレイアウトは `docs/broadcast-collect-layout.md`。

## 仕組み

```asm
@distribute _a from_param 8 size 4 $lm0    # SoA を読み、各 PE に struct {x,y,z,m}
dmwrite $lm0v $lx0                          # MAB 内 4x4 転置 (書き込み。src に v が必須)
dmread  $lx0 $lm8v                          # 転置読み出し (dst にも v が必須)
@collect _b to_param 16 size 4 $lm8         # AoS で PDM へ
```

- `distribute size 4` が SoA から各 PE に 1 粒子の struct を載せる。
- `dmwrite`/`dmread` が MAB 内の 4 PE x 4 アドレスを転置する (dev manual 3.6.10.1 / 3.6.11.1)。
  **要点は src/dst 両方に `v`** (サイクルインクリメント。無いと全サイクル同番地に上書きで壊れる。
  dev manual 3.6.1.6)。
- `collect size 4` で書き戻すと、成分ごとではなく **粒子ごと (AoS)** で PDM に並ぶ。

### 2 つの性質

- **粒子順は順列**。`out[4k..4k+3]` は粒子 k の struct とは限らないが、各 struct は壊れない。
  all-to-all では全粒子を broadcast して各 PE が合算するので、**順序は問わない**。
- **対合 (involution)**。同じカーネルを **2 回適用すると元に戻る** (順列の巡回長 = 2)。よって
  同じツールで **SoA -> AoS も AoS -> SoA** もできる (1 回で変換、2 回で元)。

## 使い方

```
source scripts/overlay
ninja -C examples/30-soa2aos test-emu-lib   # emu:lib で自己テスト (テストデータで自己完結)
ninja -C examples/30-soa2aos test-device    # 実機で自己テスト
```

### フィルタ (--pipe)

`--pipe` で stdin から SoA を読み、stdout に AoS を書く Unix フィルタになる。device pod (実機、
LD_LIBRARY_PATH 不要) なら:

```
cat soa.bin | ./_build/soa2aos --pipe > aos.bin
```

- テスト入力の生成: `./_build/soa2aos --gen > soa.bin`
- 往復 (対合の確認): `./_build/soa2aos --gen | ./_build/soa2aos --pipe | ./_build/soa2aos --pipe`
  で元の SoA に戻る。

### --use-global-id (m を on-device の global_id で上書き)

`--use-global-id` を付けると、SoA -> AoS に加えて、各 PE が自分の **global_id (flat_id 0..4095) を
on-device で計算**して m を上書きする別カーネル (`aos_id._vsm`) を使う。**id のために distribute を
使わない** (ホストが id 配列を用意しない)。出力 struct の m は「その粒子を処理した PE の flat_id」に
なる。付けなければ m は保持 (デフォルトは今までどおり)。

- `ninja -C examples/30-soa2aos test-emu-lib-id` — 自己テスト (m が 0..4095 の順列)。
- フィルタ: `cat soa.bin | ./_build/soa2aos --pipe --use-global-id > aos_id.bin`。
- flat_id = `peid + (l1bid<<6) + (l2bid<<9)` を `lpassa`/`imm`/`ilsl`/`iadd` で合成 (`24/init.vsm`
  が手本)。固定値オペランド (`$peid` 等) は ALU 第 1 入力専用なので `lpassa` 退避。
- 整数 -> double は専用命令 (itof) が無いのでマジックナンバー法: `immu f"176"` (176.0(f32)=0x43300000
  を immu の xoxo で 2^52(f64) に) + `lor` + `dvadd`。id <= 4095 は仮数部に収まり誤差なし。

## 形式

いずれも float64 little-endian、4096 粒子 (= 16384 double = 131072 byte)。

| | 並び |
|---|---|
| SoA (入力) | `[x×4096][y×4096][z×4096][m×4096]` |
| AoS (出力) | `struct { double x, y, z, m; } [4096]` (粒子順は順列) |

## ファイル構成

| ファイル | 役割 |
|---------|------|
| `aos._vsm` | distribute size4 + `dmwrite`/`dmread` 転置 + collect size4 (m 保持) |
| `aos_id._vsm` | 上に on-device flat_id 計算 + m 上書きを足した `--use-global-id` 版 |
| `aos.param` | slot 8 (SoA 入力)、slot 16 (AoS 出力) の PDM 番地と wait tag (両カーネル共用) |
| `aos.stparam` | `:pe_shape (4096)` `:pe_local (4)` |
| `soa2aos.c` | 引数なし=自己テスト (CI)、`--pipe`=フィルタ、`--gen`=テスト入力生成 |
| `build.ninja` | vsmlink + assemble3 + host ビルド + test target |
