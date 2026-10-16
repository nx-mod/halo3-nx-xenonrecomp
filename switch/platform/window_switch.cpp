// The window on the Switch (video/window.h), before there is host rendering:
// libnx's text console shows how far the game has got (frames the command
// processor presented, and their rate), SDL handles events and gamepads.
//
// Leaving ends the process (main.cpp) instead of returning to the homebrew
// loader: the guest's threads cannot be stopped, and the loader cannot take
// its memory back while they live.

#include "video/window.h"
#include "config/env.h"
#include "input/input.h"

#include <SDL3/SDL.h>
#include <switch.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>

namespace h3::debug
{
    extern std::atomic<uint32_t> g_traceFrame;
    void UpdateTraceWindow(uint32_t frame);
    void UpdateProfiler(uint32_t frame);
    void UpdateWriteWatch(uint32_t frame);
}

namespace h3::hooks
{
    void UpdateCampaignUnlock();
    void LogTickRate();
}

namespace h3::kernel
{
    void DumpWaitTotals();
    uint64_t FileBytesRead();     // kernel/xboxkrnl_io.cpp
    uint64_t FileBytesWritten();
    void DumpThreadWaits();       // kernel/objects.cpp
}

namespace h3::native::tags
{
    void CheckTagIndex();
    void RuntimeChangeReport();
}

namespace h3::video
{
    namespace
    {
        std::atomic<uint32_t> g_framesPresented{ 0 };
        std::atomic<int> g_quitCode{ -1 };
        std::atomic<const char*> g_fatal{ nullptr };
    }

    // A guest thread that hits a fatal error stops here instead of exiting:
    // exiting from it closes libnx's services (hid among them) under the
    // main thread, which then aborts in its next pad poll and hides the
    // real error. The message goes on screen and the thread sleeps; + and -
    // still leave.
    void HaltOnFatal(const char* what)
    {
        const char* expected = nullptr;
        g_fatal.compare_exchange_strong(expected, what);
        fflush(stdout);
        fflush(stderr);
        for (;;)
            svcSleepThread(INT64_MAX);
    }
}

// abort() from anywhere else (std::terminate, an assert) on a guest thread
// gets the same treatment, through -Wl,--wrap=abort. On the main thread it
// aborts as usual: nothing is left to show the message.
extern "C"
{
    [[noreturn]] void __real_abort(void);

    [[noreturn]] void __wrap_abort(void)
    {
        if (threadGetCurHandle() != envGetMainThreadHandle())
            h3::video::HaltOnFatal("abort()");
        __real_abort();
    }
}

namespace h3::video
{

    bool CreateMainWindow()
    {
        consoleInit(nullptr);
        printf("halo3-nx: no host rendering yet; this shows how far the game gets.\n");
        printf("Hold + and - to quit.\n\n");
        consoleUpdate(nullptr);

        if (!SDL_Init(SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
        {
            fprintf(stderr, "[video] SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
        input::Initialize();
        return true;
    }

    int RunEventLoop()
    {
        using Clock = std::chrono::steady_clock;

        uint32_t lastFrame = 0;
        auto lastStatus = Clock::now();
        auto lastWaitDump = Clock::now();
        while (appletMainLoop())
        {
            if (int code = g_quitCode.load(); code >= 0)
                return code;

            SDL_Event event;
            while (SDL_PollEvent(&event))
            {
                if (event.type == SDL_EVENT_QUIT)
                    return 0;
                input::HandleEvent(event);
            }
            input::Update(g_framesPresented.load());

            // + and - together: through SDL, which owns the pads (a second
            // libnx pad reader configured for one player made SDL's polling
            // of the others abort in hid).
            int count = 0;
            if (SDL_JoystickID* pads = SDL_GetGamepads(&count))
            {
                for (int i = 0; i < count; i++)
                {
                    SDL_Gamepad* gamepad = SDL_GetGamepadFromID(pads[i]);
                    if (gamepad && SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_START) &&
                        SDL_GetGamepadButton(gamepad, SDL_GAMEPAD_BUTTON_BACK))
                    {
                        SDL_free(pads);
                        return 0;
                    }
                }
                SDL_free(pads);
            }

            if (const char* fatal = g_fatal.exchange(nullptr))
            {
                printf("\n\nStopped: %s\nsdmc:/halo3-nx/halo3-err.log has the details.\n"
                    "Hold + and - to quit.\n", fatal);
                consoleUpdate(nullptr);
            }

            auto now = Clock::now();

            // Until the first frame, what each game thread is blocked on,
            // every 10 seconds: a boot that stops shows where it stopped.
            if (g_framesPresented.load() == 0 && now - lastWaitDump >= std::chrono::seconds(10))
            {
                fprintf(stderr, "[video] no frame yet; game threads blocked:\n");
                kernel::DumpThreadWaits();
                lastWaitDump = now;
            }

            double elapsed = std::chrono::duration<double>(now - lastStatus).count();
            if (elapsed >= 0.5)
            {
                uint32_t frame = g_framesPresented.load();
                printf("\rframe %u (%.0f fps)  read %llu MB  wrote %llu MB      ", frame, (frame - lastFrame) / elapsed,
                    (unsigned long long)(kernel::FileBytesRead() >> 20),
                    (unsigned long long)(kernel::FileBytesWritten() >> 20));
                consoleUpdate(nullptr);
                lastFrame = frame;
                lastStatus = now;
            }
            svcSleepThread(16'000'000);
        }
        return 0;
    }

    void RequestQuit(int exitCode)
    {
        g_quitCode.store(std::max(exitCode, 0));
    }

    void OnFramePresented(uint32_t frame)
    {
        g_framesPresented.store(frame);

        if (frame % 150 == 0)
            kernel::DumpWaitTotals();  // H3_LOG_WAITS

        debug::g_traceFrame.store(frame);
        debug::UpdateTraceWindow(frame);
        debug::UpdateProfiler(frame);
        debug::UpdateWriteWatch(frame);
        hooks::UpdateCampaignUnlock();
        hooks::LogTickRate();
        native::tags::CheckTagIndex();
        native::tags::RuntimeChangeReport();
    }
}
