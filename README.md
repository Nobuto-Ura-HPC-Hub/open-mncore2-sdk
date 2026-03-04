# open-mncore2-sdk

Unofficial development SDK for MN-Core 2.

MN-Core 2 向けの非公式開発環境です。
PFN 公式配布のエミュレータ環境と組み合わせて使います。

## 前提

- Ubuntu 22.04+ / Rocky Linux 9+
- PFN 公式配布の MN-Core 2 エミュレータ環境（`mncore2_emuenv_*.tar.xz`）
  - `assemble3` — MN-Core 2 アセンブラ
  - `gpfn3_package_main` — MN-Core 2 パッケージエミュレータ
- `libgomp` がインストールされていること
  - Ubuntu: `apt install libgomp1`
  - Rocky Linux: `dnf install libgomp`

## セットアップ

```bash
# 1. このリポジトリを取得
git clone https://github.com/Nobuto-Ura-HPC-Hub/open-mncore2-sdk.git
cd open-mncore2-sdk

# 2. PFN エミュレータ環境を取得し、tarball を配置
#    （取得方法は PFN の案内に従ってください）
cp /path/to/mncore2_emuenv_*.tar.xz .

# 3. SDK をセットアップ
./setup.sh --prefix=$HOME/.local/mncore2-sdk

# 4. 環境を有効化
source $HOME/.local/mncore2-sdk/bin/activate
```

## 使い方

```bash
# 環境を有効化
source $HOME/.local/mncore2-sdk/bin/activate

# インストール済み kit の確認
sdk-versions

# examples をコピーしてビルド
sdk-examples --list
sdk-examples libmnc2 ~/work
cd ~/work/libmnc2-*/vecadd && ninja
```

## 含まれる kit

| Kit | 内容 |
|-----|------|
| sdk-base-kit | 環境設定スクリプト（activate, sdk-versions, sdk-examples） |
| libmnc2-kit | MN-Core 2 ホスト API ライブラリ |
| vsmlink-kit | VSM リンカライブラリ |
| mncore2-emuenv-kit | PFN エミュレータ環境の SDK 統合（ユーザ提供の tarball から構成） |

## ライセンス

このリポジトリのスクリプト・ドキュメントは MIT License です。

PFN 公式配布のバイナリ（assemble3, gpfn3_package_main）は
PFN の配布条件に従います。詳細はエミュレータ環境に同梱の
DISCLAIMER.txt および NOTICE.md を参照してください。

## Disclaimer

This is an **unofficial** project and is not affiliated with or endorsed by
Preferred Networks, Inc. MN-Core is a trademark of Preferred Networks, Inc.

---

<sub>Developed by Kobe University, University of Aizu, and [Sinby Corp.](https://sinby.com/mncl-kobe/)</sub>
