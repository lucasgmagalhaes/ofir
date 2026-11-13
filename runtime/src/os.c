// The process (std.os): arguments, environment, exit and running programs.

#if defined(__APPLE__)
#define _DARWIN_C_SOURCE   // _SC_NPROCESSORS_ONLN
#endif
#ifdef _WIN32
#define _CRT_RAND_S        // rand_s
#endif

#include "ofir_rt.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <fcntl.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#if defined(__APPLE__) || defined(__linux__)
#include <sys/types.h>
#include <sys/random.h>    // getentropy
#endif

extern char **environ;
#endif

static int arg_count;
static char **arg_values;

void ofir_set_args(int argc, char **argv) {
    arg_count = argc;
    arg_values = argv;
}

int64_t ofir_arg_count(void) {
    return arg_count;
}

const uint8_t *ofir_arg(int64_t i, int64_t *len) {
    *len = (int64_t)strlen(arg_values[i]);
    return (const uint8_t *)arg_values[i];
}

// The target the program was compiled for, as `@cfg` names it (spec 12.4).
const uint8_t *ofir_os_name(int64_t *len) {
#if defined(__APPLE__)
    const char *name = "macos";
#elif defined(_WIN32)
    const char *name = "windows";
#elif defined(__linux__)
    const char *name = "linux";
#else
    const char *name = "unknown";
#endif
    *len = (int64_t)strlen(name);
    return (const uint8_t *)name;
}

const uint8_t *ofir_arch_name(int64_t *len) {
#if defined(__x86_64__) || defined(_M_X64)
    const char *name = "x86_64";
#elif defined(__aarch64__) || defined(_M_ARM64)
    const char *name = "aarch64";
#else
    const char *name = "unknown";
#endif
    *len = (int64_t)strlen(name);
    return (const uint8_t *)name;
}

// A NUL-terminated copy of `len` bytes, or NULL if they contain a NUL.
char *ofir_c_string(const uint8_t *ptr, int64_t len) {
    if (memchr(ptr, 0, (size_t)len)) {
        return NULL;
    }
    char *s = ofir_alloc(len + 1);
    memcpy(s, ptr, (size_t)len);
    s[len] = 0;
    return s;
}

const uint8_t *ofir_env(const uint8_t *name, int64_t len, int64_t *value_len) {
    char *key = ofir_c_string(name, len);
    const char *value = key ? getenv(key) : NULL;
    ofir_free(key);
    *value_len = value ? (int64_t)strlen(value) : 0;
    return (const uint8_t *)value;
}

_Noreturn void ofir_exit(int64_t code) {
    ofir_stdout_flush();
    exit((int)code);
}

static void buf_push(OfirBuf *buf, int64_t *cap, const uint8_t *data, int64_t n) {
    if (buf->len + n > *cap) {
        *cap = (buf->len + n) * 2;
        buf->ptr = ofir_realloc(buf->ptr, *cap);
    }
    memcpy(buf->ptr + buf->len, data, (size_t)n);
    buf->len += n;
}

// A started program (std.os `Child`). Its standard output and error go to
// temporary files, not pipes, so that a child that writes a lot never
// waits for its parent to read (ADR 0033).
#ifdef _WIN32

// Appends `arg` to `line` (of capacity `*cap`), quoted as the C runtime of
// the child splits its command line.
static void quote_arg(char **line, int64_t *len, int64_t *cap, const char *arg) {
    int64_t need = *len + 3 + 2 * (int64_t)strlen(arg);
    if (need > *cap) {
        *cap = need * 2;
        *line = ofir_realloc(*line, *cap);
    }
    char *out = *line;
    int64_t at = *len;
    bool plain = arg[0] != '\0' && strpbrk(arg, " \t\n\v\"") == NULL;
    if (at > 0) {
        out[at++] = ' ';
    }
    if (!plain) {
        out[at++] = '"';
    }
    for (const char *c = arg;; c++) {
        int64_t slashes = 0;
        for (; *c == '\\'; c++) {
            slashes++;
        }
        bool end = *c == '\0';
        // Backslashes are doubled before a quote, including the closing one.
        int64_t copies = plain ? slashes : end ? slashes * 2 : *c == '"' ? slashes * 2 + 1
                                                                           : slashes;
        for (int64_t i = 0; i < copies; i++) {
            out[at++] = '\\';
        }
        if (end) {
            break;
        }
        out[at++] = *c;
    }
    if (!plain) {
        out[at++] = '"';
    }
    out[at] = '\0';
    *len = at;
}

typedef struct {
    HANDLE process;
    HANDLE out, err;
    bool waited;
} Child;

// A temporary file the child inherits, deleted when the last handle to it
// is closed.
static HANDLE temp_file(void) {
    char dir[MAX_PATH + 1], path[MAX_PATH + 1];
    if (!GetTempPathA(sizeof dir, dir) || !GetTempFileNameA(dir, "ofr", 0, path)) {
        return INVALID_HANDLE_VALUE;
    }
    SECURITY_ATTRIBUTES inherit = {.nLength = sizeof inherit, .bInheritHandle = TRUE};
    return CreateFileA(path, GENERIC_READ | GENERIC_WRITE,
                       FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, &inherit,
                       CREATE_ALWAYS, FILE_ATTRIBUTE_TEMPORARY | FILE_FLAG_DELETE_ON_CLOSE, NULL);
}

// Reads the whole file from its start into `buf`, then closes it.
static void read_back(HANDLE file, OfirBuf *buf) {
    int64_t cap = 0;
    uint8_t chunk[4096];
    DWORD n;
    SetFilePointer(file, 0, NULL, FILE_BEGIN);
    while (ReadFile(file, chunk, sizeof chunk, &n, NULL) && n > 0) {
        buf_push(buf, &cap, chunk, n);
    }
    CloseHandle(file);
}

static int32_t start(char **argv, Child *child) {
    int64_t len = 0, cap = 256;
    char *line = ofir_alloc(cap);
    line[0] = '\0';
    for (char **arg = argv; *arg; arg++) {
        quote_arg(&line, &len, &cap, *arg);
    }
    child->out = temp_file();
    child->err = temp_file();
    if (child->out == INVALID_HANDLE_VALUE || child->err == INVALID_HANDLE_VALUE) {
        if (child->out != INVALID_HANDLE_VALUE) {
            CloseHandle(child->out);
        }
        if (child->err != INVALID_HANDLE_VALUE) {
            CloseHandle(child->err);
        }
        ofir_free(line);
        return EIO;
    }
    STARTUPINFOA startup = {.cb = sizeof startup, .dwFlags = STARTF_USESTDHANDLES};
    startup.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    startup.hStdOutput = child->out;
    startup.hStdError = child->err;
    PROCESS_INFORMATION process;
    ofir_stdout_flush();
    BOOL started = CreateProcessA(NULL, line, NULL, NULL, TRUE, 0, NULL, NULL, &startup,
                                  &process);
    DWORD error = GetLastError();
    ofir_free(line);
    if (!started) {
        CloseHandle(child->out);
        CloseHandle(child->err);
        return error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND ? ENOENT
               : error == ERROR_ACCESS_DENIED                                ? EACCES
                                                                             : EIO;
    }
    CloseHandle(process.hThread);
    child->process = process.hProcess;
    return 0;
}

static void finish(Child *child, OfirRun *run) {
    WaitForSingleObject(child->process, INFINITE);
    DWORD status = 0;
    GetExitCodeProcess(child->process, &status);
    CloseHandle(child->process);
    run->status = (int64_t)status;
    read_back(child->out, &run->out);
    read_back(child->err, &run->err);
}

int64_t ofir_cpu_count(void) {
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (int64_t)info.dwNumberOfProcessors : 1;
}

#else

typedef struct {
    pid_t pid;
    int out, err;
    bool waited;
} Child;

// A temporary file, already unlinked: it goes away with its last
// descriptor. Other children do not inherit it.
static int temp_file(void) {
    const char *dir = getenv("TMPDIR");
    if (!dir || !*dir) {
        dir = "/tmp";
    }
    static const char name[] = "/ofir-XXXXXX";
    size_t n = strlen(dir);
    char *path = ofir_alloc((int64_t)(n + sizeof name));
    memcpy(path, dir, n);
    memcpy(path + n, name, sizeof name);
    int fd = mkstemp(path);
    if (fd >= 0) {
        unlink(path);
        fcntl(fd, F_SETFD, FD_CLOEXEC);
    }
    ofir_free(path);
    return fd;
}

// Reads the whole file from its start into `buf`, then closes it.
static void read_back(int fd, OfirBuf *buf) {
    int64_t cap = 0;
    uint8_t chunk[4096];
    lseek(fd, 0, SEEK_SET);
    for (;;) {
        ssize_t n = read(fd, chunk, sizeof chunk);
        if (n > 0) {
            buf_push(buf, &cap, chunk, n);
        } else if (n == 0 || errno != EINTR) {
            break;
        }
    }
    close(fd);
}

static int32_t start(char **argv, Child *child) {
    child->out = temp_file();
    child->err = temp_file();
    if (child->out < 0 || child->err < 0) {
        int32_t code = errno;
        if (child->out >= 0) {
            close(child->out);
        }
        if (child->err >= 0) {
            close(child->err);
        }
        return code;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, child->out, 1);
    posix_spawn_file_actions_adddup2(&actions, child->err, 2);
    ofir_stdout_flush();
    int code = posix_spawnp(&child->pid, argv[0], &actions, NULL, argv, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (code != 0) {
        close(child->out);
        close(child->err);
    }
    return code;
}

static void finish(Child *child, OfirRun *run) {
    int status;
    while (waitpid(child->pid, &status, 0) < 0 && errno == EINTR) {
    }
    run->status = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    read_back(child->out, &run->out);
    read_back(child->err, &run->err);
}

int64_t ofir_cpu_count(void) {
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int64_t)n : 1;
}

#endif

// `args` holds `count` strings, each followed by a NUL: the program first.
int32_t ofir_spawn(const uint8_t *args, int64_t len, int64_t count, void **child) {
    char *copy = ofir_alloc(len + 1);
    memcpy(copy, args, (size_t)len);
    copy[len] = 0;
    char **argv = ofir_alloc((count + 1) * (int64_t)sizeof(char *));
    char *at = copy;
    for (int64_t i = 0; i < count; i++) {
        argv[i] = at;
        at += strlen(at) + 1;
    }
    argv[count] = NULL;
    Child *started = ofir_alloc((int64_t)sizeof(Child));
    started->waited = false;
    int32_t code = start(argv, started);
    ofir_free(argv);
    ofir_free(copy);
    if (code != 0) {
        ofir_free(started);
        started = NULL;
    }
    *child = started;
    return code;
}

void ofir_wait(void *child, OfirRun *run) {
    *run = (OfirRun){0};
    finish(child, run);
    ((Child *)child)->waited = true;
}

// A child dropped without `wait` is waited for here, so that it leaves no
// zombie and no open files.
void ofir_child_free(void *child) {
    if (!((Child *)child)->waited) {
        OfirRun run = {0};
        finish(child, &run);
        ofir_free(run.out.ptr);
        ofir_free(run.err.ptr);
    }
    ofir_free(child);
}

// Runs a program to its end: what std.os `run` called before `spawn`, kept
// for compilers built from an older seed.
int32_t ofir_run(const uint8_t *args, int64_t len, int64_t count, OfirRun *run) {
    void *child;
    int32_t code = ofir_spawn(args, len, count, &child);
    if (code != 0) {
        *run = (OfirRun){0};
        return code;
    }
    ofir_wait(child, run);
    ofir_child_free(child);
    return 0;
}

// `ofir test` passes the index of the test to run as the only argument.
int64_t ofir_test_index(void) {
    return arg_count > 1 ? strtoll(arg_values[1], NULL, 10) : -1;
}

// A monotonic clock in nanoseconds, for `Bencher` (spec 12.3).
int64_t ofir_now_ns(void) {
#ifdef _WIN32
    LARGE_INTEGER count, freq;
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&freq);
    return (int64_t)((double)count.QuadPart * 1e9 / (double)freq.QuadPart);
#else
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (int64_t)t.tv_sec * 1000000000 + t.tv_nsec;
#endif
}

// Sleeps for at least `ns` nanoseconds; 0 or less returns at once.
void ofir_sleep_ns(int64_t ns) {
    if (ns <= 0) {
        return;
    }
#ifdef _WIN32
    Sleep((DWORD)((ns + 999999) / 1000000));
#else
    struct timespec t = {(time_t)(ns / 1000000000), (long)(ns % 1000000000)};
    while (nanosleep(&t, &t) != 0 && errno == EINTR) {
    }
#endif
}

// A random nonzero word from the operating system, for `Map.seeded()` (spec 2.6).
uint64_t ofir_random_u64(void) {
    uint64_t r = 0;
#ifdef _WIN32
    unsigned int lo = 0, hi = 0;
    if (rand_s(&lo) == 0 && rand_s(&hi) == 0) {
        r = (uint64_t)hi << 32 | lo;
    }
#else
    if (getentropy(&r, sizeof r) != 0) {
        r = 0;
    }
#endif
    if (r == 0) {   // no entropy: fall back to the clock and an address
        r = (uint64_t)ofir_now_ns() ^ (uint64_t)(uintptr_t)&r;
    }
    return r != 0 ? r : 1;
}
