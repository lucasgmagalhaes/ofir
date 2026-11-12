// The small-block pool behind ofir_alloc (ADR 0041): blocks of up to
// POOL_MAX bytes come from chunks of one size class each, carved out of one
// reserved range of address space. A block has no header: ofir_free knows a
// pool block by its address, and the chunk it lies in gives its size class.
//
// Each thread keeps a free list per class. A list that grows past two batches
// gives one batch to a global list, where other threads take it back, so
// memory freed by another thread than the one that allocated it is reused.
// Chunks are never returned to the system.
//
// The pool is off on Windows, under AddressSanitizer, and when the
// environment sets OFIR_SYSTEM_ALLOC (tools/check_leaks.py does, so that
// valgrind sees every block): then every block comes from malloc.

#include "ofir_rt.h"

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

void *ofir_pool_alloc(int64_t size) {
    (void)size;
    return NULL;
}

bool ofir_pool_owns(const void *ptr) {
    (void)ptr;
    return false;
}

int64_t ofir_pool_block_size(const void *ptr) {
    (void)ptr;
    return 0;
}

void ofir_pool_free(void *ptr) {
    (void)ptr;
}

bool ofir_pool_try_free(void *ptr) {
    (void)ptr;
    return false;
}

void ofir_pool_thread_exit(void) {
}

#else

#include <stdatomic.h>
#include <stdlib.h>
#include <sys/mman.h>

#define POOL_CHUNK ((size_t)64 * 1024)
#define POOL_RESERVE ((size_t)1 << 34)   // 16 GiB of address space, committed by chunk
#define POOL_CHUNKS (POOL_RESERVE / POOL_CHUNK)
#define CLASSES 16
#define BATCH_BYTES ((size_t)32 * 1024)   // what a batch holds, whatever the class

// 16 to 128 by 16, to 256 by 32, to 512 by 64: all multiples of 16.
static const uint16_t CLASS_SIZE[CLASSES] = {16,  32,  48,  64,  80,  96,  112, 128,
                                             160, 192, 224, 256, 320, 384, 448, 512};

// The class of a size of 1 to 512 bytes, by (size + 15) / 16.
static const uint8_t CLASS_OF[33] = {0,  0,  1,  2,  3,  4,  5,  6,  7,  8,  8,
                                     9,  9,  10, 10, 11, 11, 12, 12, 12, 12, 13,
                                     13, 13, 13, 14, 14, 14, 14, 15, 15, 15, 15};

typedef struct Block {
    struct Block *next;    // the next free block
    struct Block *batch;   // in the first block of a global batch: the next batch
} Block;

typedef struct {
    Block *free;
    size_t count;   // blocks in `free`
    char *bump;     // the part of the thread's chunk not handed out yet
    char *end;
} Bin;

typedef struct {
    atomic_flag lock;
    Block *batches;
} Global;

enum { POOL_UNSET, POOL_STARTING, POOL_READY, POOL_DISABLED };

static atomic_int state;
static char *base;                  // set once, before state is POOL_READY
static atomic_size_t next_chunk;
static uint8_t chunk_class[POOL_CHUNKS];
static Global global[CLASSES];
static _Thread_local Bin bins[CLASSES];

static bool start(void) {
    int expected = POOL_UNSET;
    if (atomic_load_explicit(&state, memory_order_acquire) == POOL_UNSET
        && atomic_compare_exchange_strong(&state, &expected, POOL_STARTING)) {
        void *range = MAP_FAILED;
        if (!getenv("OFIR_SYSTEM_ALLOC")) {
            range = mmap(NULL, POOL_RESERVE, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        }
        base = range == MAP_FAILED ? NULL : range;
        atomic_store(&state, base ? POOL_READY : POOL_DISABLED);
    }
    int now;
    while ((now = atomic_load(&state)) == POOL_STARTING) {
    }
    return now == POOL_READY;
}

static size_t batch_blocks(int c) {
    return BATCH_BYTES / CLASS_SIZE[c];
}

static void lock(Global *g) {
    while (atomic_flag_test_and_set_explicit(&g->lock, memory_order_acquire)) {
    }
}

static void unlock(Global *g) {
    atomic_flag_clear_explicit(&g->lock, memory_order_release);
}

static void give_batch(int c, Block *first) {
    Global *g = &global[c];
    lock(g);
    first->batch = g->batches;
    g->batches = first;
    unlock(g);
}

static Block *take_batch(int c) {
    Global *g = &global[c];
    lock(g);
    Block *first = g->batches;
    if (first) {
        g->batches = first->batch;
    }
    unlock(g);
    return first;
}

// A new chunk for class `c`, or false when the range is used up.
static bool new_chunk(Bin *b, int c) {
    size_t index = atomic_fetch_add(&next_chunk, 1);
    if (index >= POOL_CHUNKS) {
        return false;
    }
    char *chunk = base + index * POOL_CHUNK;
    if (mprotect(chunk, POOL_CHUNK, PROT_READ | PROT_WRITE) != 0) {
        return false;
    }
    chunk_class[index] = (uint8_t)c;
    b->bump = chunk;
    b->end = chunk + POOL_CHUNK / CLASS_SIZE[c] * CLASS_SIZE[c];   // a whole number of blocks
    return true;
}

// Out of line, so that the fast path of ofir_pool_alloc saves no registers.
__attribute__((noinline, cold)) static void *refill(Bin *b, int c) {
    if (atomic_load_explicit(&state, memory_order_acquire) != POOL_READY && !start()) {
        return NULL;
    }
    Block *batch = take_batch(c);
    if (batch) {
        b->free = batch->next;
        b->count = 0;   // at most the blocks in the list: a thread's last batch may hold fewer
        return batch;
    }
    if (b->bump == b->end && !new_chunk(b, c)) {
        return NULL;
    }
    void *block = b->bump;
    b->bump += CLASS_SIZE[c];
    return block;
}

void *ofir_pool_alloc(int64_t size) {
    int c = CLASS_OF[(size + 15) >> 4];
    Bin *b = &bins[c];
    Block *block = b->free;
    if (block) {
        b->free = block->next;
        b->count -= b->count > 0;
        return block;
    }
    if (b->bump != b->end) {
        b->bump += CLASS_SIZE[c];
        return b->bump - CLASS_SIZE[c];
    }
    return refill(b, c);
}

bool ofir_pool_owns(const void *ptr) {
    return base && (uintptr_t)ptr - (uintptr_t)base < POOL_RESERVE;
}

int64_t ofir_pool_block_size(const void *ptr) {
    return CLASS_SIZE[chunk_class[((const char *)ptr - base) / POOL_CHUNK]];
}

// Out of line: a full list is rare.
__attribute__((noinline, cold)) static void give_back(Bin *b, int c, Block *block) {
    size_t batch = batch_blocks(c);
    Block *last = block;   // the batch is the first `batch` blocks
    for (size_t i = 1; i < batch; i++) {
        last = last->next;
    }
    b->free = last->next;
    b->count -= batch;
    last->next = NULL;
    give_batch(c, block);
}

bool ofir_pool_try_free(void *ptr) {
    if (!ofir_pool_owns(ptr)) {
        return false;
    }
    ofir_pool_free(ptr);
    return true;
}

void ofir_pool_free(void *ptr) {
    int c = chunk_class[((char *)ptr - base) / POOL_CHUNK];
    Bin *b = &bins[c];
    Block *block = ptr;
    block->next = b->free;
    b->free = block;
    if (++b->count >= 2 * batch_blocks(c)) {
        give_back(b, c, block);
    }
}

// Gives the thread's free blocks, and the rest of its chunks, to the other
// threads: called when a task's thread ends.
void ofir_pool_thread_exit(void) {
    for (int c = 0; c < CLASSES; c++) {
        Bin *b = &bins[c];
        for (; b->bump != b->end; b->bump += CLASS_SIZE[c]) {
            Block *block = (Block *)b->bump;
            block->next = b->free;
            b->free = block;
        }
        if (b->free) {
            give_batch(c, b->free);
        }
        b->free = NULL;
        b->count = 0;
    }
}

#endif
