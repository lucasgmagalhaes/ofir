// Panics (docs/design/runtime.md, 2). All functions are cold and noreturn.

#include "ofir_rt.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#if defined(__GLIBC__) || defined(__APPLE__)
#include <execinfo.h>
#define HAVE_BACKTRACE 1
#endif

#define COLD __attribute__((cold, noinline))

// The first thread to end the process (a panic or `os.exit`) owns the end:
// exit() from two threads at once is undefined behaviour. Any later thread
// waits here for the process to go; a panic while this thread already ends
// it (in an exit handler) ends it at once.
static atomic_bool ending;
static _Thread_local bool ending_here;

void ofir_end_begin(void) {
    if (ending_here) {
        _exit(OFIR_PANIC_STATUS);
    }
    ending_here = true;
    if (atomic_exchange(&ending, true)) {
        for (;;) {
            ofir_sleep_ns(1000000000);
        }
    }
}

// The return addresses of the panicking thread, when OFIR_BACKTRACE is set
// to anything but 0.
static void print_backtrace(void) {
#ifdef HAVE_BACKTRACE
    const char *want = getenv("OFIR_BACKTRACE");
    if (!want || !want[0] || strcmp(want, "0") == 0) {
        return;
    }
    void *frames[64];
    int n = backtrace(frames, 64);
    fputs("backtrace:\n", stderr);
    fflush(stderr);
    backtrace_symbols_fd(frames, n, 2);
#endif
}

COLD _Noreturn void ofir_panic(const uint8_t *msg, int64_t len, const OfirLoc *loc) {
    ofir_end_begin();
    ofir_stdout_flush();   // keep the program's output before the message
    fprintf(stderr, "panic: %.*s\n", (int)len, (const char *)msg);
    if (loc) {
        fprintf(stderr, "  at %s:%u:%u\n", loc->file, loc->line, loc->col);
    }
    print_backtrace();
    exit(OFIR_PANIC_STATUS);
}

COLD _Noreturn void ofir_abort(void) {
    abort();
}

static COLD _Noreturn void panic_text(const char *msg, const OfirLoc *loc) {
    ofir_panic((const uint8_t *)msg, (int64_t)strlen(msg), loc);
}

COLD _Noreturn void ofir_panic_overflow(const OfirLoc *loc) {
    panic_text("integer overflow", loc);
}

COLD _Noreturn void ofir_panic_div_zero(const OfirLoc *loc) {
    panic_text("division by zero", loc);
}

COLD _Noreturn void ofir_panic_shift(const OfirLoc *loc) {
    panic_text("shift amount out of range", loc);
}

COLD _Noreturn void ofir_panic_cast(const OfirLoc *loc) {
    panic_text("value out of range for the target type of `as`", loc);
}

COLD _Noreturn void ofir_panic_bounds(int64_t index, int64_t len, const OfirLoc *loc) {
    char msg[96];
    int n = snprintf(msg, sizeof msg,
                     "index out of bounds: the length is %lld but the index is %lld",
                     (long long)len, (long long)index);
    ofir_panic((const uint8_t *)msg, n, loc);
}

COLD _Noreturn void ofir_panic_assert(const OfirLoc *loc) {
    panic_text("assertion failed", loc);
}

COLD _Noreturn void ofir_panic_str(const OfirStr *msg, const OfirLoc *loc) {
    ofir_panic(msg->ptr, msg->len, loc);
}
