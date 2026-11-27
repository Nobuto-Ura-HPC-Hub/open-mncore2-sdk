#!/bin/sh
# compare_with_28.sh <28 の結果ディレクトリ> <この example の結果ディレクトリ>
# 各ディレクトリで最後に書かれた pos と vel のスナップショットを、バイト単位で比べる。
set -eu
a=$1
b=$2
for tag in pos vel; do
    fa=$(ls -t "$a"/${tag}_*_0000.bin | head -1)
    fb=$(ls -t "$b"/${tag}_*_0000.bin | head -1)
    if cmp "$fa" "$fb"; then
        echo "  SAME: $tag ($fa と $fb)"
    else
        echo "  DIFF: $tag ($fa と $fb)" >&2
        exit 1
    fi
done
echo "  PASS: 28-nbody-3d と、位置と速度のスナップショットがビット単位で一致"
