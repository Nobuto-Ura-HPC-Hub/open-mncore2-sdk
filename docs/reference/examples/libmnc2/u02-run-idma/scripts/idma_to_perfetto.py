#!/usr/bin/env python3
"""run-idma の stdout を Perfetto chrome trace JSON に変換する。

使い方:
    run-idma <path.idma.dat> <wd1> <wd2> ... | idma_to_perfetto.py > trace.json
    # 出来た trace.json を https://ui.perfetto.dev に drag&drop

入力フォーマット (run-idma の stdout):
    # uname: ... / # host: ... 等のコメント行
    0xLL <wall_ns> <inst> <all_nop>      ← Load 完了
    0xKK <wall_ns> <inst> <all_nop>      ← Kick 直後
    0xNN <wall_ns> <inst> <all_nop>      ← WD=NN 解除
    ...
    # Capture B snapshot (threshold=1000):
    #   all_nop   = ...
    ...
"""

import json
import re
import sys

PID_HOST = 1
TID_DMA = 1
TID_CTR = 2

NAMED = {"0xLL": "load", "0xKK": "kick"}


def main():
    events = []
    captures = []
    cur_cap = None

    events += [
        {"name": "process_name", "ph": "M", "pid": PID_HOST, "tid": 0,
         "args": {"name": "run-idma"}},
        {"name": "thread_name", "ph": "M", "pid": PID_HOST, "tid": TID_DMA,
         "args": {"name": "DMA / WD"}},
        {"name": "thread_name", "ph": "M", "pid": PID_HOST, "tid": TID_CTR,
         "args": {"name": "counters"}},
    ]

    for line in sys.stdin:
        line = line.rstrip()
        if not line:
            continue

        m = re.match(r"# Capture (\w) snapshot \(threshold=(\d+)\):", line)
        if m:
            cur_cap = {"letter": m.group(1), "threshold": int(m.group(2)), "fields": {}}
            captures.append(cur_cap)
            continue
        if line.startswith("#   ") and cur_cap is not None:
            mm = re.match(r"#\s+(\S+)\s*=\s*(\d+)", line)
            if mm:
                cur_cap["fields"][mm.group(1)] = int(mm.group(2))
            continue
        if line.startswith("#"):
            cur_cap = None
            continue

        parts = line.split()
        if len(parts) < 4 or not parts[0].startswith("0x"):
            continue
        eid = parts[0]
        try:
            wall_ns = int(parts[1])
            inst = int(parts[2])
            all_nop = int(parts[3])
        except ValueError:
            continue
        ts_us = wall_ns / 1000.0

        if eid in NAMED:
            name = NAMED[eid]
        else:
            name = f"WD={int(eid, 16):#04x}_release"

        events.append({
            "name": name, "ph": "i",
            "pid": PID_HOST, "tid": TID_DMA,
            "ts": ts_us,
            "s": "p",
            "args": {"inst": inst, "all_nop": all_nop, "raw_id": eid,
                     "wall_ns": wall_ns},
        })

        events.append({
            "name": "chip_counters", "ph": "C",
            "pid": PID_HOST, "tid": TID_CTR,
            "ts": ts_us,
            "args": {"inst": inst, "all_nop": all_nop,
                     "non_nop": inst - all_nop},
        })

    last_ts = max((e.get("ts", 0) for e in events), default=0)
    for cap in captures:
        events.append({
            "name": f"Capture {cap['letter']} (threshold={cap['threshold']})",
            "ph": "i", "pid": PID_HOST, "tid": TID_DMA,
            "ts": last_ts + 1, "s": "p",
            "args": cap["fields"],
        })

    out = {"traceEvents": events, "displayTimeUnit": "ns"}
    json.dump(out, sys.stdout, indent=2)
    sys.stdout.write("\n")


if __name__ == "__main__":
    main()
