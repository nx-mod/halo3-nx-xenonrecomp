// The sampling profiler (debug/profiler.cpp) needs POSIX signals and
// ucontext, which the Switch has not; these are its entry points, doing
// nothing.

#include <cstdint>

namespace h3::debug
{
    void SetWallProfileThread() {}
    void UpdateProfiler(uint32_t) {}
}
