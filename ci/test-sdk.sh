#!/bin/bash
# ci/test-sdk.sh — SDK kit インストール + sdk-examples ビルド・テスト
#
# 前提:
#   - Ubuntu 22.04 + libgomp1
#   - assemble3, gpfn3_package_main が PATH に存在
#   - build-essential, ninja-build, curl, jq, ca-certificates がインストール済み
#   - 環境変数: GH_TOKEN, RELEASE_TAG (任意、デフォルト v0.3.0)
set -eu

PREFIX=/opt/mncore2-sdk
WORK=/tmp/kits
EXAMPLES=/tmp/examples
REPO="${REPO:-Nobuto-Ura-HPC-Hub/open-mncore2-sdk}"
RELEASE_TAG="${RELEASE_TAG:-v0.3.0}"

mkdir -p "$WORK" "$EXAMPLES"

# --- リリースアセットをダウンロード ---
echo "=== Downloading release assets (${RELEASE_TAG}) ==="
API_URL="https://api.github.com/repos/${REPO}/releases/tags/${RELEASE_TAG}"

ASSETS=$(curl -sL -H "Authorization: token ${GH_TOKEN}" "$API_URL" \
    | jq -r '.assets[] | .name + "\t" + .url')

echo "$ASSETS" | while IFS=$'\t' read -r name url; do
    echo "  downloading $name"
    curl -sL -H "Authorization: token ${GH_TOKEN}" \
         -H "Accept: application/octet-stream" \
         "$url" -o "$WORK/$name"
done

# --- kit を依存順にインストール ---
echo "=== Installing kits ==="
cd "$WORK"

# 1. sdk-base-kit
tar xzf sdk-base-kit-*.tar.gz
sdk-base-kit-*/install.sh "$PREFIX"

# 2. mncore2-emuenv-kit — PFN バイナリはベースイメージに含まれている
mkdir -p "$PREFIX/bin"
ln -sf "$(which assemble3)" "$PREFIX/bin/assemble3"
ln -sf "$(which gpfn3_package_main)" "$PREFIX/bin/gpfn3_package_main"
printf 'mncore2-emuenv-kit\t20240826.2\n' >> "$PREFIX/.sdk-versions"

# 3. libmnc2-kit
tar xzf libmnc2-kit-*.tar.gz
libmnc2-kit-*/install.sh "$PREFIX"

# 4. mnc2-inspect-kit
tar xzf mnc2-inspect-kit-*.tar.gz
mnc2-inspect-kit-*/install.sh "$PREFIX"

# 5. vsmlink-kit
tar xzf vsmlink-kit-*.tar.gz
vsmlink-kit-*/install.sh "$PREFIX"

# 6. mncl-kit
tar xzf mncl-kit-*.tar.gz
mncl-kit-*/install.sh "$PREFIX"

# 7. openacc-c-kit
tar xzf openacc-c-kit-*.tar.gz
openacc-c-kit-*/install.sh "$PREFIX"

# --- 環境を有効化 ---
echo "=== Activating SDK ==="
export SDK_ROOT="$PREFIX"
export PATH="$PREFIX/bin:$PATH"
export LD_LIBRARY_PATH="$PREFIX/lib:${LD_LIBRARY_PATH:-}"
export MNC2_EMU_CMD="gpfn3_package_main"

sdk-versions

# --- スモークテスト ---
echo "=== Smoke test: assemble3 + gpfn3_package_main ==="
echo 'd get $lm0n0c0b0m0p0 1' > /tmp/sample.vsm
assemble3 /tmp/sample.vsm > /tmp/sample.asm
gpfn3_package_main -i /tmp/sample.asm -d /tmp/dump.txt
grep -q "DEBUG-LM0" /tmp/dump.txt
echo "PASS: emulator smoke test"

# --- sdk-examples ---
echo "=== sdk-examples ==="
sdk-examples --list

echo "=== sdk-examples libmnc2: build + test ==="
sdk-examples libmnc2 "$EXAMPLES"
cd "$EXAMPLES"/libmnc2-*/01-nop

ninja
ninja test-emu
echo "PASS: libmnc2/01-nop (emu:process)"

ninja test-emu-lib
echo "PASS: libmnc2/01-nop (emu:lib)"

echo ""
echo "=== ALL PASS ==="
