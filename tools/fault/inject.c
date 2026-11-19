// Fault injection for tools/check_faults.py (OFIR-74, ADR 0087), loaded with
// LD_PRELOAD into a program built by the Ofir compiler. OFIR_FAULT selects
// the fault:
//
//   count     count allocations and I/O calls, written to OFIR_FAULT_COUNT
//   alloc:N   the Nth allocation and every later one return NULL
//   io:N      the Nth read, write or open fails with EIO
//   eintr     every read and write fails once with EINTR before it runs
//   short     every write writes at most one byte
//
// Linux with glibc only: allocations are forwarded to glibc's __libc_*
// entry points, I/O to the next definition (dlsym RTLD_NEXT).

#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void *__libc_memalign(size_t, size_t);

enum mode { NONE, COUNT, ALLOC, IO, EINTR_ONCE, SHORT };

static enum mode mode = NONE;
static long target;
static atomic_long allocs, ios;
static _Thread_local int retried;   // eintr: the call after a failure runs
// Resolved before any fault is armed: dlsym itself may allocate.
static ssize_t (*next_write)(int, const void *, size_t);
static ssize_t (*next_read)(int, void *, size_t);
static int (*next_open)(const char *, int, ...);

__attribute__((constructor)) static void setup(void) {
    next_write = (ssize_t (*)(int, const void *, size_t))dlsym(RTLD_NEXT, "write");
    next_read = (ssize_t (*)(int, void *, size_t))dlsym(RTLD_NEXT, "read");
    next_open = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open");
    const char *spec = getenv("OFIR_FAULT");
    if (!spec) {
        return;
    }
    if (strcmp(spec, "count") == 0) {
        mode = COUNT;
    } else if (strncmp(spec, "alloc:", 6) == 0) {
        mode = ALLOC;
        target = atol(spec + 6);
    } else if (strncmp(spec, "io:", 3) == 0) {
        mode = IO;
        target = atol(spec + 3);
    } else if (strcmp(spec, "eintr") == 0) {
        mode = EINTR_ONCE;
    } else if (strcmp(spec, "short") == 0) {
        mode = SHORT;
    }
}

__attribute__((destructor)) static void report(void) {
    const char *path = getenv("OFIR_FAULT_COUNT");
    if (mode == COUNT && path) {
        FILE *f = fopen(path, "w");
        if (f) {
            fprintf(f, "%ld %ld\n", atomic_load(&allocs), atomic_load(&ios));
            fclose(f);
        }
    }
}

static int alloc_fails(void) {
    long n = atomic_fetch_add(&allocs, 1) + 1;
    if (mode == ALLOC && n >= target) {
        errno = ENOMEM;   // as glibc's malloc does: callers such as pthread_create check it
        return 1;
    }
    return 0;
}

static int io_fails(void) {
    long n = atomic_fetch_add(&ios, 1) + 1;
    if (mode == IO && n == target) {
        errno = EIO;
        return 1;
    }
    if (mode == EINTR_ONCE) {
        retried = !retried;
        if (retried) {
            errno = EINTR;
            return 1;
        }
    }
    return 0;
}

void *malloc(size_t size) {
    return alloc_fails() ? NULL : __libc_malloc(size);
}

void *calloc(size_t n, size_t size) {
    return alloc_fails() ? NULL : __libc_calloc(n, size);
}

void *realloc(void *ptr, size_t size) {
    return alloc_fails() ? NULL : __libc_realloc(ptr, size);
}

int posix_memalign(void **out, size_t align, size_t size) {
    if (alloc_fails()) {
        return ENOMEM;
    }
    void *ptr = __libc_memalign(align, size);
    if (!ptr) {
        return ENOMEM;
    }
    *out = ptr;
    return 0;
}

void *aligned_alloc(size_t align, size_t size) {
    return alloc_fails() ? NULL : __libc_memalign(align, size);
}

ssize_t write(int fd, const void *buf, size_t len) {
    if (io_fails()) {
        return -1;
    }
    return next_write(fd, buf, mode == SHORT && len > 1 ? 1 : len);
}

ssize_t read(int fd, void *buf, size_t len) {
    return io_fails() ? -1 : next_read(fd, buf, len);
}

int open(const char *path, int flags, ...) {
    mode_t perm = 0;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list args;
        va_start(args, flags);
        perm = (mode_t)va_arg(args, int);
        va_end(args);
    }
    if (mode != EINTR_ONCE && io_fails()) {
        return -1;
    }
    return next_open(path, flags, perm);
}
