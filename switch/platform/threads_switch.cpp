// Host threads on the Switch, through link-time wraps of pthread_create and
// pthread_detach (-Wl,--wrap=..., switch/CMakeLists.txt). What nfsmw-nx
// measured on hardware (docs/platform-notes.md, "Threads") decides it:
//
// - Stack: std::thread gets the default pthread stack, far smaller than
//   recompiled code needs; 8 MiB, as UnleashedRecomp-NX gives its guest
//   threads and the PC has.
// - Priority: only 0x3B is time-sliced. At any other priority a busy thread
//   keeps its core and starves the others at the same priority, even with
//   cores idle, and the game busy-waits. Every thread starts at 0x3B.
// - Cores: a thread may run on any of the three application cores; the
//   preferred core goes round so they start spread out (a preferred core
//   does not pin, and pinning measured worse).
// - std::thread::detach() can throw on Horizon when the thread has already
//   finished, which ends the program; a failed detach is ignored instead.

#include <pthread.h>
#include <switch.h>

#include <atomic>
#include <cerrno>
#include <cstddef>
#include <new>

namespace h3::platform
{
    void RegisterSampledThread(Handle thread);  // sampler_switch.cpp (H3_SAMPLE)
}

namespace
{
    constexpr size_t kMinStackSize = 8 * 1024 * 1024;
    constexpr u32 kTimeSlicedPriority = 0x3B;
    constexpr u32 kApplicationCores = 0x7;

    std::atomic<u32> g_nextCore{ 0 };

    struct Start
    {
        void* (*routine)(void*);
        void* argument;
        s32 preferredCore;
    };

    void* RunThread(void* parameter)
    {
        const Start start = *static_cast<Start*>(parameter);
        delete static_cast<Start*>(parameter);
        svcSetThreadPriority(CUR_THREAD_HANDLE, kTimeSlicedPriority);
        svcSetThreadCoreMask(CUR_THREAD_HANDLE, start.preferredCore, kApplicationCores);
        h3::platform::RegisterSampledThread(threadGetCurHandle());
        return start.routine(start.argument);
    }
}

extern "C"
{
    int __real_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg);
    int __real_pthread_detach(pthread_t thread);

    int __wrap_pthread_create(pthread_t* thread, const pthread_attr_t* attr, void* (*start)(void*), void* arg)
    {
        pthread_attr_t sized;
        if (attr)
            sized = *attr;
        else
            pthread_attr_init(&sized);
        size_t stackSize = 0;
        pthread_attr_getstacksize(&sized, &stackSize);
        if (stackSize < kMinStackSize)
            pthread_attr_setstacksize(&sized, kMinStackSize);

        auto* trampoline = new (std::nothrow) Start{ start, arg, s32(g_nextCore.fetch_add(1) % 3) };
        int result = trampoline ? __real_pthread_create(thread, &sized, RunThread, trampoline) : EAGAIN;
        if (result != 0)
            delete trampoline;
        if (!attr)
            pthread_attr_destroy(&sized);
        return result;
    }

    int __wrap_pthread_detach(pthread_t thread)
    {
        __real_pthread_detach(thread);
        return 0;
    }
}
