// H3_SAMPLE=1: a host PC sampler, libdol-nx's (src/app/main.cpp), for every
// thread rather than one. Every 2 ms it pauses each registered thread in
// turn, reads its PC and LR and lets it go, and writes them to
// sdmc:/halo3-nx/pcsamples.bin as module offsets, resolved offline against
// the ELF. Records are three u32: thread index, pc, lr. A record
// (0xFFFFFFFF, milliseconds, 0) marks each second. Threads register as they
// start (threads_switch.cpp's trampoline); the main thread is index 0.

#include <switch.h>

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <thread>

namespace h3::platform
{
    namespace
    {
        constexpr int kMaxThreads = 96;
        std::atomic<Handle> g_threads[kMaxThreads];
        std::atomic<int> g_threadCount{ 0 };
        std::atomic<bool> g_running{ false };
    }

    void RegisterSampledThread(Handle thread)
    {
        int index = g_threadCount.fetch_add(1);
        if (index < kMaxThreads)
            g_threads[index].store(thread);
    }

    void StartSampler()
    {
        const char* setting = getenv("H3_SAMPLE");
        if (!setting || strcmp(setting, "0") == 0 || g_running.exchange(true))
            return;
        // The main thread first: the trampoline only sees the threads after it.
        int count = g_threadCount.load();
        for (int i = std::min(count, kMaxThreads - 1); i > 0; i--)
            g_threads[i].store(g_threads[i - 1].load());
        g_threads[0].store(envGetMainThreadHandle());
        g_threadCount.fetch_add(1);

        std::thread([]
        {
            const Handle self = threadGetCurHandle();
            MemoryInfo info{};
            u32 page = 0;
            svcQueryMemory(&info, &page, uintptr_t(&StartSampler));
            const uintptr_t base = info.addr;
            FILE* file = fopen("sdmc:/halo3-nx/pcsamples.bin", "wb");
            if (!file)
                return;
            fprintf(stderr, "[sample] sampling every thread to sdmc:/halo3-nx/pcsamples.bin\n");
            constexpr size_t kRecords = 4096;
            static uint32_t records[kRecords * 3];
            size_t used = 0;
            uint64_t secondStart = armGetSystemTick();
            uint64_t started = secondStart;
            auto put = [&](uint32_t a, uint32_t b, uint32_t c)
            {
                records[used * 3] = a;
                records[used * 3 + 1] = b;
                records[used * 3 + 2] = c;
                if (++used == kRecords)
                {
                    fwrite(records, sizeof(uint32_t) * 3, used, file);
                    fflush(file);
                    used = 0;
                }
            };
            for (;;)
            {
                int count = std::min(g_threadCount.load(), kMaxThreads);
                for (int i = 0; i < count; i++)
                {
                    Handle thread = g_threads[i].load();
                    if (thread == INVALID_HANDLE || thread == self)
                        continue;
                    if (R_FAILED(svcSetThreadActivity(thread, ThreadActivity_Paused)))
                    {
                        g_threads[i].store(INVALID_HANDLE);  // it has ended
                        continue;
                    }
                    ThreadContext context{};
                    Result rc = svcGetThreadContext3(&context, thread);
                    svcSetThreadActivity(thread, ThreadActivity_Runnable);
                    if (R_SUCCEEDED(rc))
                        put(uint32_t(i), uint32_t(context.pc.x - base), uint32_t(context.lr - base));
                }
                uint64_t now = armGetSystemTick();
                if (armTicksToNs(now - secondStart) >= 1000000000ull)
                {
                    put(0xFFFFFFFFu, uint32_t(armTicksToNs(now - started) / 1000000ull), 0);
                    secondStart = now;
                }
                svcSleepThread(2'000'000);
            }
        }).detach();
    }
}
