#!/bin/bash
# setup.sh — open-mncore2-sdk セットアップスクリプト
#
# GitHub Releases から kit をダウンロードし、PREFIX に SDK を構成する。
# PFN エミュレータ環境はユーザが別途用意する。
#
# Usage:
#   ./setup.sh --prefix=<PREFIX> --emuenv=<path/to/mncore2_emuenv_*.tar.xz>
#   ./setup.sh --prefix=$HOME/.local/mncore2-sdk --emuenv=./mncore2_emuenv_20240826.tar.xz
#   ./setup.sh --help
set -eu

# ---- デフォルト値 ----
PREFIX=""
EMUENV_PATH=""
GITHUB_REPO="Nobuto-Ura-HPC-Hub/open-mncore2-sdk"

# ---- リリース定義 ----
# SDK リリースバージョン（GitHub Releases のタグ）
SDK_RELEASE="0.1.0"

# 各 kit のバージョン（リリース時に更新する）
SDK_BASE_VERSION="0.1.5"
LIBMNC2_VERSION="0.2.0"
VSMLINK_VERSION=""
EMUENV_VERSION=""

# ---- 引数解析 ----
show_help() {
    cat << 'HELP'
Usage: ./setup.sh --prefix=<PREFIX> [--emuenv=<path>]

Options:
  --prefix=<PATH>    SDK のインストール先（必須）
  --emuenv=<PATH>    PFN エミュレータ環境の tarball パス（任意）
  --help             このヘルプを表示

Examples:
  ./setup.sh --prefix=$HOME/.local/mncore2-sdk
  ./setup.sh --prefix=/opt/mncore2-sdk --emuenv=./mncore2_emuenv_20240826.tar.xz
HELP
    exit 0
}

for arg in "$@"; do
    case "$arg" in
        --prefix=*) PREFIX="${arg#--prefix=}" ;;
        --emuenv=*) EMUENV_PATH="${arg#--emuenv=}" ;;
        --help)     show_help ;;
        *)
            echo "エラー: 不明なオプション: $arg" >&2
            echo "  ./setup.sh --help でヘルプを表示" >&2
            exit 1
            ;;
    esac
done

if [ -z "$PREFIX" ]; then
    echo "エラー: --prefix を指定してください" >&2
    echo "  例: ./setup.sh --prefix=\$HOME/.local/mncore2-sdk" >&2
    exit 1
fi

PREFIX="$(cd "$(dirname "$PREFIX")" 2>/dev/null && pwd)/$(basename "$PREFIX")" || PREFIX="$(pwd)/$PREFIX"

# ---- ユーティリティ ----
WORK=$(mktemp -d)
trap 'rm -rf "$WORK"' EXIT

download_kit() {
    local kit_name="$1"
    local version="$2"
    local tarball="${kit_name}-${version}.tar.gz"
    local url="https://github.com/${GITHUB_REPO}/releases/download/v${SDK_RELEASE}/${tarball}"

    echo "[download] $tarball"
    if command -v curl > /dev/null 2>&1; then
        curl -fSL -o "$WORK/$tarball" "$url"
    elif command -v wget > /dev/null 2>&1; then
        wget -q -O "$WORK/$tarball" "$url"
    else
        echo "エラー: curl または wget が必要です" >&2
        exit 1
    fi
    echo "$WORK/$tarball"
}

install_kit() {
    local tarball="$1"
    local kit_dir
    kit_dir="$(basename "$tarball" .tar.gz)"

    tar xf "$tarball" -C "$WORK"
    "$WORK/$kit_dir/install.sh" "$PREFIX"
}

# ---- メイン処理 ----
echo "=== open-mncore2-sdk setup ==="
echo "PREFIX: $PREFIX"
echo ""

mkdir -p "$PREFIX"

# 1. sdk-base-kit（最初にインストール）
if [ -n "$SDK_BASE_VERSION" ]; then
    tarball=$(download_kit sdk-base-kit "$SDK_BASE_VERSION")
    install_kit "$tarball"
    echo ""
fi

# 2. libmnc2-kit
if [ -n "$LIBMNC2_VERSION" ]; then
    tarball=$(download_kit libmnc2-kit "$LIBMNC2_VERSION")
    install_kit "$tarball"
    echo ""
fi

# 3. vsmlink-kit
if [ -n "$VSMLINK_VERSION" ]; then
    tarball=$(download_kit vsmlink-kit "$VSMLINK_VERSION")
    install_kit "$tarball"
    echo ""
fi

# 4. mncore2-emuenv-kit（ユーザ提供の PFN tarball が必要）
if [ -n "$EMUENV_PATH" ]; then
    if [ ! -f "$EMUENV_PATH" ]; then
        echo "エラー: エミュレータ環境が見つからない: $EMUENV_PATH" >&2
        exit 1
    fi
    if [ -n "$EMUENV_VERSION" ]; then
        tarball=$(download_kit mncore2-emuenv-kit "$EMUENV_VERSION")
        install_kit "$tarball"
        echo ""
    fi
else
    echo "[info] --emuenv が未指定。エミュレータ環境はスキップ。"
    echo "  後から個別にインストールできます。"
    echo ""
fi

# ---- 完了 ----
echo "=== セットアップ完了 ==="
echo ""
echo "環境を有効化するには:"
echo "  source $PREFIX/bin/activate"
echo ""
echo "インストール済み kit:"
"$PREFIX/bin/sdk-versions" 2>/dev/null || true
