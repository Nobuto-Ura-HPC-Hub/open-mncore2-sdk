#!/usr/bin/env python3
"""gen_vsm.py <steps> <switch> — 35-emit-from-debug の _vsm を標準出力に書く。

全 PE に初期値（f64）を @distribute で配り、@broadcast の値（f64）を 1 回ごとに足し（dvadd）、足した後の値を @emit_from で書き出す。
書き出す PE は、<switch> 回目までが HW PE ID 817、それ以降が HW PE ID 529。最後に @reduce で全 PE の合計を取る。
"""
import sys

steps = int(sys.argv[1])
switch = int(sys.argv[2])

print("# ABI PFN3_SBY64")
print("#")
print(f"# @emit_from のデバッグ用の使い方: {steps} 回の @broadcast の後で、HW PE ID 817（{switch} 回目まで）と 529 の値を書き出す")
print("#")
print("\t@distribute _init from_param 8 size 1 $lr8")
for k in range(1, steps + 1):
    pe = 817 if k <= switch else 529
    print(f"\t@broadcast _b from_param 16 size 1 $lr16")
    print("\tlpassa $lr16 $ln0")
    print("\tnop/2")
    print("\tdvadd $ln0 $lr8 $lr8")
    print(f"\t@emit_from em pe {pe} to_param 40 size 1 $lr8")
print("\t@reduce _sum to_param 24 $lr8 dfadd")
