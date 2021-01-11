#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <netdb.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <pthread.h>

#include "thread_shared.h"
#include "array.h"

#define STAT_NOT_STARTED      (0)
#define STAT_CLIENT_STARTING  (1)
#define STAT_CLIENT_START_ERR (2)
#define STAT_CLIENT_STARTED   (3)
#define STAT_CLIENT_RUNNING   (4)
#define STAT_CLIENT_RUN_ERR   (5)
#define STAT_SERVER_STARTING  (6)
#define STAT_SERVER_START_ERR (7)
#define STAT_SERVER_STARTED   (8)
#define STAT_SERVER_RUNNING   (9)
#define STAT_SERVER_RUN_ERR   (10)
#define STAT_CONFIG_ERR       (11)


extern const int yes;

typedef struct {
    ck_thread_shared ts;
    int              port;
    char             port_str[32];
    char             server[4096];
    int              sock_fd;
    struct addrinfo  hints;
} ck_common;

int init_common(ck_common *common);

typedef struct {
    array_t         items;
    pthread_mutex_t mtx;
} ck_fifo;

#define fifo_make(T)                                  \
(ck_fifo){ array_make(T), PTHREAD_MUTEX_INITIALIZER }

int  fifo_get(ck_fifo *fifo, void *dst);
void fifo_put(ck_fifo *fifo, void *item);

int   get_port(void);
int   get_server(char *server);
int   get_backlog(void);
void *get_in_addr(struct sockaddr *sa);

int recv_all(int fd, void *buff, int n);
int send_all(int fd, void *buff, int n);

#endif
