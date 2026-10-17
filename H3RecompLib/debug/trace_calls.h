#pragma once

// Debug aid: with -DH3_TRACE_MODULE_CALLS=ON, every function in the
// recompiled DLLs logs its entry (r3-r5) and return value (r3) to stderr.
// Force-included into the module libraries before ppc_context.h.

#include <cstdint>
#include <cstdio>

// H3_TRACE_DEPTH (default 6) limits the logged call depth, and the
// register save/restore helpers are left out: each line goes to the SD
// card, and logging everything slows a run to a crawl.
#include <cstdlib>
#include <cstring>

struct H3TraceScope
{
    static inline thread_local int depth = 0;
    const char* name;
    const uint64_t* r3;
    bool logged;

    static int MaxDepth()
    {
        static const int maximum = [] { const char* v = getenv("H3_TRACE_DEPTH"); return v ? atoi(v) : 6; }();
        return maximum;
    }

    template <typename Context>
    H3TraceScope(const char* name, Context& ctx) : name(name), r3(&ctx.r3.u64)
    {
        logged = depth < MaxDepth() && strstr(name, "gprlr") == nullptr && strstr(name, "fpr_") == nullptr &&
            strstr(name, "vmx_") == nullptr;
        if (logged)
            fprintf(stderr, "%*s> %s(%08X, %08X, %08X)\n", depth * 2, "", name + 7, uint32_t(ctx.r3.u64),
                uint32_t(ctx.r4.u64), uint32_t(ctx.r5.u64));
        depth++;
    }

    ~H3TraceScope()
    {
        depth--;
        if (logged)
            fprintf(stderr, "%*s< %s = %08X\n", depth * 2, "", name + 7, uint32_t(*r3));
    }
};

#define PPC_FUNC_PROLOGUE() H3TraceScope h3TraceScope(__func__, ctx)
