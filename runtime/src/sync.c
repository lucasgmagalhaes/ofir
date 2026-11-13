// Locks and wait groups (spec 14.4, docs/design/runtime.md).
//
// Only `std.sync` calls this file; its atomics are compiler intrinsics
// (`std.mem.atomic`, ADR 0042). The locks and wait groups live on the
// heap, so that the Ofir values that own them can move.

#include "ofir_rt.h"

#include <pthread.h>

void *ofir_mutex_new(void) {
    pthread_mutex_t *m = ofir_alloc(sizeof *m);
    pthread_mutex_init(m, NULL);
    return m;
}

void ofir_mutex_lock(void *m) {
    pthread_mutex_lock(m);
}

void ofir_mutex_unlock(void *m) {
    pthread_mutex_unlock(m);
}

void ofir_mutex_free(void *m) {
    pthread_mutex_destroy(m);
    ofir_free(m);
}

void *ofir_rwlock_new(void) {
    pthread_rwlock_t *l = ofir_alloc(sizeof *l);
    pthread_rwlock_init(l, NULL);
    return l;
}

void ofir_rwlock_read(void *l) {
    pthread_rwlock_rdlock(l);
}

void ofir_rwlock_write(void *l) {
    pthread_rwlock_wrlock(l);
}

void ofir_rwlock_unlock(void *l) {
    pthread_rwlock_unlock(l);
}

void ofir_rwlock_free(void *l) {
    pthread_rwlock_destroy(l);
    ofir_free(l);
}

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t zero;   // the count reached zero
    int64_t count;
} waitgroup;

void *ofir_waitgroup_new(void) {
    waitgroup *wg = ofir_alloc(sizeof *wg);
    pthread_mutex_init(&wg->lock, NULL);
    pthread_cond_init(&wg->zero, NULL);
    wg->count = 0;
    return wg;
}

bool ofir_waitgroup_add(void *w, int64_t delta) {
    waitgroup *wg = w;
    pthread_mutex_lock(&wg->lock);
    bool ok = wg->count + delta >= 0;
    if (ok) {
        wg->count += delta;
        if (wg->count == 0) {
            pthread_cond_broadcast(&wg->zero);
        }
    }
    pthread_mutex_unlock(&wg->lock);
    return ok;
}

void ofir_waitgroup_wait(void *w) {
    waitgroup *wg = w;
    pthread_mutex_lock(&wg->lock);
    while (wg->count > 0) {
        pthread_cond_wait(&wg->zero, &wg->lock);
    }
    pthread_mutex_unlock(&wg->lock);
}

void ofir_waitgroup_free(void *w) {
    waitgroup *wg = w;
    pthread_cond_destroy(&wg->zero);
    pthread_mutex_destroy(&wg->lock);
    ofir_free(wg);
}
