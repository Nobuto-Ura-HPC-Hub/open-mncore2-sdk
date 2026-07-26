#!/usr/bin/env python3
"""data/nbody.vsm の生成スクリプト (参考)。

なぜ生成しているか
------------------
vsm はループが書けない直線コードなので、粒子 NP=32 個分の broadcast 引き出しと
計算が展開されて 500 行を超える。 内訳は l1bmp 16 個 (2 粒子ずつ) と計算ブロック
32 個で、 レジスタ番号とアドレスが規則的にずれていく。 手で書くと間違えるため
生成している。

生成結果の data/nbody.vsm は **リポジトリにコミット済み**で、 ビルドはそちらを使う。
このスクリプトは設計を変えるときだけ走らせて、 出力をコミットし直す。

使い方
------
    cd share/examples/libmnc2/65-nbody-like
    python3 gen_nbody.py data/nbody.vsm
    ninja                       # assemble3 で .idma.dat を作り直す
    ninja test-emu-lib          # golden と bit 一致するか確認

出力先を省略できないのは、 うっかり上書きするのを避けるため。

何を変えると何が変わるか
------------------------
NP (粒子数)
    broadcast は l2bmb が 1 回で運ぶ 64 u64 を (x_i, m_i) の組で埋めきる設計なので、
    NP = 32 がちょうど 64 u64 になる。 これを変えるなら 64 u64 に収まる偶数にする
    (NP=16 なら 32 u64 しか使わず余る)。 l1bmp は NP/2 個になる。
    ex_nbody.c の NP も同じ値に直すこと (golden の総和が変わる)。

LB_DIST / LB_BCAST / LB_COLLECT
    L1BM のスロット先頭アドレス。 l1bmd 1 命令は addr から 256 u64 を占有するので
    256 u64 グリッドに置く。 addr は 64 u64 アラインが必須 (dev manual 3.6.8.20)。
    詳しくは docs/test-and-example-layout.md の節 2.6 を参照。

設計のポイント
--------------
distribute (各 PE に違う値)
    l1bmd は 4 cycle 動作で L1BM[addr + cycle*64] の 64 u64 を配下 64 PE に
    1 個ずつ配る。 cycle 0 に x_j、 cycle 1 に m_j を置いて 1 命令 (/1100) で
    2 値を配る。 cycle 2,3 は未初期化領域を読むのでマスクで止める。
    上流 (mvp x8 + l2bmb@0..7) の並びは vsm-linker の distribute テンプレートと
    同じ形。 ただしテンプレートは 1 回で 1 値 (/1000) なので、 2 値を 1 命令に
    まとめている点だけがこの example 独自。

broadcast (全 PE に同じ値)
    l1bmp は 4 cycle で L1BM[addr + cycle] の 1 u64 を全 64 PE に放送する。
    L1BM 側のアラインメント制約は無い (dev manual 3.6.8.6) ので 4 u64 刻みに
    並べられる。 (x_i, m_i) の組を先頭から詰めれば host 側の詰め物は要らない。
    データ移動は mvb 1 個 + l2bmb 1 個だけで NP 粒子分が全 4096 PE に届く。
"""
import sys

NP = 32                     # broadcast する粒子数 (64 u64 / 2 値)
LB_DIST = 0                 # S(0) 分配入力: [0,64)=x_j  [64,128)=m_j
LB_BCAST = 256              # S(1) broadcast 受け: [256,320) = 64 u64
LB_COLLECT = 512            # S(2) 結合出力

L = []
def e(s=''):
    L.append(s)

e('# 65-nbody-like — distribute と broadcast の教材 (f64、 質量積 w = m_i * m_j)')
e('#')
e('# このファイルは gen_nbody.py の出力。 手で編集せず、 スクリプトを直して生成し直すこと。')
e('#')
e('# 各 PE が自粒子 (x_j, m_j) を持つ。 粒子 i=0..%d の (x_i, m_i) を broadcast し、 各 PE で' % (NP - 1))
e('#     F_j += (x_i - x_j) * (m_i * m_j)')
e('# を累積して collect する。 割り算/rsqrt 無し、 f64。')
e('#')
e('# この example の主題は 2 つの配り方の対比:')
e('#')
e('#   distribute (各 PE に違う値)  mvp x8 -> l2bmb@0..7 -> l1bmd')
e('#     l1bmd は 4 cycle 動作で L1BM[addr + cycle*64] の 64 u64 を配下 64 PE に 1 個ずつ配る。')
e('#     cycle 0 に x_j、 cycle 1 に m_j を置くことで 1 命令で 2 値を配る (/1100)。')
e('#     運ぶ実データが 4096 PE x 2 値 = 8192 u64 あるので、 上流の mvp と l2bmb は本数が要る。')
e('#')
e('#   broadcast (全 PE に同じ値)   mvb -> l2bmb -> l1bmp')
e('#     l2bmb が 1 回で運ぶ 64 u64 を (x_i, m_i) の組 %d 個で埋めきる。' % NP)
e('#     データ移動は mvb 1 個 + l2bmb 1 個だけで %d 粒子分が全 4096 PE に届く。' % NP)
e('#     l1bmp は 4 cycle で L1BM[addr + cycle] の 1 u64 を全 PE に放送するので、')
e('#     1 命令で 2 粒子分 (x, m, x, m) を引き出せる。 %d 命令で 64 u64 を使い切る。' % (NP // 2))
e('#     host 側の詰め物は不要 (先頭から %d u64 を素直に並べるだけ)。' % (NP * 2))
e('#')
e('# レジスタ:')
e('#   $ln0 = x_j、 $ln2 = m_j          (LM1、 distribute で受ける)')
e('#   $lr0/$lr2 = 偶数番粒子の (x_i, m_i)、 $lr4/$lr6 = 奇数番粒子の (x_i, m_i)  (GRF0、 broadcast)')
e('#   $lr8 = diff/prod 作業用、 $ls6 = w = m_i*m_j、 $ls4 = acc = F_j')
e('#')
e('# L1BM 割り当て (l1bmd 1 命令の占有は addr_b から 256 u64、 addr_b は 64 u64 アライン)')
e('#   $lb%-4d [%4d, %4d)  分配入力。 cycle 0 = [%d, %d) が x_j、 cycle 1 = [%d, %d) が m_j'
  % (LB_DIST, LB_DIST, LB_DIST + 256, LB_DIST, LB_DIST + 64, LB_DIST + 64, LB_DIST + 128))
e('#   $lb%-4d [%4d, %4d)  broadcast 受け。 実占有は先頭 64 u64 [%d, %d)'
  % (LB_BCAST, LB_BCAST, LB_BCAST + 256, LB_BCAST, LB_BCAST + 64))
e('#   $lb%-4d [%4d, %4d)  結合出力。 回収は cycle 0 の [%d, %d) のみ'
  % (LB_COLLECT, LB_COLLECT, LB_COLLECT + 256, LB_COLLECT, LB_COLLECT + 64))
e('#')
e('# PDM: [0..4095]=x_j、 [4096..8191]=m_j、 [8192..%d]=(x_i, m_i) x %d' % (8192 + NP * 2 - 1, NP))
e('# 出力: PDM[0..4095] = F_j (double)、 recv_wait_tag=0x1e')
e()
e('\tnop; wait i10')
e('\tnop/3')

half = ['0.0', '0.1', '1.0', '1.1', '2.0', '2.1', '3.0', '3.1']

e('\t# === distribute 1/2: PDM[0..4095] = x_j -> L2BM $lc0..511 ===')
for n in range(8):
    tag = 'i23' if n == 7 else ''
    e('\tmvp/n512%s $p%d@0 $lc0@%s' % (tag, n * 512, half[n]))
e('\tnop/2')
e('\tnop; wait i23')
e('\tnop/5')

e('\t# === distribute 2/2: PDM[4096..8191] = m_j -> L2BM $lc512..1023 ===')
for n in range(8):
    tag = 'i22' if n == 7 else ''
    e('\tmvp/n512%s $p%d@0 $lc512@%s' % (tag, 4096 + n * 512, half[n]))
e('\tnop/2')
e('\tnop; wait i22')
e('\tnop/5')

e('\t# === L2BM -> L1BM: x_j を cycle 0 ブロック [0, 64)、 m_j を cycle 1 ブロック [64, 128) へ ===')
for n in range(8):
    e('\tl2bmb@%d $lc%d $lb%d' % (n, n * 64, LB_DIST))
for n in range(8):
    e('\tl2bmb@%d $lc%d $lb%d' % (n, 512 + n * 64, LB_DIST + 64))
e('\tnop/5')
e('\t# 占有 L1BM [%d, %d)。 cycle 0 が x_j -> $ln0、 cycle 1 が m_j -> $ln2。' % (LB_DIST, LB_DIST + 256))
e('\t# cycle 2,3 は未初期化の [%d, %d) を読むので /1100 で PE 側書き込みを止める。'
  % (LB_DIST + 128, LB_DIST + 256))
e('\tl1bmd $lb%d $ln0v/1100' % LB_DIST)
e('\tnop/24')
e('\tnop/24')

e('\t# === broadcast: %d 粒子の (x_i, m_i) 64 u64 を 1 回で全 4096 PE へ ===' % NP)
e('\tmvb/n64i24 $p8192@0 $lc1024')
e('\tnop/2')
e('\tnop; wait i24')
e('\tnop/5')
e('\t# 占有 L1BM [%d, %d)。 l1bmp が 4 u64 ずつ %d 回で使い切る。' % (LB_BCAST, LB_BCAST + 64, NP // 2))
e('\tl2bmb $lc1024 $lb%d' % LB_BCAST)
e('\tnop/24')

e('\t# === acc = 0.0 ===')
e('\tlxor $ls4 $ls4 $ls4')
e('\tnop/4')

def compute(x, m, tag):
    e('\t# -- %s: F_j += (x_i - x_j) * (m_i * m_j) --' % tag)
    e('\tdvadd $lr%d -$ln0 $lr8' % x)          # diff = x_i - x_j
    e('\tnop/4')
    e('\tdvmulu $lr%d $ln2 $nowrite' % m)      # w = m_i * m_j
    e('\tdvfmad $lr%d $ln2 $mauf $ls6' % m)
    e('\tnop/4')
    e('\tdvmulu $lr8 $ls6 $nowrite')           # prod = diff * w
    e('\tdvfmad $lr8 $ls6 $mauf $lr8')
    e('\tnop/4')
    e('\tdvadd $lr8 $ls4 $ls4')                # acc += prod
    e('\tnop/4')

for k in range(NP // 2):
    p0, p1 = 2 * k, 2 * k + 1
    e('\t# === 粒子 %d, %d: l1bmp 1 命令で (x_%d, m_%d, x_%d, m_%d) を引き出す ==='
      % (p0, p1, p0, p0, p1, p1))
    e('\tl1bmp $lb%d $lr0v' % (LB_BCAST + 4 * k))
    e('\tnop/24')
    compute(0, 2, '粒子 %d' % p0)
    compute(4, 6, '粒子 %d' % p1)

e('\t# === acc ($ls4) -> LM0[0]、 collect -> PDM[0..4095] ===')
e('\tlpassa $ls4 $lm0')
e('\tnop/24')
e('\t# 占有 L1BM [%d, %d)。 結合方向にはマスクが効かないため、' % (LB_COLLECT, LB_COLLECT + 256))
e('\t# 4 cycle 分 256 u64 すべてが上書きされる。')
e('\t# 回収するのは cycle 0 の [%d, %d) のみ。' % (LB_COLLECT, LB_COLLECT + 64))
e('\tl1bmd $lm0v $lb%d' % LB_COLLECT)
e('\tnop/24')
e('\tnop/24')
for n in range(8):
    e('\tl2bm@%d $lb%d $lc%d' % (n, LB_COLLECT, 2048 + n * 64))
e('\tnop/5')
pairs = [('', 'i19', 0, 512, '0.0', '0.1'), ('i19', 'i1a', 1024, 1536, '1.0', '1.1'),
         ('i1a', 'i1c', 2048, 2560, '2.0', '2.1'), ('i1c', 'i1e', 3072, 3584, '3.0', '3.1')]
for waitbefore, tag, pa, pb, ha, hb in pairs:
    if waitbefore:
        e('\tnop; wait %s' % waitbefore)
    e('\tmvp/n512 $lc2048@%s $p%d@0' % (ha, pa))
    e('\tmvp/n512%s $lc2048@%s $p%d@0' % (tag, hb, pb))
    e('\tnop/3')
e('\tnop; wait i1e')
e('\tnop/4')
e('\t# pseudo ret')

if len(sys.argv) != 2:
    sys.exit('usage: gen_nbody.py <出力先.vsm>   (例: python3 gen_nbody.py data/nbody.vsm)')
open(sys.argv[1], 'w', encoding='utf-8').write('\n'.join(L) + '\n')
print('%s に %d 行を生成' % (sys.argv[1], len(L)))
