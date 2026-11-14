#include <errno.h>
#include <stdint.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <pthread.h>
#endif

int32_t of_describe(int32_t v);

int32_t c_fail(int32_t code) {
    errno = code;
    return -1;
}

int32_t c_errno(void) {
    return errno;
}

struct job {
    int32_t in;
    int32_t out;
};

#ifdef _WIN32
static DWORD WINAPI work(LPVOID arg) {
    struct job *j = arg;
    j->out = of_describe(j->in);
    return 0;
}
#else
static void *work(void *arg) {
    struct job *j = arg;
    j->out = of_describe(j->in);
    return 0;
}
#endif

int32_t c_in_thread(int32_t v) {
    struct job j = {v, -1};
#ifdef _WIN32
    HANDLE t = CreateThread(NULL, 0, work, &j, 0, NULL);
    if (t == NULL) {
        return -1;
    }
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
#else
    pthread_t t;
    if (pthread_create(&t, NULL, work, &j) != 0) {
        return -1;
    }
    pthread_join(t, NULL);
#endif
    return j.out;
}
