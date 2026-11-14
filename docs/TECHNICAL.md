# EasyPSP technical notes

## Files on the handheld

| Path | What |
|---|---|
| `/data/banana/easypsp/runtime/<version>/` | `PPSSPPSDL`, `assets/`, `lib/` (Mali, Vulkan loader, SDL), `mali_kbase.ko`, `run.py`, `easypsp_update.py`, `compat.py`, `ui.py`, `licenses/` |
| `/data/banana/easypsp/current`, `previous` | text files: the version the tile starts, the one before the last update |
| `/data/banana/easypsp/userdata/config/ppsspp/` | PPSSPP settings (`PSP/SYSTEM/ppsspp.ini`, `controls.ini`), saves, save states |
| `/data/banana/easypsp/userdata/logs/` | `run-*.log` (last 10 runs), `update-check.log` |
| `/data/store/tiles/EasyPSP.sh` | the tile; icon `/data/store/tiles/media/box/EasyPSP.png` |
| `/data/roms/psp` | games |

No symbolic links are used (the data partition is exFAT).

## What the launcher (run.py) does

1. Checks that `mali_kbase.ko` was built for the running kernel (`compat.py`), writes default settings on the first
   start (Vulkan, game folder `/data/roms/psp`) and, once, a `controls.ini` for the RG DS Plus
   (the old one is kept as `controls.ini.before-easypsp`).
2. Unloads Panfrost, loads `mali_kbase.ko`, sets the GPU governor to `performance` and the power policy to
   `coarse_demand` (the GPU powers down between frames).
3. Switches the other screen off (`bl_power = 4`), applies the saved brightness to the game screen.
4. Starts PPSSPP with `LD_LIBRARY_PATH` = the bundled `lib/`, `VK_ICD_FILENAMES` = the bundled `mali.json`,
   `SDL_VIDEODRIVER = kmsdrm`, `EASYPSP_DRM = 1`, `EASYPSP_RESTART_CODE = 75`.
5. If PPSSPP exits with code 75 (it asked for a restart), starts it again; if an update was installed meanwhile,
   starts the new version's launcher.
6. At exit: screens and (if EasyPSP set it) brightness back, Mali module unloaded, Panfrost back.

## Two screens: the DRM swapchain

The RG DS Plus has two DSI panels (DSI-1 = bottom, DSI-2 = top, 1024x768 each). Mali's `VK_KHR_display` only
reports the first DRM connector, so with the normal Vulkan path the game can only appear on the bottom screen.

EasyPSP (`Common/GPU/Vulkan/EasypspDrm.h`) replaces PPSSPP's Vulkan surface and swapchain function pointers
(`vkGetPhysicalDeviceSurface*`, `vkCreateSwapchainKHR`, `vkGetSwapchainImagesKHR`, `vkAcquireNextImageKHR`,
`vkQueuePresentKHR`, `vkDestroy*`). The renderer itself is unchanged:

- before `vkCreateInstance` it opens `/dev/dri/card0`, becomes DRM master and finds the connector, CRTC and mode of the
  chosen panel (otherwise the Mali driver takes the master first);
- the surface is a fake handle; it reports one format (`R8G8B8A8_UNORM`), FIFO, the panel size;
- the "swapchain" is three DRM dumb buffers exported as dma-buf and imported as `VkImage`s
  (`VK_EXT_external_memory_dma_buf`, `VK_EXT_image_drm_format_modifier`, linear modifier), each with a DRM framebuffer
  (`ABGR8888`); no CPU copies;
- acquire waits until the image has left the screen, then signals the semaphore with an empty submit;
- present submits a wait on the render semaphore with a fence and hands the image to the thread `EasyPSP-present`,
  which waits for the fence and page-flips (the first frame does a mode set). The render thread never waits for the flip.
- at exit the saved CRTC is restored and the master dropped.

The same idea is used by EasyCast for Flycast.

## Switches (environment)

| Variable | Effect |
|---|---|
| `EASYPSP_DRM=0` | use Mali's `VK_KHR_display` instead of the DRM swapchain (bottom screen only) |
| `EASYPSP_KEEP_OTHER_SCREEN=1` | do not switch the other screen off |
| `EASYPSP_LIB`, `EASYPSP_KO` | other driver library folder / kernel module |

Log lines to look for: `EASYPSP_DRM`, `EASYPSP_SCREEN`, `EASYPSP_COMPAT`, `EASYPSP_RESTART`, `EASYPSP_UPDATE`.

## Controls

SDL sees the RG DS Plus pad as `retrogame_joypad`. PPSSPP key codes (device 10): Cross 189 (bottom button),
Circle 190 (right), Triangle 191 (top), Square 188 (left), Select 196, Start 197, L 193, R 192, Menu 4,
D-Pad 19 to 22, stick axes 4000 to 4003. Combos in `controls.ini`: `Exit App = 10-196:10-4`,
`EasyPSP Brightness Up = 10-196:10-19`, `EasyPSP Brightness Down = 10-196:10-20`.
While a combo is fully held, its buttons do not act alone (`EASYPSP_CHORD` in `Core/ControlMapper.cpp`).

## Troubleshooting

- **Black screen / back to the menu at once**: see the last `userdata/logs/run-*.log`. `kernel ... this system runs ...`
  means the GPU module does not fit your kernel (see [BUILDING.md](BUILDING.md)).
- **Vulkan greyed out in the backend list**: PPSSPP remembered a failed start. Delete
  `userdata/config/ppsspp/PSP/SYSTEM/FailedGraphicsBackends.txt` if it exists and start again.
- **Wrong buttons**: delete `userdata/.controls-0.0.1` to get the default mapping again at the next start.
