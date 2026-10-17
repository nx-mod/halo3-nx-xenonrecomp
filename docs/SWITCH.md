# halo3-nx: the Switch port

This branch (`switch`) ports the runtime to the Nintendo Switch as a homebrew
NRO. `main` follows upstream ([halo-3-recomp](https://github.com/george0815/halo-3-recomp));
everything Switch-specific lives here and is merged forward from it.

The game code needs no porting: XenonRecomp's output reaches the CPU through
SIMDe, which turns the x86 vector intrinsics into NEON on AArch64. What changes
is the runtime around it.

## Plan, in order

1. **Build.** devkitA64 + libnx toolchain file and presets; XenonRecomp and the
   tools still build on the host. The recompiled sources (~22,400 functions)
   are the heavy part of the build.
2. **Guest memory** (`memory.cpp`). The PC reserves 4 GiB and maps the 512 MiB
   of physical memory at 0xA0000000, 0xC0000000 and 0xE0000000 through a
   memfd. On the Switch: reserve 4 GiB in the alias region (it is 64 GiB on a
   39-bit address space) with `virtmemReserve`, back only what the game
   allocates, and alias the physical pages into the three windows with
   `svcMapMemory` from heap memory (the technique haloce-nx uses). Guest
   accesses stay `base + address`, unchecked. Real RAM: ~0.5-1 GiB.
   Proven on hardware by `switch/probe` (h3probe, 2026-10-16):
   - the 4 GiB window: `virtmemFindAslr` + `virtmemAddReservation` in the
     ASLR region (64 GiB); `svcMapMemory` only maps into the stack region
     (under 2 GiB), so it is not used;
   - physical memory: shared memory (`svcCreateSharedMemory`) may be mapped
     more than once, and one object mapped at 0xA0000000, 0xC0000000 and
     0xE0000000 is one memory. It is two objects, the first page and the
     rest, because the 0xE0000000 view starts 4 KiB in and a view maps an
     object from its start. hbloader gives a program nearly all memory as
     heap, so shared memory fails (0x10801, limit reached) until the unused
     top of the heap is given back (`svcSetHeapSize`, newlib's
     `fake_heap_end` lowered to match); the heap is grown back before exit;
   - virtual memory: heap aliased into the window with
     `svcMapProcessCodeMemory` and made read-write with
     `svcSetProcessMemoryPermission` (one alias per heap page; a second is
     refused, 0xd401).
3. **Kernel.** Threads onto libnx threads: the game's six hardware threads on
   the three application cores. Waits, events and timers on libnx's
   primitives; file I/O from `sdmc:/halo3-nx/` (disc read-only, `hdd/`
   writable).
4. **Graphics.** The renderer is written against SDL_GPU (Vulkan), and runs
   unchanged on sdl3-nx's deko3d backend. Its Vulkan GLSL is compiled on the
   device with UAM (switch/uam, the Switch build of it from haloce-nx), after
   switch/platform/shaders_switch.cpp renumbers SDL_GPU's descriptor sets as
   deko3d's per-kind bindings; DKSH is cached in `shadercache/`. The window
   (switch/platform/window_switch.cpp) blits each finished frame onto the
   swapchain, letterboxed. H3_RENDER=0 goes back to the text console.
5. **Audio and input.** audren for the XAudio driver output; FFmpeg's XMA
   decoder cross-built for AArch64; HID pads for XInput.
6. **Speed.** Overclock, low internal resolution, then the hot functions as
   native code (upstream's phase 6 does this anyway).

## Game files

The addresses fit one executable: retail `default.xex` 0.0.0.42 (disc, no
title update), SHA-1 `927a8c49062a55179d84116e33f8a5a0b3db13c1`
(docs/DUMPING.md).

## XenonRecomp

The submodule is [nx-mod/XenonRecomp](https://github.com/nx-mod/XenonRecomp),
branch `halo3-nx`: upstream's 12 unpublished instructions done again (lvxl128,
stvlxl128, vandc, vsel128, vctuxs, vpkuwus, vpkswss, lhbrx, frsqrte, eqv,
mulhdu, dcbst), blrl, the vupkd3d128/vpkd3d128 formats from the ReXGlue SDK,
and `Image::dataSize`. The retail executable recompiles with no unrecognized
instruction and no trap left in the output.

`H3_TOOLS_ONLY=ON` (`cmake --preset release -DH3_TOOLS_ONLY=ON` once) builds
the recompiler and tools without the PC runtime, for a host that only
recompiles.
