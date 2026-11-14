# Changelog

## 0.0.1

First public version: PPSSPP 1.20.4 for the Anbernic RG DS Plus on BananaOS.

- Vulkan on the Mali G52 driver (Mali userspace driver + kernel module while a game runs, Panfrost back at exit).
- Picture on the top or bottom screen: own Vulkan swapchain on DRM/KMS (Settings → Graphics → EasyPSP → Screen);
  the other screen is switched off.
- Screen brightness: Settings → Graphics → EasyPSP, or Select + Up / Down in game.
- Buttons of the RG DS Plus mapped by position; Menu = emulator menu, Select + Menu = quit.
- Combos (two buttons) no longer also press their buttons alone.
- Swap D-Pad and analog stick (Settings → Controls), saved.
- Restart requests (graphics backend, screen) start a fresh process: Vulkan no longer falls back to OpenGL after a switch.
- Install with one file (`Install-EasyPSP.sh`), updates inside the emulator (Settings → System → EasyPSP).
