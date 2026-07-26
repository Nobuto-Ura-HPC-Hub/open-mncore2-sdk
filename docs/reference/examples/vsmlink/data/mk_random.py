#!/usr/bin/env python3
"""mk_random.py — fp64 4096 個の完全 random bin を生成して sha256 名で保存

Usage:
    mk_random.py max          → range [0, max)
    mk_random.py min max      → range [min, max)

ファイル名は内容の sha256 先頭 8 文字 (例: a3f7c821.bin)。
毎回異なる random なので実行ごとに違う sha256 になる。
"""
import sys
import struct
import random
import hashlib

if len(sys.argv) == 2:
    lo, hi = 0.0, float(sys.argv[1])
elif len(sys.argv) == 3:
    lo, hi = float(sys.argv[1]), float(sys.argv[2])
else:
    print(f"Usage: {sys.argv[0]} [min] max", file=sys.stderr)
    sys.exit(1)

if hi <= lo:
    print(f"error: max ({hi}) must be greater than min ({lo})", file=sys.stderr)
    sys.exit(1)

N = 4096
data = bytearray()
for _ in range(N):
    v = random.uniform(lo, hi)
    data += struct.pack('<d', v)

sha = hashlib.sha256(bytes(data)).hexdigest()[:8]
out = f"{sha}.bin"
with open(out, 'wb') as f:
    f.write(data)

print(f"  {out}: {N} doubles in [{lo}, {hi}) ({len(data)} bytes)")
