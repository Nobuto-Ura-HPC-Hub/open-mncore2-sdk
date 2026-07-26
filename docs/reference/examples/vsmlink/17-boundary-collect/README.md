# 17-boundary-collect — @boundary_flags の distribute 用バイナリデータ生成

## 目的

このプログラムは `@boundary_flags` の distribute 版で使用するバイナリデータを
生成するためのもの。HW レジスタ ($subpeid, $mabid, $l1bid 等) から
境界フラグを計算し、`@collect` で PDM に回収して、ホスト側で golden data として保存する。

## 経緯

元々このプログラムは directive なしで境界フラグの計算を手動実装したもの。
この手動実装をもとに vsm-linker の `@boundary_flags_compute` ディレクティブが
実装された。現在の `boundary-collect._vsm` は `@boundary_flags_compute` を
使用しており、`@boundary_flags_compute` のチェックプログラムも兼ねている。
参照実装としての役割は C テスト (`expected_flags()`) に残っている。

## boundary flags データの 3 つの生成・検証方法

| 方法 | ファイル | 出力 | 検証方法 |
|------|---------|------|---------|
| 1. 素の vsm | `bf_compute_test.vsm` | `d get` でスポットチェック | gpfn3_package_main の出力を目視確認 |
| 2. directive 版 | `boundary-collect._vsm` | @collect で 4096 PE バイナリ | `ninja test` (E2E) / `ninja cmp` |
| 3. C プログラム | `gen_golden.c` (18-bf-verify) | 4096 PE バイナリ | `ninja cmp` で directive 版と照合 |

cmp でバイナリ照合できるのは方法 2 と 3。両者が一致すれば健全と判断できる。
方法 1 はバイナリ出力を持たないが、directive を介さない素の vsm での動作確認に使う。

### 方法 1: 素の vsm でのスポット検証

`bf_compute_test.vsm` は directive なしで境界フラグを計算し、
代表的な PE の値を `d get` で確認する。

```bash
source ../../_mncore2-sdk-v1/bin/activate
assemble3 bf_compute_test.vsm --output-file /tmp/bf_compute_test.asm
gpfn3_package_main -i /tmp/bf_compute_test.asm --offchip-memory-init zero
```

出力例:
```
DEBUG-GREG0(n0c0b0m0p0,0): v:0x200   # cross_chip_left   (bit9=512)
DEBUG-GREG0(n0c0b1m0p0,0): v:0x8     # cross_L1B_left    (bit3=8)
DEBUG-GREG0(n0c0b0m1p0,0): v:0x2     # cross_MAB_left    (bit1=2)
DEBUG-GREG0(n0c0b0m0p1,0): v:0x0     # 内部 PE           (0)
DEBUG-GREG0(n0c0b0m0p3,0): v:0x1     # cross_MAB_right   (bit0=1)
DEBUG-GREG0(n0c0b0m15p3,0): v:0x4    # cross_L1B_right   (bit2=4)
DEBUG-GREG0(n0c0b7m15p3,0): v:0x10   # cross_L2B_right   (bit4=16)
DEBUG-GREG0(n0c1b7m15p3,0): v:0x10   # cross_L2B_right   (bit4=16, 旧 cross_group)
DEBUG-GREG0(n3c1b7m15p3,0): v:0x40   # cross_chip_right  (bit6=64)
DEBUG-GREG0(n1c0b0m0p0,0): v:0x20    # cross_L2B_left    (bit5=32, 旧 cross_group)
```

#### 判定基準

全行の `v:` 値が以下の期待値と一致すれば OK:

| PE アドレス | 位置 | 期待値 | フラグ |
|-----------|------|--------|-------|
| n0c0b0m0p0 | group0 L2B0 L1B0 MAB0 PE0 | 0x80 | cross_chip_left (bit7) |
| n0c0b1m0p0 | group0 L2B0 L1B1 MAB0 PE0 | 0x8 | cross_L1B_left (bit3) |
| n0c0b0m1p0 | group0 L2B0 L1B0 MAB1 PE0 | 0x2 | cross_MAB_left (bit1) |
| n0c0b0m0p1 | group0 L2B0 L1B0 MAB0 PE1 | 0x0 | 内部 PE (フラグなし) |
| n0c0b0m0p3 | group0 L2B0 L1B0 MAB0 PE3 | 0x1 | cross_MAB_right (bit0) |
| n0c0b0m15p3 | group0 L2B0 L1B0 MAB15 PE3 | 0x4 | cross_L1B_right (bit2) |
| n0c0b7m15p3 | group0 L2B0 L1B7 MAB15 PE3 | 0x10 | cross_L2B_right (bit4) |
| n0c0b0m5p2 | group0 L2B0 L1B0 MAB5 PE2 | 0x0 | 内部 PE (フラグなし) |
| n0c1b7m15p3 | group0 L2B1 L1B7 MAB15 PE3 | 0x10 | cross_L2B_right (bit4, 旧 cross_group) |
| n3c1b7m15p3 | group3 L2B1 L1B7 MAB15 PE3 | 0x40 | cross_chip_right (bit6) |
| n1c0b0m0p0 | group1 L2B0 L1B0 MAB0 PE0 | 0x20 | cross_L2B_left (bit5, 旧 cross_group) |

全 10 ビット (left 5 + right 5) と内部 PE (0) をカバーしている。
> cross_group 廃止により 12bit → 10bit に変更。旧 cross_group の PE は cross_L2B に統合。

### 方法 2・3: バイナリ照合

```bash
# source scripts/overlay 済みの前提
ninja build-e2e    # directive 版をビルド
ninja test         # E2E 実行 → _build/collected_flags.bin 生成
ninja cmp          # gen_golden.c の出力と cmp 照合
```

## フラグの意味

できるフラグは **cross** （データ転送が必要）という意味のビットフラグ。
各 PE に最大 1 ビットだけが立つ（one-hot エンコーディング）。

```
bit[0] = cross_MAB_right    (subpeid == 3)
bit[1] = cross_MAB_left     (subpeid == 0)
bit[2] = cross_L1B_right    (subpeid == 3 AND mabid == 15)
bit[3] = cross_L1B_left     (subpeid == 0 AND mabid == 0)
bit[4] = cross_L2B_right    (... AND l1bid == 7, 旧 cross_group を含む)
bit[5] = cross_L2B_left     (... AND l1bid == 0, 旧 cross_group を含む)
bit[6] = cross_chip_right   (... AND l2bid == 7)
bit[7] = cross_chip_left    (... AND l2bid == 0)
bit[8] = data_edge_right
bit[9] = data_edge_left
```

> cross_group は廃止済み。PDM0 を常に使うため cross_L2B と同じ経路になり区別不要。

例: PE3 at MAB15 L1B3 → `cross_L1B_right` (bit2 = 0x04)
例: PE0 at MAB0 L1B0 group0 → `cross_chip_left` (bit9 = 0x200)
例: PE1 at MAB5 L1B2 → 0 (内部 PE、境界越え不要)

## 検討した他の方式: edge フラグ

cross の代わりに **edge**（端にいるかどうか）という意味のフラグも検討した。

```
cross_L1B_LEFT       B 0000001000  (1 ビット)
L1B_LEFT_EDGE        B 0000001010  (PE_left も同時に立つ)
```

edge 方式では、ある上位レベルのビットが立っていれば下位レベルのビットも必ず立つ。
したがって情報としては等価で、双方向に変換可能:
- cross → edge: 立っているビット以下を全部立てる
- edge → cross: 最上位の立っているビットだけ残す

**cross を採用した理由**: cross のチェックが多く、one-hot なのでひと目で分かる。
edge をチェックしたい場合は `B 1010101010` と iand すればよい。

## 実装構造

stencil の逆操作で境界フラグを計算する。ネスト if/then/else:

```
if ( pe is right ) {
    if ( mab is right ) {
        if ( l1bm is right ) {
            if ( l2bid == 7 ) {
                cross_chip_right   (bit6)
            } else {
                cross_L2B_right    (bit4, 旧 cross_group を含む)
            }
        } else {
            cross_L1B_right  (bit2)
        }
    } else {
        cross_MAB_right  (bit0)
    }
} else if ( pe is left ) {
    // 同構造で left 側
} else {
    zero  (内部 PE)
}
```

実際の実装 (`boundary-collect._vsm`) は `@boundary_flags_compute` ディレクティブで
この if/then/else を自動生成する。C テスト (`test_boundary_collect.c`) の
`expected_flags()` 関数がこのネスト構造を忠実に実装している。

## collected_flags.bin の生成

### edge なし（通常）

```bash
ninja test   # → _build/collected_flags.bin
```

PE 0 = cross_chip_left (0x80), PE 4095 = cross_chip_right (0x40)。

sha256: （cross_group 廃止後に再生成が必要）

### edge あり（data_edge 対応）

```bash
MNC2_BACKEND=emu:lib _build/test_boundary_collect --edge-left --edge-right
```

PE 0 = data_edge_left (0x200), PE 4095 = data_edge_right (0x100)。
カーネルは cross_chip しか出せないため MISMATCH が報告されるが、
保存される bin は expected_flags の計算結果であり正しい。

sha256: （cross_group 廃止後に再生成が必要）

## ファイル

| ファイル | 内容 |
|---------|------|
| `boundary-collect._vsm` | @boundary_flags_compute + @collect |
| `boundary-collect.param` | 配置パラメタ |
| `boundary-collect.stparam` | 構造パラメタ |
| `test_boundary_collect.c` | E2E テスト（期待値と比較）。`--edge-left --edge-right` で data_edge 対応 bin 生成 |
| `bf_compute_test.vsm` | compute 版の単体テスト |
| `build.ninja` | ビルド定義 |
| `dump_flags.py` | bin のビット分布表示。`--human` で詳細表示 |
