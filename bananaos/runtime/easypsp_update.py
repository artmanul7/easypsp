"""EasyPSP install / update from GitHub Releases (github.com/artmanul7/easypsp).

Layout (exFAT friendly, no symlinks):
  <ROOT>/runtime/<version>/   emulator, libraries, scripts of one version
  <ROOT>/current              text file: the version the "EasyPSP" tile starts
  <ROOT>/previous             text file: the version before the last update (kept until the next update)
  <ROOT>/userdata/            settings, saves, logs (never touched by updates)

  easypsp_update.py --check      newer release?   last line: RESULT newer|latest|error <version> <message>
  easypsp_update.py --install    download, verify SHA-256, check the kernel, unpack, switch "current"
  easypsp_update.py --auto       --check at most once a day, quietly (the launcher runs it in the background)
  easypsp_update.py --setup <unpacked release dir>   first installation (used by "Install-EasyPSP.sh")
Each step prints "STEP <text>" lines; the emulator shows them in Settings > System > EasyPSP."""
import hashlib, json, os, shutil, sys, tarfile, time, urllib.request
from pathlib import Path

REPO = 'artmanul7/easypsp'
API = 'https://api.github.com/repos/' + REPO + '/releases/latest'
HERE = Path(__file__).resolve().parent              # <ROOT>/runtime/<version> (or the unpacked release)
ROOT = Path(os.environ.get('EASYPSP_ROOT', '/data/banana/easypsp'))
TILES = Path('/data/store/tiles')
TILE = 'EasyPSP.sh'
UA = {'User-Agent': 'EasyPSP-updater', 'Accept': 'application/vnd.github+json'}


def say(kind, text):
    print(kind + ' ' + text, flush=True)


def version_tuple(v):
    out = []
    for part in v.lstrip('vV').split('.'):
        num = ''.join(ch for ch in part if ch.isdigit())
        out.append(int(num) if num else 0)
    return tuple(out)


def current_version():
    try: return (HERE / 'VERSION').read_text().strip()
    except OSError: return '0'


def fetch_json(url):
    with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=15) as r:
        return json.loads(r.read().decode())


def latest():
    rel = fetch_json(API)
    tag = rel.get('tag_name', '').lstrip('vV')
    assets = {a['name']: a['browser_download_url'] for a in rel.get('assets', [])}
    tar = next((n for n in assets if n.endswith('.tar.gz') and n.startswith('easypsp-')), None)
    if not tag or not tar or tar + '.sha256' not in assets:
        raise RuntimeError('release ' + (tag or '?') + ' has no package')
    return tag, assets[tar], assets[tar + '.sha256'], tar


def state_file():
    return ROOT / 'userdata' / 'update.json'


def check(quiet=False):
    cur = current_version()
    try:
        tag, _, _, _ = latest()
    except Exception as exc:
        say('RESULT', 'error ' + cur + ' ' + ('no connection to GitHub' if 'urlopen' in repr(exc) or 'timed out' in str(exc) else str(exc)))
        return 1
    newer = version_tuple(tag) > version_tuple(cur)
    try:
        state_file().parent.mkdir(parents=True, exist_ok=True)
        state_file().write_text(json.dumps({'checked': int(time.time()), 'current': cur, 'latest': tag, 'newer': newer}))
    except OSError: pass
    say('RESULT', ('newer ' if newer else 'latest ') + tag + (' EasyPSP ' + tag + ' is available' if newer else ' up to date'))
    return 0


def auto():
    try:
        st = json.loads(state_file().read_text())
        if time.time() - st.get('checked', 0) < 24 * 3600: return 0
    except (OSError, ValueError): pass
    return check(quiet=True)


def download(url, dest):
    with urllib.request.urlopen(urllib.request.Request(url, headers={'User-Agent': UA['User-Agent']}), timeout=30) as r, open(dest, 'wb') as f:
        total = int(r.headers.get('Content-Length') or 0); done = 0; last = 0
        while True:
            chunk = r.read(1 << 20)
            if not chunk: break
            f.write(chunk); done += len(chunk)
            if total and time.time() - last > 1:
                say('STEP', 'Downloading %d%% (%d of %d MB)' % (done * 100 // total, done >> 20, total >> 20)); last = time.time()


def sha256(path):
    h = hashlib.sha256()
    with open(path, 'rb') as f:
        for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
    return h.hexdigest()


def place(release_dir):
    """release_dir = unpacked package (runtime/<v>/..., tiles/, media/). Installs it as the current version."""
    release_dir = Path(release_dir)
    vdirs = [p for p in (release_dir / 'runtime').iterdir() if p.is_dir()]
    if len(vdirs) != 1: raise RuntimeError('package must hold one version')
    src = vdirs[0]; ver = src.name
    say('STEP', 'Checking this system (kernel / GPU module)')
    sys.path.insert(0, str(src))
    import importlib; compat = importlib.import_module('compat'); importlib.reload(compat)
    info = compat.check(src / 'mali_kbase.ko')
    sys.path.pop(0)
    say('STEP', 'Compatible: ' + info['os'] + ' ' + info['os_version'] + ', kernel ' + info['kernel'] + ' (' + info['validation'] + ')')
    (ROOT / 'runtime').mkdir(parents=True, exist_ok=True)
    dst = ROOT / 'runtime' / ver
    if dst.exists(): shutil.rmtree(dst)
    shutil.copytree(src, dst)
    for d in ('logs', 'home'): (ROOT / 'userdata' / d).mkdir(parents=True, exist_ok=True)   # settings: run.py on first start
    Path('/data/roms/psp').mkdir(parents=True, exist_ok=True)
    old = (ROOT / 'current').read_text().strip() if (ROOT / 'current').exists() else ''
    if old and old != ver: (ROOT / 'previous').write_text(old + '\n')
    (ROOT / 'current').write_text(ver + '\n')
    keep = {ver, old}
    for p in (ROOT / 'runtime').iterdir():
        if p.is_dir() and p.name not in keep: shutil.rmtree(p, ignore_errors=True)
    if TILES.is_dir():
        shutil.copyfile(release_dir / 'tiles' / TILE, TILES / TILE)
        box = TILES / 'media' / 'box'; box.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(release_dir / 'media' / 'easypsp.png', box / 'EasyPSP.png')
    say('RESULT', 'installed ' + ver + ' EasyPSP ' + ver + ' installed')
    return ver


def install():
    tag, url, sha_url, name = latest()
    if version_tuple(tag) <= version_tuple(current_version()):
        say('RESULT', 'latest ' + tag + ' up to date'); return 0
    work = ROOT / 'downloads'; shutil.rmtree(work, ignore_errors=True); work.mkdir(parents=True)
    say('STEP', 'Downloading EasyPSP ' + tag)
    download(url, work / name)
    want = urllib.request.urlopen(urllib.request.Request(sha_url, headers={'User-Agent': UA['User-Agent']}), timeout=15).read().decode().split()[0]
    say('STEP', 'Verifying')
    if sha256(work / name) != want: raise RuntimeError('download is damaged (SHA-256 mismatch), try again')
    say('STEP', 'Unpacking')
    with tarfile.open(work / name) as t: t.extractall(work / 'pkg', filter='data')
    place(work / 'pkg')
    shutil.rmtree(work, ignore_errors=True)
    return 0


def main():
    try:
        if '--check' in sys.argv: return check()
        if '--auto' in sys.argv: return auto()
        if '--install' in sys.argv: return install()
        if '--setup' in sys.argv: place(sys.argv[sys.argv.index('--setup') + 1]); return 0
        print(__doc__); return 2
    except Exception as exc:
        say('RESULT', 'error ' + current_version() + ' ' + str(exc)); return 1


if __name__ == '__main__':
    sys.exit(main())
