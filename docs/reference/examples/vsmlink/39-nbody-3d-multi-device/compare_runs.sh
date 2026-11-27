#!/bin/sh
# compare_runs.sh <結果ディレクトリ A> <結果ディレクトリ B> [<比べる先頭のバイト数>]
#
# 2 つの実行の pos_*.bin と vel_*.bin (あれば) を、バイト単位で比べる。デバイスの分け方を変えた
# 2 つの実行 (--devs=0 と --devs=0,1) の一致を確かめるのに使う。vel_*.bin は --vel を付けた実行にだけある。
#
# 3 つ目の引数を付けると、pos_0000.bin の先頭のそのバイト数だけを比べる。タイムステップ数が違う実行
# (1000 タイムステップの実行と、4 タイムステップの実行など) の先頭のコマを比べるときに使う。
# 1 コマ = 粒子数 x 3 成分 x 8 byte。
set -eu
a=$1
b=$2
n=${3:-}
status=0
found=0
for f in "$a"/pos_*.bin "$a"/vel_*.bin; do
    [ -e "$f" ] || continue
    name=$(basename "$f")
    found=1
    if [ ! -e "$b/$name" ]; then
        echo "  DIFF: $name が $b にない" >&2
        status=1
        continue
    fi
    if [ -n "$n" ]; then
        if cmp -n "$n" "$f" "$b/$name"; then echo "  SAME: $name (先頭 $n バイト)"; else echo "  DIFF: $name" >&2; status=1; fi
    else
        if cmp "$f" "$b/$name"; then echo "  SAME: $name"; else echo "  DIFF: $name" >&2; status=1; fi
    fi
done
if [ "$found" -eq 0 ]; then echo "  FAIL: $a に pos_*.bin がない" >&2; exit 1; fi
if [ "$status" -eq 0 ]; then echo "  PASS: $a と $b が一致"; fi
exit $status
