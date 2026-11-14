#!/bin/sh
# Install EasyPSP (PSP emulator, PPSSPP + Vulkan) on BananaOS: put this file into the Apps folder
# (/data/store/tiles, or "store\tiles" on the banana share) and start it from Apps. Needs Wi-Fi.
# It downloads the latest release from https://github.com/artmanul7/easypsp, checks its SHA-256 and that the
# GPU module fits this kernel, installs to /data/banana/easypsp and adds the "EasyPSP" tile. Then it removes itself.
# Your games go to /data/roms/psp (.iso .cso .chd).
R=/data/banana/easypsp
W=$R/downloads
mkdir -p "$W"
exec > "$W/install.log" 2>&1
python3 - "$0" <<'PYEOF'
import ctypes as c, hashlib, json, os, select, shutil, struct, subprocess, sys, tarfile, threading, time, urllib.request
from pathlib import Path
SELF = Path(sys.argv[1]); R = Path('/data/banana/easypsp'); W = R / 'downloads'
UA = {'User-Agent': 'EasyPSP-installer'}
GREEN, TEXT, DIM, BG = (90, 200, 90), (230, 235, 243), (150, 170, 190), (14, 22, 35)


class Screen:
    """Both panels of the handheld (BananaOS SDL, side by side 2 x 1024 x 768): text, progress bar, button."""
    def __init__(self):
        os.environ['SDL_VIDEODRIVER'] = 'dummy'; os.environ['SDL_BANANA_PANELS'] = 'side'
        s = c.CDLL('libSDL2-2.0.so.0'); t = c.CDLL('libSDL2_ttf-2.0.so.0'); P, I = c.c_void_p, c.c_int
        def api(lib, name, res, *args):
            f = getattr(lib, name); f.restype = res; f.argtypes = list(args); return f
        class Color(c.Structure): _fields_ = [('r', c.c_uint8), ('g', c.c_uint8), ('b', c.c_uint8), ('a', c.c_uint8)]
        class Rect(c.Structure): _fields_ = [('x', I), ('y', I), ('w', I), ('h', I)]
        self.C, self.R = Color, Rect
        self.color = api(s, 'SDL_SetRenderDrawColor', I, P, c.c_uint8, c.c_uint8, c.c_uint8, c.c_uint8)
        self.clear = api(s, 'SDL_RenderClear', I, P); self.present = api(s, 'SDL_RenderPresent', None, P)
        self.fill = api(s, 'SDL_RenderFillRect', I, P, c.POINTER(Rect)); self.poll = api(s, 'SDL_PollEvent', I, P)
        self.text = api(t, 'TTF_RenderUTF8_Blended', P, P, c.c_char_p, Color)
        self.tex = api(s, 'SDL_CreateTextureFromSurface', P, P, P); self.free = api(s, 'SDL_FreeSurface', None, P)
        self.dtex = api(s, 'SDL_DestroyTexture', None, P)
        self.query = api(s, 'SDL_QueryTexture', I, P, P, P, c.POINTER(I), c.POINTER(I))
        self.copy = api(s, 'SDL_RenderCopy', I, P, P, P, c.POINTER(Rect))
        self.quit_fns = (api(s, 'SDL_Quit', None), api(t, 'TTF_Quit', None))
        if api(s, 'SDL_Init', I, c.c_uint32)(0x20) or api(t, 'TTF_Init', I)(): raise RuntimeError('SDL init failed')
        win = api(s, 'SDL_CreateWindow', P, c.c_char_p, I, I, I, I, c.c_uint32)(b'EasyPSP Installer', 0, 0, 2048, 768, 1)
        mk = api(s, 'SDL_CreateRenderer', P, P, I, c.c_uint32)
        self.r = mk(win, -1, 1) or mk(win, -1, 0)
        open_font = api(t, 'TTF_OpenFont', P, c.c_char_p, I)
        self.big = open_font(b'/usr/share/banana/banana/fonts/Inter-Regular.ttf', 40)
        self.font = open_font(b'/usr/share/banana/banana/fonts/Inter-Regular.ttf', 27)
        if not (self.r and self.big and self.font): raise RuntimeError('screen not available')
        try:
            self.fd = os.open('/dev/input/js0', os.O_RDONLY | os.O_NONBLOCK)
            while select.select([self.fd], [], [], 0)[0]: os.read(self.fd, 4096)
        except OSError: self.fd = None

    def _line(self, font, s, x, y, rgb):
        surf = self.text(font, s.encode('utf-8'), self.C(*rgb, 255))
        if not surf: return
        tex = self.tex(self.r, surf); self.free(surf)
        w, h = c.c_int(), c.c_int(); self.query(tex, None, None, c.byref(w), c.byref(h))
        self.copy(self.r, tex, None, c.byref(self.R(x, y, min(w.value, 930), h.value))); self.dtex(tex)

    def draw(self, title, lines, progress=None, footer=''):
        self.color(self.r, *BG, 255); self.clear(self.r)
        tick = int(time.monotonic() * 8)
        for x in (48, 1072):
            self._line(self.big, title, x, 60, GREEN)
            for i, l in enumerate(lines): self._line(self.font, l, x, 160 + i * 44, TEXT)
            if progress is not None:      # bar 0..1, or None-busy animation (a moving green block)
                self.color(self.r, 40, 55, 75, 255); self.fill(self.r, c.byref(self.R(x, 420, 900, 26)))
                self.color(self.r, *GREEN, 255)
                if progress >= 0: self.fill(self.r, c.byref(self.R(x, 420, int(900 * min(progress, 1)), 26)))
                else: self.fill(self.r, c.byref(self.R(x + (tick * 30) % 760, 420, 140, 26)))
                for k in range(3):            # three dots that light up one after another
                    on = (tick // 2) % 3 == k
                    self.color(self.r, *(GREEN if on else (40, 55, 75)), 255)
                    self.fill(self.r, c.byref(self.R(x + 420 + k * 34, 490, 18, 18)))
            if footer: self._line(self.font, footer, x, 700, DIM)
        self.present(self.r)
        ev = c.create_string_buffer(64)
        while self.poll(ev): pass

    def wait_button(self):
        while self.fd is not None:
            if select.select([self.fd], [], [], .1)[0]:
                data = os.read(self.fd, 4096)
                for off in range(0, len(data) - 7, 8):
                    _, value, kind, _n = struct.unpack_from('IhBB', data, off)
                    if kind == 1 and value == 1: return
        time.sleep(5)

    def close(self):
        for f in self.quit_fns: f()


try: scr = Screen()
except Exception as exc: print('screen:', exc); scr = None
state = {'title': 'Installing EasyPSP', 'lines': ['Connecting to GitHub...'], 'progress': -1.0, 'done': False, 'error': None}


def show():
    if scr: scr.draw(state['title'], state['lines'], state['progress'])


def work():
    try:
        rel = json.loads(urllib.request.urlopen(urllib.request.Request(
            'https://api.github.com/repos/artmanul7/easypsp/releases/latest', headers=UA), timeout=20).read())
        tag = rel['tag_name'].lstrip('v')
        assets = {a['name']: (a['browser_download_url'], a.get('size', 0)) for a in rel['assets']}
        name = next(n for n in assets if n.startswith('easypsp-') and n.endswith('.tar.gz'))
        state['title'] = 'Installing EasyPSP ' + tag
        url, size = assets[name]
        with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r, open(W / name, 'wb') as f:
            size = int(r.headers.get('Content-Length') or size or 0); done = 0
            while True:
                chunk = r.read(256 * 1024)
                if not chunk: break
                f.write(chunk); done += len(chunk)
                state['progress'] = done / size if size else -1.0
                state['lines'] = ['Downloading...', '%.1f of %.1f MB' % (done / 1048576, size / 1048576),
                                  'Please wait, do not turn the handheld off.']
        state['progress'] = -1.0; state['lines'] = ['Checking the download (SHA-256)...']
        want = urllib.request.urlopen(urllib.request.Request(assets[name + '.sha256'][0], headers=UA), timeout=20).read().decode().split()[0]
        h = hashlib.sha256()
        with open(W / name, 'rb') as f:
            for b in iter(lambda: f.read(1 << 20), b''): h.update(b)
        if h.hexdigest() != want: raise RuntimeError('Download is damaged (SHA-256 mismatch). Start the installer again.')
        state['lines'] = ['Unpacking...']
        shutil.rmtree(W / 'pkg', ignore_errors=True)
        with tarfile.open(W / name) as t: t.extractall(W / 'pkg', filter='data')
        state['lines'] = ['Checking this system and installing...']
        tool = next((W / 'pkg' / 'runtime').iterdir()) / 'easypsp_update.py'
        out = subprocess.run([sys.executable, str(tool), '--setup', str(W / 'pkg')], capture_output=True, text=True,
                             env={**os.environ, 'EASYPSP_ROOT': str(R)})
        print(out.stdout, out.stderr, flush=True)
        last = [l for l in out.stdout.splitlines() if l.startswith('RESULT ')]
        if out.returncode or not last or not last[-1].startswith('RESULT installed'):
            raise RuntimeError(last[-1][7:].split(' ', 2)[-1] if last else 'installation failed')
        state['title'] = 'EasyPSP ' + tag + ' installed'
        state['lines'] = ['Start it from Apps: "EasyPSP".', 'Games: /data/roms/psp',
                          'Updates: Settings > System > EasyPSP']
    except Exception as exc:
        print('ERROR', exc, flush=True)
        state['error'] = str(exc); state['title'] = 'EasyPSP: could not install'
        state['lines'] = [str(exc)[i:i + 60] for i in range(0, min(len(str(exc)), 240), 60)] + \
                         ['Details: /data/banana/easypsp/downloads/install.log']
    state['progress'] = None; state['done'] = True


th = threading.Thread(target=work, daemon=True); th.start()
while not state['done']:
    show(); time.sleep(.1)
if scr:
    scr.draw(state['title'], state['lines'], None, 'Press any button to return to Apps')
    scr.wait_button(); scr.close()
if state['error'] is None:
    shutil.rmtree(W, ignore_errors=True)
    SELF.unlink(missing_ok=True)        # the installer is not needed any more
PYEOF
