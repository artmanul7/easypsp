<p align="center"><img src="media/easypsp-logo.png" width="180" alt="EasyPSP logo"></p>

<h1 align="center">EasyPSP</h1>

<p align="center"><b>A ready-to-play PPSSPP for BananaOS: the right GPU driver, Vulkan, both screens, handheld controls, one-file install.</b></p>

EasyPSP is a ready build of the [PPSSPP](https://github.com/hrydgard/ppsspp) emulator (v1.20.4) for BananaOS on the
Anbernic RG DS Plus (Rockchip RK3568, Mali G52 GPU). BananaOS has no PSP emulator of its own; EasyPSP adds one with a
single file and everything already set up: the fast Mali Vulkan driver instead of the slower open Panfrost driver,
the picture on the screen you choose, the buttons of the handheld mapped, brightness and quit on button combos,
updates from inside the emulator. Like its sibling [EasyCast](https://github.com/artmanul7/easycast) (Dreamcast), it
loads its own Vulkan driver stack (Mali userspace driver + kernel module) only for the time a game runs and gives the
system's Panfrost driver back when you quit.

## This is only the start

Version 0.0.1 is the **stock PPSSPP emulator** with the handheld parts around it. **The emulator itself is not optimized
yet**: no work on the CPU (JIT), the GPU path or the sound for this chip has been done so far.

The plan, as it went with EasyCast (Dreamcast from about 25 to 58-60 FPS on the same handheld):
- profile real games on the RG DS Plus and find the hot spots (CPU JIT, GPU driver calls, waits between threads);
- optimize them one by one, measuring each change, and keep only what helps without hurting picture or accuracy;
- tune heat and power: GPU clock from the measured frame time, the GPU sleeping between frames;
- build with profile-guided optimization (PGO).

Every change and every measurement will be documented here, including what did not work.

## Please take it

The goal is to share as much as possible. If you build a custom OS for low-power handhelds (ROCKNIX, Knulli, muOS,
ArkOS, BananaOS or your own), you are welcome to ship EasyPSP as a ready PSP emulator. The way it shows the picture on
a chosen screen of a two-screen handheld (its own Vulkan swapchain on top of DRM/KMS, see
[docs/TECHNICAL.md](docs/TECHNICAL.md)) works for any Vulkan program on the same driver.

| Document | What is inside |
|---|---|
| [docs/TECHNICAL.md](docs/TECHNICAL.md) | driver switching, two screens, the DRM swapchain, controls, files, logs, troubleshooting |
| [docs/BUILDING.md](docs/BUILDING.md) | cross-compiling, the patch script, making a release |
| [CHANGELOG.md](CHANGELOG.md) | what each version changed |

## Results

Measured by playing on the RG DS Plus (BananaOS 0.5.3.4), Vulkan:

| Game | Rendering resolution | EasyPSP 0.0.1 |
|---|---|---|
| Tekken 6 | 2x PSP | 60 FPS |
| Tekken 6 | 3x PSP | 45 FPS |
| GTA: Vice City Stories | 3x PSP | 30 FPS |
| God of War: Ghost of Sparta | 1x PSP | 48 to 50 FPS |

These numbers are stock PPSSPP speed on this handheld with the Mali Vulkan driver. Speed work on the emulator itself
(as in EasyCast) comes next.

## What is different from stock PPSSPP

- **Picture on the screen you choose** (top or bottom) of a two-screen handheld; the other screen is switched off.
  Mali's own Vulkan display support only reaches the first screen, so EasyPSP presents the frames itself: GPU images
  imported from DRM buffers, page flips in their own thread, no CPU copies.
- **Mali Vulkan driver while a game runs**, Panfrost back when you quit (the same stack as EasyCast).
- **Handheld controls out of the box**: the RG DS Plus buttons are mapped, Select + Menu quits, Select + Up / Down sets
  the screen brightness, Menu opens the emulator menu. Combos do not also press their buttons alone.
- **Swap D-Pad and analog stick** as a saved setting.
- **Restarts done by the launcher**: switching the graphics backend or the screen starts a fresh process, so Vulkan
  never gets stuck on OpenGL.
- **Updates inside the emulator**: Settings → System → EasyPSP.

## New settings

| Setting | Where | What it does |
|---|---|---|
| **Screen (Top / Bottom)** | Settings → Graphics → EasyPSP | Which screen shows the game; the other one is switched off to save power. EasyPSP restarts to apply it. |
| **Screen brightness** | Settings → Graphics → EasyPSP, or Select + Up / Down in game | Brightness of the screen the game is on, 0 = the system value. The handheld's menu keeps its own brightness. |
| **Swap D-Pad and analog stick** | Settings → Controls | The stick works as the D-Pad and the D-Pad as the stick. |
| **Check for updates** | Settings → System → EasyPSP | Finds and installs a new EasyPSP version from GitHub. |

## Compatibility

EasyPSP needs a handheld with the **Rockchip RK3566 or RK3568** chip (four Cortex-A55 cores, Mali-G52 GPU), 64-bit
Linux, and a GPU kernel module built for the kernel of the system. Only one device and one system are tested.

| Status | What |
|---|---|
| ✅ Tested | Anbernic RG DS Plus (RK3568, two screens) on BananaOS 0.5.3.4 (kernel 7.2.7-banana) |
| ⚠️ Same chip, untested | RG DS, RG353 series, RG503, RG ARC, Powkiddy RGB30 / RK2023 / RGB10 Max 3 / RGB20SX / X55, Miyoo Flip, MiniLoong Pocket One (see the [EasyCast list](https://github.com/artmanul7/easycast#handhelds-with-the-same-chip-might-work)) |
| ⚠️ One screen | untested |
| ❌ Different chip | RK3326, RK3399, RK3588, Allwinner, Snapdragon, MediaTek: another GPU, the bundled driver does not fit |

The installer and the launcher check one hard rule: the GPU module must be built for exactly the running kernel
(`uname -r`). **Ports are welcome**: all sources are open, and results from other handhelds are welcome in Issues.

## Requirements

- BananaOS with kernel 7.2.7-banana on an RK3566/RK3568 handheld (tested only on the RG DS Plus), Wi-Fi for installing and updates
- About 150 MB free on the card
- PSP games as `.iso`, `.cso` or `.chd` in `/data/roms/psp`

## Known issues

- Select + Menu quits only during a game; in PPSSPP's own menus use *Exit*.
- Press Select first: if Menu is pressed first, the emulator menu opens.
- Heavy 3D games (God of War) run below 60 FPS at 1x; use frame skipping in PPSSPP's settings if you prefer.
- One-screen devices are untested.

## Install

1. Connect the handheld to Wi-Fi.
2. Download **[Install-EasyPSP.sh](https://github.com/artmanul7/easypsp/releases/latest/download/Install-EasyPSP.sh)**
   and put it into the **Apps** folder: `/data/store/tiles` (on the BananaOS network share: `store\tiles`).
3. On the handheld open **Apps** and start **Install-EasyPSP**. It downloads the latest release, checks its SHA-256
   and the kernel, installs to `/data/banana/easypsp`, adds the **EasyPSP** tile and removes itself.
4. Put your games into `/data/roms/psp`.

## Update

Settings → System → EasyPSP → **Check for updates** → **Install the new version** → **Restart EasyPSP**.
EasyPSP also checks once a day at start (when online) and shows a short notice if a new version exists.
Your settings and saves (`/data/banana/easypsp/userdata`) are never touched by updates; the previous version is kept
until the next update.

## Remove

Delete the folder `/data/banana/easypsp` and the tile `/data/store/tiles/EasyPSP.sh`.

## Controls (RG DS Plus)

| Buttons | Action |
|---|---|
| A B X Y | Circle, Cross, Triangle, Square (by position, like on a PSP) |
| L, R | L, R |
| Menu | Emulator menu |
| Select + Menu | Quit |
| Select + Up / Down | Screen brightness |

Change anything in Settings → Controls → Control mapping.

## Sources and licences

- PPSSPP with all EasyPSP changes: **[this repository, branch `easypsp`](https://github.com/artmanul7/easypsp/tree/easypsp)** (GPL-2.0 or later).
  The changes are marked `EASYPSP_` in the code; the original README is [README-ppsspp.md](README-ppsspp.md).
- SDL2 (patched), `mali_kbase` module (GPL-2.0), libmali hook and kernel build inputs: `easypsp-x.y.z-gpl-sources.tar.gz` in every release.
- Mali userspace driver: binary from Arm/Rockchip (via [ROCKNIX/libmali](https://github.com/ROCKNIX/libmali)), unchanged, under the Mali EULA included in the package.
- PPSSPP is made by Henrik Rydgård and contributors. BananaOS by [JorreFog](https://github.com/JorreFog/BananaLinux).

EasyPSP is a fan project, not affiliated with Sony. PSP is a trademark of Sony Interactive Entertainment. Bring your own games.

## About

AI was used during development. Bug reports and pull requests are welcome.

**Keywords:** PSP emulator, PPSSPP fork, Vulkan, Mali G52, Bifrost, libmali, mali_kbase, Panfrost, RK3566, RK3568,
Rockchip, Anbernic RG DS Plus, RG DS, RG353, RG503, Powkiddy RGB30, Miyoo Flip, dual screen handheld, DRM KMS,
ARM64, Cortex-A55, BananaOS, retro handheld.

---

### По-русски

EasyPSP это готовый PPSSPP для BananaOS на Anbernic RG DS Plus: с подходящим видеодрайвером (Mali Vulkan вместо
Panfrost), выводом на выбранный экран, кнопками приставки, яркостью и обновлениями. Это только начало: сам эмулятор
пока не оптимизировался. В планах найти горячие места на реальных играх и ускорить их, как было с EasyCast. **Установка:** подключите Wi-Fi,
скачайте `Install-EasyPSP.sh`, положите в папку Apps (`/data/store/tiles`) и запустите его из Apps. Игры кладите в
`/data/roms/psp`. **Обновление:** Settings → System → EasyPSP. **Удаление:** удалите папку `/data/banana/easypsp`
и ярлык `EasyPSP.sh`. Новые настройки: выбор экрана и яркость (Settings → Graphics → EasyPSP, или Select + вверх/вниз),
смена местами крестовины и стика (Settings → Controls). Select + Menu закрывает эмулятор, Menu открывает его меню.

Буду рад, если разработчики своих прошивок возьмут EasyPSP себе как готовый эмулятор. Всё открыто.

При разработке был использован ИИ.
