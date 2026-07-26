#!/bin/bash
# test-run-idma.sh — 任意の idma.dat を send → exec → recv で実行
# send/recv は u01-mmap の send-dma / read-pdm (libgpfn3 直接) を使用
# Usage: test-run-idma.sh IDMA_DAT [OPTIONS]
set -eu


SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
T="$SCRIPT_DIR/../_build"                    # u02-run-idma (run-idma)
T1="$SCRIPT_DIR/../../u01-mmap/_build"       # u01-mmap (send-dma, read-pdm)

if [ $# -lt 1 ]; then
  cat >&2 <<'USAGE'
Usage: test-run-idma.sh IDMA_DAT [OPTIONS]

  exec のみ:
    test-run-idma.sh kernel.idma.dat

  send → exec → recv:
    test-run-idma.sh kernel.idma.dat --send OFFSET FILE --recv OFFSET SIZE

  例 (roundtrip):
    test-run-idma.sh roundtrip.idma.dat \
      --send 0 input.bin \
      --recv 1048576 32768
USAGE
  exit 1
fi

IDMA="$1"
shift

if [ ! -f "$IDMA" ]; then
  echo "エラー: $IDMA が見つからない" >&2
  exit 1
fi

SEND_OFFSET=""
SEND_FILE=""
RECV_OFFSET=""
RECV_SIZE=""

while [ $# -gt 0 ]; do
  case "$1" in
    --send)
      SEND_OFFSET="$2"
      SEND_FILE="$3"
      shift 3
      ;;
    --recv)
      RECV_OFFSET="$2"
      RECV_SIZE="$3"
      shift 3
      ;;
    *)
      echo "エラー: 不明なオプション: $1" >&2
      exit 1
      ;;
  esac
done

echo "=== test-run-idma ==="
echo "  idma: $IDMA"

# send (u01-mmap の send-dma)
if [ -n "$SEND_FILE" ]; then
  if [ ! -x "$T1/send-dma" ]; then
    echo "エラー: u01-mmap がビルドされていない: $T1" >&2
    exit 1
  fi
  echo ""
  echo "--- send: $SEND_FILE → PDM0+$SEND_OFFSET (via u01-mmap send-dma) ---"
  cat "$SEND_FILE" | "$T1/send-dma" "$SEND_OFFSET"
fi

# exec (u02 の run-idma)
echo ""
echo "--- exec ---"
"$T/run-idma" "$IDMA"

# recv (u01-mmap の read-pdm)
if [ -n "$RECV_SIZE" ]; then
  if [ ! -x "$T1/read-pdm" ]; then
    echo "エラー: u01-mmap がビルドされていない: $T1" >&2
    exit 1
  fi
  echo ""
  echo "--- recv: PDM0+$RECV_OFFSET, $RECV_SIZE bytes (via u01-mmap read-pdm) ---"
  "$T1/read-pdm" "$RECV_OFFSET" "$RECV_SIZE" | xxd
fi
