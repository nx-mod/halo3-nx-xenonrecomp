# halo3-nx

Halo 3 (Xbox 360), recompiled to run natively on the Nintendo Switch as a homebrew NRO.

A fork of [halo-3-recomp](https://github.com/george0815/halo-3-recomp). The `switch` branch
(the default) holds the port, and `main` follows upstream.

**No game data is included.** You need your own dump of the retail disc (`default.xex` 0.0.0.42,
no title update). See [docs/DUMPING.md](docs/DUMPING.md).

## Status

Work in progress, and not playable yet. Boot gets through the cache and save setup, the audio
engine, and the start of rendering. Shaders compile on the console. The first frames are next.

## How it works

- [xenonrecomp-nx](https://github.com/nx-mod/xenonrecomp-nx) turns the game's PowerPC code (and
  the DLLs it loads) into C++, which devkitA64 compiles for AArch64. SIMDe maps the vector code
  to NEON.
- The runtime (`H3Recomp/`, `switch/`) provides what the 360 did:
  - guest memory, mapped in the Switch's address space;
  - kernel and XAM imports, threads, file I/O from `sdmc:/halo3-nx/`;
  - XMA audio through FFmpeg;
  - input through SDL.
- Rendering uses the game's SDL_GPU renderer on [sdl3-nx](https://github.com/nx-mod/sdl3-nx)'s
  deko3d backend. Shaders are compiled on the console with UAM and cached on the SD card.

The design notes and the porting plan are in [docs/SWITCH.md](docs/SWITCH.md).

## Building

You need:

- devkitPro: devkitA64, libnx, switch-cmake;
- [sdl3-nx](https://github.com/nx-mod/sdl3-nx), installed into a prefix;
- FFmpeg for the Switch: run `tools/build_ffmpeg_switch.sh`;
- UAM, built for the Switch as in haloce-nx;
- a host build of the tools, to recompile.

```sh
git submodule update --init --recursive

# Host: recompile (default.xex and the disc files in private/)
cmake --preset release -DH3_TOOLS_ONLY=ON
tools/recompile.sh

# Switch
cmake -S . -B build-switch -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
  -DCMAKE_PREFIX_PATH=<sdl3-nx prefix>
cmake --build build-switch    # -> build-switch/switch/halo3-nx.nro
```

## Installing

- Copy the NRO to `sdmc:/switch/halo3-nx/halo3-nx.nro`.
- Put `default.xex` and the disc's files in `sdmc:/halo3-nx/`.

The game writes these beside them:

- `halo3.log` and `halo3-err.log`, the logs;
- `hdd/`, the cache partitions and saves;
- `shadercache/`, the compiled shaders.

`sdmc:/halo3-nx/env.txt` takes `H3_*` debug switches, one per line.

## Credits

- [halo-3-recomp](https://github.com/george0815/halo-3-recomp): george0815. It is the PC runtime
  this port builds on.
- [XenonRecomp](https://github.com/hedge-dev/XenonRecomp) and
  [Unleashed Recompiled](https://github.com/hedge-dev/UnleashedRecomp): hedge-dev.
- [Xenia](https://github.com/xenia-project/xenia), the reference for kernel and GPU behaviour.
- [deko3d](https://github.com/devkitPro/deko3d) and UAM: fincs. [devkitPro](https://devkitpro.org).
- The reference Switch ports: UnleashedRecomp-NX and nfsmw-nx.
