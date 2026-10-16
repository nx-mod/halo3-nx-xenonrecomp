// One lock around every entry into the SD card driver, as haloce-nx found it
// needs (port/switch/host/host.h, host_sd_lock): libnx's sdmc: device is not
// safe to enter from several threads at once. Here the game's I/O threads,
// the main thread and every thread's log lines all reach it, and with them
// in it together a file's writes landed in the middle of halo3-err.log and
// the file itself stayed empty (the game's cache drive check then reported a
// dirty disc).
//
// The lock goes in at the device rather than at the callers: sdmc:'s table
// is copied with each entry wrapped and registered again under its own name,
// which replaces it in place, so stdio, std::filesystem, the logs and SDL
// all take it. This runs as the first constructor, before anything opens a
// file; libnx mounts the card before constructors run.

#include <switch.h>
#include <sys/iosupport.h>

#include <cstring>

namespace
{
    RMutex g_lock;  // zero is unlocked: usable before any constructor
    devoptab_t g_real;
    devoptab_t g_locked;

    struct Guard
    {
        Guard() { rmutexLock(&g_lock); }
        ~Guard() { rmutexUnlock(&g_lock); }
    };

    template <auto Field, typename Function>
    struct Locked;

    template <auto Field, typename Result, typename... Args>
    struct Locked<Field, Result (*)(Args...)>
    {
        static Result Call(Args... args)
        {
            Guard guard;
            return (g_real.*Field)(args...);
        }
    };

#define H3_LOCK(name)                                                                   \
    if (g_real.name)                                                                    \
        g_locked.name = Locked<&devoptab_t::name, decltype(devoptab_t::name)>::Call

    __attribute__((constructor(101))) void LockSdCard()
    {
        const devoptab_t* sdmc = GetDeviceOpTab("sdmc");
        if (!sdmc)
            return;
        g_real = *sdmc;
        g_locked = g_real;
        H3_LOCK(open_r);
        H3_LOCK(close_r);
        H3_LOCK(write_r);
        H3_LOCK(read_r);
        H3_LOCK(seek_r);
        H3_LOCK(fstat_r);
        H3_LOCK(stat_r);
        H3_LOCK(link_r);
        H3_LOCK(unlink_r);
        H3_LOCK(chdir_r);
        H3_LOCK(rename_r);
        H3_LOCK(mkdir_r);
        H3_LOCK(diropen_r);
        H3_LOCK(dirreset_r);
        H3_LOCK(dirnext_r);
        H3_LOCK(dirclose_r);
        H3_LOCK(statvfs_r);
        H3_LOCK(ftruncate_r);
        H3_LOCK(fsync_r);
        H3_LOCK(chmod_r);
        H3_LOCK(fchmod_r);
        H3_LOCK(rmdir_r);
        H3_LOCK(lstat_r);
        H3_LOCK(utimes_r);
        H3_LOCK(fpathconf_r);
        H3_LOCK(pathconf_r);
        H3_LOCK(symlink_r);
        H3_LOCK(readlink_r);
        AddDevice(&g_locked);
    }

#undef H3_LOCK
}
