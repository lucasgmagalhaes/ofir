// UTF-8 names on Windows (OFIR-52): Ofir strings are UTF-8, and only the
// wide (UTF-16) Windows functions take every name the system allows. Each
// function here takes and gives UTF-8, as its C library namesake does on
// the other systems.

#ifndef OFIR_WIDE_H
#define OFIR_WIDE_H

#ifdef _WIN32

#include <stddef.h>
#include <stdint.h>
#include <sys/stat.h>
#include <wchar.h>

// A UTF-16 copy of `s` from ofir_alloc, or NULL (with errno) for NULL.
wchar_t *ofir_wide(const char *s);
// A UTF-8 copy of `w` from ofir_alloc.
char *ofir_narrow(const wchar_t *w);

int ofir_w_open(const char *name, int flags, int mode);
int ofir_w_stat(const char *name, struct stat *info);
int ofir_w_unlink(const char *name);
int ofir_w_rmdir(const char *name);
int ofir_w_mkdir(const char *name);
int ofir_w_access(const char *name, int mode);
int ofir_w_utime(const char *name);
char *ofir_w_fullpath(char *full, const char *name, size_t size);
char *ofir_w_getcwd(char *path, size_t size);
int ofir_w_rename(const char *from, const char *to);
const char *ofir_w_getenv(const char *name);
// The executable's path in `path`, or -1 when it does not fit.
int64_t ofir_w_module_path(char *path, size_t size);
// The sorted names in a directory, as `list_names` gives them; -1 with errno.
int ofir_w_dir_names(const char *name, char ***names, size_t *count);

#endif

#endif
