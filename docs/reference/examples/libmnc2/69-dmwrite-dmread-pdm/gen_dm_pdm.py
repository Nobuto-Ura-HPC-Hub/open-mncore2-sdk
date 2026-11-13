#!/usr/bin/env python3
"""data/dm_pdm.vsm と data/dm_pdm_nop.vsm の生成スクリプト (参考)。

なぜ生成しているか
------------------
PDM から 4096 PE へ 4 語ずつ配るには mvp 32 本 + l2bmb 32 本、回収には
4 ブロック分の l2bm 32 本 + mvp 32 本が要る。アドレスと wait タグが規則的に
ずれていくので、手で書くと間違える。

2 本作るのは、PDM 上の並びの変化が命令によるものか経路によるものかを切り分ける
ため。両者は distribute と collect が同じで、間に dmwrite / dmread があるかどうか
だけが違う。

    dm_pdm.vsm      配る -> dmwrite / dmread -> 集める   (添字の交換あり)
    dm_pdm_nop.vsm  配る -> 集める                        (経路だけ)

命令なし版は collect が $lm30v ではなく $lm0v (配った先) から読む。出力が入力と
一致すれば、配って集める経路そのものは並びを変えないと確定する。

生成結果はどちらも **リポジトリにコミット済み**で、ビルドはそちらを使う。
このスクリプトは設計を変えるときだけ走らせて、出力をコミットし直す。

使い方
------
    cd share/examples/libmnc2/69-dmwrite-dmread-pdm
    python3 gen_dm_pdm.py data/dm_pdm.vsm data/dm_pdm_nop.vsm
    ninja
    ninja test-emu-lib

出力先を省略できないのは、うっかり上書きするのを避けるため。

何を変えると何が変わるか
------------------------
NWORD (PE あたりの語数)
    fp64 の行列レジスタは 4 行 4 列なので 4 が上限。l1bmd の 4 サイクルにも
    ちょうど一致する。減らすと分配にマスクが要る (65-nbody-like の /1100 を参照)。

LB_DIST / LB_COLLECT
    L1BM のスロット先頭アドレス。l1bmd 1 命令は addr から 256 u64 を占有するので
    256 u64 グリッドに置く (l1bmd は v やマスクの有無に関わらず 4 サイクル動作し、addr から
    256 u64 を必ず占有する。dev manual 3.6.8.20 / 3.6.8.21)。

PDM_OUT
    出力の PDM 先頭。入力が [0, 16384) を使うのでその後ろに置く。
    ex_dm_pdm.c の OFFSET_OUT と data/config_pdm0.json の length も合わせること。

wait タグ
    distribute が 4 個 (0x11..0x14)、collect が 16 個。collect は 67-l1bmp-2u64 と
    同じ割り当てで、ブロック base が 8 刻み (0x19, 0x21, 0x29, 0x31)、ブロック内が
    base から +0, +1, +3, +5。タグは 63 までなので 4 ブロックが上限に近い。
    最後のタグ 0x36 が host の recv_wait_tag になる。2 本の vsm は同じタグを使うが、
    host 側でデバイスを開き直してから 2 本目を実行するので衝突しない。
"""
import sys

NWORD = 4                   # PE あたりの語数 (= l1bmd の 4 サイクル)
NPE = 4096                  # 全 PE
LB_DIST = 0                 # S(0) 分配入力: [0,256) を 4 サイクル全部使う
LB_COLLECT = 256            # S(1) 結合出力: [256,512)
LC_COLLECT = 2048           # collect の L2BM ワーク (分配が [0,2048) を使うのでその後ろ)
PDM_OUT = 16384             # 出力の PDM 先頭
SEND_TAG = 0x10
DIST_TAG0 = 0x11            # distribute の wait タグ先頭 (0x11..0x14)
COLLECT_BASE = [0x19, 0x21, 0x29, 0x31]   # collect の 4 ブロックの base

half = ['0.0', '0.1', '1.0', '1.1', '2.0', '2.1', '3.0', '3.1']

RECV_TAG = COLLECT_BASE[-1] + 5


def build(with_swap):
    """with_swap が真なら dmwrite / dmread を挟む版、偽なら経路だけの版を返す。"""
    L = []
    def e(s=''):
        L.append(s)

    if with_swap:
        e('# 69-dmwrite-dmread-pdm — PDM 上の並びが添字の交換でどう変わるか')
    else:
        e('# 69-dmwrite-dmread-pdm — 配って集める経路だけを通す版 (比較用)')
    e('#')
    e('# このファイルは gen_dm_pdm.py の出力。 手で編集せず、 スクリプトを直して生成し直すこと。')
    e('#')
    if with_swap:
        e('# これは行列の転置ではない。 行列演算の機能ではなく、 起きるのは 2 次元配列の')
        e('# 添字の交換だけである。 命令そのものの説明は 68-dmwrite-dmread を参照。')
    else:
        e('# dm_pdm.vsm から dmwrite / dmread を抜いたもの。 配って集める経路が並びを')
        e('# 変えないことを確かめるために使う。 出力が入力と一致すれば、 dm_pdm.vsm で')
        e('# 観測される並びの変化はすべて命令によるものだと切り分けられる。')
        e('#')
        e('# collect は $lm30v ではなく、 配った先の $lm0v から直接読む。')
    e('#')
    e('# 経路')
    e('#   PDM -> L2BM -> L1BM -> 各 PE の LM0 に %d 語  (distribute)' % NWORD)
    if with_swap:
        e('#   dmwrite / dmread                              (添字の交換)')
    e('#   LM0 -> L1BM -> L2BM -> PDM                    (collect)')
    e('#')
    e('# PDM の並び。 p を通し PE 番号 (0..%d)、 c を語番号 (0..%d) として' % (NPE - 1, NWORD - 1))
    e('#')
    e('#   入力  PDM[%d * c + p]' % NPE)
    e('#   出力  PDM[%d + %d * c + p]' % (PDM_OUT, NPE))
    e('#')
    if with_swap:
        e('# 交換は MAB 内で閉じるので、 s = p の下位 2 ビット (MAB 内の PE 番号)、')
        e('# b = p - s (MAB の先頭) と置くと、 host から見た移動は')
        e('#')
        e('#   入力 PDM[%d * s + b + c]  ->  出力 PDM[%d + %d * c + b + s]'
          % (NPE, PDM_OUT, NPE))
        e('#')
        e('# s と c が入れ替わる。 PDM のアドレスをビットで書くと bit13-12 (%d の位) と' % NPE)
        e('# bit1-0 (下位 2 ビット) の交換になる。 詳しくは ../README.md を参照。')
    else:
        e('# 期待は恒等写像。 出力 PDM[%d + i] == 入力 PDM[i] が全要素で成り立つ。' % PDM_OUT)
    e('#')
    e('# L1BM 割り当て (l1bmd 1 命令の占有は addr_b から 256 u64、 addr_b は 64 u64 アライン)')
    e('#   $lb%-4d [%4d, %4d)  分配入力。 語 c が cycle c のブロック [%d + 64c, +64) に入る'
      % (LB_DIST, LB_DIST, LB_DIST + 256, LB_DIST))
    e('#                       %d サイクルとも有効データなのでマスクは付けない' % NWORD)
    e('#   $lb%-4d [%4d, %4d)  結合出力。 cycle c の出力が [%d + 64c) に入る'
      % (LB_COLLECT, LB_COLLECT, LB_COLLECT + 256, LB_COLLECT))
    e('#')
    e('# wait タグ: send=0x%02x、 distribute=0x%02x..0x%02x、 collect の 4 ブロックが'
      % (SEND_TAG, DIST_TAG0, DIST_TAG0 + NWORD - 1))
    e('#   %s。 recv_wait_tag=0x%02x'
      % (', '.join('0x%02x' % b for b in COLLECT_BASE), RECV_TAG))

    e()
    e('\tnop; wait i%02x' % SEND_TAG)
    e('\tnop/3')

    # --- distribute: PDM -> L2BM ---
    for c in range(NWORD):
        tag = DIST_TAG0 + c
        e('\t# === distribute %d/%d: 語 %d = PDM[%d..%d] -> L2BM $lc%d ==='
          % (c + 1, NWORD, c, NPE * c, NPE * (c + 1) - 1, 512 * c))
        for n in range(8):
            t = 'i%02x' % tag if n == 7 else ''
            e('\tmvp/n512%s $p%d@0 $lc%d@%s' % (t, NPE * c + 512 * n, 512 * c, half[n]))
        e('\tnop/2')
        e('\tnop; wait i%02x' % tag)
        e('\tnop/5')

    # --- L2BM -> L1BM ---
    e('\t# === L2BM -> L1BM: 語 c を cycle c のブロック [%d + 64c) へ ===' % LB_DIST)
    for c in range(NWORD):
        for n in range(8):
            e('\tl2bmb@%d $lc%d $lb%d' % (n, 512 * c + 64 * n, LB_DIST + 64 * c))
    e('\tnop/5')
    e('\t# 占有 L1BM [%d, %d)。 %d サイクルとも有効データなのでマスク無し。'
      % (LB_DIST, LB_DIST + 256, NWORD))
    e('\t# cycle c が L1BM[%d + 64c] を読み、 LM0 の u32 アドレス 2c に書く。' % LB_DIST)
    e('\tl1bmd $lb%d $lm0v' % LB_DIST)
    e('\tnop/24')
    e('\tnop/24')

    # --- 添字の交換 (with_swap のときだけ) ---
    if with_swap:
        e('\t# === 添字の交換: MAB 内で PE 番号 s と語番号 c を入れ替える ===')
        e('\t# 入力 LM0 u32 アドレス 0,2,4,6 -> 出力 LM0 u32 アドレス 30,32,34,36')
        e('\tdmwrite $lm0v $lx0')
        e('\tnop/24')
        e('\tdmread $lx0 $lm30v')
        e('\tnop/24')
        src = '$lm30v'
        src_addr = '30,32,34,36'
    else:
        e('\t# === 命令を挟まない。 配った先をそのまま集める ===')
        src = '$lm0v'
        src_addr = '0,2,4,6'

    # --- collect: LM0 -> L1BM ---
    e('\t# === collect: LM0 u32 アドレス %s の %d 語を L1BM の %d ブロックへ ==='
      % (src_addr, NWORD, NWORD))
    e('\t# 占有 L1BM [%d, %d)。 結合方向にはマスクが効かないため %d サイクル分すべてが書かれる。'
      % (LB_COLLECT, LB_COLLECT + 256, NWORD))
    e('\tl1bmd %s $lb%d' % (src, LB_COLLECT))
    e('\tnop/24')
    e('\tnop/24')

    # --- collect: L1BM -> L2BM -> PDM (ブロックごと) ---
    for c in range(NWORD):
        lb = LB_COLLECT + 64 * c
        out = PDM_OUT + NPE * c
        base = COLLECT_BASE[c]
        tags = [base + 0, base + 1, base + 3, base + 5]
        e('\t# --- 語 %d: L1BM[%d] -> PDM[%d..%d] ---' % (c, lb, out, out + NPE - 1))
        for n in range(8):
            e('\tl2bm@%d $lb%d $lc%d' % (n, lb, LC_COLLECT + 64 * n))
        e('\tnop/5')
        for k in range(4):
            e('\tmvp/n512 $lc%d@%d.0 $p%d@0' % (LC_COLLECT, k, out + 1024 * k))
            e('\tmvp/n512i%02x $lc%d@%d.1 $p%d@0'
              % (tags[k], LC_COLLECT, k, out + 1024 * k + 512))
            e('\tnop/3')
            e('\tnop; wait i%02x' % tags[k])
        e('\tnop/4')

    e('\t# pseudo ret')
    return L


if len(sys.argv) != 3:
    sys.exit('usage: gen_dm_pdm.py <交換あり.vsm> <交換なし.vsm>\n'
             '  例: python3 gen_dm_pdm.py data/dm_pdm.vsm data/dm_pdm_nop.vsm')

for path, with_swap in ((sys.argv[1], True), (sys.argv[2], False)):
    lines = build(with_swap)
    open(path, 'w', encoding='utf-8').write('\n'.join(lines) + '\n')
    print('%s に %d 行を生成' % (path, len(lines)))
