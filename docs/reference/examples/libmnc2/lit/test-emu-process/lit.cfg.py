"""share/examples/libmnc2/lit/test-emu-process/ — **emu:process backend 用** の lit suite

SDK 配布対象の share/examples/libmnc2/ 配下に置かれた 3 backend lit の
emu:process 版。各 .test は `ninja -C %examples/NN-* test-emu` を実行する。

構造上 emu:process で未対応の examples (kernel 間 state 引き継ぎ必須なもの、
06, 13-18, 56-59 = 計 11 個) は `# UNSUPPORTED: true` で明示的に skip。
"""

import os
import shutil
import lit.formats

config.name = "mncore2-sdk-emu-process"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = ['.test']

# emu:process には gpfn3_package_main と assemble3 が必要
if not shutil.which('gpfn3_package_main'):
    config.unsupported = True
if not shutil.which('assemble3'):
    config.unsupported = True

# %examples = share/examples/libmnc2/ (このファイルから 2 段上)
examples_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sdk_ninja = os.path.join(examples_dir, '..', 'scripts', 'sdk.ninja')
if not os.path.isfile(sdk_ninja):
    config.unsupported = True

config.substitutions.append(('%examples', examples_dir))
