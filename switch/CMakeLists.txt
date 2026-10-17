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
    ${R}/gpu/renderer.cpp
    ${R}/gpu/renderer/copy.cpp
    ${R}/gpu/renderer/debug.cpp
    ${R}/gpu/renderer/draw.cpp
    ${R}/gpu/renderer/memexport.cpp
    ${R}/gpu/renderer/pipelines.cpp
    ${R}/gpu/renderer/postprocess.cpp
    ${R}/gpu/renderer/queries.cpp
    ${R}/gpu/renderer/resolve.cpp
    ${R}/gpu/renderer/shaders.cpp
    ${R}/gpu/renderer/targets.cpp
    ${R}/gpu/renderer/textures.cpp
    ${R}/gpu/shader/translator.cpp
    ${R}/gpu/texture.cpp
    ${R}/input/input.cpp
    ${R}/debug/first_calls.cpp
    ${R}/debug/state_hash.cpp
    ${R}/hooks/block_copy.cpp
    ${R}/hooks/campaign.cpp
    ${R}/hooks/create_file.cpp
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
    # Switch platform: the window on SDL_GPU's deko3d backend, shaders
    # compiled for it with UAM, no frame captures, the profiler (POSIX
    # signals) left out, and the fibers' context switch.
    platform/window_switch.cpp
    platform/shaders_switch.cpp
    platform/icon_switch.cpp
    platform/sampler_switch.cpp
    platform/capture_null.cpp
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
# UAM, deko3d's shader compiler, for the renderer's shaders
# (platform/shaders_switch.cpp): the Switch build of it Halo CE's deko3d port
# makes (nx-mod/haloce-nx, patched to compile from several threads), as one
# object with its symbols renamed (tools/h3-uam-object).
set(H3_UAM_SOURCE_DIR "${H3_ROOT}/../haloce-nx/build/switch/third_party/uam" CACHE PATH "UAM's patched source tree")
set(H3_UAM_BUILD_DIR "${H3_ROOT}/../haloce-nx/build/switch/uam" CACHE PATH "The folder holding UAM's Switch libuam.a and meson/")
if (NOT EXISTS "${H3_UAM_BUILD_DIR}/libuam.a" OR NOT EXISTS "${H3_UAM_SOURCE_DIR}/source/compiler_iface.h")
    message(FATAL_ERROR "Rendering needs UAM built for the Switch: build haloce-nx's Switch port, or set H3_UAM_SOURCE_DIR and H3_UAM_BUILD_DIR")
endif()
set(H3_UAM_OBJECT "${CMAKE_CURRENT_BINARY_DIR}/uam_compiler.o")
add_custom_command(
    OUTPUT "${H3_UAM_OBJECT}"
    COMMAND "${H3_ROOT}/tools/h3-uam-object" "${CMAKE_CXX_COMPILER}" "${CMAKE_LINKER}" "${CMAKE_NM}" "${CMAKE_OBJCOPY}"
            "${H3_UAM_SOURCE_DIR}" "${H3_UAM_BUILD_DIR}" "${H3_UAM_OBJECT}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/uam/uam_compile.cpp" "${H3_ROOT}/tools/h3-uam-object" "${H3_UAM_BUILD_DIR}/libuam.a"
    COMMENT "UAM shader compiler object")
add_custom_target(h3_uam_object DEPENDS "${H3_UAM_OBJECT}")
add_dependencies(halo3-nx h3_uam_object)
target_link_libraries(halo3-nx PRIVATE "${H3_UAM_OBJECT}")

find_package(SDL3 REQUIRED CONFIG)
target_link_libraries(halo3-nx PRIVATE H3RecompLib H3Config H3NativeCache H3NativeTags
    ${H3_MODULES} ${H3_MODULE_STUBS} SDL3::SDL3-static)

# deko3d's debug build: it checks every call and reports what was wrong
# through SDL_GPU's debug callback (debug mode is on with it) instead of
# aborting with a bare result code. Whole-archive, so its objects stand in
# for the release library SDL3 pulls in.
option(H3_DEKO3D_DEBUG "Link libdeko3dd and create the GPU device in debug mode" OFF)
if (H3_DEKO3D_DEBUG)
    target_link_options(halo3-nx PRIVATE -Wl,--whole-archive -ldeko3dd -Wl,--no-whole-archive)
    # Only the file that creates the device: a target-wide define recompiled everything.
    set_source_files_properties(platform/window_switch.cpp PROPERTIES COMPILE_DEFINITIONS H3_DEKO3D_DEBUG=1)
endif()

# FFmpeg with the xmaframes decoder, from tools/build_ffmpeg_switch.sh.
set(H3_FFMPEG_DIR ${H3_ROOT}/build/ffmpeg-switch)
if (NOT EXISTS ${H3_FFMPEG_DIR}/lib/libavcodec.a)
    message(FATAL_ERROR "XMA audio needs FFmpeg for the Switch: run tools/build_ffmpeg_switch.sh first")
endif()
target_include_directories(halo3-nx BEFORE PRIVATE ${H3_FFMPEG_DIR}/include)
target_link_libraries(halo3-nx PRIVATE ${H3_FFMPEG_DIR}/lib/libavcodec.a ${H3_FFMPEG_DIR}/lib/libavutil.a m)
# libjpeg (portlibs) writes the forwarder icon (platform/icon_switch.cpp).
# Only there: portlibs also carries FFmpeg headers, which must not stand in for ours.
set_source_files_properties(platform/icon_switch.cpp PROPERTIES INCLUDE_DIRECTORIES ${DEVKITPRO}/portlibs/switch/include)
target_link_libraries(halo3-nx PRIVATE ${DEVKITPRO}/portlibs/switch/lib/libjpeg.a)

nx_generate_nacp(halo3-nx.nacp NAME "Halo 3" AUTHOR "nx-mod" VERSION "0.1")
nx_create_nro(halo3-nx NACP halo3-nx.nacp)
