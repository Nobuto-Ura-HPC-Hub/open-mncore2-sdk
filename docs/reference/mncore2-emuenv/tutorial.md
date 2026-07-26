# MN-Core勉強会 追加資料

tarballを解凍すると以下のファイルが見つかると思います。

- `assemble3`: アセンブラ
- `gpfn3_package_main`: エミュレータ

`./assemble3 --help`などを実行して手持ちの環境で動作することを確認したら、以下のコードを適当なファイル(sample.vsmとします)に書き込みます。

```
d get $lm0n0c0b0m0p0 1
```

ファイルが用意出来たら次のコマンドを実行します。

```sh
$ ./assemble3 sample.vsm > sample.asm
```

ここまで問題がなければsample.asmをエミュレータで実行してみます。

```sh
$ ./gpfn3_package_main -i sample.asm -d dump.txt
$ cat dump.txt
```

これで以下のような出力が得られたら実行環境は問題ありません。

```
DEBUG-LM0(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x0}}, v:0x0) #d get $lm0n0c0b0m0p0 1
```

以降の文章では断りのない限り、`d get`を含むコードブロックは実行可能なvsm(MN-Coreアセンブリ)であり、それを上と同じ手順で実行して得られるdump.txtの出力を直後のコードブロックに載せています。

vsm中の`#`以降はコメントです。

## 1PE (1/4096 MN-Core2) での実行

- この節での追加事項
  - Debug set/get
  - Arithmetic and Logic Unit (ALU)
  - LM0/LM1
  - GRF0/GRF1
  - T register
  - Mask register
  - Forwarding-path

### d set/get

`d set/get`で扱う`<raw-payload>`は長語単位(64bit)で指定する。単語(32bit)や半語(16bit)の値を`d set`する場合、残りの32bitもしくは48bit分の要素を付け加える必要がある。

n0c0b0m0p0に位置するLM0のアドレス0にdouble型の1.0(0x3FF0000000000000)を1つだけ書き込む:

```
d set $lm0n0c0b0m0p0 1 3FF0000000000000
# $lm0: LM0 ($ln => LM1, $lr => GRF0, $ls => GRF1)
#    ^: address
# n0c0b0m0p0
#  ^        : group-id [0,4)
#    ^      : l2b-id [0,2)
#      ^    : l1b-id [0,8)
#        ^  : mab-id [0,16)
#          ^: pe-id [0,4)
d getd $lm0n0c0b0m0p0 1
#    ^ d: double
```

```
DEBUG-LM0(n0c0b0m0p0,0):(1) (0x3ff0000000000000) #d getd $lm0n0c0b0m0p0 1
```

n0c0b0m0p0に位置するLM0のアドレス0にfloat型の1.0(0x3F800000)を2つ書き込む:

```
d set $lm0n0c0b0m0p0 1 3F8000003F800000
d getf $lm0n0c0b0m0p0 1
#    ^ f: float
```

```
DEBUG-LM0(n0c0b0m0p0,0):(1, 1) (0x3f800000, 0x3f800000) #d getf $lm0n0c0b0m0p0 1
```

n0c0b0m0p0に位置するLM0のアドレス0にhalf型の1.0(0x3e00)を4つ書き込む:

```
d set $lm0n0c0b0m0p0 1 3e003e003e003e00
d geth $lm0n0c0b0m0p0 1
#    ^ h: half
```

```
DEBUG-LM0(n0c0b0m0p0,0):(1, 1, 1, 1) (0x3e00, 0x3e00, 0x3e00, 0x3e00) #d geth $lm0n0c0b0m0p0 1
```

`d set/get`で複数のペイロードを指定する際、アドレス増分の最小単位が単語であるのに注意する。`$`の直後に来る`l`は長語単位でアクセスすることを指定しており、これを外すと単語指定となる。
note: 長語指定の場合、アドレスは2の倍数にアラインメントされていなければならない。例えば$lm1は不適格であるため、エミュレータは未定義動作をする。

```
LM0上でのデータの並び:  3FF00000_00000000_3FF00000_00000000
             長語指定: |      $lm0       |      $lm2       |
             単語指定: |  $m0   |  $m1   |  $m2   |  $m3   |
```

double型の1.0を2つ書き込む:

```
d set $lm0n0c0b0m0p0 2 3FF00000000000003FF0000000000000
d getd $lm0n0c0b0m0p0 2
```

```
DEBUG-LM0(n0c0b0m0p0,0):(1) (0x3ff0000000000000) #d getd $lm0n0c0b0m0p0 2
DEBUG-LM0(n0c0b0m0p0,2):(1) (0x3ff0000000000000) #d getd $lm0n0c0b0m0p0 2
                     ^: 次のアドレスが2になっている
```

整数を扱う場合は`d get`に精度指定は不要:

```
d set $lm0n0c0b0m0p0 1 0042004200000042
d get $lm0n0c0b0m0p0 1
```

```
DEBUG-LM0(n0c0b0m0p0,0):(f:2.00268e-307, i:{{0x42,0x42},{0x0,0x42}}, v:0x42004200000042) #d get $lm0n0c0b0m0p0 1
```

LM0以外のロケーションも`d set/get`の対象に出来る(`d set`については省略する)。

```
d get $ln0n0c0b0m0p0 1
d get $lr0n0c0b0m0p0 1
d get $ls0n0c0b0m0p0 1
d get $ltn0c0b0m0p0 1
d get $omr1n0c0b0m0p0 1
```

```
      vvv: この部分でロケーションを表示
DEBUG-LM1(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x0}}, v:0x0) #d get $ln0n0c0b0m0p0 1
DEBUG-GREG0(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x0}}, v:0x0) #d get $lr0n0c0b0m0p0 1
DEBUG-GREG1(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x0}}, v:0x0) #d get $ls0n0c0b0m0p0 1
DEBUG-TREG(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x0}}, v:0x0) #d get $ltn0c0b0m0p0 1
      vvv: マスクレジスタは4cycle(=1step)分がまとめて出力される。
DEBUG-OMR(n0c0b0m0p0,1):Mask{0} #d get $omr1n0c0b0m0p0 1
DEBUG-OMR(n0c0b0m0p0,1):Mask{0} #d get $omr1n0c0b0m0p0 1
DEBUG-OMR(n0c0b0m0p0,1):Mask{0} #d get $omr1n0c0b0m0p0 1
DEBUG-OMR(n0c0b0m0p0,1):Mask{0} #d get $omr1n0c0b0m0p0 1
```

### 各オペランドの特徴

LM0/LM1 (2048長語): それぞれvsmでは$lm/$lnで表す。生存期間の長い変数の格納場所として設計されており、容量の大きさの引き換えに、一部の例外を除いてRead及びWriteは同一ステップに片方のみしか行えない。

```
d set $lm0n0c0b0m0p0 1 0000000000000042

linc $lm0 $ln0

d get $ln0n0c0b0m0p0 1

# 書き込んだものが読み込めるまで2stepかかる。(optional: nopの数を減らすと...?)
# これは実機での制約であり、エミュレータ内部の状態は即座に更新されるため、`d get`によって例外的に更新後の値を読める。
# また、LMに限り書き込み後はアドレスによらず2stepは読み込めない。
nop
nop
linc $ln0 $lm2

d get $lm2n0c0b0m0p0 1

nop/2
# ladd $ln0 $lm2 $ln2 # <= error
ladd $ln0 $lm2 $ln0   # <= valid (命令ビット衝突が起きない例外)

d get $ln0n0c0b0m0p0 1
```

```
DEBUG-LM1(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x43}}, v:0x43) #d get $ln0n0c0b0m0p0 1
DEBUG-LM0(n0c0b0m0p0,2):(f:0, i:{{0x0,0x0},{0x0,0x44}}, v:0x44) #d get $lm2n0c0b0m0p0 1
DEBUG-LM1(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x87}}, v:0x87) #d get $ln0n0c0b0m0p0 1
```

GRF0/GRF1 (256長語): それぞれvsmでは$lr/$lsで表す。こちらは生存期間の短い変数の格納場所として設計されており、Read及びWriteは同一ステップに同時に行える。

```
d set $lr0n0c0b0m0p0 1 0000000000000002
d set $ls0n0c0b0m0p0 1 0000000000000005

linc $lr0 $lr0
linc $ls0 $ls2

d get $lr0n0c0b0m0p0 1
d get $ls2n0c0b0m0p0 1

nop
# $ls2への書き込みから1stepしか経っていないが、別アドレスからの読み込みは可能。(LMでは不可)
ladd $lr0 $ls0 $lr2

d get $lr2n0c0b0m0p0 1

nop/2
ladd $lr2 $ls2 $lr4 $ls4

d get $lr4n0c0b0m0p0 1
d get $lr4n0c0b0m0p0 1
```

```
DEBUG-GREG0(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x3}}, v:0x3) #d get $lr0n0c0b0m0p0 1
DEBUG-GREG1(n0c0b0m0p0,2):(f:0, i:{{0x0,0x0},{0x0,0x6}}, v:0x6) #d get $ls2n0c0b0m0p0 1
DEBUG-GREG0(n0c0b0m0p0,2):(f:0, i:{{0x0,0x0},{0x0,0x8}}, v:0x8) #d get $lr2n0c0b0m0p0 1
DEBUG-GREG0(n0c0b0m0p0,4):(f:0, i:{{0x0,0x0},{0x0,0xE}}, v:0xE) #d get $lr4n0c0b0m0p0 1
DEBUG-GREG0(n0c0b0m0p0,4):(f:0, i:{{0x0,0x0},{0x0,0xE}}, v:0xE) #d get $lr4n0c0b0m0p0 1
```

T (通常4長語、最大4\*二長語): vsmでは$t($lt,$llt)で表す。アドレスの指定が出来ないことと容量以外はGRFと同様に扱える他、Tレジスタ間接参照といった特殊な用途がある。語長の指定は出来ないが、これはオペコードの精度変換オプションの値から自動的に決定される。

```
d set $lm0n0c0b0m0p0 4 l0000000000000001l0000000000000002l0000000000000003l0000000000000004
d set $lm8n0c0b0m0p0 4 l0000000000000005l0000000000000006l0000000000000007l0000000000000008

lpassa $lm0v $lt

d get $tn0c0b0m0p0 4

lpassa $lm8v $t

d get $ltn0c0b0m0p0 4
```

```
DEBUG-TREG(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x1}}, v:0x1) #d get $ltn0c0b0m0p0 4
DEBUG-TREG(n0c0b0m0p0,1):(f:0, i:{{0x0,0x0},{0x0,0x2}}, v:0x2) #d get $ltn0c0b0m0p0 4
DEBUG-TREG(n0c0b0m0p0,2):(f:0, i:{{0x0,0x0},{0x0,0x3}}, v:0x3) #d get $ltn0c0b0m0p0 4
DEBUG-TREG(n0c0b0m0p0,3):(f:0, i:{{0x0,0x0},{0x0,0x4}}, v:0x4) #d get $ltn0c0b0m0p0 4
DEBUG-TREG(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x5}}, v:0x5) #d get $ltn0c0b0m0p0 4
DEBUG-TREG(n0c0b0m0p0,1):(f:0, i:{{0x0,0x0},{0x0,0x6}}, v:0x6) #d get $ltn0c0b0m0p0 4
DEBUG-TREG(n0c0b0m0p0,2):(f:0, i:{{0x0,0x0},{0x0,0x7}}, v:0x7) #d get $ltn0c0b0m0p0 4
DEBUG-TREG(n0c0b0m0p0,3):(f:0, i:{{0x0,0x0},{0x0,0x8}}, v:0x8) #d get $ltn0c0b0m0p0 4
```

M (4bit): vsmでは$omrで表す。各サイクルにおいて書き込む(1)か書き込まない(0)かの情報を保持する。

```
d set $lm0n0c0b0m0p0 4 l0000000000000000l0000000000000001l0000000000000042l0000000000000000
d set $lm8n0c0b0m0p0 4 l3FF0000000000000l3FF0000000000000l3FF0000000000000l3FF0000000000000

lpassa $lm0v $omr1
maskn 1
lpassa $lm8v $ln0v
mask 0

d get $lm0n0c0b0m0p0 4
d get $omr1n0c0b0m0p0 1
d getd $ln0n0c0b0m0p0 4
```

```
DEBUG-LM0(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x0}}, v:0x0) #d get $lm0n0c0b0m0p0 4
DEBUG-LM0(n0c0b0m0p0,2):(f:0, i:{{0x0,0x0},{0x0,0x1}}, v:0x1) #d get $lm0n0c0b0m0p0 4
DEBUG-LM0(n0c0b0m0p0,4):(f:0, i:{{0x0,0x0},{0x0,0x42}}, v:0x42) #d get $lm0n0c0b0m0p0 4
DEBUG-LM0(n0c0b0m0p0,6):(f:0, i:{{0x0,0x0},{0x0,0x0}}, v:0x0) #d get $lm0n0c0b0m0p0 4
                             vv: 実装の都合で15(=0b1111)が表示されている
DEBUG-OMR(n0c0b0m0p0,1):Mask{15} #d get $omr1n0c0b0m0p0 1  1cycle目: 書き込む(1)
DEBUG-OMR(n0c0b0m0p0,1):Mask{0} #d get $omr1n0c0b0m0p0 1   2cycle目: 書き込まない(0)
DEBUG-OMR(n0c0b0m0p0,1):Mask{0} #d get $omr1n0c0b0m0p0 1   3cycle目: 書き込まない(0)
DEBUG-OMR(n0c0b0m0p0,1):Mask{15} #d get $omr1n0c0b0m0p0 1  4cycle目: 書き込む(1)
DEBUG-LM1(n0c0b0m0p0,0):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0p0 4
DEBUG-LM1(n0c0b0m0p0,2):(0) (0x0000000000000000) #d getd $ln0n0c0b0m0p0 4
DEBUG-LM1(n0c0b0m0p0,4):(0) (0x0000000000000000) #d getd $ln0n0c0b0m0p0 4
DEBUG-LM1(n0c0b0m0p0,6):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0p0 4
```

MR (4x4(double),8x4(float),8x8(pseudo-float),16x16(half)の2面): $x,$yに対応する。詳細はMABの項で。

forwarding-path: vsmでは出力元ユニットに応じて$mauf,$aluf,$lbf,$mreadfで表す。直前の演算ユニット等の出力をここから読み出すことが出来る。例外を除いて毎ステップ更新されるため、内容を使い回すことは出来ない。演算結果を使いまわしたい場合、単にそれをメモリに書き込めば良く、その場合でも forwarding-path の内容は更新される。

```
d set $lr0n0c0b0m0p0 1 0000000000000042

linc $lr0 $lm0
linc $aluf $ln0

d get $lm0n0c0b0m0p0 1
d get $ln0n0c0b0m0p0 1
```

```
DEBUG-LM0(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x43}}, v:0x43) #d get $lm0n0c0b0m0p0 1
DEBUG-LM1(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x44}}, v:0x44) #d get $ln0n0c0b0m0p0 1
```

固定値入力: vsmでは$l2bid,$l1bid,$mabid,$peid,$subpeid,$msb1で表す。MN-Core2上の全PEは同一のアセンブリを受け取る(SIMD)ため、PE毎に異なる処理をしたい場合は、これらのオペランドを利用してマスク処理などをする。

#### Tレジスタ間接参照

numpy.ndarrayに対するindexingのように、実行時に定まるアドレスを元に間接参照したい場合がある。その際はTレジスタ間接参照の機能を使うと良い。

```
d set $lm0n0c0b0m0p0 4 l0000000000000000l3FF0000000000000l4000000000000000l4008000000000000
# LM0上の4要素に対して[2,1,3,0]の順番でアクセスしたい場合、Tレジスタに以下のように書き込む。
#                                     v: 2*2           v: 2*1           v: 2*3           v: 2*0
d set $ltn0c0b0m0p0 4 l0000000000000004l0000000000000002l0000000000000006l0000000000000000

lpassa $lmt $ln0v

d getd $lm0n0c0b0m0p0 4
d getd $ln0n0c0b0m0p0 4
```

```
DEBUG-LM0(n0c0b0m0p0,0):(0) (0x0000000000000000) #d getd $lm0n0c0b0m0p0 4
DEBUG-LM0(n0c0b0m0p0,2):(1) (0x3ff0000000000000) #d getd $lm0n0c0b0m0p0 4
DEBUG-LM0(n0c0b0m0p0,4):(2) (0x4000000000000000) #d getd $lm0n0c0b0m0p0 4
DEBUG-LM0(n0c0b0m0p0,6):(3) (0x4008000000000000) #d getd $lm0n0c0b0m0p0 4
DEBUG-LM1(n0c0b0m0p0,0):(2) (0x4000000000000000) #d getd $ln0n0c0b0m0p0 4
DEBUG-LM1(n0c0b0m0p0,2):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0p0 4
DEBUG-LM1(n0c0b0m0p0,4):(3) (0x4008000000000000) #d getd $ln0n0c0b0m0p0 4
DEBUG-LM1(n0c0b0m0p0,6):(0) (0x0000000000000000) #d getd $ln0n0c0b0m0p0 4
```

## 1MAB=4PE (4/4096 MN-Core2)

- この節での追加事項
  - Matrix Arithmetic Unit (MAU)
  - Matrix register

### ベクトル演算

Matrix Arithmetic Unit (MAU) という名前だが、MN-Core2におけるベクトル演算はMAUが行う。多くのケースでPE毎に閉じた計算を行うが、倍精度の積は4つのPEを意識した操作が入る。

#### dv(mul|fma)

```
d set $lm0n0c0b0m0p0 1 3FF0000000000000
d set $lm0n0c0b0m0p1 1 4000000000000000
d set $lm0n0c0b0m0p2 1 4008000000000000
d set $lm0n0c0b0m0p3 1 4010000000000000
d set $ln0n0c0b0m0p0 1 3FF0000000000000
d set $ln0n0c0b0m0p1 1 4008000000000000
d set $ln0n0c0b0m0p2 1 4014000000000000
d set $ln0n0c0b0m0p3 1 401C000000000000

# p[0:3]は省略可能。その場合自動的に展開される。
d getd $lm0n0c0b0m0 1
d getd $ln0n0c0b0m0 1

dvmulu $lm0 $ln0 $nowrite
dvfmad $lm0 $ln0 $mauf $ls2

# 上の2行を冗長に書くと以下のようになる。(optional: 下を実行してdump結果を見てみる)

# dvmulu $lm0 $ln0 $lr0
# dvmuld $lm0 $ln0 $ls0
# d getd $lr0n0c0b0m0 1
# d getd $ls0n0c0b0m0 1
# nop/2
# dvadd $lr0 $ls0 $ls2

d getd $ls2n0c0b0m0 1
```

```
DEBUG-LM0(n0c0b0m0p0,0):(1) (0x3ff0000000000000) #d getd $lm0n0c0b0m0 1
DEBUG-LM0(n0c0b0m0p1,0):(2) (0x4000000000000000) #d getd $lm0n0c0b0m0 1
DEBUG-LM0(n0c0b0m0p2,0):(3) (0x4008000000000000) #d getd $lm0n0c0b0m0 1
DEBUG-LM0(n0c0b0m0p3,0):(4) (0x4010000000000000) #d getd $lm0n0c0b0m0 1
DEBUG-LM1(n0c0b0m0p0,0):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0 1
DEBUG-LM1(n0c0b0m0p1,0):(3) (0x4008000000000000) #d getd $ln0n0c0b0m0 1
DEBUG-LM1(n0c0b0m0p2,0):(5) (0x4014000000000000) #d getd $ln0n0c0b0m0 1
DEBUG-LM1(n0c0b0m0p3,0):(7) (0x401c000000000000) #d getd $ln0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p0,2):(1) (0x3ff0000000000000) #d getd $ls2n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p1,2):(6) (0x4018000000000000) #d getd $ls2n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p2,2):(15) (0x402e000000000000) #d getd $ls2n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p3,2):(28) (0x403c000000000000) #d getd $ls2n0c0b0m0 1
```

#### fv(mul|fma)

```
d set $lm0n0c0b0m0p0 1 3F80000040A00000
d set $lm0n0c0b0m0p1 1 4000000040C00000
d set $lm0n0c0b0m0p2 1 4040000040E00000
d set $lm0n0c0b0m0p3 1 4080000041000000
d set $ln0n0c0b0m0p0 1 3F80000041100000
d set $ln0n0c0b0m0p1 1 4040000041300000
d set $ln0n0c0b0m0p2 1 40A0000041500000
d set $ln0n0c0b0m0p3 1 40E0000041700000
d set $lr0n0c0b0m0p0 1 3F8000003F800000
d set $lr0n0c0b0m0p1 1 3F8000003F800000
d set $lr0n0c0b0m0p2 1 3F8000003F800000
d set $lr0n0c0b0m0p3 1 3F8000003F800000

d getf $lm0n0c0b0m0 1
d getf $ln0n0c0b0m0 1

fvmul $lm0 $ln0 $ls0

d getf $ls0n0c0b0m0 1

fvfma $lm0 $ln0 $lr0 $ls0

d getf $ls0n0c0b0m0 1
```

```
DEBUG-LM0(n0c0b0m0p0,0):(1, 5) (0x3f800000, 0x40a00000) #d getf $lm0n0c0b0m0 1
DEBUG-LM0(n0c0b0m0p1,0):(2, 6) (0x40000000, 0x40c00000) #d getf $lm0n0c0b0m0 1
DEBUG-LM0(n0c0b0m0p2,0):(3, 7) (0x40400000, 0x40e00000) #d getf $lm0n0c0b0m0 1
DEBUG-LM0(n0c0b0m0p3,0):(4, 8) (0x40800000, 0x41000000) #d getf $lm0n0c0b0m0 1
DEBUG-LM1(n0c0b0m0p0,0):(1, 9) (0x3f800000, 0x41100000) #d getf $ln0n0c0b0m0 1
DEBUG-LM1(n0c0b0m0p1,0):(3, 11) (0x40400000, 0x41300000) #d getf $ln0n0c0b0m0 1
DEBUG-LM1(n0c0b0m0p2,0):(5, 13) (0x40a00000, 0x41500000) #d getf $ln0n0c0b0m0 1
DEBUG-LM1(n0c0b0m0p3,0):(7, 15) (0x40e00000, 0x41700000) #d getf $ln0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p0,0):(1, 45) (0x3f800000, 0x42340000) #d getf $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p1,0):(6, 66) (0x40c00000, 0x42840000) #d getf $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p2,0):(15, 91) (0x41700000, 0x42b60000) #d getf $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p3,0):(28, 120) (0x41e00000, 0x42f00000) #d getf $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p0,0):(2, 46) (0x40000000, 0x42380000) #d getf $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p1,0):(7, 67) (0x40e00000, 0x42860000) #d getf $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p2,0):(16, 92) (0x41800000, 0x42b80000) #d getf $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p3,0):(29, 121) (0x41e80000, 0x42f20000) #d getf $ls0n0c0b0m0 1
```

#### hv(mul|fma)

```
d set $lm0n0c0b0m0p0 1 3E00428044404540
d set $lm0n0c0b0m0p1 1 4000430044804580
d set $lm0n0c0b0m0p2 1 4100438044C045C0
d set $lm0n0c0b0m0p3 1 4200440045004600
d set $ln0n0c0b0m0p0 1 3E00444046204720
d set $ln0n0c0b0m0p1 1 410044C046604760
d set $ln0n0c0b0m0p2 1 4280454046A047A0
d set $ln0n0c0b0m0p3 1 438045C046E047E0
d set $lr0n0c0b0m0p0 1 3E003E003E003E00
d set $lr0n0c0b0m0p1 1 3E003E003E003E00
d set $lr0n0c0b0m0p2 1 3E003E003E003E00
d set $lr0n0c0b0m0p3 1 3E003E003E003E00

# (optional: 上を参考にexpectedと一致するようなvsmをここに足してみる)
```

想定出力:

```
DEBUG-GREG1(n0c0b0m0p0,0):(1, 45, 153, 325) (0x3e00, 0x48d0, 0x4c64, 0x4e8a) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p1,0):(6, 66, 190, 378) (0x4300, 0x4a10, 0x4cf8, 0x4ef4) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p2,0):(15, 91, 231, 435) (0x45c0, 0x4ad8, 0x4d9c, 0x4f66) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p3,0):(28, 120, 276, 496) (0x4780, 0x4bc0, 0x4e28, 0x4fe0) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p0,0):(2, 46, 154, 326) (0x4000, 0x48e0, 0x4c68, 0x4e8c) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p1,0):(7, 67, 191, 379) (0x4380, 0x4a18, 0x4cfc, 0x4ef6) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p2,0):(16, 92, 232, 436) (0x4600, 0x4ae0, 0x4da0, 0x4f68) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p3,0):(29, 121, 277, 497) (0x47a0, 0x4bc8, 0x4e2a, 0x4fe2) #d geth $ls0n0c0b0m0 1
```

想定ではない出力(hint: 基本的に半精度の加算は精度拡張してから行う):

```
DEBUG-GREG1(n0c0b0m0p0,0):(1.75, 0, 4.40625, 0) (0x3f80, 0x0000, 0x4234, 0x0000) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p1,0):(2.75, 0, 5.03125, 0) (0x40c0, 0x0000, 0x4284, 0x0000) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p2,0):(3.4375, 0, 5.42188, 0) (0x4170, 0x0000, 0x42b6, 0x0000) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p3,0):(3.875, 0, 5.875, 0) (0x41e0, 0x0000, 0x42f0, 0x0000) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p0,0):(1.78125, 6.98492e-09, 4.40625, -0) (0x3f90, 0x07c0, 0x4234, 0x803e) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p1,0):(2.76562, 0, 5.03125, 2.12109) (0x40c4, 0x01f0, 0x4284, 0x401f) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p2,0):(3.44531, 0, 5.42188, 2.12109) (0x4172, 0x00f8, 0x42b6, 0x401f) #d geth $ls0n0c0b0m0 1
DEBUG-GREG1(n0c0b0m0p3,0):(3.87891, 0, 5.875, 2.12109) (0x41e1, 0x007c, 0x42f0, 0x401f) #d geth $ls0n0c0b0m0 1
```

(d|f|h)vpassaは一見IALUのpassaと同じ動作をするように見えるが、その過程で正規化を挟むため、整数や特殊な負数に対してvpassaをしてはいけない:

```
d set $lm0n0c0b0m0p0 1 0000004280000000

fvpassa $lm0 $ln0

d getf $lm0n0c0b0m0p0 1
d get $lm0n0c0b0m0p0 1
d getf $ln0n0c0b0m0p0 1
d get $ln0n0c0b0m0p0 1
```

```
DEBUG-LM0(n0c0b0m0p0,0):(0, -0) (0x00000042, 0x80000000) #d getf $lm0n0c0b0m0p0 1
DEBUG-LM0(n0c0b0m0p0,0):(f:0, i:{{0x0,0x42},{0x8000,0x0}}, v:0x4280000000) #d get $lm0n0c0b0m0p0 1
# 以下の出力では42や-0が潰れてしまっていることがわかる
DEBUG-LM1(n0c0b0m0p0,0):(0, 0) (0x00000000, 0x00000000) #d getf $ln0n0c0b0m0p0 1
DEBUG-LM1(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x0}}, v:0x0) #d get $ln0n0c0b0m0p0 1
```

vpassaの有効な使い方に精度変換がある:

```
d set $lm0n0c0b0m0p0 4 l3FF0000000000000l4000000000000000l4008000000000000l4010000000000000

dvpassar $lm0v $n0v

d getd $lm0n0c0b0m0p0 4
d getf $ln0n0c0b0m0p0 2

d set $lm0n0c0b0m0p0 1 3E00428044404540

hvpassa $lm0 $lln0

d geth $lm0n0c0b0m0p0 1
d getf $ln0n0c0b0m0p0 2
```

```
DEBUG-LM0(n0c0b0m0p0,0):(1) (0x3ff0000000000000) #d getd $lm0n0c0b0m0p0 4
DEBUG-LM0(n0c0b0m0p0,2):(2) (0x4000000000000000) #d getd $lm0n0c0b0m0p0 4
DEBUG-LM0(n0c0b0m0p0,4):(3) (0x4008000000000000) #d getd $lm0n0c0b0m0p0 4
DEBUG-LM0(n0c0b0m0p0,6):(4) (0x4010000000000000) #d getd $lm0n0c0b0m0p0 4
DEBUG-LM1(n0c0b0m0p0,0):(1, 2) (0x3f800000, 0x40000000) #d getf $ln0n0c0b0m0p0 2
DEBUG-LM1(n0c0b0m0p0,2):(3, 4) (0x40400000, 0x40800000) #d getf $ln0n0c0b0m0p0 2
DEBUG-LM0(n0c0b0m0p0,0):(1, 5, 9, 13) (0x3e00, 0x4280, 0x4440, 0x4540) #d geth $lm0n0c0b0m0p0 1
DEBUG-LM1(n0c0b0m0p0,0):(1, 5) (0x3f800000, 0x40a00000) #d getf $ln0n0c0b0m0p0 2
DEBUG-LM1(n0c0b0m0p0,2):(9, 13) (0x41100000, 0x41500000) #d getf $ln0n0c0b0m0p0 2
```

### 行列演算

行列演算は大まかに各行列のブロックフロート化と行列レジスタへの書き込み、m(mul|fma)命令の呼び出しの手順が必要になる。ここでは倍精度における行列積を取り上げて説明する。

#### 4x4 matmul in double (AxB) を考える

```
# A
d set $lm0n0c0b0m0p0 4 80000000000000000000000000000000BFF0000000000000BFF0000000000000
d set $lm0n0c0b0m0p1 4 000000000000000040100000000000003FF0000000000000BFF0000000000000
d set $lm0n0c0b0m0p2 4 4000000000000000BFF0000000000000BFF00000000000003FF0000000000000
d set $lm0n0c0b0m0p3 4 BFF0000000000000C010000000000000BFF0000000000000BFF0000000000000
# B
d set $ln0n0c0b0m0p0 4 80000000000000003FF00000000000003FF0000000000000BFF0000000000000
d set $ln0n0c0b0m0p1 4 3FF0000000000000BFF00000000000003FF00000000000003FF0000000000000
d set $ln0n0c0b0m0p2 4 BFF000000000000040080000000000003FF00000000000003FF0000000000000
d set $ln0n0c0b0m0p3 4 C0100000000000003FF0000000000000C008000000000000BFF0000000000000

d getd $lm0n0c0b0m0 4
d getd $ln0n0c0b0m0 4

# ブロックフロート化とは内積回路への2入力それぞれに対して指数部の値を揃えることである。倍精度の場合はdbfn命令を使ってこれを行う。
dbfn $lm0v $lr0v # BF-ed A
dbfn $ln0v $ls0v # BF-ed B

# 次にBF-ed Aを行列レジスタへ書き込む
dmwrite $lr0v $lx0

# dmmulu+dmfmadを発行する
dmmulu $lx $ls0v $nowrite
dmfmad $lx $ls0v $mauf $lm8v

d getd $lm8n0c0b0m0 4
```

```
DEBUG-LM0(n0c0b0m0p0,0):(-0) (0x8000000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p0,2):(0) (0x0000000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p0,4):(-1) (0xbff0000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p0,6):(-1) (0xbff0000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p1,0):(0) (0x0000000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p1,2):(4) (0x4010000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p1,4):(1) (0x3ff0000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p1,6):(-1) (0xbff0000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p2,0):(2) (0x4000000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p2,2):(-1) (0xbff0000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p2,4):(-1) (0xbff0000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p2,6):(1) (0x3ff0000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p3,0):(-1) (0xbff0000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p3,2):(-4) (0xc010000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p3,4):(-1) (0xbff0000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p3,6):(-1) (0xbff0000000000000) #d getd $lm0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p0,0):(-0) (0x8000000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p0,2):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p0,4):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p0,6):(-1) (0xbff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p1,0):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p1,2):(-1) (0xbff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p1,4):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p1,6):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p2,0):(-1) (0xbff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p2,2):(3) (0x4008000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p2,4):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p2,6):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p3,0):(-4) (0xc010000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p3,2):(1) (0x3ff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p3,4):(-3) (0xc008000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM1(n0c0b0m0p3,6):(-1) (0xbff0000000000000) #d getd $ln0n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p0,8):(2) (0x4000000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p0,10):(5) (0x4014000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p0,12):(5) (0x4014000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p0,14):(3) (0x4008000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p1,8):(21) (0x4035000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p1,10):(-11) (0xc026000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p1,12):(15) (0x402e000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p1,14):(7) (0x401c000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p2,8):(6) (0x4018000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p2,10):(-6) (0xc018000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p2,12):(2) (0x4000000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p2,14):(2) (0x4000000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p3,8):(2) (0x4000000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p3,10):(2) (0x4000000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p3,12):(2) (0x4000000000000000) #d getd $lm8n0c0b0m0 4
DEBUG-LM0(n0c0b0m0p3,14):(2) (0x4000000000000000) #d getd $lm8n0c0b0m0 4
```

上のDump結果を簡単に図示するとこのようになる。

```
                   A (on $lx)                     B                                  Result
         PE0   PE1   PE2   PE3          PE0   PE1   PE2   PE3                PE0   PE1   PE2   PE3
       +-----+-----+-----+-----+      +-----+-----+-----+-----+            +-----+-----+-----+-----+
addr 0 | -0  |  0  |  2  | -1  | mmul | -0  |  1  | -1  | -4  |    addr  8 |  2  |  21 |  6  |  2  |
addr 2 |  0  |  4  | -1  | -4  |  +   |  1  | -1  |  3  |  1  | =  addr 10 |  5  | -11 | -6  |  2  |
addr 4 | -1  |  1  | -1  | -1  | mfma |  1  |  1  |  1  | -3  |    addr 12 |  5  |  15 |  2  |  2  |
addr 6 | -1  | -1  |  1  | -1  |      | -1  |  1  |  1  | -1  |    addr 14 |  3  |  7  |  2  |  2  |
       +-----+-----+-----+-----+      +-----+-----+-----+-----+            +-----+-----+-----+-----+
```

図を確認しながらここまでの計算を追うと、普段考える行列積と異なるものを計算したような気分になるはず。その理由はアドレス伸長方向を行、PE方向を列と見たとき、ここで計算したものは (A@B^T)^T であるため。[ソフトウェア開発者マニュアル](https://projects.preferred.jp/mn-core/assets/mncore2_dev_manual_ja.pdf)により詳細な説明があるが、直感的にはアドレス方向に一行ずつ取り出したベクトルと行列の積を取り、それをサイクル毎に繰り返したものになっている。例えば、

```
Result[addr8,PE0]=A[addr0,PE0]*B[addr0,PE0]+A[addr0,PE1]*B[addr0,PE1]+A[addr0,PE2]*B[addr0,PE2]+A[addr0,PE3]*B[addr0,PE3]
Result[addr8,PE1]=A[addr2,PE0]*B[addr0,PE0]+A[addr2,PE1]*B[addr0,PE1]+A[addr2,PE2]*B[addr0,PE2]+A[addr2,PE3]*B[addr0,PE3]
Result[addr8,PE2]=A[addr4,PE0]*B[addr0,PE0]+A[addr4,PE1]*B[addr0,PE1]+A[addr4,PE2]*B[addr0,PE2]+A[addr4,PE3]*B[addr0,PE3]
Result[addr8,PE3]=A[addr6,PE0]*B[addr0,PE0]+A[addr6,PE1]*B[addr0,PE1]+A[addr6,PE2]*B[addr0,PE2]+A[addr6,PE3]*B[addr0,PE3]
               ^        ^                         ^                         ^                         ^
...
```

という具合に計算が進んでいく。この計算内容をNumPyを使って再現すると以下のようになる。

```py
import numpy as np
a = np.array(
    [[0, 0, 2, -1], [0, 4, -1, -4], [-1, 1, -1, -1], [-1, -1, 1, -1]], dtype=np.float32
)
b = np.array(
    [[0, 1, -1, -4], [1, -1, 3, 1], [1, 1, 1, -3], [-1, 1, 1, -1]], dtype=np.float32
)
print((a @ b.T).T)
```

```
[[  2.  21.   6.   2.]
 [  5. -11.  -6.   2.]
 [  5.  15.   2.   2.]
 [  3.   7.   2.   2.]]
```

行列積の片方が転置されている場合、全く異なる計算内容になっていると考えるかもしれない。しかし、MN-Coreはその特殊なメモリ構造から、ホスト上のデータを並べ替えてからデバイスへ送る必要があり、その際に転置も含めてやってしまうことで結果的に求めたいものを得ることができる。また、もちろんMN-Core上で行列の転置を行うこともできる。

challenge: 16x16 matmul (A@B) in half を書いてみる。直感的なA@Bを実現するために、MN-Core2上でB^TxAを計算してみよう。行列の転置についての説明は[ソフトウェア開発者マニュアル](https://projects.preferred.jp/mn-core/assets/mncore2_dev_manual_ja.pdf)のmread付近でなされている。

```
# A
d set $lm0n0c0b0m0p0 16 8000BE00BE00BE003E003E003E003E003E003E00BE003E003E00BE003E00BE00BE003E00BE003E00BE00BE003E003E003E00BE003E003E003E00BE003E00BE003E00BE00BE003E003E00BE00BE00BE00BE003E00BE00BE00BE00BE00BE00BE00BE003E00BE003E003E00BE003E00BE00BE00BE003E003E003E00BE00BE003E00
d set $lm0n0c0b0m0p1 16 3E003E003E00BE003E00BE003E003E003E00BE00BE003E003E003E00BE0041003E003E003E003E003E00BE003E003E003E003E003E00BE003E004280BE003E003E00BE00BE00BE00BE003E00BE003E00BE00BE003E003E00BE00BE00BE003E00BE003E003E00BE00BE00BE00BE00BE003E00BE00C1003E00BE003E00BE00BE00
d set $lm0n0c0b0m0p2 16 BE00BE00BE003E003E003E003E00BE003E00BE003E003E00BE00BE00BE00BE003E003E00BE00BE00BE003E003E003E003E003E003E003E00BE003E003E003E003E00BE00BE003E00BE00BE004000BE003E003E003E003E00BE003E003E00BE00BE00BE00BE004000BE00BE003E003E00BE003E003E00BE003E00BE003E00BE00
d set $lm0n0c0b0m0p3 16 BE00BE00BE00BE00BE003E003E00BE00BE00BE003E00BE003E003E00BE00BE00BE003E003E003E003E00BE003E003E00BE003E003E00BE003E00BE00BE00BE00BE00BE00BE003E00BE00BE003E003E003E00BE00BE003E00BE00BE00BE00BE003E00BE00BE003E00BE00BE00BE003E003E003E003E003E00BE00BE00BE00BE00
# B
d set $ln0n0c0b0m0p0 16 80003E003E00BE003E003E003E003E00BE00BE003E003E00BE00BE003E003E004200BE003E003E00BE00BE00BE003E00BE003E00BE003E003E003E003E003E003E003E00BE003E003E003E003E00BE0040003E00BE003E00BE003E003E00BE003E003E00BE00BE00BE00BE00BE003E003E003E00BE00BE003E00BE003E00BE00
d set $ln0n0c0b0m0p1 16 3E00BE003E003E00BE00BE00BE003E0042003E003E003E003E00BE00BE00BE00BE00BE003E00BE00BE003E003E00BE00BE003E003E00BE003E00BE00BE00BE003E003E003E00BE00BE003E003E003E00BE003E00BE003E00BE00BE00BE003E003E003E003E003E00BE00BE003E003E00BE003E003E003E00BE003E003E003E00
d set $ln0n0c0b0m0p2 16 3E003E003E00BE00BE003E00BE00BE003E003E003E00BE00BE003E003E003E003E003E00BE00BE003E003E00BE00BE003E003E00BE003E003E003E00BE003E00BE003E004100BE00BE003E00BE003E003E003E003E00BE003E00BE00BE003E00BE003E003E00BE003E00BE003E003E003E003E003E00BE003E004200BE00BE00
d set $ln0n0c0b0m0p3 16 BE00BE003E00BE003E00BE00BE003E00BE003E00BE00BE003E00BE00BE00BE00BE003E00BE00BE003E00BE00BE003E003E003E00BE00BE00BE00BE003E00BE003E003E00BE00BE003E00BE003E003E003E00BE00BE000000BE00BE003E00BE003E00BE00BE003E00BE00BE00BE003E003E00BE00BE003E003E003E003E00BE00

# (write vsm here!)
d geth $ln32n0c0b0m0 16
```

想定出力:

```
DEBUG-LM1(n0c0b0m0p0,32):(-5, -3, -1, -1) (0xc280, 0xc100, 0xbe00, 0xbe00) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,34):(7, 4, 2, 8) (0x4380, 0x4200, 0x4000, 0x4400) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,36):(9, 6, 4, 2) (0x4440, 0x4300, 0x4200, 0x4000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,38):(1, -4, 2, 4) (0x3e00, 0xc200, 0x4000, 0x4200) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,40):(5, -2, 0, 6) (0x4280, 0xc000, 0x0000, 0x4300) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,42):(7, 2, 4, -2) (0x4380, 0x4000, 0x4200, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,44):(-1, 0, -2, 4) (0xbe00, 0x0000, 0xc000, 0x4200) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,46):(1, -2, 0, 2) (0x3e00, 0xc000, 0x0000, 0x4000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,48):(1, -4, 6, -4) (0x3e00, 0xc200, 0x4300, 0xc200) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,50):(3, 1, -3, -3) (0x4100, 0x3e00, 0xc100, 0xc100) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,52):(5, 10, 0, -2) (0x4280, 0x4480, 0x0000, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,54):(1, 2, 0, -2) (0x3e00, 0x4000, 0x0000, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,56):(-10, -1, 1, -3) (0xc480, 0xbe00, 0x3e00, 0xc100) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,58):(-5, -2, 4, -6) (0xc280, 0xc000, 0x4200, 0xc300) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,60):(11, -6, 4, -2) (0x44c0, 0xc300, 0x4200, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p0,62):(-5, -2, -4, 2) (0xc280, 0xc000, 0xc200, 0x4000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,32):(-6, -3, -1, -7) (0xc300, 0xc100, 0xbe00, 0xc380) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,34):(3, -2, 2, 0) (0x4100, 0xc000, 0x4000, 0x0000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,36):(-1, -8, -8, -2) (0xbe00, 0xc400, 0xc400, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,38):(11, -6, 2, -4) (0x44c0, 0xc300, 0x4000, 0xc200) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,40):(-9, 0, 4, -6) (0xc440, 0x0000, 0x4200, 0xc300) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,42):(1, 4, 0, 2) (0x3e00, 0x4200, 0x0000, 0x4000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,44):(-1, 2, 6, 0) (0xbe00, 0x4000, 0x4300, 0x0000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,46):(1, 4, 4, -2) (0x3e00, 0x4200, 0x4200, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,48):(1, -6, -2, -4) (0x3e00, 0xc300, 0xc000, 0xc200) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,50):(-4, 3, -3, 1) (0xc200, 0x4100, 0xc100, 0x3e00) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,52):(-5, 4, -4, 2) (0xc280, 0x4200, 0xc200, 0x4000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,54):(-1, 0, -8, -2) (0xbe00, 0x0000, 0xc400, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,56):(-6, -1, -5, -1) (0xc300, 0xbe00, 0xc280, 0xbe00) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,58):(5, 0, -4, 6) (0x4280, 0x0000, 0xc200, 0x4300) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,60):(5, 0, 0, 6) (0x4280, 0x0000, 0x0000, 0x4300) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p1,62):(3, 0, -4, -6) (0x4100, 0x0000, 0xc200, 0xc300) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,32):(3, -10, -9, 3) (0x4100, 0xc480, 0xc440, 0x4100) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,34):(2, 5, 6, 0) (0x4000, 0x4280, 0x4300, 0x0000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,36):(0, -1, 4, -2) (0x0000, 0xbe00, 0x4200, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,38):(6, -3, -2, 0) (0x4300, 0xc100, 0xc000, 0x0000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,40):(0, 9, -4, 2) (0x0000, 0x4440, 0xc200, 0x4000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,42):(4, 9, -4, 2) (0x4200, 0x4440, 0xc200, 0x4000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,44):(6, 1, 6, 0) (0x4300, 0x3e00, 0x4300, 0x0000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,46):(8, 3, -8, -6) (0x4400, 0x4100, 0xc400, 0xc300) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,48):(-2, -1, 2, 0) (0xc000, 0xbe00, 0x4000, 0x0000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,50):(7, 4, -1, -5) (0x4380, 0x4200, 0xbe00, 0xc280) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,52):(-4, 5, -4, 2) (0xc200, 0x4280, 0xc200, 0x4000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,54):(-4, -9, -4, 6) (0xc200, 0xc440, 0xc200, 0x4300) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,56):(-3, 0, -9, 3) (0xc100, 0x0000, 0xc440, 0x4100) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,58):(4, -3, 0, -2) (0x4200, 0xc100, 0x0000, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,60):(0, 5, 4, -2) (0x0000, 0x4280, 0x4200, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p2,62):(-4, -5, 8, -2) (0xc200, 0xc280, 0x4400, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,32):(-5, 5, 3, -2) (0xc280, 0x4280, 0x4100, 0xc000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,34):(0, -2, -6, -3) (0x0000, 0xc000, 0xc300, 0xc100) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,36):(-2, -4, 0, -5) (0xc000, 0xc200, 0x0000, 0xc280) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,38):(-12, -2, 2, -1) (0xc500, 0xc000, 0x4000, 0xbe00) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,40):(6, 0, -4, 1) (0x4300, 0x0000, 0xc200, 0x3e00) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,42):(2, 0, 0, -5) (0x4000, 0x0000, 0x0000, 0xc280) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,44):(0, -2, -6, -3) (0x0000, 0xc000, 0xc300, 0xc100) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,46):(-2, -8, 0, 3) (0xc000, 0xc400, 0x0000, 0x4100) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,48):(-4, 6, 6, -9) (0xc200, 0x4300, 0x4300, 0xc440) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,50):(1, -3, 5, 1) (0x3e00, 0xc100, 0x4280, 0x3e00) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,52):(6, 0, 4, -1) (0x4300, 0x0000, 0x4200, 0xbe00) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,54):(-2, 0, 8, 3) (0xc000, 0x0000, 0x4400, 0x4100) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,56):(5, -1, 1, 0) (0x4280, 0xbe00, 0x3e00, 0x0000) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,58):(-6, 4, 8, -5) (0xc300, 0x4200, 0x4400, 0xc280) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,60):(-2, -4, 0, 3) (0xc000, 0xc200, 0x0000, 0x4100) #d geth $ln32n0c0b0m0 16
DEBUG-LM1(n0c0b0m0p3,62):(2, 0, 0, -1) (0x4000, 0x0000, 0x0000, 0xbe00) #d geth $ln32n0c0b0m0 16
```

## 1L1B=16MAB (64/4096 MN-Core2)

- この節での追加事項
  - Distribute/Gather + Broadcast/Reduce between L1B and 16MABs (result reduce network, RRN)
  - L1BM (8192長語): vsmでは$lbで表す。

### RRNを利用したSumの実装

複数のMAB間にまたがるデータを縮約する場合にはRRNを利用する。Sum (torch.sum) の実装では倍精度の加算を用いて縮約し、その後同じ通信経路で各MABに計算結果を放送で送り返している。

```
d set $lm0n0c0b0m0p0 1 8000000000000000
d set $lm0n0c0b0m0p1 1 C02F000000000000
d set $lm0n0c0b0m0p2 1 C02E000000000000
d set $lm0n0c0b0m0p3 1 C02D000000000000
d set $lm0n0c0b0m1p0 1 C02C000000000000
d set $lm0n0c0b0m1p1 1 C02B000000000000
d set $lm0n0c0b0m1p2 1 C02A000000000000
d set $lm0n0c0b0m1p3 1 C029000000000000
d set $lm0n0c0b0m2p0 1 C028000000000000
d set $lm0n0c0b0m2p1 1 C027000000000000
d set $lm0n0c0b0m2p2 1 C026000000000000
d set $lm0n0c0b0m2p3 1 C025000000000000
d set $lm0n0c0b0m3p0 1 C024000000000000
d set $lm0n0c0b0m3p1 1 C023000000000000
d set $lm0n0c0b0m3p2 1 C022000000000000
d set $lm0n0c0b0m3p3 1 C021000000000000
d set $lm0n0c0b0m4p0 1 C020000000000000
d set $lm0n0c0b0m4p1 1 C01E000000000000
d set $lm0n0c0b0m4p2 1 C01C000000000000
d set $lm0n0c0b0m4p3 1 C01A000000000000
d set $lm0n0c0b0m5p0 1 C018000000000000
d set $lm0n0c0b0m5p1 1 C016000000000000
d set $lm0n0c0b0m5p2 1 C014000000000000
d set $lm0n0c0b0m5p3 1 C012000000000000
d set $lm0n0c0b0m6p0 1 C010000000000000
d set $lm0n0c0b0m6p1 1 C00C000000000000
d set $lm0n0c0b0m6p2 1 C008000000000000
d set $lm0n0c0b0m6p3 1 C004000000000000
d set $lm0n0c0b0m7p0 1 C000000000000000
d set $lm0n0c0b0m7p1 1 BFF8000000000000
d set $lm0n0c0b0m7p2 1 BFF0000000000000
d set $lm0n0c0b0m7p3 1 BFE0000000000000
d set $lm0n0c0b0m8p0 1 0000000000000000
d set $lm0n0c0b0m8p1 1 3FE0000000000000
d set $lm0n0c0b0m8p2 1 3FF0000000000000
d set $lm0n0c0b0m8p3 1 3FF8000000000000
d set $lm0n0c0b0m9p0 1 4000000000000000
d set $lm0n0c0b0m9p1 1 4004000000000000
d set $lm0n0c0b0m9p2 1 4008000000000000
d set $lm0n0c0b0m9p3 1 400C000000000000
d set $lm0n0c0b0m10p0 1 4010000000000000
d set $lm0n0c0b0m10p1 1 4012000000000000
d set $lm0n0c0b0m10p2 1 4014000000000000
d set $lm0n0c0b0m10p3 1 4016000000000000
d set $lm0n0c0b0m11p0 1 4018000000000000
d set $lm0n0c0b0m11p1 1 401A000000000000
d set $lm0n0c0b0m11p2 1 401C000000000000
d set $lm0n0c0b0m11p3 1 401E000000000000
d set $lm0n0c0b0m12p0 1 4020000000000000
d set $lm0n0c0b0m12p1 1 4021000000000000
d set $lm0n0c0b0m12p2 1 4022000000000000
d set $lm0n0c0b0m12p3 1 4023000000000000
d set $lm0n0c0b0m13p0 1 4024000000000000
d set $lm0n0c0b0m13p1 1 4025000000000000
d set $lm0n0c0b0m13p2 1 4026000000000000
d set $lm0n0c0b0m13p3 1 4027000000000000
d set $lm0n0c0b0m14p0 1 4028000000000000
d set $lm0n0c0b0m14p1 1 4029000000000000
d set $lm0n0c0b0m14p2 1 402A000000000000
d set $lm0n0c0b0m14p3 1 402B000000000000
d set $lm0n0c0b0m15p0 1 402C000000000000
d set $lm0n0c0b0m15p1 1 402D000000000000
d set $lm0n0c0b0m15p2 1 402E000000000000
d set $lm0n0c0b0m15p3 1 402F000000000000

d getd $lm0n0c0b0 1

l1bmrdfadd $lm0 $lb0
nop
nop
l1bmm $lb0 $lm4v/1000

d getd $lm4n0c0b0m0p0 1
d getd $lm4n0c0b0m0p1 1
d getd $lm4n0c0b0m0p2 1
d getd $lm4n0c0b0m0p3 1
```

```
DEBUG-LM0(n0c0b0m0p0,0):(-0) (0x8000000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m0p1,0):(-15.5) (0xc02f000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m0p2,0):(-15) (0xc02e000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m0p3,0):(-14.5) (0xc02d000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m1p0,0):(-14) (0xc02c000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m1p1,0):(-13.5) (0xc02b000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m1p2,0):(-13) (0xc02a000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m1p3,0):(-12.5) (0xc029000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m2p0,0):(-12) (0xc028000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m2p1,0):(-11.5) (0xc027000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m2p2,0):(-11) (0xc026000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m2p3,0):(-10.5) (0xc025000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m3p0,0):(-10) (0xc024000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m3p1,0):(-9.5) (0xc023000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m3p2,0):(-9) (0xc022000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m3p3,0):(-8.5) (0xc021000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m4p0,0):(-8) (0xc020000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m4p1,0):(-7.5) (0xc01e000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m4p2,0):(-7) (0xc01c000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m4p3,0):(-6.5) (0xc01a000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m5p0,0):(-6) (0xc018000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m5p1,0):(-5.5) (0xc016000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m5p2,0):(-5) (0xc014000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m5p3,0):(-4.5) (0xc012000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m6p0,0):(-4) (0xc010000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m6p1,0):(-3.5) (0xc00c000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m6p2,0):(-3) (0xc008000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m6p3,0):(-2.5) (0xc004000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m7p0,0):(-2) (0xc000000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m7p1,0):(-1.5) (0xbff8000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m7p2,0):(-1) (0xbff0000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m7p3,0):(-0.5) (0xbfe0000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m8p0,0):(0) (0x0000000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m8p1,0):(0.5) (0x3fe0000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m8p2,0):(1) (0x3ff0000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m8p3,0):(1.5) (0x3ff8000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m9p0,0):(2) (0x4000000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m9p1,0):(2.5) (0x4004000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m9p2,0):(3) (0x4008000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m9p3,0):(3.5) (0x400c000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m10p0,0):(4) (0x4010000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m10p1,0):(4.5) (0x4012000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m10p2,0):(5) (0x4014000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m10p3,0):(5.5) (0x4016000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m11p0,0):(6) (0x4018000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m11p1,0):(6.5) (0x401a000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m11p2,0):(7) (0x401c000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m11p3,0):(7.5) (0x401e000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m12p0,0):(8) (0x4020000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m12p1,0):(8.5) (0x4021000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m12p2,0):(9) (0x4022000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m12p3,0):(9.5) (0x4023000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m13p0,0):(10) (0x4024000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m13p1,0):(10.5) (0x4025000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m13p2,0):(11) (0x4026000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m13p3,0):(11.5) (0x4027000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m14p0,0):(12) (0x4028000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m14p1,0):(12.5) (0x4029000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m14p2,0):(13) (0x402a000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m14p3,0):(13.5) (0x402b000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m15p0,0):(14) (0x402c000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m15p1,0):(14.5) (0x402d000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m15p2,0):(15) (0x402e000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m15p3,0):(15.5) (0x402f000000000000) #d getd $lm0n0c0b0 1
DEBUG-LM0(n0c0b0m0p0,4):(0) (0x0000000000000000) #d getd $lm4n0c0b0m0p0 1
DEBUG-LM0(n0c0b0m0p1,4):(-8) (0xc020000000000000) #d getd $lm4n0c0b0m0p1 1
DEBUG-LM0(n0c0b0m0p2,4):(0) (0x0000000000000000) #d getd $lm4n0c0b0m0p2 1
DEBUG-LM0(n0c0b0m0p3,4):(8) (0x4020000000000000) #d getd $lm4n0c0b0m0p3 1
```

今回のチュートリアルはここまでです。以降に記す、より上位の階層を利用したvsmもエミュレータは実行できますが、要素数が多すぎて`d get`や`d set`による手動の検証は実用的でなくなってきます。
次回のMN-Core勉強会ではより高度な環境を提供出来たらと思います。

## 1L2B=8L1B (512/4096 MN-Core2)

- この節での追加事項
  - Distribute/Gather + Broadcast/Reduce between L2B and 8L1Bs
  - L2BM (32768長語): vsmでは$lcで表す。

## Package=4Group=8L2B (4096/4096 MN-Core2)

- この節での追加事項
  - Distribute/Gather + Broadcast/Reduce between DRAM and 8L2Bs
  - DRAM (16GiB)
  - PDM (kind of a memory mainly for communication with the host)
