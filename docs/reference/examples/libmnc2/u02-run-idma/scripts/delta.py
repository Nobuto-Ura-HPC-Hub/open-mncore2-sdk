#!/usr/bin/env python3
"""run-idma の出力 (0xID WALL_NS INST ALL_NOP 形式) を標準入力から読み、差分を表示する。

入力:
  # から始まる行は Linux 環境情報等のコメントとして読み飛ばす
  それ以外の行は 4 カラム: `0xID  WALL_NS  INST  ALL_NOP`

Usage:
    run-idma some.idma.dat 0x0a 0x1a | python3 scripts/delta.py

出力カラム (各 checkpoint 行):
    label   WALL_NS          INST       ALL_NOP
    D_WALL_PREV   D_INST_PREV   D_NOP_PREV   ACTIVE_PREV
    D_WALL_FIRST  D_INST_FIRST  ACTIVE_FIRST

    ACTIVE = D_INST - D_NOP  (nop 占有を除外した実質 chip cycle)
"""

import sys


def main():
    rows = []
    for line in sys.stdin:
        line = line.rstrip("\n")
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith("#"):
            # Linux 環境情報等: そのまま stdout へエコー (後で参照用)
            print(stripped)
            continue
        parts = stripped.split()
        if len(parts) != 4:
            print(f"skip: {line!r}", file=sys.stderr)
            continue
        label, w_str, i_str, n_str = parts
        try:
            w = int(w_str)
            i = int(i_str)
            n = int(n_str)
        except ValueError:
            print(f"skip (invalid): {line!r}", file=sys.stderr)
            continue
        rows.append((label, w, i, n))

    if not rows:
        return 0

    first_w, first_i, first_n = rows[0][1], rows[0][2], rows[0][3]
    prev_w, prev_i, prev_n = first_w, first_i, first_n

    for label, w, i, n in rows:
        d_w_prev  = w - prev_w
        d_i_prev  = i - prev_i
        d_n_prev  = n - prev_n
        active_prev = d_i_prev - d_n_prev
        d_w_first = w - first_w
        d_i_first = i - first_i
        active_first = d_i_first - (n - first_n)

        print(
            f"{label:<6} "
            f"wall={w:>18}  inst={i:>14}  nop={n:>14}  "
            f"d_wall_prev={d_w_prev:>12}  d_inst_prev={d_i_prev:>10}  "
            f"d_nop_prev={d_n_prev:>10}  active_prev={active_prev:>10}  "
            f"d_wall_first={d_w_first:>14}  d_inst_first={d_i_first:>12}  "
            f"active_first={active_first:>12}"
        )

        prev_w, prev_i, prev_n = w, i, n

    return 0


if __name__ == "__main__":
    sys.exit(main())
