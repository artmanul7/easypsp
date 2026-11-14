"""Compatibility check (EasyPSP 0.0.1): the one hard rule is that the bundled Mali GPU module (mali_kbase.ko) was
built for exactly the running kernel (its vermagic = uname -r) on an aarch64 system. The OS name and the device model
are only reported (tested: BananaOS 0.5.2.2 / 0.5.3.4 on the Anbernic RG DS Plus, kernel 7.2.7-banana)."""
from pathlib import Path
import platform
import re
import shlex

TESTED = {('bananaos', '0.5.2.2', 'Anbernic RG DS Plus'), ('bananaos', '0.5.3.4', 'Anbernic RG DS Plus')}


def parse_release(text):
    values = {}
    for line in text.splitlines():
        if not line.strip() or line.lstrip().startswith('#') or '=' not in line: continue
        key, value = line.split('=', 1)
        parts = shlex.split(value, comments=True)
        values[key.strip()] = parts[0] if len(parts) == 1 else value.strip()
    return values


def module_vermagic(module):
    match = re.search(rb'\bvermagic=([^\0]+)', Path(module).read_bytes())
    if not match: raise RuntimeError('GPU module has no vermagic')
    return match[1].decode('ascii')


def validate(kernel, arch, vermagic, os_id='', os_version='', model=''):
    if arch != 'aarch64': raise RuntimeError('Requires a 64-bit ARM (aarch64) system; found ' + arch)
    built_for = vermagic.split()[0]
    if built_for != kernel:
        raise RuntimeError('The GPU module is built for kernel ' + built_for + ', this system runs ' + kernel)
    return {'os': os_id, 'os_version': os_version, 'kernel': kernel, 'arch': arch, 'model': model,
            'module_vermagic': vermagic,
            'validation': 'tested' if (os_id, os_version, model) in TESTED else 'untested (kernel matches)'}


def check(module=None):
    try: release = parse_release(Path('/etc/os-release').read_text())
    except OSError: release = {}
    try: model = Path('/proc/device-tree/model').read_bytes().decode(errors='replace').rstrip('\0\n')
    except OSError: model = ''
    module = Path(module) if module is not None else Path(__file__).resolve().parent / 'mali_kbase.ko'
    return validate(platform.release(), platform.machine(), module_vermagic(module),
                    release.get('ID', ''), release.get('VERSION_ID', ''), model)
