// Locks and wait groups (spec 14.4, docs/design/runtime.md).
//
// Only `std.sync` calls this file; its atomics are compiler intrinsics
// (`std.mem.atomic`, ADR 0042). The locks and wait groups live on the
// heap, so that the Ofir values that own them can move.

#include "ofir_rt.h"

#include <pthread.h>
#include <time.h>

// Waits on `cond` until it is signalled or the monotonic clock reaches
// `deadline` (ofir_now_ns). The timed wait takes a wall-clock time, so the
// time left is measured on the monotonic clock and added to the wall
// clock; callers loop, so an early wake-up only costs a recheck.
bool ofir_wait_until(void *cond, void *lock, int64_t deadline) {
    int64_t left = deadline - ofir_now_ns();
    if (left <= 0) {
        return false;
    }
    struct timespec t;
    clock_gettime(CLOCK_REALTIME, &t);
    int64_t ns = t.tv_nsec + left % 1000000000;
    t.tv_sec += (time_t)(left / 1000000000 + ns / 1000000000);
    t.tv_nsec = (long)(ns % 1000000000);
    pthread_cond_timedwait(cond, lock, &t);
    return true;
}

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

bool ofir_waitgroup_wait_until(void *w, int64_t deadline) {
    waitgroup *wg = w;
    pthread_mutex_lock(&wg->lock);
    while (wg->count > 0 && ofir_wait_until(&wg->zero, &wg->lock, deadline)) {
    }
    bool zero = wg->count == 0;
    pthread_mutex_unlock(&wg->lock);
    return zero;
}

void ofir_waitgroup_free(void *w) {
    waitgroup *wg = w;
    pthread_cond_destroy(&wg->zero);
    pthread_mutex_destroy(&wg->lock);
    ofir_free(wg);
}
