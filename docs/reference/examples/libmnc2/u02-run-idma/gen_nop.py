"""data/nop_xN.vsm の生成スクリプト (参考)。

なぜ生成しているか
------------------
IDMA のあふれ (キューと命令バッファ) が何個目の kick で起きるかを実測するため、
大きさの違う nop だけの命令列が要る。 vsm はループが書けない直線コードなので、
命令数を増やすには行を並べるしかない。 手で並べる量ではないので生成している。

生成結果の data/nop_x*.vsm は **リポジトリにコミット済み**で、 ビルドはそちらを使う。
このスクリプトは別の大きさを試すときだけ走らせて、 出力をコミットし直す。

使い方
------
    cd share/examples/libmnc2/u02-run-idma
    python3 gen_nop.py 8192 data/nop_x8192.vsm
    ninja                       # assemble3 で .idma.dat を作り直す

出力先を省略できないのは、 うっかり上書きするのを避けるため。
"""

import sys

# tests/unit/api/data/nop.vsm と同じ 1 行 (先頭はタブ)。
NOP_LINE = "\tnop/4\n"


def main(argv):
    if len(argv) != 3:
        print("usage: gen_nop.py COUNT OUT.vsm", file=sys.stderr)
        return 2

    try:
        count = int(argv[1], 0)
    except ValueError:
        print("invalid COUNT: %s" % argv[1], file=sys.stderr)
        return 2
    if count < 1:
        print("COUNT must be >= 1", file=sys.stderr)
        return 2

    out = argv[2]
    with open(out, "w") as fp:
        fp.write("# nop x %d — gen_nop.py の生成物。直接編集しない\n" % count)
        for _ in range(count):
            fp.write(NOP_LINE)

    print("wrote %s (%d nop lines)" % (out, count))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
