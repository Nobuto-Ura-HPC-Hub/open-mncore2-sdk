#!/bin/bash
# test-random.sh — random 入力で sort して sorted と cmp (あそび用)
# Usage: bash test-random.sh [emu-lib|device]
#   emu-lib (default): emu:lib backend (数十分かかる)
#   device:            実機 backend
set -eu

backend="${1:-emu-lib}"
case "$backend" in
    emu-lib) export LD_LIBRARY_PATH="$(realpath ../../../_mncore2-sdk-v1/lib/emu-lib)" ;;
    device)  unset LD_LIBRARY_PATH ;;
    *) echo "Usage: $0 [emu-lib|device]" >&2; exit 1 ;;
esac

# build + fetch
ninja build-e2e
ninja _build/1720baa2.bin _build/d5575075.bin

# run + cmp
./_build/sort _build/1720baa2.bin _build/out.bin
cmp _build/out.bin _build/d5575075.bin && echo "[test-random] PASS" || { echo "[test-random] FAIL"; exit 1; }
