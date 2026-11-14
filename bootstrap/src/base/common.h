// Common definitions shared by every part of the stage 0 compiler.

#ifndef OFIR_COMMON_H
#define OFIR_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#define ARRAY_LEN(a) (sizeof(a) / sizeof((a)[0]))

// Allocation that never returns NULL: the compiler exits on out-of-memory.
void *xmalloc(size_t size);
void *xcalloc(size_t count, size_t size);
void *xrealloc(void *ptr, size_t size);

// Reports an internal compiler error and aborts.
_Noreturn void fatal(const char *format, ...);

// Writes `v` as printf's %g does with glibc on every system: `inf`, `-inf`,
// `nan` or `-nan`, and at least two exponent digits (MinGW writes three).
void print_float_g(FILE *out, double v);

#endif
