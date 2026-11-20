// Operating system services of the driver (platform.h).

#ifdef _WIN32

#include "base/platform.h"

#include <direct.h>
#include <errno.h>
#include <io.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

static void forward_slashes(char *path) {
    for (; *path; path++) {
        *path = *path == '\\' ? '/' : *path;
    }
}

bool platform_realpath(const char *path, char out[PLATFORM_PATH_SIZE]) {
    if (_access(path, 0) != 0 || !_fullpath(out, path, PLATFORM_PATH_SIZE)) {
        return false;
    }
    forward_slashes(out);
    return true;
}

bool platform_exe_path(const char *argv0, char out[PLATFORM_PATH_SIZE]) {
    DWORD n = GetModuleFileNameA(NULL, out, PLATFORM_PATH_SIZE);
    if (n == 0 || n >= PLATFORM_PATH_SIZE) {
        return platform_realpath(argv0, out);
    }
    forward_slashes(out);
    return true;
}

bool platform_make_temp_dir(const char *prefix, char out[PLATFORM_PATH_SIZE]) {
    char base[MAX_PATH + 1];
    DWORD n = GetTempPathA(sizeof base, base);
    if (n == 0 || n > sizeof base) {
        return false;
    }
    forward_slashes(base);
    for (unsigned attempt = 0; attempt < 100; attempt++) {
        unsigned tag = (unsigned)GetTickCount() * 2654435761u + attempt * 40503u +
                       (unsigned)GetCurrentProcessId();
        int len = snprintf(out, PLATFORM_PATH_SIZE, "%s%s%06x", base, prefix, tag & 0xffffff);
        if (len <= 0 || len >= PLATFORM_PATH_SIZE) {
            return false;
        }
        if (_mkdir(out) == 0) {
            return true;
        }
        if (errno != EEXIST) {
            return false;
        }
    }
    return false;
}

// Appends `arg` to `line`, quoted as CommandLineToArgvW and the C runtime
// parse it.
static bool quote_arg(char *line, size_t size, size_t *len, const char *arg) {
    size_t at = *len;
    bool plain = arg[0] != '\0' && strpbrk(arg, " \t\n\v\"") == NULL;
    if (at + 1 >= size) {
        return false;
    }
    if (at > 0) {
        line[at++] = ' ';
    }
    if (!plain) {
        line[at++] = '"';
    }
    for (const char *c = arg;; c++) {
        size_t slashes = 0;
        for (; *c == '\\'; c++) {
            slashes++;
        }
        bool end = *c == '\0';
        // Backslashes are doubled before a quote, including the closing one.
        size_t copies = plain ? slashes : end ? slashes * 2 : *c == '"' ? slashes * 2 + 1
                                                                          : slashes;
        if (at + copies + 3 >= size) {
            return false;
        }
        for (size_t i = 0; i < copies; i++) {
            line[at++] = '\\';
        }
        if (end) {
            break;
        }
        line[at++] = *c;
    }
    if (!plain) {
        line[at++] = '"';
    }
    line[at] = '\0';
    *len = at;
    return true;
}

int platform_spawn_wait(char *const argv[]) {
    static char line[32768];
    size_t len = 0;
    line[0] = '\0';
    for (size_t i = 0; argv[i]; i++) {
        if (!quote_arg(line, sizeof line, &len, argv[i])) {
            fprintf(stderr, "error[E0902]: cannot run `%s`: command line too long\n", argv[0]);
            return -1;
        }
    }
    fflush(stdout);
    fflush(stderr);
    STARTUPINFOA startup = {.cb = sizeof startup};
    PROCESS_INFORMATION process;
    if (!CreateProcessA(NULL, line, NULL, NULL, TRUE, 0, NULL, NULL, &startup, &process)) {
        fprintf(stderr, "error[E0902]: cannot run `%s`: error %lu\n", argv[0],
                (unsigned long)GetLastError());
        return -1;
    }
    WaitForSingleObject(process.hProcess, INFINITE);
    DWORD status = 1;
    GetExitCodeProcess(process.hProcess, &status);
    CloseHandle(process.hProcess);
    CloseHandle(process.hThread);
    return (int)status;
}

#else

#define _XOPEN_SOURCE 700   // realpath
#define _DARWIN_C_SOURCE    // mkdtemp on macOS

#include "base/platform.h"

#include <errno.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

bool platform_realpath(const char *path, char out[PLATFORM_PATH_SIZE]) {
    return realpath(path, out) != NULL;
}

bool platform_exe_path(const char *argv0, char out[PLATFORM_PATH_SIZE]) {
    ssize_t n = readlink("/proc/self/exe", out, PLATFORM_PATH_SIZE - 1);   // Linux
    if (n > 0) {
        out[n] = '\0';
        return true;
    }
    // GCOVR_EXCL_START: macOS, which has no /proc; CI measures on Linux
    return realpath(argv0, out) != NULL;   // the path the compiler was started with
    // GCOVR_EXCL_STOP
}

bool platform_make_temp_dir(const char *prefix, char out[PLATFORM_PATH_SIZE]) {
    const char *tmp = getenv("TMPDIR");
    int len = snprintf(out, PLATFORM_PATH_SIZE, "%s/%sXXXXXX", tmp && tmp[0] ? tmp : "/tmp",
                       prefix);
    return len > 0 && len < PLATFORM_PATH_SIZE && mkdtemp(out) != NULL;
}

int platform_spawn_wait(char *const argv[]) {
    pid_t pid;
    int err = posix_spawnp(&pid, argv[0], NULL, NULL, argv, environ);
    if (err != 0) {
        fprintf(stderr, "error[E0902]: cannot run `%s`: %s\n", argv[0], strerror(err));
        return -1;
    }
    int status;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {   // GCOVR_EXCL_START: waitpid fails only for a bad pid
            return -1;
        }   // GCOVR_EXCL_STOP
    }
    return WIFEXITED(status) ? WEXITSTATUS(status) : 128 + WTERMSIG(status);
}

#endif
