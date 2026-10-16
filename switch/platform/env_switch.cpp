// The H3_* switches (config/env.h) on the Switch, which has no environment:
// each line of sdmc:/halo3-nx/env.txt, NAME or NAME=VALUE, is set before
// anything reads them. Several are read by globals' initializers, so this
// runs as an early constructor; libnx has mounted the SD card by then.
// Lines starting with # are skipped.

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    __attribute__((constructor(101))) void LoadEnvFile()
    {
        FILE* file = fopen("sdmc:/halo3-nx/env.txt", "r");
        if (!file)
            return;

        char line[512];
        while (fgets(line, sizeof(line), file))
        {
            line[strcspn(line, "\r\n")] = '\0';
            char* name = line + strspn(line, " \t");
            if (*name == '\0' || *name == '#')
                continue;
            char* value = strchr(name, '=');
            if (value)
                *value++ = '\0';
            setenv(name, value ? value : "1", 1);
        }
        fclose(file);
    }
}
