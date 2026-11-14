#!/bin/sh
# Install EasyPSP (PSP emulator, PPSSPP + Vulkan) on BananaOS: put this file into the Apps folder
# (/data/store/tiles, or "store\tiles" on the banana share) and start it from Apps. Needs Wi-Fi.
# It downloads the latest release from https://github.com/artmanul7/easypsp, checks its SHA-256 and that the
# GPU module fits this kernel, installs to /data/banana/easypsp and adds the "EasyPSP" tile. Then it removes itself.
# Your games stay in /data/roms/psp (.iso .cso .chd).
R=/data/banana/easypsp
W=$R/downloads
mkdir -p "$W"
exec > "$W/install.log" 2>&1
python3 - "$0" <<'PYEOF'
import hashlib, json, os, shutil, subprocess, sys, tarfile, urllib.request
from pathlib import Path
SELF = Path(sys.argv[1]); R = Path('/data/banana/easypsp'); W = R / 'downloads'
UA = {'User-Agent': 'EasyPSP-installer'}
def show(title, lines):
    try:
        sys.path.insert(0, str(next((W / 'pkg' / 'runtime').iterdir())))
        from ui import message
        message(title, lines)
    except Exception as exc:
        print('screen:', exc)
try:
    rel = json.loads(urllib.request.urlopen(urllib.request.Request(
        'https://api.github.com/repos/artmanul7/easypsp/releases/latest', headers=UA), timeout=20).read())
    assets = {a['name']: a['browser_download_url'] for a in rel['assets']}
    name = next(n for n in assets if n.startswith('easypsp-') and n.endswith('.tar.gz'))
    print('release', rel['tag_name'], name, flush=True)
    with urllib.request.urlopen(urllib.request.Request(assets[name], headers=UA), timeout=60) as r, open(W / name, 'wb') as f:
        shutil.copyfileobj(r, f, 1 << 20)
    want = urllib.request.urlopen(urllib.request.Request(assets[name + '.sha256'], headers=UA), timeout=20).read().decode().split()[0]
    h = hashlib.sha256(); h.update((W / name).read_bytes())
    if h.hexdigest() != want: raise RuntimeError('Download is damaged (SHA-256 mismatch). Start the installer again.')
    shutil.rmtree(W / 'pkg', ignore_errors=True)
    with tarfile.open(W / name) as t: t.extractall(W / 'pkg', filter='data')
    tool = next((W / 'pkg' / 'runtime').iterdir()) / 'easypsp_update.py'
    out = subprocess.run([sys.executable, str(tool), '--setup', str(W / 'pkg')], capture_output=True, text=True,
                         env={**os.environ, 'EASYPSP_ROOT': str(R)})
    print(out.stdout, out.stderr, flush=True)
    last = [l for l in out.stdout.splitlines() if l.startswith('RESULT ')]
    if out.returncode or not last or not last[-1].startswith('RESULT installed'):
        raise RuntimeError(last[-1][7:].split(' ', 2)[-1] if last else 'installation failed')
    show('EasyPSP ' + rel['tag_name'].lstrip('v') + ' installed', ['Start it from Apps: "EasyPSP".',
         'Games: /data/roms/psp', 'Updates: Settings > System > EasyPSP'])
    shutil.rmtree(W, ignore_errors=True)
    SELF.unlink(missing_ok=True)        # the installer is not needed any more
except Exception as exc:
    print('ERROR', exc, flush=True)
    show('EasyPSP: could not install', [str(exc)[i:i + 60] for i in range(0, min(len(str(exc)), 240), 60)] +
         ['Details: /data/banana/easypsp/downloads/install.log'])
PYEOF
