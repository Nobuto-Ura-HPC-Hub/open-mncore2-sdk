#!/usr/bin/env python3
"""gen_ic_plummer.py — Plummer 球の初期条件を struct 形バイナリで生成する。

出力は gen_ic.py と同じ形式 (1 粒子 8 double, little endian: x, y, z, m, vx, vy, vz, pad)。
乱数は gen_ic.py と同じ xorshift を使い、決定的に生成する (再現性のため)。

gen_ic.py の一様乱数配置と違い、**力学的な平衡から生成する**。Plummer 球は分布関数が
解析的に書けるので、そこから直接標本化できる。平衡から作れば、実行しても系の形は
そのまま保たれる (一様乱数配置はまず収縮し、そのあと広がる)。

星団を 2 個置けば衝突を計算できる。総粒子数を星団数で割った数が 1 個あたりの粒子数に
なるので、4096 を 2 個にすると 2048 + 2048 になる。

**総粒子数は PE 数 (4096) ちょうどにすること。** accumulate の番地を連番に採る条件が
それだからである (gen_accumulate.py の total == PE_COUNT を参照)。

Usage: gen_ic_plummer.py <N> <out.bin> [--clusters=n] [--sep=D] [--vrel=V]
                                       [--impact=B] [--scale=A]

  --clusters  星団の数 (1 または 2)。既定 2
  --sep       2 個のときの重心間の距離。既定 2.0
  --vrel      相対速度 (近づく向きが正)。既定 0.0 (静止から落とす)
  --impact    衝突径数。0 で正面衝突。既定 0.0
  --scale     Plummer のスケール長 a。既定 0.3
  --eps2      ビリアル調整に使う softening の 2 乗。既定 0.01 (nbody.c の EPS2 と同じ)
"""
import sys
import math
import struct

MASK = (1 << 64) - 1

# 速度の棄却法で使う密度 q^2 (1-q^2)^(7/2) の上限。実際の最大は q=sqrt(2/9) で 0.0919...
G_MAX = 0.1


def make_urand(seed):
    """gen_ic.py と同じ xorshift。[0,1) の一様乱数を返す。"""
    s = seed

    def urand():
        nonlocal s
        s ^= (s << 13) & MASK
        s ^= s >> 7
        s ^= (s << 17) & MASK
        return (s >> 11) * (1.0 / 9007199254740992.0)

    return urand


def sample_plummer(n, mass, scale, urand):
    """Plummer 球から n 粒子を標本化し、(x, y, z, m, vx, vy, vz) のリストを返す。

    G = M = a = 1 の単位系で標本化してから、質量 mass とスケール長 scale に合わせて
    掛け直す。速度のスケールは sqrt(G * mass / scale) になる。
    """
    vscale = math.sqrt(mass / scale)
    out = []
    for _ in range(n):
        # 半径。質量分布 M(<r) = r^3 / (1+r^2)^(3/2) を逆に解く。
        # X を (0,1) の一様乱数として r = 1 / sqrt(X^(-2/3) - 1)。
        # X が 1 に近いと r が発散するので、外れ値は引き直す。
        while True:
            x = urand()
            if x <= 0.0 or x >= 0.999:
                continue
            r = 1.0 / math.sqrt(x ** (-2.0 / 3.0) - 1.0)
            break
        # 向きは球面上に一様。cos θ を [-1,1] の一様乱数から引く。
        ct = 2.0 * urand() - 1.0
        st = math.sqrt(max(0.0, 1.0 - ct * ct))
        ph = 2.0 * math.pi * urand()
        px, py, pz = r * st * math.cos(ph), r * st * math.sin(ph), r * ct

        # 速度。分布関数から出る q = v / v_esc の密度は q^2 (1-q^2)^(7/2)。
        # 棄却法で引く (Aarseth, Henon, Wielen 1974 の手順)。
        while True:
            q = urand()
            if urand() * G_MAX <= q * q * (1.0 - q * q) ** 3.5:
                break
        v = q * math.sqrt(2.0) * (1.0 + r * r) ** -0.25
        ct = 2.0 * urand() - 1.0
        st = math.sqrt(max(0.0, 1.0 - ct * ct))
        ph = 2.0 * math.pi * urand()
        vx, vy, vz = v * st * math.cos(ph), v * st * math.sin(ph), v * ct

        out.append((px * scale, py * scale, pz * scale, mass / n,
                    vx * vscale, vy * vscale, vz * vscale))
    return out


def recenter(ps):
    """重心と全体の運動量を 0 にする。標本化の揺らぎで残るずれを取り除く。"""
    mt = sum(p[3] for p in ps)
    cx = sum(p[0] * p[3] for p in ps) / mt
    cy = sum(p[1] * p[3] for p in ps) / mt
    cz = sum(p[2] * p[3] for p in ps) / mt
    ux = sum(p[4] * p[3] for p in ps) / mt
    uy = sum(p[5] * p[3] for p in ps) / mt
    uz = sum(p[6] * p[3] for p in ps) / mt
    return [(p[0] - cx, p[1] - cy, p[2] - cz, p[3],
             p[4] - ux, p[5] - uy, p[6] - uz) for p in ps]


def virial_ratio(ps, eps2):
    """-T/U を返す。平衡なら 0.5 付近になる。O(N^2) なので生成時の確認にだけ使う。"""
    t = 0.5 * sum(p[3] * (p[4] ** 2 + p[5] ** 2 + p[6] ** 2) for p in ps)
    u = 0.0
    for i in range(len(ps)):
        a = ps[i]
        for j in range(i + 1, len(ps)):
            b = ps[j]
            dx, dy, dz = a[0] - b[0], a[1] - b[1], a[2] - b[2]
            u -= a[3] * b[3] / math.sqrt(dx * dx + dy * dy + dz * dz + eps2)
    return t, u, (-t / u if u != 0.0 else float("nan"))


def virial_scale(ps, eps2):
    """ビリアル平衡 (-T/U = 0.5) になるよう速度を一律に掛け直し、係数を返す。

    Plummer の分布関数は softening の無い 1/r ポテンシャルに対する平衡なので、
    softening を入れて計算すると**ポテンシャルが浅くなって速度が余る**。
    a=0.3 に対し eps=0.1 では -T/U が 0.53 程度になり、そのまま実行すると系が膨らむ。

    ここではシミュレーションが実際に使う softening でビリアル平衡にする。分布関数の
    意味での厳密な平衡にはならない (形が少し違う) が、全体として広がったり縮んだり
    しない状態にはなる。
    """
    t, u, _ = virial_ratio(ps, eps2)
    if t <= 0.0:
        return ps, 1.0
    k = math.sqrt(0.5 * abs(u) / t)
    return [(p[0], p[1], p[2], p[3], p[4] * k, p[5] * k, p[6] * k) for p in ps], k


def opt(argv, name, default):
    for a in argv:
        if a.startswith(name + "="):
            return a[len(name) + 1:]
    return default


def main():
    args = [a for a in sys.argv[1:] if not a.startswith("--")]
    if len(args) != 2:
        sys.stderr.write(__doc__.split("Usage: ")[1])
        return 1
    n = int(args[0])
    out = args[1]
    clusters = int(opt(sys.argv, "--clusters", "2"))
    sep = float(opt(sys.argv, "--sep", "2.0"))
    vrel = float(opt(sys.argv, "--vrel", "0.0"))
    impact = float(opt(sys.argv, "--impact", "0.0"))
    scale = float(opt(sys.argv, "--scale", "0.3"))
    eps2 = float(opt(sys.argv, "--eps2", "0.01"))

    if clusters not in (1, 2):
        sys.stderr.write("clusters は 1 か 2\n")
        return 1
    if n % clusters != 0:
        sys.stderr.write("N ({}) が星団数 ({}) で割り切れない\n".format(n, clusters))
        return 1

    urand = make_urand(88172645463325252)
    per = n // clusters
    mass = 1.0 / clusters          # 合計質量が 1 になるように分ける

    # ビリアル調整は**星団ごとに、配置する前に**行う。2 個を並べてから全体で調整すると、
    # 星団どうしの重力ポテンシャルまで数えてしまい、1 個ずつの平衡が崩れる。
    ps = []
    kfac = []
    if clusters == 1:
        c, k = virial_scale(recenter(sample_plummer(per, mass, scale, urand)), eps2)
        kfac.append(k)
        ps = c
    else:
        # 重心を x 方向に ±sep/2、衝突径数は y 方向にずらして与える。
        # 相対速度は近づく向き (x 方向) に vrel。全体の運動量が 0 になるよう
        # 両方に半分ずつ逆向きに入れる。
        for sign in (+1.0, -1.0):
            c, k = virial_scale(recenter(sample_plummer(per, mass, scale, urand)), eps2)
            kfac.append(k)
            ps += [(p[0] + sign * sep / 2.0,
                    p[1] + sign * impact / 2.0,
                    p[2],
                    p[3],
                    p[4] - sign * vrel / 2.0,
                    p[5], p[6]) for p in c]

    with open(out, "wb") as f:
        for p in ps:
            f.write(struct.pack("<8d", p[0], p[1], p[2], p[3], p[4], p[5], p[6], 0.0))

    # 生成した系の素性を出す。星団 1 個なら -T/U が 0.5 になっているはずである。
    # 2 個のときは分離した 2 星団をまとめて見るので 0.5 にはならない (星団どうしの
    # ポテンシャルが入り、まだ落下していないぶん運動エネルギーも足りない)。
    t, u, q = virial_ratio(ps, eps2)
    sys.stderr.write(
        "generated {} (N={}, clusters={}, scale={}, sep={}, vrel={}, impact={}, eps2={})\n"
        .format(out, n, clusters, scale, sep, vrel, impact, eps2))
    sys.stderr.write("  T={:.6g} U={:.6g} E={:.6g} -T/U={:.4f}\n".format(t, u, t + u, q))
    sys.stderr.write("  ビリアル調整の係数: {}\n".format(
        " ".join("{:.4f}".format(k) for k in kfac)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
