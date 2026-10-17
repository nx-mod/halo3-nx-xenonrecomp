// UAM, deko3d's shader compiler, linked into the Switch build: GLSL in, a DKSH
// out, in memory (used by switch/platform/shaders_switch.cpp).
//
// The one file compiled with UAM's own include paths and defines.
// tools/h3-uam-object links it with UAM's library into a single object and
// renames every symbol in it but h3_dk_compile_glsl, so Mesa's names inside
// UAM can't clash with the runtime's. The scheme and UAM's Switch build are
// Halo CE's deko3d port's (nx-mod/haloce-nx, host_dk_compiler.cpp), as in
// nx-mod/libdol-nx (src/uam/uam_compile.cpp).
//
// One compile at a time.
#include "compiler_iface.h"

#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>

// `stage` 0 vertex, 1 fragment, 2 compute. On success *dksh is a malloc'd
// DKSH the caller frees, *size its bytes.
extern "C" int h3_dk_compile_glsl(int stage, const char* glsl, char** dksh, size_t* size) {
    static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
    static const pipeline_stage stages[] = { pipeline_stage_vertex, pipeline_stage_fragment, pipeline_stage_compute };
    int ok = 0;

    *dksh = nullptr;
    *size = 0;
    if (stage < 0 || stage > 2)
        return 0;
    pthread_mutex_lock(&lock);
    {
        DekoCompiler compiler{stages[stage]};

        if (compiler.CompileGlsl(glsl)) {
            FILE* memory = open_memstream(dksh, size);
            if (memory) {
                compiler.WriteDksh(memory);
                ok = !ferror(memory);
                if (fclose(memory) != 0)
                    ok = 0;
            }
        }
    }
    pthread_mutex_unlock(&lock);
    if (!ok) {
        free(*dksh);
        *dksh = nullptr;
        *size = 0;
    }
    return ok;
}
