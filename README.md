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

[Releases](https://github.com/Nobuto-Ura-HPC-Hub/open-mncore2-sdk/releases) から kit tarball をダウンロードし、順にインストールします。

```bash
PREFIX=$HOME/.local/mncore2-sdk

# 1. sdk-base-kit（最初にインストール）
tar xf sdk-base-kit-*.tar.gz
sdk-base-kit-*/install.sh $PREFIX

# 2. mncore2-emuenv-kit（PFN ファイルの配置が必要。kit 内の README.md を参照）
tar xf mncore2-emuenv-kit-*.tar.gz
mncore2-emuenv-kit-*/install.sh $PREFIX

# 3. libmnc2-kit
tar xf libmnc2-kit-*.tar.gz
libmnc2-kit-*/install.sh $PREFIX

# 4. mnc2-inspect-kit
tar xf mnc2-inspect-kit-*.tar.gz
mnc2-inspect-kit-*/install.sh $PREFIX

# 5. 環境を有効化
source $PREFIX/bin/activate
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

| Kit | 内容 | 状態 |
|-----|------|------|
| sdk-base-kit | 環境設定スクリプト（activate, sdk-versions, sdk-examples） | 提供中 |
| mncore2-emuenv-kit | PFN エミュレータ環境の SDK 統合（ユーザ提供の tarball から構成） | 提供中 |
| libmnc2-kit | MN-Core 2 ホスト API ライブラリ | 提供中 |
| mnc2-inspect-kit | ハードウェア情報取得ライブラリ | 提供中 |
| vsmlink-kit | VSM リンカライブラリ | 今後提供 |

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
