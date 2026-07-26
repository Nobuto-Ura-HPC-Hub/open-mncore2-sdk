"""examples/lit/ — examples 用 lit suite (emu:lib + device)

examples/lit/test-emu-lib/ : emu:lib 用 (assemble3 が必要)
examples/lit/test-device/  : 実機 用 (gpfn3-smi + assemble3 が必要)

実行例:
    lit examples/lit/              # vsm-linker/ 直下から
"""
import os
import shutil
import lit.formats

config.name = "vsm-linker-examples"
config.test_format = lit.formats.ShTest(execute_external=True)
config.suffixes = ['.test']

# パス: lit/ → examples/ → vsm-linker/
examples_root = os.path.abspath(os.path.join(os.path.dirname(__file__), '..'))
root = os.path.abspath(os.path.join(examples_root, '..'))
sdk_root = os.path.abspath(os.path.join(root, '..', '_mncore2-sdk-v1'))

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
