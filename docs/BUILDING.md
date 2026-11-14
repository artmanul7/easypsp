# Building EasyPSP

EasyPSP is PPSSPP v1.20.4 plus the changes in this branch (marked `EASYPSP_` in the code). It is cross-compiled on
a Linux PC (WSL is fine) for aarch64.

## Toolchain and target libraries

- Bootlin `aarch64--glibc--stable-2025.08-1` (GCC 14.3); the result needs glibc 2.38 or newer (BananaOS has 2.41).
- SDL 2.32.10 built with Vulkan and the BananaOS display patches (sources in the release asset
  `easypsp-x.y.z-gpl-sources.tar.gz`), its headers, plus libdrm and GBM headers.
- `libmali.so.1` (g29p1) to link the OpenGL part; at run time only Vulkan is used.
- GLES/EGL headers (Khronos).

## Configure

```bash
git clone --branch easypsp --recurse-submodules https://github.com/artmanul7/easypsp.git
cmake -S easypsp -B build -DCMAKE_SYSTEM_NAME=Linux -DCMAKE_SYSTEM_PROCESSOR=aarch64 \
  -DCMAKE_C_COMPILER=aarch64-buildroot-linux-gnu-gcc -DCMAKE_CXX_COMPILER=aarch64-buildroot-linux-gnu-g++ \
  -DCMAKE_C_FLAGS="-mcpu=cortex-a55 -I<sdl deps>/include -I<sdl deps>/include/libdrm -I<gles headers>" \
  -DCMAKE_CXX_FLAGS="<same>" \
  -DCMAKE_EXE_LINKER_FLAGS="-L<target libs> -Wl,--allow-shlib-undefined" \
  -DCMAKE_BUILD_TYPE=Release -DSDL2_DIR=<SDL cmake config> -DCMAKE_FIND_PACKAGE_PREFER_CONFIG=ON \
  -DARM64=ON -DUSING_EGL=OFF -DUSING_GLES2=ON -DOPENGL_LIBRARIES=<path>/libmali.so.1 \
  -DVULKAN=ON -DUSE_VULKAN_DISPLAY_KHR=ON -DUSING_X11_VULKAN=OFF -DUSE_WAYLAND_WSI=OFF \
  -DUSE_DISCORD=OFF -DUSE_MINIUPNPC=OFF -DUSE_SYSTEM_LIBPNG=OFF -DUSE_SYSTEM_ZSTD=OFF -DUSE_SYSTEM_LIBZIP=OFF \
  -DUSE_SYSTEM_SNAPPY=OFF -DUSE_SYSTEM_FREETYPE=OFF -DUSE_SYSTEM_LIBCHDR=OFF -DUSE_SYSTEM_FFMPEG=OFF \
  -DHEADLESS=OFF -DUNITTEST=OFF -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=ON -DCMAKE_DISABLE_FIND_PACKAGE_SDL2_ttf=ON
cmake --build build -j
```

Keep pkg-config away from the host libraries (`PKG_CONFIG_LIBDIR` set to an empty folder). After changing options,
delete the build folder: CMake keeps old values in its cache.

## The GPU kernel module

`mali_kbase.ko` must be built for exactly the kernel of the system. EasyPSP uses the same module as EasyCast;
the recipe is in [EasyCast's BUILDING.md](https://github.com/artmanul7/easycast/blob/easycast/docs/BUILDING.md)
and the sources in the release asset `easypsp-x.y.z-gpl-sources.tar.gz`.

## Release package

`easypsp-x.y.z-bananaos-aarch64.tar.gz` holds `runtime/<version>/` (the stripped `PPSSPPSDL`, `assets/` from the
build folder, the driver `lib/`, `mali_kbase.ko`, the Python launcher and helpers, licences), `tiles/EasyPSP.sh` and
`media/easypsp.png`. The installer and the updater take the newest GitHub release that has this file and its
`.sha256`.
