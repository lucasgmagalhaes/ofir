// UTF-8 names on Windows (OFIR-52): see wide.h.

#include "ofir_rt.h"
#include "wide.h"

#ifdef _WIN32

#include <dirent.h>
#include <direct.h>
#include <errno.h>
#include <fcntl.h>
#include <io.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utime.h>
#include <windows.h>

// The C runtime's `stat` and `_wstat` fill the same structure.
_Static_assert(sizeof(struct stat) == sizeof(struct _stat64i32), "stat layouts differ");

wchar_t *ofir_wide(const char *s) {
    if (!s) {
        errno = EINVAL;
        return NULL;
    }
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) {
        errno = EINVAL;
        return NULL;
    }
    wchar_t *w = ofir_alloc((int64_t)n * (int64_t)sizeof(wchar_t));
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

char *ofir_narrow(const wchar_t *w) {
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    char *s = ofir_alloc(n > 0 ? n : 1);
    if (n <= 0 || !WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL)) {
        s[0] = '\0';
    }
    return s;
}

int ofir_w_open(const char *name, int flags, int mode) {
    wchar_t *w = ofir_wide(name);
    int fd = w ? _wopen(w, flags, mode) : -1;
    int code = errno;
    ofir_free(w);
    errno = code;
    return fd;
}

// Runs `call` on the wide copy of `name`, keeping its errno.
#define WIDE_CALL(name, call)                                                                 \
    do {                                                                                      \
        wchar_t *w = ofir_wide(name);                                                         \
        int result = w ? (call) : -1;                                                         \
        int code = errno;                                                                     \
        ofir_free(w);                                                                         \
        errno = code;                                                                         \
        return result;                                                                        \
    } while (0)

int ofir_w_stat(const char *name, struct stat *info) {
    WIDE_CALL(name, _wstat(w, (struct _stat64i32 *)info));
}

int ofir_w_unlink(const char *name) {
    WIDE_CALL(name, _wunlink(w));
}

int ofir_w_rmdir(const char *name) {
    WIDE_CALL(name, _wrmdir(w));
}

int ofir_w_mkdir(const char *name) {
    WIDE_CALL(name, _wmkdir(w));
}

int ofir_w_access(const char *name, int mode) {
    WIDE_CALL(name, _waccess(w, mode));
}

int ofir_w_utime(const char *name) {
    WIDE_CALL(name, _wutime(w, NULL));
}

// Copies the UTF-8 form of `w` into `out` of `size` bytes; NULL if it does not fit.
static char *narrow_into(const wchar_t *w, char *out, size_t size) {
    char *s = ofir_narrow(w);
    size_t n = strlen(s);
    char *result = n < size ? memcpy(out, s, n + 1) : NULL;
    ofir_free(s);
    errno = result ? errno : ENAMETOOLONG;
    return result;
}

char *ofir_w_fullpath(char *full, const char *name, size_t size) {
    wchar_t *w = ofir_wide(name);
    wchar_t *path = w ? _wfullpath(NULL, w, 0) : NULL;
    ofir_free(w);
    char *result = path ? narrow_into(path, full, size) : NULL;
    free(path);
    return result;
}

char *ofir_w_getcwd(char *path, size_t size) {
    wchar_t *w = _wgetcwd(NULL, 0);
    char *result = w ? narrow_into(w, path, size) : NULL;
    free(w);
    return result;
}

int ofir_w_rename(const char *from, const char *to) {
    wchar_t *old_name = ofir_wide(from);
    wchar_t *new_name = ofir_wide(to);
    int code = !old_name || !new_name ? EINVAL
               : MoveFileExW(old_name, new_name, MOVEFILE_REPLACE_EXISTING) ? 0
               : GetLastError() == ERROR_FILE_NOT_FOUND ? ENOENT
                                                         : EACCES;
    ofir_free(old_name);
    ofir_free(new_name);
    return code;
}

// The value stays valid until the thread's next call, as `getenv`'s does.
const char *ofir_w_getenv(const char *name) {
    static _Thread_local char *value;
    ofir_free(value);
    value = NULL;
    wchar_t *w = ofir_wide(name);
    const wchar_t *found = w ? _wgetenv(w) : NULL;
    ofir_free(w);
    value = found ? ofir_narrow(found) : NULL;
    return value;
}

int64_t ofir_w_module_path(char *path, size_t size) {
    wchar_t w[32768];
    DWORD n = GetModuleFileNameW(NULL, w, sizeof w / sizeof w[0]);
    if (n == 0 || n >= sizeof w / sizeof w[0] || !narrow_into(w, path, size)) {
        errno = ENAMETOOLONG;
        return -1;
    }
    return (int64_t)strlen(path);
}

int ofir_w_dir_names(const char *name, char ***names, size_t *count) {
    wchar_t *w = ofir_wide(name);
    _WDIR *dir = w ? _wopendir(w) : NULL;
    int code = errno;
    ofir_free(w);
    *names = NULL;
    *count = 0;
    if (!dir) {
        errno = code;
        return -1;
    }
    size_t cap = 0;
    for (struct _wdirent *entry; (entry = _wreaddir(dir));) {
        if (wcscmp(entry->d_name, L".") == 0 || wcscmp(entry->d_name, L"..") == 0) {
            continue;
        }
        if (*count == cap) {
            cap = cap ? cap * 2 : 16;
            *names = ofir_realloc(*names, (int64_t)(cap * sizeof(char *)));
        }
        (*names)[(*count)++] = ofir_narrow(entry->d_name);
    }
    _wclosedir(dir);
    return 0;
}

#else

typedef int ofir_wide_unused;   // ISO C wants a declaration in every file

#endif
