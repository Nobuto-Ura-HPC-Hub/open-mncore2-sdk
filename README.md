# open-mncore2-sdk

Unofficial development SDK for MN-Core 2.

MN-Core 2 向けの非公式開発環境です。
PFN が公開している MN-Core SDK（apt パッケージ `mncore-sdk`）と組み合わせて使います。

## できること

- OpenCL C（mncl）または MN-Core 2 のアセンブリ（.vsm と vsmlink）で計算（kernel）を書く
- C のホストプログラムから libmnc2 で、kernel の読み込み、データの送受信、実行を行う
- 同じプログラムを、手元の PC ではエミュレータで、PFCP では MN-Core 2 の実機で動かす

## 始め方

| 方法 | 向いている用途 | 説明 |
|------|----------------|------|
| PFCP で使う | 実機で動かす | SDK を入れたコンテナイメージから pod を作る。インストール不要（[docs/pfcp.md](docs/pfcp.md)） |
| 手元の PC で使う | エミュレータで確かめる | 下の「前提」と「セットアップ」の手順でインストールする（[docs/local.md](docs/local.md)） |

ドキュメントの入口は [docs/index.md](docs/index.md) です。

## 前提

- Ubuntu 24.04+
- PFN の apt パッケージ `mncore-sdk`（0.8 で確認）
  - 次の 2 つが `/opt/pfn/pfcomp/codegen/build/tools/` に入ります。`source $PREFIX/bin/activate`
    がこのディレクトリを `PATH` に加えます
    - `assemble3` — MN-Core 2 アセンブラ
    - `gpfn3_package_main` — MN-Core 2 パッケージエミュレータ
  - インストール例（PFN の apt リポジトリを登録してから入れます）:

    ```bash
    sudo apt-get install -y ca-certificates curl
    curl -fsSL https://asia-northeast1-apt.pkg.dev/doc/repo-signing-key.gpg \
        | sudo tee /usr/share/keyrings/mncore-packages-archive-keyring.gpg.asc > /dev/null
    echo 'deb [signed-by=/usr/share/keyrings/mncore-packages-archive-keyring.gpg.asc] https://asia-northeast1-apt.pkg.dev/projects/mncore-packages noble main' \
        | sudo tee /etc/apt/sources.list.d/mncore-packages.list
    sudo apt-get update
    sudo apt-get install -y mncore-sdk=0.8
    ```
- `libgomp` がインストールされていること（`apt install libgomp1`）

## セットアップ

[Releases](https://github.com/Nobuto-Ura-HPC-Hub/open-mncore2-sdk/releases) から kit tarball をダウンロードし、依存順にインストールします。

```bash
PREFIX=$HOME/.local/mncore2-sdk

# 1. sdk-base-kit（最初にインストール）
tar xf sdk-base-kit-*.tar.gz
sdk-base-kit-*/install.sh $PREFIX

# 2. mnc2-inspect-kit
tar xf mnc2-inspect-kit-*.tar.gz
mnc2-inspect-kit-*/install.sh $PREFIX

# 3. libmnc2-kit
tar xf libmnc2-kit-*.tar.gz
libmnc2-kit-*/install.sh $PREFIX

# 4. vsmlink-kit
tar xf vsmlink-kit-*.tar.gz
vsmlink-kit-*/install.sh $PREFIX

# 5. mncl-kit
tar xf mncl-kit-*.tar.gz
mncl-kit-*/install.sh $PREFIX

# 6. 環境を有効化
source $PREFIX/bin/activate
```

## activate が設定する環境変数

`source $PREFIX/bin/activate` を実行すると以下が設定されます。`deactivate` で元に戻せます。

| 変数 | 内容 |
|------|------|
| `SDK_ROOT` | SDK のインストールパス（絶対パス） |
| `PATH` | `$SDK_ROOT/bin` を追加（`assemble3`, `gpfn3_package_main`, `sdk-versions` 等） |
| `LD_LIBRARY_PATH` | `$SDK_ROOT/lib` を追加（LLVM 共有ライブラリ等） |

## 使い方

```bash
# 環境を有効化
source $HOME/.local/mncore2-sdk/bin/activate

# インストール済み kit の確認
sdk-versions

# examples をコピーしてビルド・実行
sdk-examples --list
sdk-examples libmnc2 ~/work
cd ~/work/libmnc2-*/01-nop

ninja                 # ビルド
ninja test-emu-lib    # emu:lib（エミュレータ）で実行・検証
```

## チュートリアル

道具の流れとサンプルの種類は、[docs/tools.md](docs/tools.md) を参照してください。
`sdk-examples libmnc2` でコピーされる各サンプルの詳細は、サンプルごとの `README.md` にあります。

## 含まれる kit

| Kit | 内容 | 状態 |
|-----|------|------|
| sdk-base-kit | 環境設定スクリプト（activate, sdk-versions, sdk-examples） | 提供中 |
| mnc2-inspect-kit | ハードウェア情報取得ライブラリ | 提供中 |
| libmnc2-kit | MN-Core 2 ホスト API ライブラリ | 提供中 |
| vsmlink-kit | VSM リンカライブラリ | 提供中 |
| mncl-kit | MN-Core 2 OpenCL C コンパイラ（clang ベース） | 提供中 |

## ライセンス

このリポジトリのスクリプト・ドキュメントは MIT License です。

PFN の `mncore-sdk`（assemble3, gpfn3_package_main を含む）は PFN の配布条件に従います。
詳細はパッケージに同梱の `/opt/pfn/pfcomp/licenses/MN-Core_SDK_End-User_License_Agreement.pdf`
を参照してください。

## Disclaimer

This is an **unofficial** project and is not affiliated with or endorsed by
Preferred Networks, Inc. MN-Core is a trademark of Preferred Networks, Inc.

---

<sub>Developed by Kobe University, University of Aizu, and [Sinby Corp.](https://sinby.com/mncl-kobe/)</sub>
