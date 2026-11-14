# 手元の PC で使う

手元の PC（Ubuntu 24.04）に SDK をインストールし、エミュレータで動かします。
実機が無くても、プログラムが正しく動くかを確かめられます。

インストールの手順は [README のセットアップ](https://github.com/Nobuto-Ura-HPC-Hub/open-mncore2-sdk#セットアップ) にあります
（PFN の `mncore-sdk` を apt で入れ、SDK の kit を依存順にインストールします）。

インストールしたら、サンプルをエミュレータで実行します。

```bash
source $HOME/.local/mncore2-sdk/bin/activate
sdk-examples libmnc2 ~/work
cd ~/work/libmnc2-*/01-nop
ninja
ninja test-emu          # エミュレータ（プロセス版）で実行
ninja test-emu-lib      # エミュレータ（ライブラリ版）で実行
```

実機で動かすときは [PFCP で使う](pfcp.md) を参照してください。
