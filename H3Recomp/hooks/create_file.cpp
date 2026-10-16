// CreateFileA as the game links it (XAPI, sub_82582338), logged with
// H3_LOG_FILES. Its opens go through XAPI's own dispatch table, and those on
// devices the game creates itself (its cache drive, \Device\cache0, linked
// as cache:) never reach NtCreateFile, so the I/O log alone cannot show
// which path failed.

#include "config/env.h"
#include "memory.h"

#include <ppc_recomp_shared.h>

#include <cstdio>
#include <string>

using namespace h3;

PPC_FUNC_IMPL(__imp__sub_82582338);
PPC_FUNC(sub_82582338)
{
    static const bool log = EnvFlag("H3_LOG_FILES");
    if (!log)
    {
        __imp__sub_82582338(ctx, base);
        return;
    }

    std::string path(reinterpret_cast<const char*>(GuestPtr<uint8_t>(ctx.r3.u32)));
    uint32_t access = ctx.r4.u32;
    uint32_t disposition = ctx.r7.u32;
    __imp__sub_82582338(ctx, base);
    fprintf(stderr, "[xapi] CreateFileA %s (access 0x%08X, disposition %u) -> 0x%08X\n", path.c_str(), access,
        disposition, ctx.r3.u32);
}
