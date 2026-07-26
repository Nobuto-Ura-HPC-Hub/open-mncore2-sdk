#!/bin/bash
# capture-golden-endian.sh
#
# 52-all-id-collect を実行した直後の PDM0 に残っている all-ID collect 結果を
# ENDIAN_CTRL=0..4 の各値で mmap 経由で読み出し、バイナリと sha256 を保存する。
#
# 前提:
#   - 直前に 52-all-id-collect を実機で実行し、PDM0 に 32768 bytes の結果が
#     残っている状態 (_build/all_id_collect.bin が生成される実行が成功した直後)
#   - u01-mmap を ninja install 済みで ../bin/ に pio-write / read-pdm がある
#
# 出力:
#   golden/golden-ec${N}.bin       — ENDIAN_CTRL=N での mmap 読み出し結果
#   golden/golden-endian.sha256    — 5 ファイルの sha256 一覧
#
# ※ golden/ は _build/ と違って永続（git 管理対象）。一度取得したら
#    意図的に消さない限り再取得しない。
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
OUT_DIR="$SCRIPT_DIR/golden"
BIN_DIR="$SCRIPT_DIR/../bin"

ADDR=0
LEN=32768   # 4096 * sizeof(uint64_t)

if [ ! -x "$BIN_DIR/pio-write" ] || [ ! -x "$BIN_DIR/read-pdm" ]; then
    echo "error: u01-mmap の pio-write / read-pdm が $BIN_DIR に見つかりません" >&2
    echo "  先に u01-mmap で 'ninja install' を実行してください" >&2
    exit 1
fi

mkdir -p "$OUT_DIR"
SHA_FILE="$OUT_DIR/golden-endian.sha256"

# 既にゴールデンデータが揃っている場合は再取得しない
already_have=1
for ec in 0 1 2 3 4; do
    if [ ! -s "$OUT_DIR/golden-ec${ec}.bin" ]; then
        already_have=0
        break
    fi
done
if [ "$already_have" = "1" ] && [ -s "$SHA_FILE" ]; then
    echo "既にゴールデンデータあり。取得をスキップします。"
    echo "  再取得するには $OUT_DIR/golden-ec*.bin $SHA_FILE を削除してください。"
    echo ""
    echo "=== summary ==="
    cat "$SHA_FILE"
    exit 0
fi

: > "$SHA_FILE"

# $OUT_DIR に cd して相対パスで sha256 を記録する
# (sha256sum の出力にフルパスを埋め込まないため)
pushd "$OUT_DIR" >/dev/null
for ec in 0 1 2 3 4; do
    echo "=== ENDIAN_CTRL=$ec ==="
    "$BIN_DIR/pio-write" 0x40 "0x$ec"

    name="golden-ec${ec}.bin"
    "$BIN_DIR/read-pdm" "$ADDR" "$LEN" > "$name"

    sha256sum "$name" | tee -a "golden-endian.sha256"
done
popd >/dev/null

# ENDIAN_CTRL を 0 に戻す
"$BIN_DIR/pio-write" 0x40 0x0

echo ""
echo "=== summary ==="
cat "$SHA_FILE"
