// Files, directories and standard input (std.fs, std.io). Every function
// returns 0 or an errno value, which std turns into an IoError.

// realpath is an XSI function: glibc declares it only with _DEFAULT_SOURCE
// under clang.
#define _DEFAULT_SOURCE

#include "ofir_rt.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifdef _WIN32
#include <sys/utime.h>
#else
#include <utime.h>
#endif

#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

#ifdef _WIN32
#include <direct.h>
#include <windows.h>

#include "wide.h"

// UTF-8 names through the wide functions (wide.h).
#define open ofir_w_open
#define stat(name, info) ofir_w_stat(name, info)
#define unlink ofir_w_unlink
#define rmdir ofir_w_rmdir
#define mkdir(name, mode) ofir_w_mkdir(name)
#define getcwd ofir_w_getcwd

// Paths given to programs use `/`, as on the other systems.
static void forward_slashes(char *path) {
    for (; *path; path++) {
        *path = *path == '\\' ? '/' : *path;
    }
}

static char *realpath(const char *name, char *full) {
    if (ofir_w_access(name, 0) != 0) {
        return NULL;
    }
    if (!ofir_w_fullpath(full, name, PATH_MAX)) {
        return NULL;
    }
    forward_slashes(full);
    return full;
}
#else
#define O_BINARY 0
#endif

static int32_t read_fd(int fd, OfirBuf *out) {
    int64_t cap = 4096;
    *out = (OfirBuf){ofir_alloc(cap), 0};
    for (;;) {
        if (out->len == cap) {
            cap *= 2;
            out->ptr = ofir_realloc(out->ptr, cap);
        }
        ssize_t n = read(fd, out->ptr + out->len, (size_t)(cap - out->len));
        if (n == 0) {
            return 0;
        }
        if (n < 0 && errno != EINTR) {
            int32_t code = errno;
            ofir_free(out->ptr);
            *out = (OfirBuf){0};
            return code;
        }
        out->len += n > 0 ? n : 0;
    }
}

int32_t ofir_read_file(const uint8_t *path, int64_t len, OfirBuf *out) {
    char *name = ofir_c_string(path, len);
    int fd = name ? open(name, O_RDONLY | O_BINARY, 0) : -1;
    int32_t code = name ? errno : EINVAL;
    ofir_free(name);
    if (fd < 0) {
        *out = (OfirBuf){0};
        return code;
    }
    code = read_fd(fd, out);
    close(fd);
    return code;
}

// Writes all of `data` to the file opened with `flags` (O_TRUNC or O_APPEND).
static int32_t write_with(const uint8_t *path, int64_t len, const uint8_t *data, int64_t n,
                          int flags) {
    char *name = ofir_c_string(path, len);
    int fd = name ? open(name, O_WRONLY | O_CREAT | O_BINARY | flags, 0666) : -1;
    int32_t code = name ? errno : EINVAL;
    ofir_free(name);
    if (fd < 0) {
        return code;
    }
    while (n > 0) {
        ssize_t done = write(fd, data, (size_t)n);
        if (done < 0 && errno != EINTR) {
            code = errno;
            close(fd);
            return code;
        }
        data += done > 0 ? done : 0;
        n -= done > 0 ? done : 0;
    }
    return close(fd) == 0 ? 0 : errno;
}

int32_t ofir_write_file(const uint8_t *path, int64_t len, const uint8_t *data, int64_t n) {
    return write_with(path, len, data, n, O_TRUNC);
}

// Adds `data` at the end of the file, creating it if it is missing (ADR 0058).
int32_t ofir_append_file(const uint8_t *path, int64_t len, const uint8_t *data, int64_t n) {
    return write_with(path, len, data, n, O_APPEND);
}

bool ofir_path_exists(const uint8_t *path, int64_t len) {
    char *name = ofir_c_string(path, len);
    struct stat info;
    bool found = name && stat(name, &info) == 0;
    ofir_free(name);
    return found;
}

int32_t ofir_canonicalize(const uint8_t *path, int64_t len, OfirBuf *out) {
    char *name = ofir_c_string(path, len);
    char full[PATH_MAX];
    *out = (OfirBuf){0};
    if (!name) {
        return EINVAL;
    }
    bool found = realpath(name, full) != NULL;
    ofir_free(name);
    if (!found) {
        return errno;
    }
    out->len = (int64_t)strlen(full);
    out->ptr = ofir_alloc(out->len);
    memcpy(out->ptr, full, (size_t)out->len);
    return 0;
}

int32_t ofir_remove_file(const uint8_t *path, int64_t len) {
    char *name = ofir_c_string(path, len);
    int32_t code = !name ? EINVAL : unlink(name) == 0 ? 0 : errno;
    ofir_free(name);
    return code;
}

// Removes an empty directory (ADR 0058).
int32_t ofir_remove_dir(const uint8_t *path, int64_t len) {
    char *name = ofir_c_string(path, len);
    int32_t code = !name ? EINVAL : rmdir(name) == 0 ? 0 : errno;
    ofir_free(name);
    return code;
}

// Replaces `to` if it exists, on Windows too.
int32_t ofir_rename(const uint8_t *from, int64_t from_len, const uint8_t *to, int64_t to_len) {
    char *old_name = ofir_c_string(from, from_len);
    char *new_name = ofir_c_string(to, to_len);
    int32_t code = 0;
    if (!old_name || !new_name) {
        code = EINVAL;
    } else {
#ifdef _WIN32
        code = ofir_w_rename(old_name, new_name);
#else
        code = rename(old_name, new_name) == 0 ? 0 : errno;
#endif
    }
    ofir_free(old_name);
    ofir_free(new_name);
    return code;
}

// The size in bytes and the modification time in nanoseconds since 1970.
int32_t ofir_file_info(const uint8_t *path, int64_t len, int64_t *size, int64_t *modified) {
    char *name = ofir_c_string(path, len);
    struct stat info;
    int32_t code = !name ? EINVAL : stat(name, &info) == 0 ? 0 : errno;
    ofir_free(name);
    *size = code == 0 ? (int64_t)info.st_size : 0;
    *modified = code == 0 ? (int64_t)info.st_mtime * 1000000000 : 0;
#if defined(__APPLE__)   // st_mtimespec is hidden by _POSIX_C_SOURCE
    *modified += code == 0 ? (int64_t)info.st_mtimensec : 0;
#elif !defined(_WIN32)
    *modified += code == 0 ? (int64_t)info.st_mtim.tv_nsec : 0;
#endif
    return code;
}

// Sets the access and modification times to now.
int32_t ofir_touch(const uint8_t *path, int64_t len) {
    char *name = ofir_c_string(path, len);
#ifdef _WIN32
    int32_t code = !name ? EINVAL : ofir_w_utime(name) == 0 ? 0 : errno;
#else
    int32_t code = !name ? EINVAL : utime(name, NULL) == 0 ? 0 : errno;
#endif
    ofir_free(name);
    return code;
}

// Creates each missing directory of `name`, which it modifies in place.
static int32_t make_dirs(char *name) {
    for (char *at = name + 1;; at++) {
#ifdef _WIN32
        int separator = *at == '/' || *at == '\\';   // paths mix both on Windows
#else
        int separator = *at == '/';
#endif
        if (!separator && *at != 0) {
            continue;
        }
        char saved = *at;
        *at = 0;
        // Only the last directory decides: a prefix such as `C:` can fail on Windows.
        struct stat info;
        int32_t code = mkdir(name, 0777) == 0 || errno == EEXIST ? 0 : errno;
        if (code != 0 && stat(name, &info) == 0 && S_ISDIR(info.st_mode)) {
            code = 0;
        }
        *at = saved;
        if (saved == 0) {
            return code;
        }
    }
}

int32_t ofir_create_dir_all(const uint8_t *path, int64_t len) {
    char *name = ofir_c_string(path, len);
    int32_t code = name && len > 0 ? make_dirs(name) : EINVAL;
    ofir_free(name);
    return code;
}

static int compare_names(const void *a, const void *b) {
    return strcmp(*(char *const *)a, *(char *const *)b);
}

// Packs the names, each from ofir_alloc, and frees them.
static void pack_names(char **names, size_t count, OfirBuf *out) {
    qsort(names, count, sizeof(char *), compare_names);   // sorted, for determinism
    int64_t size = 0;
    for (size_t i = 0; i < count; i++) {
        size += (int64_t)strlen(names[i]) + 1;
    }
    *out = (OfirBuf){ofir_alloc(size), 0};
    for (size_t i = 0; i < count; i++) {
        size_t n = strlen(names[i]) + 1;
        memcpy(out->ptr + out->len, names[i], n);
        out->len += (int64_t)n;
        ofir_free(names[i]);
    }
    ofir_free(names);
}

#ifndef _WIN32
// The names in the directory but `.` and `..`, each from ofir_alloc.
static int dir_names(const char *name, char ***names, size_t *count) {
    DIR *dir = opendir(name);
    *names = NULL;
    *count = 0;
    if (!dir) {
        return -1;
    }
    size_t cap = 0;
    for (struct dirent *entry; (entry = readdir(dir));) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
            continue;
        }
        if (*count == cap) {
            cap = cap ? cap * 2 : 16;
            *names = ofir_realloc(*names, (int64_t)(cap * sizeof(char *)));
        }
        size_t n = strlen(entry->d_name) + 1;
        (*names)[(*count)++] = memcpy(ofir_alloc((int64_t)n), entry->d_name, n);
    }
    closedir(dir);
    return 0;
}
#else
#define dir_names ofir_w_dir_names
#endif

// The names in the directory, sorted, each followed by a NUL.
int32_t ofir_read_dir(const uint8_t *path, int64_t len, OfirBuf *out) {
    char *name = ofir_c_string(path, len);
    char **names = NULL;
    size_t count = 0;
    int found = name ? dir_names(name, &names, &count) : -1;
    int32_t code = name ? errno : EINVAL;
    ofir_free(name);
    *out = (OfirBuf){0};
    if (found != 0) {
        return code;
    }
    pack_names(names, count, out);
    return 0;
}

// One line of standard input with its `\n`; an empty buffer at the end.
int32_t ofir_read_line(OfirBuf *out) {
    ofir_stdout_flush();   // a prompt appears before the program waits
    char *line = NULL;
    size_t cap = 0;
#ifdef _WIN32
    ssize_t n = 0;   // MinGW has no getline
    for (int c; (c = getc(stdin)) != EOF;) {
        if ((size_t)n + 1 >= cap) {
            cap = cap ? cap * 2 : 128;
            line = realloc(line, cap);
        }
        line[n++] = (char)c;
        if (c == '\n') {
            break;
        }
    }
    n = n == 0 ? -1 : n;
#else
    ssize_t n = getline(&line, &cap, stdin);
#endif
    *out = (OfirBuf){0};
    if (n < 0) {
        free(line);
        return ferror(stdin) ? errno : 0;
    }
    out->ptr = ofir_alloc(n);
    memcpy(out->ptr, line, (size_t)n);
    out->len = n;
    free(line);
    return 0;
}

// Up to `n` bytes of standard input: fewer only at the end of the input.
int32_t ofir_read_bytes(int64_t n, OfirBuf *out) {
    ofir_stdout_flush();
    *out = (OfirBuf){0};
    if (n <= 0) {
        return 0;
    }
    uint8_t *bytes = ofir_alloc(n);
    size_t got = fread(bytes, 1, (size_t)n, stdin);
    if (got == 0) {
        ofir_free(bytes);
        return ferror(stdin) ? errno : 0;
    }
    out->ptr = bytes;
    out->len = (int64_t)got;
    return 0;
}

int32_t ofir_read_stdin(OfirBuf *out) {
    ofir_stdout_flush();
    return read_fd(0, out);
}

int32_t ofir_current_dir(OfirBuf *out) {
    char path[4096];
    *out = (OfirBuf){0};
    if (!getcwd(path, sizeof path)) {
        return errno;
    }
#ifdef _WIN32
    forward_slashes(path);
#endif
    out->len = (int64_t)strlen(path);
    out->ptr = ofir_alloc(out->len);
    memcpy(out->ptr, path, (size_t)out->len);
    return 0;
}

int32_t ofir_executable_path(OfirBuf *out) {
    char path[4096];
    *out = (OfirBuf){0};
#ifdef __APPLE__
    uint32_t size = sizeof path;
    ssize_t n = _NSGetExecutablePath(path, &size) == 0 ? (ssize_t)strlen(path) : -1;
#elif defined(_WIN32)
    ssize_t n = (ssize_t)ofir_w_module_path(path, sizeof path);
    forward_slashes(path);
#else
    ssize_t n = readlink("/proc/self/exe", path, sizeof path);
#endif
    if (n < 0) {
        return errno;
    }
    out->ptr = ofir_alloc(n);
    memcpy(out->ptr, path, (size_t)n);
    out->len = n;
    return 0;
}

// The IoErrorKind of an errno value: NotFound, PermissionDenied,
// AlreadyExists, InvalidInput, UnexpectedEof, Interrupted, Other.
int32_t ofir_error_kind(int32_t code) {
    switch (code) {
    case ENOENT: case ENOTDIR: return 0;
    case EACCES: case EPERM: return 1;
    case EEXIST: return 2;
    case EINVAL: return 3;
    case EINTR: return 5;
    default: return 6;
    }
}

const uint8_t *ofir_error_message(int32_t code, int64_t *len) {
    const char *text = strerror(code);
    *len = (int64_t)strlen(text);
    return (const uint8_t *)text;
}
