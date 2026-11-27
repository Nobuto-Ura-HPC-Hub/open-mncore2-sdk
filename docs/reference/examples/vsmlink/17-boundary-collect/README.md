# 17-boundary-collect — @boundary_flags の distribute 用バイナリデータ生成

## 目的

このプログラムは `@boundary_flags` の distribute 版で使用するバイナリデータを
生成するためのもの。HW レジスタ ($subpeid, $mabid, $l1bid 等) から
境界フラグを計算し、`@collect` で PDM に回収して、ホスト側で golden data として保存する。

`boundary-collect._vsm` と `boundary-collect-edge{01,10,11}._vsm` は、境界フラグを計算する命令列を、data_edge の組ごとに
directive を使わずに書いたものである（`@boundary_flags_compute` は廃止した）。期待値は C テストの `expected_flags()` が作る。

## boundary flags データの 2 つの生成・検証方法

| 方法 | ファイル | 出力 | 検証方法 |
|------|---------|------|---------|
| 1. PE で計算する版 | `boundary-collect._vsm` | @collect で 4096 PE バイナリ | `ninja test-emu-lib` / `ninja test-device`（sha256 検証）|
| 2. C プログラム | `examples/data/gen_bf_golden.c` | 4096 PE バイナリ | `ninja cmp` で PE で計算する版と照合 |

両者のバイナリが一致すれば健全と判断できる。ビットの割り当ては `share/vsmlink/directives-spec.md` の
`@boundary_flags` の「ビットレイアウト (正式仕様)」にある。

### バイナリ照合

```bash
# SDK を有効にし、この example のディレクトリで実行する（examples の README.md の「動かし方」）
ninja build-e2e    # PE で計算する版をビルド
ninja test-emu-lib # E2E 実行 → _build/collected_flags.bin 生成
ninja cmp          # gen_bf_golden の出力と cmp 照合
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

実際の実装 (`boundary-collect._vsm`) は、`@boundary_flags_compute` が生成していた
この if/then/else の命令列を、そのまま書いたものである。C テスト (`test_boundary_collect.c`) の
`expected_flags()` 関数がこのネスト構造を忠実に実装している。

## collected_flags.bin の生成

### edge なし（通常）

```bash
ninja test-emu-lib   # → _build/collected_flags.bin
```

PE 0 = cross_chip_left (0x80), PE 4095 = cross_chip_right (0x40)。

sha256: `57b42dea9b158e8e1b9de447a9c54b522ebdef7b8da8ab3779edd9ca55b4d6eb`（`sha256.ninja`。`ninja test-emu-lib` が検証する）

### edge あり（data_edge 対応）

```bash
MNC2_BACKEND=emu:lib _build/test_boundary_collect --edge-left --edge-right
```

PE 0 = data_edge_left (0x200), PE 4095 = data_edge_right (0x100)。
カーネルは cross_chip しか出せないため MISMATCH が報告されるが、
保存される bin は expected_flags の計算結果であり正しい。

この bin の sha256 は、`build.ninja` では検証していない。

## ファイル

| ファイル | 内容 |
|---------|------|
| `boundary-collect._vsm` | 境界フラグの計算（data_edge 00）+ @collect |
| `boundary-collect-edge{01,10,11}._vsm` | data_edge 01 / 10 / 11 の組の境界フラグの計算 + @collect |
| `boundary-collect.param` | 配置パラメタ |
| `test_boundary_collect.c` | E2E テスト（期待値と比較）。`--edge-left --edge-right` で data_edge 対応 bin 生成 |
| `build.ninja` | ビルド定義 |
| `examples/data/dump_flags.py` | bin のビット分布表示。`--human` で詳細表示 |
