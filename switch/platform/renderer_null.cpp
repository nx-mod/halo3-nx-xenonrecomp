// No host rendering yet on the Switch: the command processor still consumes
// the game's ring buffer (fences, interrupts, swaps), so the game runs, but
// nothing is drawn. The SDL_GPU renderer comes back once sdl3-nx has its
// deko3d backend. Also the frame capture hooks, whose implementation decodes
// textures through the renderer's code.

#include "gpu/frame_capture.h"
#include "gpu/renderer.h"

#include <string>

namespace h3::gpu::renderer
{
    bool Initialize(SDL_GPUDevice*) { return false; }
    void SetShaderCacheDirectory(const std::filesystem::path&) {}
    bool IsEnabled() { return false; }
    void OnShaderLoad(uint32_t, const uint32_t*, uint32_t) {}
    void OnDraw(uint32_t, uint32_t, uint32_t) {}
    void OnResolve() {}
    void OnSwap(uint32_t, uint32_t, uint32_t) {}
    bool OnOcclusionReport(uint32_t) { return false; }
    bool AcquireFrame(Frame&) { return false; }
}

namespace h3::gpu::capture
{
    void OnShaderLoad(uint32_t, uint32_t, uint32_t) {}
    void OnDraw(uint32_t, uint32_t, uint32_t) {}
    void OnBinState(uint64_t, uint64_t) {}
    void OnPredicatedSkip(uint32_t) {}
    void OnZpd(uint32_t, uint32_t) {}
    void OnSwap(uint32_t) {}
    void RequestCapture(bool) {}
    bool HotkeyDeep() { return false; }
    uint32_t HotkeyFrame() { return 0; }

    const std::string& HotkeyDirectory()
    {
        static const std::string none;
        return none;
    }

    std::string CapturePath(const std::string& name) { return name; }
}
