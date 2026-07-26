# OpenACC C Examples

OpenACC C ソースから MN-Core 2 上で実行するまでの E2E サンプル。

## 構成

| # | 内容 | 計算 | host C |
|---|------|------|--------|
| 01-vecadd | c[i] = a[i] + b[i] | 1D, 4096 PE | 4096 要素 |
| 02-vecadd-2d | 同上 | 2D (64x64) | 4096 要素 |
| 03-vecadd-3d | 同上 | 3D (16x16x16) | 4096 要素 |
| 04-vecadd-3d-batch | 同上 | 3D, N ラウンド | 4096*N 要素 (N=2) |

layout :any のため .cl / .param は全ケース同一。.stparam の pe_shape のみ異なる。

## 各 example のファイル構成

| ファイル | 説明 |
|---------|------|
| input.c | ユーザーが書く OpenACC C ソース |
| vecadd.cl | デバイスカーネル（S2S コンパイラ出力） |
| vecadd.param | 配置パラメタ（S2S コンパイラ出力） |
| vecadd*.stparam | 構造パラメタ（S2S コンパイラ出力） |
| test_vecadd.c | ホスト側テストドライバ（mnc2.h API） |
| Makefile | ビルド定義 |

## 使い方

```bash
source <SDK_ROOT>/bin/activate
sdk-examples openacc-c ~/work
cd ~/work/openacc-c/01-vecadd

make              # S2S + MNCL: input.c -> .cl/.stparam -> ._vsm
make build-e2e    # + vsmlink + assemble3 + host C
make test         # emu:lib で実行・検証
```

## パイプライン

```
input.c (OpenACC C)
    |  S2S コンパイラ (将来自動化)
    v
vecadd.cl + vecadd.param + vecadd.stparam + test_vecadd.c
    |  make (default)
    v
_build/vecadd._vsm                [MNCL: clang]
    |  make build-e2e
    v
_build/vecadd.vsm                 [vsm-linker]
_build/vecadd.asm                  [assemble3]
_build/vecadd.idma.dat             [assemble3 --loader]
_build/test_vecadd                 [gcc + libmnc2]
    |  make test
    v
PASS: c[i] == a[i] + b[i]         [emu:lib]
```

## 前提

SDK に以下の kit がインストール済みであること:
- mncl-kit (clang, opencl-c.h)
- vsmlink-kit (vsmlink CLI)
- mncore2-emuenv-kit (assemble3)
- libmnc2-kit (mnc2.h, libmnc2.a)
