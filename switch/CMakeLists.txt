# The Nintendo Switch build: the recompiled game and the runtime as one NRO
# (docs/SWITCH.md). Configure with devkitPro's toolchain file:
#
#   cmake -S . -B build-switch -G Ninja \
#     -DCMAKE_TOOLCHAIN_FILE=$DEVKITPRO/cmake/Switch.cmake \
#     -DCMAKE_PREFIX_PATH=<sdl3-nx prefix> -DCMAKE_BUILD_TYPE=Release
#
# The recompiled sources come from a host run of tools/recompile.sh.

enable_language(ASM)  # platform/co_switch.S

set(H3_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/..)
set(H3_XENON ${H3_ROOT}/thirdparty/XenonRecomp)
set(THIRDPARTY_ROOT ${H3_XENON}/thirdparty)
set(H3_PPC_DIR ${H3_ROOT}/H3RecompLib/ppc)

if (NOT EXISTS ${H3_PPC_DIR}/ppc_config.h)
    message(FATAL_ERROR "No recompiled code in H3RecompLib/ppc: run tools/recompile.sh on the host first")
endif()

# XenonUtils, the part the runtime uses: the XEX loader (LZX, AES, and the
# patcher, which holds the LZX entry point; memory-mapped files are read into
# memory on the Switch), and the headers the recompiled code includes. Not the
# disassembler, which only the recompiler needs.
add_library(XenonUtils STATIC
    ${H3_XENON}/XenonUtils/xex.cpp
    ${H3_XENON}/XenonUtils/image.cpp
    ${H3_XENON}/XenonUtils/xex_patcher.cpp
    ${H3_XENON}/XenonUtils/memory_mapped_file.cpp
    ${THIRDPARTY_ROOT}/libmspack/libmspack/mspack/lzxd.c
    ${THIRDPARTY_ROOT}/tiny-AES-c/aes.c
)
target_include_directories(XenonUtils
    PUBLIC ${H3_XENON}/XenonUtils ${THIRDPARTY_ROOT}/simde
    PRIVATE ${THIRDPARTY_ROOT}/libmspack/libmspack/mspack ${THIRDPARTY_ROOT}/tiny-AES-c
        ${THIRDPARTY_ROOT}/TinySHA1)
target_compile_definitions(XenonUtils PRIVATE NOMINMAX)

# The recompiled game and its DLLs (sets H3_MODULES, H3_MODULE_STUBS,
# H3_MODULE_REGISTRY).
add_subdirectory(${H3_ROOT}/H3RecompLib H3RecompLib)

# Shared with the PC build.
add_library(H3Config STATIC ${H3_ROOT}/H3Recomp/config/settings.cpp)
target_include_directories(H3Config PUBLIC ${H3_ROOT}/H3Recomp)
add_subdirectory(${H3_ROOT}/H3Native H3Native)

file(GLOB_RECURSE H3_NATIVE_SOURCES CONFIGURE_DEPENDS ${H3_ROOT}/H3Native/*.cpp)
list(FILTER H3_NATIVE_SOURCES EXCLUDE REGEX "/H3Native/(cache/cache_file\\.cpp|tags/generated/)")

set(R ${H3_ROOT}/H3Recomp)
add_executable(halo3-nx
    # Shared runtime sources.
    ${R}/main.cpp
    ${R}/memory.cpp
    ${R}/gpu/command_processor.cpp
    ${R}/input/input.cpp
    ${R}/debug/first_calls.cpp
    ${R}/debug/state_hash.cpp
    ${R}/hooks/block_copy.cpp
    ${R}/hooks/campaign.cpp
    ${R}/hooks/fibers.cpp
    ${R}/hooks/random_seed.cpp
    ${R}/hooks/tick_rate.cpp
    ${R}/hooks/xma.cpp
    ${R}/kernel/imports.cpp
    ${R}/kernel/module.cpp
    ${R}/kernel/modules.cpp
    ${R}/kernel/objects.cpp
    ${R}/kernel/thread.cpp
    ${R}/kernel/variables.cpp
    ${R}/kernel/vfs.cpp
    ${R}/kernel/xam.cpp
    ${R}/kernel/xam_user.cpp
    ${R}/kernel/xam_content.cpp
    ${R}/kernel/xam_notify.cpp
    ${R}/kernel/xnet.cpp
    ${R}/apu/audio_system.cpp
    ${R}/apu/xma_decoder.cpp
    ${R}/debug/write_watch.cpp
    ${R}/kernel/xboxkrnl.cpp
    ${R}/kernel/xboxkrnl_audio.cpp
    ${R}/kernel/xboxkrnl_crypt.cpp
    ${R}/kernel/xboxkrnl_io.cpp
    ${R}/kernel/xboxkrnl_memory.cpp
    ${R}/kernel/xboxkrnl_modules.cpp
    ${R}/kernel/xboxkrnl_objects.cpp
    ${R}/kernel/xboxkrnl_rtl.cpp
    ${R}/kernel/xboxkrnl_strings.cpp
    ${R}/kernel/xboxkrnl_threading.cpp
    ${R}/kernel/xboxkrnl_video.cpp
    ${R}/video/bink_view.cpp
    ${H3_NATIVE_SOURCES}
    ${H3_PPC_DIR}/import_stubs.cpp
    ${H3_MODULE_REGISTRY}
    # Switch platform: the window without a GPU device, no host rendering
    # yet (the command processor still consumes the ring), the profiler
    # (POSIX signals) left out, and the fibers' context switch.
    platform/window_switch.cpp
    platform/renderer_null.cpp
    platform/threads_switch.cpp
    platform/debug_null.cpp
    platform/env_switch.cpp
    platform/sd_lock_switch.cpp
    platform/co_switch.S
)

target_include_directories(halo3-nx PRIVATE ${R} ${THIRDPARTY_ROOT}/xxHash)
# Host threads: stack, priority, cores and a detach that cannot throw (threads_switch.cpp).
target_link_options(halo3-nx PRIVATE -Wl,--wrap=pthread_create -Wl,--wrap=pthread_detach -Wl,--wrap=abort)
# The DLLs each recompile their own copy of the save/restore helpers
# (__savegprlr_14...), identical code under the same names; any one will do.
# UnleashedRecomp-NX's Switch toolchain links this way too.
target_link_options(halo3-nx PRIVATE -Wl,--allow-multiple-definition)
find_package(SDL3 REQUIRED CONFIG)
target_link_libraries(halo3-nx PRIVATE H3RecompLib H3Config H3NativeCache H3NativeTags
    ${H3_MODULES} ${H3_MODULE_STUBS} SDL3::SDL3-static)

# FFmpeg with the xmaframes decoder, from tools/build_ffmpeg_switch.sh.
set(H3_FFMPEG_DIR ${H3_ROOT}/build/ffmpeg-switch)
if (NOT EXISTS ${H3_FFMPEG_DIR}/lib/libavcodec.a)
    message(FATAL_ERROR "XMA audio needs FFmpeg for the Switch: run tools/build_ffmpeg_switch.sh first")
endif()
target_include_directories(halo3-nx BEFORE PRIVATE ${H3_FFMPEG_DIR}/include)
target_link_libraries(halo3-nx PRIVATE ${H3_FFMPEG_DIR}/lib/libavcodec.a ${H3_FFMPEG_DIR}/lib/libavutil.a m)

nx_generate_nacp(halo3-nx.nacp NAME "Halo 3" AUTHOR "nx-mod" VERSION "0.1")
nx_create_nro(halo3-nx NACP halo3-nx.nacp)
