"""EasyPSP launcher for BananaOS (RG DS Plus), as EasyCast: Mali kbase + Vulkan only while the emulator runs,
picture on the chosen panel through our own DRM presentation (EASYPSP_DRM=1), the other panel dark.
Layout: /data/banana/easypsp/runtime/<v>/{run.py,PPSSPPSDL,assets,VERSION}, userdata in /data/banana/easypsp/userdata.
Mali/Vulkan/SDL libraries and the kbase module come from runtime/<v>/lib if present, else from the installed EasyCast."""
from pathlib import Path
import datetime
import json
import os
import re
import subprocess
import sys
import threading
import time

HERE = Path(__file__).resolve().parent
ROOT = HERE.parent.parent
USER = ROOT / 'userdata'
INI = USER / 'config/ppsspp/PSP/SYSTEM/ppsspp.ini'
CONTROLS = USER / 'config/ppsspp/PSP/SYSTEM/controls.ini'
RESTART = 75
BL = Path('/sys/class/backlight')

# RG DS Plus (SDL mapping retrogame_joypad): b0 bottom=Cross, b1 right=Circle, b2 top=Triangle, b3 left=Square,
# Select=196, Start=197, L=193, R=192, F (guide)=4. Combos: Select+F exit, Select+Up/Down brightness, F = menu.
DEFAULT_CONTROLS = """[ControlMapping]
Up = 10-19
Down = 10-20
Left = 10-21
Right = 10-22
Circle = 10-190
Cross = 10-189
Square = 10-188
Triangle = 10-191
Start = 10-197
Select = 10-196
L = 10-193
R = 10-192
An.Up = 10-4003
An.Down = 10-4002
An.Left = 10-4001
An.Right = 10-4000
Pause = 10-4
Exit App = 10-196:10-4
EasyPSP Brightness Up = 10-196:10-19
EasyPSP Brightness Down = 10-196:10-20
"""

DEFAULT_INI = """[General]
CurrentDirectory = /data/roms/psp
[Graphics]
GraphicsBackend = 3 (VULKAN)
EasypspScreen = 0
EasypspBrightness = 0
"""


def _easycast():
    r = Path('/data/banana/easycast')
    try: return r / 'runtime' / (r / 'current').read_text().strip()
    except OSError: return r / 'runtime/none'


LIB = Path(os.environ.get('EASYPSP_LIB') or (HERE / 'lib' if (HERE / 'lib').exists() else _easycast() / 'lib'))
KO = Path(os.environ.get('EASYPSP_KO') or (HERE / 'mali_kbase.ko' if (HERE / 'mali_kbase.ko').exists() else _easycast() / 'mali_kbase.ko'))


def command(*args):
    return subprocess.run(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)


def ini_int(name, default=0):
    try: m = re.search(r'(?m)^' + name + r' = (\d+)', INI.read_text(errors='replace'))
    except OSError: m = None
    return int(m[1]) if m else default


def seed(log):
    INI.parent.mkdir(parents=True, exist_ok=True)
    if not INI.exists():
        INI.write_text(DEFAULT_INI); log('seeded ppsspp.ini')
    marker = USER / '.controls-0.0.1'
    if not marker.exists():   # once: RG DS Plus buttons and EasyPSP combos (later user changes are kept)
        if CONTROLS.exists():
            try: CONTROLS.replace(CONTROLS.with_name('controls.ini.before-easypsp'))
            except OSError: pass
        CONTROLS.write_text(DEFAULT_CONTROLS)
        marker.write_text('1\n'); log('wrote default controls.ini')


class Panels:
    """Game panel: screen 0 = top (backlight1), 1 = bottom (backlight0). The other one is dark while we run."""
    def __init__(self, log):
        self.log = log; self.saved = {}; self.stop = threading.Event(); self.thread = None
        for name in ('backlight0', 'backlight1'):
            try: self.saved[name] = ((BL / name / 'brightness').read_text().strip(), (BL / name / 'bl_power').read_text().strip())
            except OSError: pass
        screen = ini_int('EasypspScreen')
        self.game = 'backlight0' if screen == 1 else 'backlight1'
        self.other = 'backlight1' if screen == 1 else 'backlight0'
        pct = ini_int('EasypspBrightness')
        if 5 <= pct <= 100:
            try:
                top = int((BL / self.game / 'max_brightness').read_text())
                (BL / self.game / 'brightness').write_text(str(max(1, top * pct // 100)))
            except (OSError, ValueError) as exc: log('brightness: ' + str(exc))
        if os.environ.get('EASYPSP_KEEP_OTHER_SCREEN') != '1':
            try:
                (BL / self.other / 'bl_power').write_text('4')
                self.thread = threading.Thread(target=self.keep, daemon=True); self.thread.start()
            except OSError as exc: log('dark other panel: ' + str(exc))
        log('EASYPSP_SCREEN game=' + ('bottom' if screen == 1 else 'top') + ' brightness=' + str(pct) + ' dark=' + self.other)

    def keep(self):
        while not self.stop.wait(1.0):
            try:
                p = BL / self.other / 'bl_power'
                if p.read_text().strip() != '4': p.write_text('4')
            except OSError: pass

    def restore(self):
        self.stop.set()
        if self.thread: self.thread.join(timeout=2)
        own = ini_int('EasypspBrightness') > 0   # else keep brightness changes made with F + volume in game
        for name, (bright, power) in self.saved.items():
            try:
                if own: (BL / name / 'brightness').write_text(bright)
                (BL / name / 'bl_power').write_text(power)
            except OSError as exc: self.log('restore ' + name + ': ' + str(exc))
        self.log('EASYPSP_SCREEN restored ' + str(self.saved))


def main():
    logs = USER / 'logs'; logs.mkdir(parents=True, exist_ok=True)
    for p in sorted(logs.glob('run-*.log'))[:-10]:
        try: p.unlink()
        except OSError: pass
    path = logs / ('run-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S') + '.log')
    out = path.open('a')
    def log(text):
        out.write(str(text) + '\n'); out.flush()
    active = False
    status = 1
    panels = None
    error = None
    new_run = None
    try:
        try: version = (HERE / 'VERSION').read_text().strip()
        except OSError: version = '?'
        log('EasyPSP ' + version + ' ' + datetime.datetime.now().isoformat() + ' runtime ' + str(HERE) + ' lib ' + str(LIB))
        if (HERE / 'compat.py').exists():
            sys.path.insert(0, str(HERE))
            import compat
            log('EASYPSP_COMPAT ' + json.dumps(compat.check(KO), sort_keys=True))   # raises if the GPU module does not fit the kernel
        seed(log)
        updater = HERE / 'easypsp_update.py'
        if updater.exists():   # GitHub release: helper for Settings > System, daily check in the background
            os.environ.update({'EASYPSP_UPDATER': str(updater), 'EASYPSP_ROOT': str(ROOT), 'EASYPSP_VERSION': version})
            try:
                with (USER / 'logs' / 'update-check.log').open('w') as upd:
                    subprocess.Popen([sys.executable, str(updater), '--auto'], stdout=upd, stderr=subprocess.STDOUT,
                                     stdin=subprocess.DEVNULL, start_new_session=True)
            except OSError as exc: log('update check: ' + str(exc))
        r = command('rmmod', 'panfrost'); log(r.stdout.decode(errors='replace'))
        if r.returncode: raise RuntimeError('Could not release Panfrost')
        active = True
        r = command('insmod', str(KO)); log(r.stdout.decode(errors='replace'))
        if r.returncode: raise RuntimeError('Mali module could not start')
        for name, value in [('/sys/class/devfreq/fde60000.gpu/governor', 'performance'),
                            ('/sys/devices/platform/fde60000.gpu/power_policy', 'coarse_demand')]:
            try: Path(name).write_text(value)
            except OSError as exc: log('GPU setting: ' + str(exc))
        (USER / 'home').mkdir(parents=True, exist_ok=True)
        env = dict(os.environ)
        env.update({
            'LD_LIBRARY_PATH': str(LIB),
            'VK_ICD_FILENAMES': str(LIB / 'mali.json'),
            'VK_DRIVER_FILES': str(LIB / 'mali.json'),
            'HOME': str(USER / 'home'),
            'XDG_CONFIG_HOME': str(USER / 'config'),
            'XDG_DATA_HOME': str(USER / 'data'),
            'SDL_VIDEODRIVER': 'kmsdrm',
            'SDL_KMSDRM_FIRST': 'DSI-2',
            'EASYPSP_DRM': os.environ.get('EASYPSP_DRM', '1'),
            'EASYPSP_RESTART_CODE': str(RESTART),
        })
        env.pop('SDL_BANANA_PANELS', None)
        args = [str(HERE / 'PPSSPPSDL'), '--fullscreen'] + sys.argv[1:]
        for _ in range(5):   # PPSSPP asks for a restart (GPU backend, screen) -> fresh process
            panels = Panels(log)
            log('exec ' + ' '.join(args))
            p = subprocess.Popen(args, cwd=str(HERE), env=env, stdout=out, stderr=subprocess.STDOUT)
            status = p.wait()
            log('PPSSPP exit status=' + str(status))
            panels.restore(); panels = None
            if status != RESTART: break
            try: switch_to = (ROOT / 'current').read_text().strip()
            except OSError: switch_to = HERE.name
            if switch_to != HERE.name and (ROOT / 'runtime' / switch_to / 'run.py').exists():
                log('EASYPSP_UPDATE restart into ' + switch_to); new_run = ROOT / 'runtime' / switch_to / 'run.py'; break
            switch_to = None
            args = [str(HERE / 'PPSSPPSDL'), '--fullscreen']
            time.sleep(0.3)
        if status == RESTART: status = 0
    except Exception as exc:
        error = str(exc); log('ERROR: ' + error)
    finally:
        if panels: panels.restore()
        try:
            dev = Path('/sys/class/devfreq/fde60000.gpu')
            top = max(int(x) for x in (dev / 'available_frequencies').read_text().split())
            (dev / 'max_freq').write_text(str(top))
        except Exception as exc: log('GPU max_freq restore: ' + str(exc))
        if active:
            for _ in range(10):
                r = command('rmmod', 'mali_kbase')
                if not r.returncode or not Path('/sys/module/mali_kbase').exists(): break
                time.sleep(.25)
            r = command('modprobe', 'panfrost')
            log('Panfrost restore status=' + str(r.returncode))
        out.close()
    if new_run:   # an update was installed from the settings: start the new version
        os.execv(sys.executable, [sys.executable, str(new_run)])
    if error and (HERE / 'ui.py').exists():
        try:
            sys.path.insert(0, str(HERE))
            from ui import message
            message('EasyPSP: could not continue', [error[i:i + 62] for i in range(0, min(len(error), 248), 62)] +
                    ['Details: ' + str(USER / 'logs')])
        except Exception:
            pass
        return 1
    return status


if __name__ == '__main__':
    sys.exit(main())
