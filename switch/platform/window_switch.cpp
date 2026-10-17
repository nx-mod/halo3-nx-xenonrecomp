// The window on the Switch (video/window.h): an SDL window with an SDL_GPU
// device on sdl3-nx's deko3d backend, which the renderer draws the command
// stream with (as on the PC); each frame it finishes is scaled onto the
// swapchain here. The PC shows frames through SDL's GPU renderer, whose
// built-in shaders have no DKSH, so this blits instead. Without a device
// (H3_RENDER=0, or deko3d failing) libnx's text console shows how far the
// game has got. SDL handles events and gamepads.
//
// Leaving ends the process (main.cpp) instead of returning to the homebrew
// loader: the guest's threads cannot be stopped, and the loader cannot take
// its memory back while they live.

#include "video/window.h"
#include "config/env.h"
#include "gpu/renderer.h"
#include "input/input.h"
#include "memory.h"

#include <SDL3/SDL.h>
#include <switch.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

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
    void DumpThreads();           // kernel/thread.cpp
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

        // The screen the swapchain is made for.
        constexpr int kScreenWidth = 1280, kScreenHeight = 720;

        SDL_Window* g_window = nullptr;
        SDL_GPUDevice* g_device = nullptr;  // null: the text console instead
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
    namespace
    {
        // H3_PEEK=ADDRESS:LENGTH[,...] (hex address, decimal length): guest
        // memory printed with the thread dumps, as big-endian words.
        void PeekGuestMemory()
        {
            const char* spec = EnvString("H3_PEEK");
            if (spec == nullptr)
                return;
            for (const char* at = spec; *at;)
            {
                char* end = nullptr;
                uint32_t address = uint32_t(strtoul(at, &end, 16));
                if (end == at)
                    break;
                uint32_t length = (*end == ':') ? uint32_t(strtoul(end + 1, &end, 10)) : 16;
                for (uint32_t offset = 0; offset < length; offset += 16)
                {
                    fprintf(stderr, "[peek] %08X:", address + offset);
                    for (uint32_t word = 0; word < 16 && offset + word < length; word += 4)
                        fprintf(stderr, " %08X", uint32_t(GuestRef<uint32_t>(address + offset + word)));
                    fprintf(stderr, "\n");
                }
                at = (*end == ',') ? end + 1 : end;
            }
        }

        // H3_WATCH=ADDRESS[,...] (hex): a thread polls each guest word and
        // logs every change with the threads' last calls, to find who
        // writes over something (the threads run on, so it's a hint).
        void StartGuestWatch()
        {
            const char* spec = EnvString("H3_WATCH");
            if (spec == nullptr)
                return;
            std::vector<uint32_t> addresses;
            for (const char* at = spec; *at;)
            {
                char* end = nullptr;
                uint32_t address = uint32_t(strtoul(at, &end, 16));
                if (end == at)
                    break;
                addresses.push_back(address & ~3u);
                at = (*end == ',') ? end + 1 : end;
            }
            std::thread([addresses]
            {
                // Guest memory is mapped as it's first used (the image after
                // this starts): reading before then faults.
                for (uint32_t address : addresses)
                {
                    for (;;)
                    {
                        MemoryInfo info{};
                        u32 page = 0;
                        if (R_SUCCEEDED(svcQueryMemory(&info, &page, uintptr_t(g_base + address))) &&
                            info.type != MemType_Unmapped && (info.perm & Perm_R))
                            break;
                        svcSleepThread(1'000'000);
                    }
                }
                std::vector<uint32_t> values(addresses.size());
                for (size_t i = 0; i < addresses.size(); i++)
                    values[i] = GuestRef<uint32_t>(addresses[i]);
                for (;;)
                {
                    for (size_t i = 0; i < addresses.size(); i++)
                    {
                        uint32_t value = GuestRef<uint32_t>(addresses[i]);
                        if (value == values[i])
                            continue;
                        fprintf(stderr, "[watch] %08X: %08X -> %08X\n", addresses[i], values[i], value);
                        values[i] = value;
                        kernel::DumpThreads();
                    }
                    svcSleepThread(50'000);
                }
            }).detach();
        }
    }

    namespace
    {
        // The deko3d device, claiming the window, with the renderer on it.
        // False leaves neither behind: the console needs the screen.
        bool CreateGpu()
        {
            const char* setting = EnvString("H3_RENDER");
            if (setting && strcmp(setting, "0") == 0)
                return false;
            g_window = SDL_CreateWindow("Halo 3", kScreenWidth, kScreenHeight, 0);
            if (!g_window)
            {
                fprintf(stderr, "[video] SDL_CreateWindow failed: %s\n", SDL_GetError());
                return false;
            }
            g_device = SDL_CreateGPUDevice(SDL_GPU_SHADERFORMAT_PRIVATE, false, "deko3d");
            if (!g_device || !SDL_ClaimWindowForGPUDevice(g_device, g_window))
                fprintf(stderr, "[video] no deko3d device: %s\n", SDL_GetError());
            else if (!gpu::renderer::Initialize(g_device))
                fprintf(stderr, "[video] the renderer didn't start\n");
            else
                return true;
            if (g_device)
                SDL_DestroyGPUDevice(g_device);
            SDL_DestroyWindow(g_window);
            g_device = nullptr;
            g_window = nullptr;
            return false;
        }

        // The renderer's latest frame, scaled to fit the screen with its
        // aspect ratio kept (black bars), then presented. Main thread.
        void Present()
        {
            SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(g_device);
            if (!commands)
                return;
            SDL_GPUTexture* screen = nullptr;
            Uint32 width = 0, height = 0;
            if (!SDL_WaitAndAcquireGPUSwapchainTexture(commands, g_window, &screen, &width, &height) || !screen)
            {
                SDL_CancelGPUCommandBuffer(commands);
                return;
            }
            gpu::renderer::Frame frame;
            SDL_GPUBlitInfo blit{};
            blit.destination.texture = screen;
            blit.load_op = SDL_GPU_LOADOP_CLEAR;
            blit.clear_color = { 0.0f, 0.0f, 0.0f, 1.0f };
            blit.filter = SDL_GPU_FILTER_LINEAR;
            if (gpu::renderer::AcquireFrame(frame) && frame.width && frame.height)
            {
                float scale = std::min(float(width) / frame.width, float(height) / frame.height);
                blit.source.texture = frame.texture;
                blit.source.x = frame.x;
                blit.source.y = frame.y;
                blit.source.w = frame.width;
                blit.source.h = frame.height;
                blit.destination.w = Uint32(frame.width * scale);
                blit.destination.h = Uint32(frame.height * scale);
                blit.destination.x = (width - blit.destination.w) / 2;
                blit.destination.y = (height - blit.destination.h) / 2;
                SDL_BlitGPUTexture(commands, &blit);
            }
            else
            {
                // Nothing drawn yet: black.
                SDL_GPUColorTargetInfo target{};
                target.texture = screen;
                target.load_op = SDL_GPU_LOADOP_CLEAR;
                target.store_op = SDL_GPU_STOREOP_STORE;
                target.clear_color = blit.clear_color;
                SDL_EndGPURenderPass(SDL_BeginGPURenderPass(commands, &target, 1, nullptr));
            }
            SDL_SubmitGPUCommandBuffer(commands);
        }
    }

    bool CreateMainWindow()
    {
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMEPAD))
        {
            fprintf(stderr, "[video] SDL_Init failed: %s\n", SDL_GetError());
            return false;
        }
        if (!CreateGpu())
        {
            consoleInit(nullptr);
            printf("halo3-nx: no host rendering; this shows how far the game gets.\n");
            printf("Hold + and - to quit.\n\n");
            consoleUpdate(nullptr);
        }
        input::Initialize();
        StartGuestWatch();
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
                if (!g_device)
                    consoleUpdate(nullptr);
            }

            auto now = Clock::now();

            // Until the first frame, what each game thread is blocked on,
            // every 10 seconds: a boot that stops shows where it stopped.
            if (g_framesPresented.load() == 0 && now - lastWaitDump >= std::chrono::seconds(10))
            {
                fprintf(stderr, "[video] no frame yet; game threads blocked:\n");
                kernel::DumpThreadWaits();
                kernel::DumpThreads();
                PeekGuestMemory();
                lastWaitDump = now;
            }

            double elapsed = std::chrono::duration<double>(now - lastStatus).count();
            if (elapsed >= 0.5)
            {
                uint32_t frame = g_framesPresented.load();
                printf("\rframe %u (%.0f fps)  read %llu MB  wrote %llu MB      ", frame, (frame - lastFrame) / elapsed,
                    (unsigned long long)(kernel::FileBytesRead() >> 20),
                    (unsigned long long)(kernel::FileBytesWritten() >> 20));
                if (!g_device)
                    consoleUpdate(nullptr);
                lastFrame = frame;
                lastStatus = now;
            }
            if (g_device)
                Present();  // waits for the swapchain: paces the loop
            else
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
