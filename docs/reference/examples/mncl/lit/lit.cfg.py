# lit.cfg.py — MNCL examples lit suite (親)
#
# 使い方:
#   source <SDK>/bin/activate
#   llvm-lit -v share/examples/mncl/lit/test-emu-lib/

import os
import lit.formats

config.name = "MNCL examples"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = ['.test']

# %examples = share/examples/mncl/ (.test ファイルから ../.. で到達)
examples_dir = os.path.normpath(os.path.join(os.path.dirname(__file__), '..'))
config.substitutions.append(('%examples', examples_dir))

# 環境変数 (PATH / LD_LIBRARY_PATH / SDK_ROOT 等) を引き継ぐ
config.environment = dict(os.environ)
config.test_source_root = os.path.dirname(__file__)

# device suite を 1 worker 直列化 (HW 共有資源)
lit_config.parallelism_groups["device"] = 1
