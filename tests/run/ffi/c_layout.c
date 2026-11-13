// The C side of c_layout.of: the same structs declared in C. Each check
// compares the size and alignment Ofir reports with C's, reads the fields
// Ofir wrote and writes new ones back.
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t tag;
    uint32_t len;
    uint16_t flags;
    uint64_t id;
} Header;

typedef struct {
    Header head;
    float scale;
    uint8_t bytes[3];
    double weight;
} Record;

typedef struct __attribute__((packed)) {
    uint8_t tag;
    uint32_t len;
    uint16_t flags;
} Wire;

typedef struct {
    alignas(16) uint8_t a;
    int32_t b;
} Block;

typedef enum { RED, GREEN, BLUE } Color;

static int32_t mismatch(int64_t size, int64_t align, size_t c_size, size_t c_align) {
    return ((size_t)size != c_size) + ((size_t)align != c_align);
}

int32_t c_header(Header *h, int64_t size, int64_t align) {
    int32_t wrong = mismatch(size, align, sizeof(Header), alignof(Header));
    wrong += offsetof(Header, len) != 4 || offsetof(Header, id) != 16;
    wrong += h->tag != 7 || h->len != 70000 || h->flags != 0xBEEF || h->id != 1ull << 40;
    h->tag = 8;
    h->len += 1;
    h->flags = 0xCAFE;
    h->id = UINT64_MAX;
    return wrong;
}

int32_t c_records(Record *r, int64_t n, int64_t size, int64_t align) {
    int32_t wrong = mismatch(size, align, sizeof(Record), alignof(Record));
    for (int64_t i = 0; i < n; i++) {
        wrong += r[i].head.tag != i || r[i].bytes[2] != 3 * i || r[i].weight != 0.5 * i;
        r[i].scale = (float)i * 1.5f;
        r[i].bytes[0] = (uint8_t)(100 + i);
    }
    return wrong;
}

int32_t c_wire(Wire *w, int64_t size, int64_t align) {
    int32_t wrong = mismatch(size, align, sizeof(Wire), alignof(Wire));
    wrong += offsetof(Wire, len) != 1 || offsetof(Wire, flags) != 5;
    wrong += w->tag != 1 || w->len != 0x01020304 || w->flags != 9;
    w->len = 0x0A0B0C0D;
    return wrong;
}

int32_t c_block(Block *b, int64_t size, int64_t align) {
    int32_t wrong = mismatch(size, align, sizeof(Block), alignof(Block));
    wrong += ((uintptr_t)b % 16) != 0;
    wrong += b->a != 5 || b->b != -6;
    b->b = 60;
    return wrong;
}

void c_next_color(Color *c) {
    *c = (Color)((*c + 1) % 3);
}

int32_t c_color_size(int64_t size) {
    return (size_t)size != sizeof(Color);
}
