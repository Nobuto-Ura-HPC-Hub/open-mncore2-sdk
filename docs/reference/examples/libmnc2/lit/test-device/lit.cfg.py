"""share/examples/libmnc2/lit/test-device/ — **実機 (device) 専用** の lit suite

SDK 配布対象の share/examples/libmnc2/ 配下に置かれた 3 backend lit の
device 版。SDK ユーザが実機環境でそのまま走らせられる。

各 .test は `# REQUIRES: device` で実機の存在をガード
(gpfn3-smi が PATH にあるかで判定)。
"""

import os
import shutil
import lit.formats

config.name = "mncore2-sdk-device"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = ['.test']

# device は HW 共有資源なので並列実行不可。この suite の全テストを 1 worker に
# 直列化する (semaphore ベース、lit がヘッダで表示する worker 数はそのままだが
# 実際には 1 つずつしか実行されない)。lit version によっては parallelism_groups
# の効きが弱いので、保険として `lit -j1 --order=lexical <lit-dir>` の CLI 実行を
# 推奨する (cfg 経由では --order は設定できない)。
lit_config.parallelism_groups["device"] = 1
config.parallelism_group = "device"

# features: 各 .test が # REQUIRES: で参照する。
# - device: 実機環境の判定は gpfn3-smi コマンドの存在で行う (libgpfn3
#   同梱の管理 CLI で、これが PATH にあれば実機 pod と判断できる)
# - assemble3: カーネルビルド用のアセンブラ。実機 pod では SDK 同梱で
#   PATH に入っている想定
if shutil.which('gpfn3-smi'):
    config.available_features.add('device')
if shutil.which('assemble3'):
    config.available_features.add('assemble3')

# %examples = share/examples/libmnc2/ (このファイルから 2 段上)
examples_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
config.substitutions.append(('%examples', examples_dir))
