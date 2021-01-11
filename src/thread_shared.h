#ifndef __THREAD_SHARED_H__
#define __THREAD_SHARED_H__

typedef struct {
    pthread_mutex_t mtx;
    int             status;
    int             msg_kind; /* 0 = info, 1 = err */
    int             msg_handled;
    char            msg[4096];
} ck_thread_shared;


#define GET_STAT() (THREAD_SHARED.status)

#define SET_STAT(stat)                                     \
do {                                                       \
pthread_mutex_lock(&THREAD_SHARED.mtx); {                  \
    THREAD_SHARED.status = (stat);                         \
} pthread_mutex_unlock(&THREAD_SHARED.mtx);                \
} while (0)

#define SET_MSG(...)                                       \
do {                                                       \
pthread_mutex_lock(&THREAD_SHARED.mtx); {                  \
    if (snprintf(THREAD_SHARED.msg,                        \
                 sizeof(THREAD_SHARED.msg),                \
                 __VA_ARGS__)) {}                          \
    THREAD_SHARED.msg_kind    = 0;                         \
    THREAD_SHARED.msg_handled = 0;                         \
} pthread_mutex_unlock(&THREAD_SHARED.mtx);                \
} while (0)

#define SET_ERR(stat, ...)                                 \
do {                                                       \
pthread_mutex_lock(&THREAD_SHARED.mtx); {                  \
    if (snprintf(THREAD_SHARED.msg,                        \
                 sizeof(THREAD_SHARED.msg),                \
                 __VA_ARGS__)) {}                          \
    THREAD_SHARED.msg_kind    = 1;                         \
    THREAD_SHARED.msg_handled = 0;                         \
    THREAD_SHARED.status      = (stat);                    \
} pthread_mutex_unlock(&THREAD_SHARED.mtx);                \
} while (0)

#define PRINT_MSG()                                        \
do {                                                       \
pthread_mutex_lock(&THREAD_SHARED.mtx); {                  \
    if (!THREAD_SHARED.msg_handled) {                      \
        if (THREAD_SHARED.msg_kind == 1) {                 \
            yed_cerr("%s", THREAD_SHARED.msg);             \
        } else {                                           \
            yed_cprint("%s", THREAD_SHARED.msg);           \
        }                                                  \
        THREAD_SHARED.msg_handled = 1;                     \
    }                                                      \
} pthread_mutex_unlock(&THREAD_SHARED.mtx);                \
} while (0)

#endif
