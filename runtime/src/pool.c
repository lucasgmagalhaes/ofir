// The operating-system side of the small-block pool, whose logic is Ofir
// (std/core/pool.of, ADR 0041 and ADR 0042): it reserves the pool's range of
// address space and commits it chunk by chunk.
//
// No range is reserved on Windows, under AddressSanitizer, or when the
// environment sets OFIR_SYSTEM_ALLOC (tools/check_leaks.py does, so that
// valgrind sees every block): the pool is then off, and every block comes
// from malloc.

#if defined(__APPLE__)
#define _DARWIN_C_SOURCE   // MAP_ANONYMOUS
#endif

#include "ofir_rt.h"

#include <stdlib.h>

#if defined(__SANITIZE_ADDRESS__)
#define POOL_OFF 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define POOL_OFF 1
#endif
#endif
#if defined(_WIN32)
#define POOL_OFF 1
#endif

#ifdef POOL_OFF

void *ofir_mem_reserve(int64_t size) {
    (void)size;
    return NULL;
}

bool ofir_mem_commit(void *ptr, int64_t size) {
    (void)ptr;
    (void)size;
    return false;
}

#else

#include <sys/mman.h>

void *ofir_mem_reserve(int64_t size) {
    if (getenv("OFIR_SYSTEM_ALLOC")) {
        return NULL;
    }
    void *range = mmap(NULL, (size_t)size, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return range == MAP_FAILED ? NULL : range;
}

bool ofir_mem_commit(void *ptr, int64_t size) {
    return mprotect(ptr, (size_t)size, PROT_READ | PROT_WRITE) == 0;
}

#endif
