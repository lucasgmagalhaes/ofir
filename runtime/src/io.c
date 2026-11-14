// Standard output buffer (docs/design/runtime.md, 2): line buffered on a
// terminal, block buffered (64 KiB) otherwise. Once a task starts
// (task.c), a lock guards the buffer, and each thread gathers its writes
// into a line of its own that reaches the buffer whole, so the pieces of
// one `println` never interleave with another thread's. Until then writes
// take no lock.

#include "ofir_rt.h"

#include <errno.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BUFFER_SIZE (64 * 1024)

static uint8_t buffer[BUFFER_SIZE];
static int64_t used;
static int line_buffered = -1;   // unknown until the first write
static atomic_bool threaded;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;

void ofir_tasks_started(void) {
    atomic_store(&threaded, true);
}

void ofir_write(int fd, const uint8_t *ptr, int64_t len) {
    while (len > 0) {
        ssize_t n = write(fd, ptr, (size_t)len);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return;   // nowhere to report the error: a closed pipe, a full disk
        }
        ptr += n;
        len -= n;
    }
}

// Whether a file descriptor is a terminal (std.io.isTerminal, ADR 0060).
bool ofir_is_terminal(int64_t fd) {
    return isatty((int)fd) != 0;
}

static _Thread_local uint8_t line[1024];
static _Thread_local int64_t line_used;

static void flush_buffer(void) {
    ofir_write(1, buffer, used);
    used = 0;
}

static void write_buffered(const uint8_t *ptr, int64_t len) {
    if (line_buffered < 0) {
        line_buffered = isatty(1);
        // A library has no `main` to flush at the end (ADR 0064).
        atexit(ofir_stdout_flush);
    }
    if (used + len > BUFFER_SIZE) {
        flush_buffer();
        if (len > BUFFER_SIZE) {
            ofir_write(1, ptr, len);
            return;
        }
    }
    memcpy(buffer + used, ptr, (size_t)len);
    used += len;
    if (line_buffered && memchr(ptr, '\n', (size_t)len)) {
        flush_buffer();
    }
}

static void write_locked(const uint8_t *ptr, int64_t len) {
    pthread_mutex_lock(&lock);
    write_buffered(ptr, len);
    pthread_mutex_unlock(&lock);
}

void ofir_stdout_publish(void) {
    if (line_used > 0) {
        write_locked(line, line_used);
        line_used = 0;
    }
}

void ofir_stdout_flush(void) {
    if (!atomic_load_explicit(&threaded, memory_order_relaxed)) {
        flush_buffer();
        return;
    }
    ofir_stdout_publish();
    pthread_mutex_lock(&lock);
    flush_buffer();
    pthread_mutex_unlock(&lock);
}

void ofir_stdout_write(const uint8_t *ptr, int64_t len) {
    if (!atomic_load_explicit(&threaded, memory_order_relaxed)) {
        write_buffered(ptr, len);
        return;
    }
    if (line_used + len > (int64_t)sizeof line) {
        ofir_stdout_publish();
        if (len > (int64_t)sizeof line) {
            write_locked(ptr, len);
            return;
        }
    }
    memcpy(line + line_used, ptr, (size_t)len);
    line_used += len;
    if (memchr(ptr, '\n', (size_t)len)) {
        ofir_stdout_publish();
    }
}

void ofir_print_str(const OfirStr *s) {
    ofir_stdout_write(s->ptr, s->len);
}

void ofir_print_int(int64_t value) {
    char text[OFIR_FORMAT_SIZE];
    ofir_stdout_write((const uint8_t *)text, ofir_format_int(text, value));
}

void ofir_print_uint(uint64_t value) {
    char text[OFIR_FORMAT_SIZE];
    ofir_stdout_write((const uint8_t *)text, ofir_format_uint(text, value));
}

void ofir_print_float(double value) {
    char text[OFIR_FORMAT_SIZE];
    ofir_stdout_write((const uint8_t *)text, ofir_format_float(text, value));
}

void ofir_print_f32(float value) {
    char text[OFIR_FORMAT_SIZE];
    ofir_stdout_write((const uint8_t *)text, ofir_format_f32(text, value));
}

void ofir_print_bool(bool value) {
    ofir_stdout_write((const uint8_t *)(value ? "true" : "false"), value ? 4 : 5);
}

void ofir_print_newline(void) {
    ofir_stdout_write((const uint8_t *)"\n", 1);
}
