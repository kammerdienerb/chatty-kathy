#ifndef __SERVER_H__
#define __SERVER_H__

#include "common.h"
#include "thread_shared.h"

typedef struct {
    ck_common common;
    ck_fifo   outbox;
} ck_server;

extern ck_server S;

int start_server(void);
int run_server(void);

#endif
