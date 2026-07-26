## 配布物

- `README.md` 本文書
- `DISCLAIMER.txt` 配布物についての免責事項
- `NOTICE.md` OSSライセンス表示
- `tutorial.md` アセンブラ・エミュレータを利用したチュートリアル
- `assemble3` MN-Core 2 アセンブラ
- `gpfn3_package_main` MN-Core 2 エミュレータ
- `Changelog.md` 変更履歴

## Software Development Manual (SDM)

https://projects.preferred.jp/mn-core/assets/mncore2_dev_manual_ja.pdf

## 想定環境

本パッケージで配布するアセンブラ・エミュレータは実行環境としてUbuntu22.04を想定している。

## 依存ライブラリのインストール

### Debian/Ubuntu

```
apt-get install libgomp1
```

### Rocky Linux/Fedora

```
yum install libgomp
```

## アセンブラ

MN-Core 2 アセンブラは、MN-Core 2 ソフトウェア開発者マニュアル(以下SDM)第3章で述べるアセンブリ言語で実装されたプログラムを機械語に変換する。
標準のバイナリ名は`assemble3`である。

`assemble3`にPATHが通っている状況で以下を実行するとpass.asmにアセンブル結果が出力される。

```
echo 'lpassa $lm0v $ln0v' > pass.vsm
assemble3 pass.vsm > pass.asm
```

## エミュレータ

MN-Core 2ボードのエミュレータが存在する。
命令ストリームがパック形式 (SDM 第1.2節) になっている必要がなく、またアセンブリ言語内で主要なメモリ要素の内容を出力する制御文 (Debug get文、SDM 第3.4.3節) が記述できるため挙動の確認に向く。

標準のバイナリ名は`gpfn3_package_main`である。

以下はファイル`sample.vsm`に手書きした命令列をアセンブルし、エミュレータで実行する例である。
`assemble3`および`gpfn3_package_main`にPATHが通っている前提とする。

1行目の`lpassa`命令はLM0にそれが属するPEの番号 (0から3) を書き込み、2行目の`d get`命令はあるひとつのMABについて、書き込んだ場所の内容を読み出している。
`d get`命令で読み出した値は`-d`オプションで指定したファイルに出力される。
よってファイル`sample.dmp`には、PE番号に対応して0から3の値が書き込まれることとなる。

1行目の`lpassa`命令についての詳細はSDM 第3.6.12.4節、第3.6.1.20節、第3.6.1.6節を、2行目の`d get`命令についての詳細は第3.4.3節を参照のこと。

```
$ cat sample.vsm
lpassa $subpeid $lm0
d get $lm0n0c0b0m0 1
$ assemble3 sample.vsm > sample.asm
$ gpfn3_package_main -i sample.asm -d sample.dmp
$ cat sample.dmp
DEBUG-LM0(n0c0b0m0p0,0):(f:0, i:{{0x0,0x0},{0x0,0x0}}, v:0x0) #d get $lm0n0c0b0m0 1
DEBUG-LM0(n0c0b0m0p1,0):(f:0, i:{{0x0,0x0},{0x0,0x1}}, v:0x1) #d get $lm0n0c0b0m0 1
DEBUG-LM0(n0c0b0m0p2,0):(f:0, i:{{0x0,0x0},{0x0,0x2}}, v:0x2) #d get $lm0n0c0b0m0 1
DEBUG-LM0(n0c0b0m0p3,0):(f:0, i:{{0x0,0x0},{0x0,0x3}}, v:0x3) #d get $lm0n0c0b0m0 1
```
