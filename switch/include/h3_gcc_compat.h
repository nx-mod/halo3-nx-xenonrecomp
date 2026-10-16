#pragma once

// XenonRecomp's output uses a few Clang builtins; devkitA64 is GCC. This
// header is force-included (-include) into the recompiled sources.
#if !defined(__clang__)
#include <cstdint>

#define __builtin_assume(condition) do { if (!(condition)) __builtin_unreachable(); } while (false)
#define __builtin_debugtrap() __builtin_trap()

constexpr uint32_t __builtin_rotateleft32(uint32_t value, uint32_t shift)
{
    shift &= 31;
    return (value << shift) | (value >> ((32 - shift) & 31));
}

constexpr uint64_t __builtin_rotateleft64(uint64_t value, uint64_t shift)
{
    shift &= 63;
    return (value << shift) | (value >> ((64 - shift) & 63));
}
#endif
