#!/bin/bash
# test-endian-ctrl.sh — ENDIAN_CTRL 全値 (0,1,2,3,4) の mmap 読み出し比較
# 64 バイト連番 (0x01..0x40) を CTRL=0 で DMA 送信し、各 CTRL で read-pdm する
set -eu

DMA_CTRL=${1:-0}
echo

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
T="$SCRIPT_DIR/../_build"

echo "=== ゼロクリア (128 bytes) ==="
"$T/pio-write" 0x40 $DMA_CTRL
python3 -c 'import sys; sys.stdout.buffer.write(b"\0"*128)' | "$T/write-pdm" 0

echo "=== DMA 送信: 0x01..0x40 (64 bytes) at CTRL=$DMA_CTRL ==="
python3 -c 'import sys; sys.stdout.buffer.write(bytes(range(1,65)))' | "$T/send-dma" 0x0

for c in 0 1 2 3 4; do
  echo ""
  echo "=== CTRL=$c ==="
  "$T/pio-write" 0x40 "0x$c"
  "$T/read-pdm" 0 128 | xxd -g 1
done

"$T/pio-write" 0x40 0x0
