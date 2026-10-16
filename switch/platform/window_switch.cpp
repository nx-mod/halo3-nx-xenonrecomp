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
    }

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
        PadState pad;
        padConfigureInput(1, HidNpadStyleSet_NpadStandard);
        padInitializeDefault(&pad);

        uint32_t lastFrame = 0;
        auto lastStatus = Clock::now();
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

            padUpdate(&pad);
            const u64 held = padGetButtons(&pad);
            if ((held & HidNpadButton_Plus) && (held & HidNpadButton_Minus))
                return 0;

            auto now = Clock::now();
            double elapsed = std::chrono::duration<double>(now - lastStatus).count();
            if (elapsed >= 0.5)
            {
                uint32_t frame = g_framesPresented.load();
                printf("\rframe %u (%.0f fps)      ", frame, (frame - lastFrame) / elapsed);
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
