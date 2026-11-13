# 道具の流れとサンプル

## 道具の流れ

| 段階 | 道具 | 入力 | 出力 |
|------|------|------|------|
| 1 | mncl（OpenCL C のコンパイラ） | .cl | _vsm |
| 2 | 利用者が書く | | 配置パラメタ（PDM のどこに何を置くか） |
| 3 | vsmlink | _vsm と配置パラメタ | .vsm |
| 4 | assemble3（アセンブラ） | .vsm | 命令列（.idma.dat 等） |
| 5 | ホストのプログラム（libmnc2） | 命令列とデータ | データの送信、kernel の実行、結果の受信 |

PDM は、ホストと MN-Core 2 の演算器の間にあるメモリです。ホストはここへデータを送り、kernel は
ここから読み、ここへ書きます。

.vsm を直接書く場合は、1〜3 を飛ばして 4 から始めます。

## サンプル

`sdk-examples` で、kit ごとのサンプルを手元にコピーできます。

```bash
sdk-examples --list              # サンプルのある kit の一覧
sdk-examples <kit 名> ~/work     # ~/work/<kit 名>-<版>/ にコピーする
```

| kit | 内容 |
|-----|------|
| libmnc2 | ホストのプログラムの書き方（.vsm を直接書く） |
| mncl | OpenCL C で kernel を書く（ベクトル加算、リダクション、ソート、N 体など） |
| vsmlink | _vsm と配置パラメタから .vsm を作る |
| mnc2-inspect | kernel の検査 |

どのサンプルも `ninja` でビルドし、次のターゲットで実行します。

| ターゲット | 実行先 |
|------------|--------|
| `ninja test-emu` | エミュレータ（プロセス版） |
| `ninja test-emu-lib` | エミュレータ（ライブラリ版） |
| `ninja test-device` | 実機（PFCP の pod の中で） |

例（OpenCL C のベクトル加算）:

```bash
sdk-examples mncl ~/work
cd ~/work/mncl-*/01-vecadd
ninja
ninja test-emu-lib
```

各サンプルの詳細は、サンプルごとの `README.md` と [リファレンス](./reference/README.md) を参照してください。
