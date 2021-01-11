#ifndef __CLIENT_H__
#define __CLIENT_H__

#include "common.h"
#include "thread_shared.h"
#include "message.h"

typedef struct {
    ck_common common;
    char      id[ID_MAX];
    ck_fifo   outbox;
    ck_fifo   inbox;
} ck_client;

extern ck_client C;

int start_client(void);
int run_client(void);

#endif
