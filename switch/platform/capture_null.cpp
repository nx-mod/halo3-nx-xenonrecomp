// The frame capture hooks (gpu/frame_capture.h) do nothing on the Switch:
// captures are a PC debugging tool, written to the working directory.

#include "gpu/frame_capture.h"

#include <string>

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
