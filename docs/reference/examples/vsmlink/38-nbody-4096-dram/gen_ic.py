#!/usr/bin/env python3
"""gen_ic.py — 束縛系の初期条件を struct 形バイナリで生成する。

CI 用の self-contained な入力。1 粒子 8 double (little endian): x, y, z, m, vx, vy, vz, pad。
半径 1 の球内にランダム配置、質量は等しく合計 1、小さい速度。決定的 (再現性のため)。
realistic な Plummer 球はユーザが NEMO の mkplummer で作り、同じ struct 形に正規化して
差し替える (https://carma.astro.umd.edu/nemo/archive/)。

Usage: gen_ic.py <N> <out.bin>
"""
import sys
import struct

MASK = (1 << 64) - 1


def main():
    if len(sys.argv) != 3:
        sys.stderr.write("usage: gen_ic.py <N> <out.bin>\n")
        return 1
    n = int(sys.argv[1])
    out = sys.argv[2]

    s = 88172645463325252

    def urand():
        nonlocal s
        s ^= (s << 13) & MASK
        s ^= s >> 7
        s ^= (s << 17) & MASK
        return (s >> 11) * (1.0 / 9007199254740992.0)

    with open(out, "wb") as f:
        for _ in range(n):
            while True:
                px = 2 * urand() - 1
                py = 2 * urand() - 1
                pz = 2 * urand() - 1
                r2 = px * px + py * py + pz * pz
                if 0.02 <= r2 <= 1.0:
                    break
            m = 1.0 / n
            vx = 0.1 * (2 * urand() - 1)
            vy = 0.1 * (2 * urand() - 1)
            vz = 0.1 * (2 * urand() - 1)
            f.write(struct.pack("<8d", px, py, pz, m, vx, vy, vz, 0.0))
    return 0


if __name__ == "__main__":
    sys.exit(main())
