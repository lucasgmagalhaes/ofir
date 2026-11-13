// Channels between tasks (spec 14.4, docs/design/runtime.md).
//
// Only `std.sync` calls this file. A channel is a queue of values that
// `std.sync` boxed, guarded by a lock. It counts its senders and knows
// whether its receiver is alive; the last of them to go frees it. The
// values are opaque here: `std.sync` drops those the receiver never took.
// A bounded channel holds at most `capacity` values: a send waits for
// room, or fails once the receiver closes.

#include "ofir_rt.h"

#include <pthread.h>
#include <stdbool.h>

typedef struct node {
    struct node *next;
    void *value;
} node;

typedef struct {
    pthread_mutex_t lock;
    pthread_cond_t ready;   // a value arrived, or the last sender went
    pthread_cond_t space;   // a value left, or the receiver closed
    node *head;
    node *tail;
    int64_t count;
    int64_t capacity;       // 0: unbounded
    int64_t senders;
    bool receiver;
} channel;

void *ofir_channel_new_bounded(int64_t capacity) {
    channel *ch = ofir_alloc(sizeof *ch);
    pthread_mutex_init(&ch->lock, NULL);
    pthread_cond_init(&ch->ready, NULL);
    pthread_cond_init(&ch->space, NULL);
    ch->head = NULL;
    ch->tail = NULL;
    ch->count = 0;
    ch->capacity = capacity;
    ch->senders = 1;
    ch->receiver = true;
    return ch;
}

void *ofir_channel_new(void) {
    return ofir_channel_new_bounded(0);
}

static void destroy(channel *ch) {
    pthread_cond_destroy(&ch->space);
    pthread_cond_destroy(&ch->ready);
    pthread_mutex_destroy(&ch->lock);
    ofir_free(ch);
}

bool ofir_channel_send(void *c, void *value) {
    channel *ch = c;
    pthread_mutex_lock(&ch->lock);
    while (ch->receiver && ch->capacity > 0 && ch->count >= ch->capacity) {
        pthread_cond_wait(&ch->space, &ch->lock);
    }
    if (!ch->receiver) {
        pthread_mutex_unlock(&ch->lock);
        return false;
    }
    node *n = ofir_alloc(sizeof *n);
    n->next = NULL;
    n->value = value;
    if (ch->tail) {
        ch->tail->next = n;
    } else {
        ch->head = n;
    }
    ch->tail = n;
    ch->count++;
    pthread_cond_signal(&ch->ready);
    pthread_mutex_unlock(&ch->lock);
    return true;
}

// The first value, or NULL. The lock is held.
static void *pop(channel *ch) {
    node *n = ch->head;
    if (!n) {
        return NULL;
    }
    ch->head = n->next;
    if (!ch->head) {
        ch->tail = NULL;
    }
    void *value = n->value;
    ofir_free(n);
    ch->count--;
    pthread_cond_signal(&ch->space);
    return value;
}

void *ofir_channel_recv(void *c) {
    channel *ch = c;
    pthread_mutex_lock(&ch->lock);
    while (!ch->head && ch->senders > 0) {
        pthread_cond_wait(&ch->ready, &ch->lock);
    }
    void *value = pop(ch);
    pthread_mutex_unlock(&ch->lock);
    return value;
}

void *ofir_channel_try_recv(void *c) {
    channel *ch = c;
    pthread_mutex_lock(&ch->lock);
    void *value = pop(ch);
    pthread_mutex_unlock(&ch->lock);
    return value;
}

void ofir_channel_add_sender(void *c) {
    channel *ch = c;
    pthread_mutex_lock(&ch->lock);
    ch->senders++;
    pthread_mutex_unlock(&ch->lock);
}

void ofir_channel_drop_sender(void *c) {
    channel *ch = c;
    pthread_mutex_lock(&ch->lock);
    bool last = --ch->senders == 0 && !ch->receiver;
    pthread_cond_broadcast(&ch->ready);
    pthread_mutex_unlock(&ch->lock);
    if (last) {
        destroy(ch);
    }
}

void ofir_channel_close(void *c) {
    channel *ch = c;
    pthread_mutex_lock(&ch->lock);
    ch->receiver = false;
    pthread_cond_broadcast(&ch->space);
    pthread_mutex_unlock(&ch->lock);
}

void ofir_channel_drop_receiver(void *c) {
    channel *ch = c;
    pthread_mutex_lock(&ch->lock);
    bool last = ch->senders == 0;
    pthread_mutex_unlock(&ch->lock);
    if (last) {
        destroy(ch);
    }
}
