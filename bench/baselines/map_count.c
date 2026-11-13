// Counting keys in a hash map: the C baseline of programs/map_count.of.
// C has no standard map, so this is a small open-addressing table that
// grows at a load of one half.
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

typedef struct {
    int64_t *keys;
    int64_t *values;
    char *used;
    size_t cap;
    size_t len;
} Map;

static uint64_t hash(int64_t key) {
    uint64_t h = (uint64_t)key * 0x9E3779B97F4A7C15ull;
    return h ^ (h >> 32);
}

static size_t slot(const Map *m, int64_t key) {
    size_t i = hash(key) & (m->cap - 1);
    while (m->used[i] && m->keys[i] != key) {
        i = (i + 1) & (m->cap - 1);
    }
    return i;
}

static void init(Map *m, size_t cap) {
    m->keys = malloc(cap * sizeof *m->keys);
    m->values = malloc(cap * sizeof *m->values);
    m->used = calloc(cap, 1);
    m->cap = cap;
    m->len = 0;
}

static int64_t *get_or_insert(Map *m, int64_t key) {
    if (2 * (m->len + 1) > m->cap) {
        Map old = *m;
        init(m, old.cap * 2);
        for (size_t i = 0; i < old.cap; i++) {
            if (old.used[i]) {
                size_t j = slot(m, old.keys[i]);
                m->used[j] = 1;
                m->keys[j] = old.keys[i];
                m->values[j] = old.values[i];
                m->len++;
            }
        }
        free(old.keys);
        free(old.values);
        free(old.used);
    }
    size_t i = slot(m, key);
    if (!m->used[i]) {
        m->used[i] = 1;
        m->keys[i] = key;
        m->values[i] = 0;
        m->len++;
    }
    return &m->values[i];
}

int main(void) {
    const int64_t n = 2000000, keys = 200000;
    Map counts;
    init(&counts, 16);
    int64_t seed = 7;
    for (int64_t i = 0; i < n; i++) {
        seed = (seed * 1103515245 + 12345) % 2147483648;
        *get_or_insert(&counts, seed % keys) += 1;
    }
    int64_t sum = 0;
    for (int64_t k = 0; k < keys; k++) {
        size_t i = slot(&counts, k);
        if (counts.used[i]) {
            sum += counts.values[i] * (k % 13);
        }
    }
    printf("%zu %lld\n", counts.len, (long long)sum);
    return 0;
}
