// Locating the files installed with the compiler: the runtime library and
// the standard library (docs/design/toolchain.md, 4).

#include "driver_internal.h"

#include "base/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

// The directory of the compiler's executable.
static bool compiler_dir(const char *argv0, char out[PATH_SIZE]) {
    if (!platform_exe_path(argv0, out)) {
        return false;   // GCOVR_EXCL_LINE: /proc/self/exe names the running compiler
    }
    char *slash = strrchr(out, '/');
    if (slash) {
        *slash = '\0';
    }
    return true;
}

bool find_installed(const char *argv0, const char *env, const char *const *places,
                    size_t count, char out[PATH_SIZE]) {
    const char *given = getenv(env);
    if (given && given[0]) {
        return snprintf(out, PATH_SIZE, "%s", given) < PATH_SIZE;
    }
    char dir[PATH_SIZE];
    if (!compiler_dir(argv0, dir)) {
        return false;   // GCOVR_EXCL_LINE: /proc/self/exe names the running compiler
    }
    for (size_t i = 0; i < count; i++) {
        int n = snprintf(out, PATH_SIZE, "%s/%s", dir, places[i]);
        if (n > 0 && n < PATH_SIZE && access(out, R_OK) == 0) {
            return true;
        }
    }
    return false;
}
