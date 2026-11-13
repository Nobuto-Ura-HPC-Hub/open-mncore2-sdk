"""examples/lit/ — examples 用 lit suite (emu:lib + device)

examples/lit/test-emu-lib/ : emu:lib 用 (assemble3 が必要)
examples/lit/test-device/  : 実機 用 (gpfn3-smi + assemble3 が必要)

実行例:
    lit examples/lit/              # vsmlink/ 直下から
"""
import os
import shutil
import lit.formats

config.name = "vsmlink-examples"
config.test_format = lit.formats.ShTest(execute_external=False)
config.suffixes = ['.test']

# パス: lit/ → examples/ → vsmlink/
examples_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
root = os.path.abspath(os.path.join(examples_root, '..'))


def _read_sdk_root(root):
    """scripts/sdk.ninja の sdk_root 行を読む (examples/*/build.ninja と同じ参照元)。"""
    sdk_ninja = os.path.join(root, 'scripts', 'sdk.ninja')
    with open(sdk_ninja) as f:
        for line in f:
            line = line.strip()
            if line.startswith('sdk_root'):
                return line.split('=', 1)[1].strip()
    raise RuntimeError(f'{sdk_ninja}: sdk_root not found')


sdk_root = _read_sdk_root(root)

config.substitutions.append(('%examples', examples_root))
config.substitutions.append(('%root', root))
config.substitutions.append(('%sdk', sdk_root))

# feature 判定 (subdir の lit.local.cfg で unsupported 判定に使う)
if shutil.which('assemble3'):
    config.available_features.add('assemble3')
if shutil.which('gpfn3-smi'):
    config.available_features.add('device')

# FileCheck
filecheck = '/usr/lib/llvm-18/bin/FileCheck'
if os.path.isfile(filecheck):
    config.substitutions.append(('%FileCheck', filecheck))
    config.available_features.add('filecheck')
elif shutil.which('FileCheck'):
    config.substitutions.append(('%FileCheck', shutil.which('FileCheck')))
    config.available_features.add('filecheck')

config.environment['PATH'] = os.pathsep.join([
    os.path.join(root, 'bin'),
    os.environ.get('PATH', '')
])
