// Tasks on OS threads of their own (spec 14.2, docs/design/runtime.md).
//
// Only `std.task` calls this file, so a program that starts no task does
// not link it. Each task's job is a heap value that `std.task` owns; the
// thread hands it back to `ofir_task_entry`, which `std.task` defines.

#include "ofir_rt.h"

#include <pthread.h>
#include <stdlib.h>

void ofir_task_entry(void *job);

static void *start(void *job) {
    ofir_task_entry(job);
    ofir_pool_thread_exit();
    ofir_stdout_publish();   // a last line that has no newline
    return NULL;
}

void *ofir_task_spawn(void *job) {
    pthread_t *thread = ofir_alloc(sizeof *thread);
    ofir_tasks_started();
    if (pthread_create(thread, NULL, start, job) != 0) {
        static const char msg[] = "cannot start a task";
        ofir_panic((const uint8_t *)msg, sizeof msg - 1, NULL);
    }
    return thread;
}

void ofir_task_join(void *thread) {
    pthread_join(*(pthread_t *)thread, NULL);
    ofir_free(thread);
}
