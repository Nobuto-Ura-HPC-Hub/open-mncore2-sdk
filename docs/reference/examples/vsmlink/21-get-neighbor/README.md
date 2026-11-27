# 21-get-neighbor

## 目的

`@get_neighbor` のアプリ寄りテスト。
全階層で隣接値が正しく取得できることを厳密に検証する。

### 厳密とは

全階層で隣接値が正しく取得できることを検証する。

**隣接値**: stencil 計算上の隣接値。stencil の計算処理は各領域を PE 単位で行われるため、
隣接値が必要になった際に袖交換が必要。今回の実装は 1D を対象とし、left(-1)、right(+1) の隣接値が必要。

実装では PDM を使う。PDM は PE の複製と考え：
- put: PE → PDM の書き込み。自分の情報を distribute 元の正しい PDM アドレスに書き込む
- get: PDM → PE の読み込み。隣のアドレスの値を読む

端の PE（PE の番号 0 / 4095）の値は、端の指定 `:reg $lm0` で自分の値にする（下の「端の PE」）。

**階層**: 袖交換が行われるレベル。

- within MAB — 袖交換の必要のない PE どうし
- cross MAB — MAB をまたぐ袖交換
- cross L1B — L1BM をまたぐ袖交換
- cross L2B — L2BM をまたぐ袖交換。L2BM ↔ PDM で達成
- ~~cross group~~ — 廃止。PDM0 固定方針により cross L2B に統合
- cross chip — チップをまたぐ袖交換。4096 PE を超えた場合に発生。L2BM ↔ PDM で達成
- edge — 端。袖交換が発生しない

**今回の実装範囲**: cross chip は考慮の外。edge は対応する。

### 隣接値の正しい取得と検証

あらかじめデータを書き込み、@get_neighbor で隣接値を取得し、期待値と照合する。

- データ投入: PDM0 に host dma API で書き込み、@distribute で PE に配布
- 隣接値取得: @get_neighbor で取得
- 検証: データは予測しやすいものにし、整合性をチェック

### 端の PE

`get_left._vsm` と `get_right._vsm` は、端の指定 `:reg $lm0`（`<src_reg>` と同じレジスタ）を書いている。端の PE
（左隣では PE の番号 0、右隣では 4095）の結果は、その PE 自身の値になる。

`input[i] = i + 1`（PE 0〜4095 に 1〜4096）として、PE 0 の左隣の結果は 1、PE 4095 の右隣の結果は 4096 になる。
これにより全 4096 PE（端の PE を含む）で照合する。

## 検証すべきこと

### 1. PE に正しく境界フラグが設定される

- 袖交換の必要のない PE がわかる
- cross MAB の PE がわかる
- cross L1B の PE がわかる
- cross L2B の PE がわかる

### 2. 全階層で左隣の PE の値が正しく取得される

- within MAB で左隣の PE から値を取得できる
- cross MAB で左隣の PE から値を取得できる
- cross L1B で左隣の PE から値を取得できる
- cross L2B で左隣の PE から値を取得できる


## テスト

```bash
ninja build-e2e
ninja test-emu-lib
```

境界フラグのデータは `../data/` で作ったものを使う（`data/README.md`）。

## ファイル構成

| ファイル | 内容 |
|---------|------|
| `data/get_left._vsm` | `@distribute` + `@boundary_flags` + `@alloc` + `@get_neighbor -1 ... :reg $lm0` + `@free` + `@collect` |
| `data/get_right._vsm` | `get_left._vsm` の `@get_neighbor` を `+1` にしたもの |
| `data/get_left.param` | PDM レイアウト: input(4096), output(8192), bf(12288), バッファ(16384)。`get_left` と `get_right` で共用 |
| `test_get_left.c` | 全 4096 PE 検証: input[i]=i+1, output[i]=input[i-1]（PE 0 は input[0]） |
| `test_get_right.c` | 全 4096 PE 検証: output[i]=input[i+1]（PE 4095 は input[4095]） |
| `build.ninja` | vsmlink + assemble3 + C ビルド + テスト |
