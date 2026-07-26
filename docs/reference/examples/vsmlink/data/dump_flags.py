#!/usr/bin/env python3
"""collected_flags.bin のビット分布を表示・検証する。

Usage:
  python3 dump_flags.py <file>              # 数値列 (自動検証向き)
  python3 dump_flags.py <file> --human      # 人間向け表示
"""
import struct, sys

BIT_NAMES = [
    'cross_MAB_right', 'cross_MAB_left',
    'cross_L1B_right', 'cross_L1B_left',
    'cross_L2B_right', 'cross_L2B_left',
    'cross_chip_right', 'cross_chip_left',
    'data_edge_right', 'data_edge_left',
]

def main():
    if len(sys.argv) < 2:
        print(f"usage: {sys.argv[0]} <file> [--human]", file=sys.stderr)
        sys.exit(1)

    human = '--human' in sys.argv

    with open(sys.argv[1], 'rb') as f:
        data = f.read()

    n = len(data) // 8
    vals = struct.unpack(f'<{n}Q', data)

    bit_counts = [0] * 10
    zero_count = 0
    for v in vals:
        if v == 0:
            zero_count += 1
        for b in range(10):
            if v & (1 << b):
                bit_counts[b] += 1

    total_bits = sum(bit_counts)

    # outermost → innermost 順 (bit 9, 8, ..., 0)
    order = list(range(9, -1, -1))

    if human:
        print(f'total PEs: {n}')
        print(f'total set bits: {total_bits}')
        print(f'within_MAB (flags=0): {zero_count}')
        print()
        for b in order:
            print(f'  bit {b:2d} {BIT_NAMES[b]:25s}: {bit_counts[b]}')
    else:
        # total : within_MAB : bit9 bit8 ... bit0
        parts = [str(n), ':', str(zero_count), ':']
        for b in order:
            parts.append(str(bit_counts[b]))
        print(' '.join(parts))

if __name__ == '__main__':
    main()
