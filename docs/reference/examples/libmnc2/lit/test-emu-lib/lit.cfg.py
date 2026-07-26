"""share/examples/libmnc2/lit/test-emu-lib/ — **emu:lib backend 用** の lit suite

SDK 配布対象の share/examples/libmnc2/ 配下に置かれた 3 backend lit の
emu:lib 版。各 .test は `ninja -C %examples/NN-* test-emu-lib` を実行する。
"""

import os
import shutil
import lit.formats

config.name = "mncore2-sdk-emu-lib"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = ['.test']

# examples テストには assemble3 と sdk.ninja が必要
# (source scripts/activate で PATH 有効化)
if not shutil.which('assemble3'):
    config.unsupported = True

# %examples = share/examples/libmnc2/ (このファイルから 2 段上)
examples_dir = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
sdk_ninja = os.path.join(examples_dir, '..', 'scripts', 'sdk.ninja')
if not os.path.isfile(sdk_ninja):
    config.unsupported = True

config.substitutions.append(('%examples', examples_dir))
