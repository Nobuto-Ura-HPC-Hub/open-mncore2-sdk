# 21-get-neighbor-left

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

edge 処理における隣接値の扱いは TBD。

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

### edge 処理（:boundary 指定なし）

`:boundary` 指定なしの場合、アプリが edge を自分で処理する。
このテストでは、edge PE の外に値を意図的に PDM に置くことで対応する。

`input[i] = i + 1`（PE 0〜4095 に 1〜4096）の連続として：
- left edge: PDM の PE -1 相当の位置に `0` を置く → PE 0 の get(-1) が 0 を返す
- right edge: PDM の PE 4096 相当の位置に `4097` を置く → PE 4095 の get(+1) が 4097 を返す

これにより全 4096 PE（edge PE 含む）で検証できる。

### TBD

- `:clamp` / `:periodic` の実装と検証

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
source scripts/overlay
ninja -C examples/21-get-neighbor-left build-e2e
ninja -C examples/21-get-neighbor-left test
```

前提: `examples/17-boundary-collect` の `collected_flags.bin` が必要。

## ファイル構成

### 正式ファイル

| ファイル | 内容 |
|---------|------|
| `data/get_left._vsm` | `@distribute` + `@boundary_flags` + `@alloc` + `@get_neighbor -1` + `@free` + `@collect` |
| `data/get_left.param` | PDM レイアウト: input(0), output(4096), bf(8192) |
| `data/get_left.stparam` | 構造パラメタ（1D, 4096 PE） |
| `test_get_left.c` | 全 4096 PE 検証: input[i]=i+1, output[i]=input[i-1] |
| `build.ninja` | vsmlink + assemble3 + C ビルド + テスト |

### 暫定ファイル（c_step4 互換テスト用）

c_step4（特定 PE テスト、ALL PASS 実績あり）を example 21 にコピーして、
`@get_neighbor` のテンプレート出力が正しいことを段階的に検証するために使用した。

| ファイル | 内容 |
|---------|------|
| `data/step4._vsm` | c_step4 の元 _vsm（@alloc/@get_neighbor に書き換え済み） |
| `data/step4_out._vsm` | @boundary_flags 展開済み + @alloc/@get_neighbor/@free |
| `data/step4.param` | c_step4 の .param |
| `data/golden_step4.vsm` | 元の step4._vsm の vsmlink 出力（ALL PASS 確認済み基準） |
| `ex_step4.c` | c_step4 のホスト C コード（特定 PE テスト） |
| `build_step4.ninja` | c_step4 互換テスト + verify diff |
| `do.sh` | clean + vsmlink + diff のショートカット |

正式ファイルで全 4096 PE テスト PASS 後、暫定ファイルは整理対象。
