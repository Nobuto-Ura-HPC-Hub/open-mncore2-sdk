#!/usr/bin/env python3
"""gen_input.py — odd-even-sort / get-neighbor 用の入力データ生成

Usage: gen_input.py <type> <out>

types:
  sorted            — 4096 fp64, [0, 1, ..., 4095]
  reversed          — 4096 fp64, [4095, ..., 0]
  negated           — 4096 fp64, [0, -1, ..., -4095]
  group_reversed    — 4096 fp64, セクション (512 要素) 単位で逆順
  random-123        — 4096 fp64, [0..4095] を seed=123 で完全シャッフル
  nearly_sorted-42  — 4096 fp64, sorted を seed=42 で隣接 50 要素内シャッフル
  sequential        — 16384 u64,  [0, 1, ..., 16383]
"""
import sys
import struct
import random

N_FP64 = 4096
N_U64_SEQ = 16384
GROUP_REVERSED_SIZE = 256  # group_reversed のグループサイズ
NEARLY_SORTED_CHUNK = 50   # nearly_sorted-42 の chunk shuffle サイズ


def write_fp64(path, values):
    with open(path, "wb") as f:
        for v in values:
            f.write(struct.pack("<d", float(v)))


def write_u64(path, values):
    with open(path, "wb") as f:
        for v in values:
            f.write(struct.pack("<Q", int(v)))


def gen_group_reversed():
    data = []
    for sec in range(N_FP64 // GROUP_REVERSED_SIZE):
        data.extend(range(sec * GROUP_REVERSED_SIZE + GROUP_REVERSED_SIZE - 1,
                          sec * GROUP_REVERSED_SIZE - 1, -1))
    return data


def gen_random_123():
    random.seed(123)
    data = list(range(N_FP64))
    random.shuffle(data)
    return data


def gen_nearly_sorted_42():
    random.seed(42)
    data = list(range(N_FP64))
    for k in range(0, N_FP64, NEARLY_SORTED_CHUNK):
        chunk = data[k:k + NEARLY_SORTED_CHUNK]
        random.shuffle(chunk)
        data[k:k + NEARLY_SORTED_CHUNK] = chunk
    return data


def main():
    if len(sys.argv) != 3:
        print("Usage: gen_input.py <type> <out>", file=sys.stderr)
        return 1

    type_, out = sys.argv[1], sys.argv[2]

    if type_ == "sorted":
        write_fp64(out, range(N_FP64))
    elif type_ == "reversed":
        write_fp64(out, range(N_FP64 - 1, -1, -1))
    elif type_ == "negated":
        write_fp64(out, (-i for i in range(N_FP64)))
    elif type_ == "group_reversed":
        write_fp64(out, gen_group_reversed())
    elif type_ == "random-123":
        write_fp64(out, gen_random_123())
    elif type_ == "nearly_sorted-42":
        write_fp64(out, gen_nearly_sorted_42())
    elif type_ == "sequential":
        write_u64(out, range(N_U64_SEQ))
    else:
        print(f"unknown type: {type_}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
