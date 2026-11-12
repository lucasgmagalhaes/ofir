// Memory allocation: blocks of up to OFIR_POOL_MAX bytes come from the pool
// (pool.c), larger ones from malloc; both panic when out of memory.
//
// On Windows every block comes from `_aligned_malloc`, so that `ofir_free`
// releases aligned and plain blocks alike; elsewhere `free` accepts what
// `posix_memalign` returns.

#include "ofir_rt.h"

#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <malloc.h>
#endif

static _Noreturn void out_of_memory(void) {
    static const char msg[] = "out of memory";
    ofir_panic((const uint8_t *)msg, (int64_t)strlen(msg), NULL);
}

static size_t at_least_one(int64_t size) {
    return size > 0 ? (size_t)size : 1;
}

void *ofir_alloc(int64_t size) {
    if ((uint64_t)size - 1 < OFIR_POOL_MAX) {   // 1 to OFIR_POOL_MAX bytes
        void *block = ofir_pool_alloc(size);
        if (block) {
            return block;
        }
    }
#ifdef _WIN32
    void *ptr = _aligned_malloc(at_least_one(size), 16);
#else
    void *ptr = malloc(at_least_one(size));
#endif
    if (!ptr) {
        out_of_memory();
    }
    return ptr;
}

void *ofir_realloc(void *ptr, int64_t size) {
    if (!ptr) {
        return ofir_alloc(size);
    }
    if (ofir_pool_owns(ptr)) {
        int64_t room = ofir_pool_block_size(ptr);
        if (size <= room && size > room / 2) {
            return ptr;
        }
        void *moved = ofir_alloc(size);
        memcpy(moved, ptr, size < room ? at_least_one(size) : (size_t)room);
        ofir_pool_free(ptr);
        return moved;
    }
#ifdef _WIN32
    void *grown = _aligned_realloc(ptr, at_least_one(size), 16);
#else
    void *grown = realloc(ptr, at_least_one(size));
#endif
    if (!grown) {
        out_of_memory();
    }
    return grown;
}

// Memory aligned to `align`, a power of two above 16.
void *ofir_alloc_aligned(int64_t size, int64_t align) {
#ifdef _WIN32
    void *ptr = _aligned_malloc(at_least_one(size), (size_t)align);
#else
    void *ptr = NULL;
    if (posix_memalign(&ptr, (size_t)align, at_least_one(size)) != 0) {
        ptr = NULL;
    }
#endif
    if (!ptr) {
        out_of_memory();
    }
    return ptr;
}

// Grows a block from `ofir_alloc_aligned` with the same `align`.
void *ofir_realloc_aligned(void *ptr, int64_t size, int64_t align) {
#ifdef _WIN32
    void *grown = _aligned_realloc(ptr, at_least_one(size), (size_t)align);
    if (!grown) {
        out_of_memory();
    }
    return grown;
#else
    // `realloc` keeps only malloc's alignment: a block it moved to a
    // misaligned address is copied once more.
    void *grown = ofir_realloc(ptr, size);
    if (((uintptr_t)grown & (uintptr_t)(align - 1)) == 0) {
        return grown;
    }
    void *aligned = ofir_alloc_aligned(size, align);
    memcpy(aligned, grown, at_least_one(size));
    free(grown);
    return aligned;
#endif
}

void ofir_free(void *ptr) {
    if (ofir_pool_try_free(ptr)) {
        return;
    }
#ifdef _WIN32
    _aligned_free(ptr);
#else
    free(ptr);
#endif
}
