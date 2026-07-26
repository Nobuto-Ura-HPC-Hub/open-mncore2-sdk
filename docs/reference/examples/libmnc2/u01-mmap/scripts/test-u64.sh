#!/bin/bash
# test-u64.sh — u64 値を DMA 送信し、mmap で PDM を観測する
# Usage: test-u64.sh [HEX_U64]
#   HEX_U64: 0x なしの 16 桁 hex (例: 3ff0000000000000)
#   省略時は data/test-u64.txt から読む
set -eu

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
TOOL_DIR="$SCRIPT_DIR/../_build"

HEX="${1:-$(tr -d '[:space:]' < "$SCRIPT_DIR/../data/test-u64.txt")}"

echo "=== PIO 0x40 (ENDIAN_CTRL) ==="
"$TOOL_DIR/pio-read" 0x40

echo ""
echo "=== send-dma 0x0: u64 0x${HEX} ==="
echo "$HEX" | xxd -r -p | "$TOOL_DIR/send-dma" 0x0

echo ""
echo "=== read-pdm 0 8 ==="
"$TOOL_DIR/read-pdm" 0 8 | od -A x -t x1z -v
