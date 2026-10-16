// h3probe: can this console hold Halo 3's guest memory the way the PC
// runtime does (H3Recomp/memory.cpp)?
//
// - a 4 GiB window, guest address = window + address, in the ASLR region
//   (svcMapMemory only maps into the stack region, which is smaller);
// - the 512 MiB of physical memory seen at 0xA0000000, 0xC0000000 and
//   0xE0000000 (that view 4 KiB in) as one memory: shared memory mapped three
//   times, split into its first page and the rest so the views never overlap;
// - virtual memory as heap aliased into the window with
//   svcMapProcessCodeMemory and made read-write (haloce-nx's way).
//
// Everything is unmapped again before it exits. Results go to the screen and
// to sdmc:/halo3-nx/probe.txt.

#include <switch.h>

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#define GUEST_SIZE 0x100000000ull
#define PHYS_SIZE 0x20000000ull
#define PAGE 0x1000ull
#define VIRT_TEST_SIZE (64ull << 20)
#define VIRT_TEST_GUEST 0x40000000u

static FILE* g_log;

static void plog(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vprintf(fmt, args);
    va_end(args);
    if (g_log) {
        va_start(args, fmt);
        vfprintf(g_log, fmt, args);
        va_end(args);
        fflush(g_log);
    }
    consoleUpdate(NULL);
}

static void memory_use(const char* when) {
    u64 total = 0, used = 0;
    svcGetInfo(&total, InfoType_TotalMemorySize, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&used, InfoType_UsedMemorySize, CUR_PROCESS_HANDLE, 0);
    plog("[mem] %s: %llu of %llu MiB used\n", when, (unsigned long long)(used >> 20),
         (unsigned long long)(total >> 20));
}

static void region(const char* name, InfoType address, InfoType size) {
    u64 a = 0, s = 0;
    svcGetInfo(&a, address, CUR_PROCESS_HANDLE, 0);
    svcGetInfo(&s, size, CUR_PROCESS_HANDLE, 0);
    plog("  %-6s %010llx - %010llx (%llu MiB)\n", name, (unsigned long long)a, (unsigned long long)(a + s),
         (unsigned long long)(s >> 20));
}

// One view of physical memory: `size` bytes of shared memory `handle`, from
// its start, at guest address `guest`.
typedef struct {
    const char* name;
    u32 guest;
    Handle* handle;
    u64 size;
    bool mapped;
} View;

// Writes through one guest address and reads it back through another.
static bool same_memory(u8* base, u32 write_at, u32 read_at, u32 value) {
    volatile u32* w = (volatile u32*)(base + write_at);
    volatile u32* r = (volatile u32*)(base + read_at);
    *w = value;
    const u32 seen = *r;
    plog("  %08x -> %08x: wrote %08x, read %08x  %s\n", write_at, read_at, value, seen,
         seen == value ? "same" : "DIFFERENT");
    return seen == value;
}

int main(void) {
    consoleInit(NULL);
    padConfigureInput(1, HidNpadStyleSet_NpadStandard);
    PadState pad;
    padInitializeDefault(&pad);
    mkdir("sdmc:/halo3-nx", 0777);
    g_log = fopen("sdmc:/halo3-nx/probe.txt", "w");

    plog("h3probe: Halo 3 guest memory on this console\n\n");
    memory_use("start");
    const Handle process = envGetOwnProcessHandle();
    plog("own process handle: %s\n", process ? "yes" : "no (svcMapProcessCodeMemory unavailable)");
    plog("address space:\n");
    region("heap", InfoType_HeapRegionAddress, InfoType_HeapRegionSize);
    region("alias", InfoType_AliasRegionAddress, InfoType_AliasRegionSize);
    region("stack", InfoType_StackRegionAddress, InfoType_StackRegionSize);
    region("aslr", InfoType_AslrRegionAddress, InfoType_AslrRegionSize);

    // 1. The window.
    virtmemLock();
    void* window = virtmemFindAslr(GUEST_SIZE, 0x10000000);
    VirtmemReservation* reservation = window ? virtmemAddReservation(window, GUEST_SIZE) : NULL;
    virtmemUnlock();
    plog("\n1. 4 GiB window: %p %s\n", window, reservation ? "reserved" : "NOT FOUND");
    u8* const base = (u8*)window;

    // 2. Physical memory: its first page and the rest, each shared memory.
    Handle page0 = 0, rest = 0;
    Result rc = svcCreateSharedMemory(&page0, PAGE, Perm_Rw, Perm_Rw);
    plog("\n2. svcCreateSharedMemory(4 KiB): 0x%x\n", rc);
    rc = svcCreateSharedMemory(&rest, PHYS_SIZE - PAGE, Perm_Rw, Perm_Rw);
    plog("   svcCreateSharedMemory(512 MiB - 4 KiB): 0x%x\n", rc);
    memory_use("with physical memory");

    View views[] = {
        {"0xA0000000 page 0", 0xA0000000u, &page0, PAGE, false},
        {"0xA0000000 rest", 0xA0001000u, &rest, PHYS_SIZE - PAGE, false},
        {"0xC0000000 page 0", 0xC0000000u, &page0, PAGE, false},
        {"0xC0000000 rest", 0xC0001000u, &rest, PHYS_SIZE - PAGE, false},
        {"0xE0000000 (rest)", 0xE0000000u, &rest, PHYS_SIZE - PAGE, false},
    };
    const size_t view_count = sizeof(views) / sizeof(views[0]);
    bool all_views = base != NULL && page0 && rest;
    for (size_t i = 0; i < view_count && base; i++) {
        if (!*views[i].handle) continue;
        rc = svcMapSharedMemory(*views[i].handle, base + views[i].guest, views[i].size, Perm_Rw);
        views[i].mapped = R_SUCCEEDED(rc);
        all_views = all_views && views[i].mapped;
        plog("   map %-18s 0x%x\n", views[i].name, rc);
    }

    // 3. One memory through all three views.
    plog("\n3. the views alias:\n");
    if (all_views) {
        bool ok = true;
        ok &= same_memory(base, 0xA0000010u, 0xC0000010u, 0x11111111u);   // page 0
        ok &= same_memory(base, 0xA0001000u, 0xC0001000u, 0x22222222u);
        ok &= same_memory(base, 0xA0001000u, 0xE0000000u, 0x33333333u);   // E is 4 KiB in
        ok &= same_memory(base, 0xE0123450u, 0xA0124450u, 0x44444444u);
        ok &= same_memory(base, 0xC1FFFFF0u, 0xA1FFFFF0u, 0x55555555u);   // last word
        ok &= same_memory(base, 0xA1FFFFF0u, 0xE1FFEFF0u, 0x66666666u);
        plog("   %s\n", ok ? "PASS: one memory in three views" : "FAIL");
    } else {
        plog("   skipped (not every view mapped)\n");
    }

    // 4. Virtual memory: heap aliased into the window.
    plog("\n4. virtual memory (heap aliased at 0x%08x):\n", VIRT_TEST_GUEST);
    void* heap = aligned_alloc(0x200000, VIRT_TEST_SIZE);
    bool virt_mapped = false;
    if (heap && base && process) {
        rc = svcMapProcessCodeMemory(process, (u64)(uintptr_t)(base + VIRT_TEST_GUEST), (u64)(uintptr_t)heap,
                                     VIRT_TEST_SIZE);
        plog("   svcMapProcessCodeMemory(64 MiB): 0x%x\n", rc);
        virt_mapped = R_SUCCEEDED(rc);
        if (virt_mapped) {
            rc = svcSetProcessMemoryPermission(process, (u64)(uintptr_t)(base + VIRT_TEST_GUEST), VIRT_TEST_SIZE,
                                               Perm_Rw);
            plog("   svcSetProcessMemoryPermission(rw): 0x%x\n", rc);
            if (R_SUCCEEDED(rc)) {
                volatile u32* p = (volatile u32*)(base + VIRT_TEST_GUEST + 0x1230);
                *p = 0x77777777u;
                plog("   write/read through the window: %08x %s\n", *p, *p == 0x77777777u ? "ok" : "BAD");
            }
            // A second alias of the same heap is expected to be refused.
            rc = svcMapProcessCodeMemory(process, (u64)(uintptr_t)(base + VIRT_TEST_GUEST + VIRT_TEST_SIZE),
                                         (u64)(uintptr_t)heap, PAGE);
            plog("   second alias of the same heap: 0x%x (%s)\n", rc,
                 R_SUCCEEDED(rc) ? "allowed" : "refused, as expected");
            if (R_SUCCEEDED(rc))
                svcUnmapProcessCodeMemory(process, (u64)(uintptr_t)(base + VIRT_TEST_GUEST + VIRT_TEST_SIZE),
                                          (u64)(uintptr_t)heap, PAGE);
        }
    } else {
        plog("   skipped (%s)\n", !heap ? "no heap" : !process ? "no process handle" : "no window");
    }
    memory_use("everything mapped");

    // 5. Undo it all, so the loader gets its memory back.
    plog("\n5. cleanup:\n");
    if (virt_mapped) {
        rc = svcUnmapProcessCodeMemory(process, (u64)(uintptr_t)(base + VIRT_TEST_GUEST), (u64)(uintptr_t)heap,
                                       VIRT_TEST_SIZE);
        plog("   unmap virtual: 0x%x\n", rc);
    }
    free(heap);
    for (size_t i = 0; i < view_count; i++) {
        if (views[i].mapped) {
            rc = svcUnmapSharedMemory(*views[i].handle, base + views[i].guest, views[i].size);
            if (R_FAILED(rc)) plog("   unmap %s: 0x%x\n", views[i].name, rc);
        }
    }
    if (page0) svcCloseHandle(page0);
    if (rest) svcCloseHandle(rest);
    if (reservation) {
        virtmemLock();
        virtmemRemoveReservation(reservation);
        virtmemUnlock();
    }
    memory_use("end");
    plog("\ndone. Press + to exit.\n");

    while (appletMainLoop()) {
        padUpdate(&pad);
        if (padGetButtonsDown(&pad) & HidNpadButton_Plus) break;
        consoleUpdate(NULL);
    }
    if (g_log) fclose(g_log);
    consoleExit(NULL);
    return 0;
}
