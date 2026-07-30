#include <dlfcn.h>
#include <stdio.h>

static const char *g_required_symbols[] = {
    "FT_Create",
    "FT_Close",
    "FT_GetChipConfiguration",
    "FT_SetChipConfiguration",
    "FT_SetSuspendTimeout",
    "FT_SetPipeTimeout",
    "FT_AbortPipe",
    "FT_WritePipe",
    "FT_WritePipeEx",
    "FT_ReadPipe",
    "FT_ReadPipeEx",
    "FT_InitializeOverlapped",
    "FT_ReleaseOverlapped",
    "FT_GetOverlappedResult"
};

int main(void)
{
    const char *error;
    size_t i;
    void *library = dlopen(
        "../../files/leechcore_ft601_driver_linux.so",
        RTLD_NOW | RTLD_LOCAL);
    if(!library) {
        fprintf(stderr, "dlopen failed: %s\n", dlerror());
        return 1;
    }
    for(i = 0;
        i < sizeof(g_required_symbols) / sizeof(g_required_symbols[0]);
        i++) {
        dlerror();
        (void)dlsym(library, g_required_symbols[i]);
        error = dlerror();
        if(error) {
            fprintf(
                stderr,
                "missing %s: %s\n",
                g_required_symbols[i],
                error);
            dlclose(library);
            return 1;
        }
    }
    if(dlclose(library)) {
        fprintf(stderr, "dlclose failed: %s\n", dlerror());
        return 1;
    }
    printf(
        "PASS: %zu required FT601 exports\n",
        sizeof(g_required_symbols) / sizeof(g_required_symbols[0]));
    return 0;
}
