# Open MN-Core2 SDK

**Open MN-Core2 SDK** は、MN-Core 2 で動く計算を書いて動かすための開発環境です（非公式プロジェクト。
Preferred Networks, Inc. の公式配布ではありません。詳細は [README](https://github.com/Nobuto-Ura-HPC-Hub/open-mncore2-sdk#disclaimer) の Disclaimer を参照）。

このドキュメントは v0.6.0 に対応しています。

## できること

- **計算（kernel）を書く**: OpenCL C で書いて mncl でコンパイルする方法と、MN-Core 2 のアセンブリ
  （.vsm）を書いて vsmlink で配置を決める方法があります
- **ホストから動かす**: C のプログラムから libmnc2 を使い、kernel の読み込み、データの送受信、
  kernel の実行を行います
- **同じプログラムを、エミュレータと実機の両方で動かす**: 手元の PC ではエミュレータで、
  PFCP（Preferred Computing Platform）では MN-Core 2 の実機で動かせます

## 始め方

| 方法 | 向いている用途 | 準備 |
|------|----------------|------|
| [A: PFCP で使う](pfcp.md) | 実機で動かす | インストール不要。SDK を入れたコンテナイメージから pod を作る |
| [B: 手元の PC で使う](local.md) | エミュレータで確かめる | PFN の `mncore-sdk` と SDK の kit をインストールする |

## ソフトウェアの層

| 層 | 役割 | 提供 |
|----|------|------|
| アプリ（.cl、.vsm、ホストの C） | 計算とその呼び出しを書く | 利用者 |
| mncl | OpenCL C のコンパイラ。.cl から _vsm を出力する | この SDK |
| vsmlink | _vsm と配置パラメタから、実行できる .vsm を出力する | この SDK |
| assemble3 | アセンブラ。.vsm から命令列を作る | PFN（`mncore-sdk`） |
| libmnc2 | ホスト側のライブラリ | この SDK |
| エミュレータ（gpfn3_package_main） | 実機の代わりに実行する | PFN（`mncore-sdk`） |
| libgpfn3、ドライバ、MN-Core 2 | 実機 | PFN |

## 次に読むもの

- [道具の流れとサンプル](tools.md) — 道具をどの順に使うか、最初のサンプルの動かし方
- [リファレンス](./reference/README.md) — 各 kit の API とサンプル（SDK v0.6.0 の `share/` と同じ内容）
