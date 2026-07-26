# lit.cfg.py — openacc-c-kit examples lit suite (親)
#
# 使い方:
#   source <SDK>/bin/activate
#   lit -v share/examples/openacc-c/lit/test-emu-lib/
#   lit -v share/examples/openacc-c/lit/test-device/
#
# 各 .test は make の test-{emu-lib,device} target を呼ぶだけ。
# build-e2e + test-{emu-lib,device} の 2 行で 1 example 分の E2E 検証。

import os
import lit.formats

config.name = "openacc-c-kit examples"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = ['.test']

# %examples = share/examples/openacc-c/ (.test ファイルから ../.. で到達)
examples_dir = os.path.normpath(os.path.join(os.path.dirname(__file__), '..'))
config.substitutions.append(('%examples', examples_dir))

# 環境変数 (PATH / LD_LIBRARY_PATH / SDK_ROOT 等) を引き継ぐ
config.environment = dict(os.environ)
config.test_source_root = os.path.dirname(__file__)

# device suite を 1 worker 直列化 (HW 共有資源)
lit_config.parallelism_groups["device"] = 1
