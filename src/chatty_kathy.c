#include <yed/plugin.h>

#include "server.h"
#include "client.h"
#include "message.h"
#include "thread_shared.h"

pthread_t   tid;
yed_plugin *Self;

/****************************** yed interface ********************************/
void ck_start_client(int n_args, char **args);
void ck_start_server(int n_args, char **args);
void ck_send(int n_args, char **args);

yed_event_handler ck_pump_client;
void ck_pump_handler_client(yed_event *event);
yed_event_handler ck_pump_server;
void ck_pump_handler_server(yed_event *event);

static yed_buffer *get_or_make_buff(char *name) {
    yed_buffer *buff;
    int         i;

    buff = yed_get_buffer(name);

    if (buff == NULL) {
        buff = yed_create_buffer(name);
        buff->flags |= BUFF_RD_ONLY | BUFF_SPECIAL;
        for (i = 0; i < strlen(name); i += 1) {
            yed_append_to_line_no_undo(buff, 1, G(name[i]));
        }
    }

    return buff;
}
/*****************************************************************************/


int yed_plugin_boot(yed_plugin *self) {
    YED_PLUG_VERSION_CHECK();

    yed_plugin_set_command(self, "chatty-kathy-client", ck_start_client);
    yed_plugin_set_command(self, "chatty-kathy-server", ck_start_server);
    yed_plugin_set_command(self, "chatty-kathy-send",   ck_send);

    ck_pump_client.kind = EVENT_PRE_PUMP;
    ck_pump_client.fn   = ck_pump_handler_client;
    ck_pump_server.kind = EVENT_PRE_PUMP;
    ck_pump_server.fn   = ck_pump_handler_server;

    pthread_mutex_init(&C.common.ts.mtx, NULL);
    pthread_mutex_init(&S.common.ts.mtx, NULL);
    C.common.ts.status      = STAT_NOT_STARTED;
    S.common.ts.status      = STAT_NOT_STARTED;
    C.common.ts.msg_handled = 1;
    S.common.ts.msg_handled = 1;

    Self = self;

    return 0;
}


/* Wrappers for pthread */
static void *client_start_thread(void *arg) { start_client(); return NULL; }
static void *server_start_thread(void *arg) { start_server(); return NULL; }
static void *client_run_thread(void *arg)   { run_client();   return NULL; }
static void *server_run_thread(void *arg)   { run_server();   return NULL; }


/******************************** commands **********************************/
void ck_start_client(int n_args, char **args) {
#define THREAD_SHARED (C.common.ts)

    int   status;
    char *id;

    if (n_args != 0) {
        yed_cerr("did not expect arguments.. use the appropriate variables to configure chatty-kathy");
        return;
    }

    status = GET_STAT();

    if (status == STAT_CLIENT_RUNNING) {
        yed_cerr("a client is already running");
        return;
    }
    if (status == STAT_SERVER_RUNNING) {
        yed_cerr("a server is already running");
        return;
    }
    if (status != STAT_NOT_STARTED) {
        yed_cerr("can't start a client now -- status is %d", status);
        return;
    }

    if (!init_common(&C.common)) {
        PRINT_MSG();
        SET_STAT(STAT_NOT_STARTED);
        return;
    }

    id = yed_get_var("chatty-kathy-id");
    if (id == NULL) {
        yed_cerr("chatty-kathy-id must be set to run a client");
        return;
    }
    if (strlen(id) > ID_MAX) {
        yed_cerr("chatty-kathy-id is too long -- max length is %d", ID_MAX);
        return;
    }
    if (strlen(id) == 0) {
        yed_cerr("chatty-kathy-id empty");
        return;
    }
    strcpy(C.id, id);

    yed_cprint("starting chatty-kathy client..");
    SET_STAT(STAT_CLIENT_STARTING);

    pthread_create(&tid, NULL, client_start_thread, NULL);

    yed_plugin_add_event_handler(Self, ck_pump_client);
#undef THREAD_SHARED
}

void ck_start_server(int n_args, char **args) {
#define THREAD_SHARED (S.common.ts)

    int status;

    if (n_args != 0) {
        yed_cerr("did not expect arguments.. use the appropriate variables to configure chatty-kathy");
        return;
    }

    status = GET_STAT();

    if (status == STAT_CLIENT_RUNNING) {
        yed_cerr("a client is already running");
        return;
    }
    if (status == STAT_SERVER_RUNNING) {
        yed_cerr("a server is already running");
        return;
    }
    if (status != STAT_NOT_STARTED) {
        yed_cerr("can't start a server now -- status is %d", status);
        return;
    }

    if (!init_common(&S.common)) {
        PRINT_MSG();
        SET_STAT(STAT_NOT_STARTED);
        return;
    }

    yed_cprint("starting chatty-kathy server..");
    SET_STAT(STAT_SERVER_STARTING);

    pthread_create(&tid, NULL, server_start_thread, NULL);

    yed_plugin_add_event_handler(Self, ck_pump_server);

#undef THREAD_SHARED
}

void ck_send(int n_args, char **args) {
#define THREAD_SHARED (C.common.ts)

    ck_imessage  imsg;
    char        *lazy_space;
    int          i;

    if (GET_STAT() != STAT_CLIENT_RUNNING) {
        yed_cerr("a client is not running");
        return;
    }

    imsg.content_len = 0;
    lazy_space       = "";
    for (i = 0; i < n_args; i += 1) {
        imsg.content_len += strlen(lazy_space) + strlen(args[i]);
        lazy_space = " ";
    }

    if (imsg.content_len == 0) {
        yed_cerr("message is empty");
        return;
    }

    imsg.contents    = malloc(imsg.content_len + 1); /* +1 for zero byte */
    imsg.contents[0] = 0;

    lazy_space = "";
    for (i = 0; i < n_args; i += 1) {
        strcat(imsg.contents, lazy_space);
        strcat(imsg.contents, args[i]);
        lazy_space = " ";
    }

    fifo_put(&C.outbox, &imsg);

#undef THREAD_SHARED
}

/****************************************************************************/



static void empty_inbox(void) {
    ck_imessage  imsg;
    yed_buffer  *buffer;
    int          new_row;
    int          n_added;
    int          glyph_len;
    yed_glyph   *g;

    buffer = get_or_make_buff("*chatty-kathy-messages");

    while (fifo_get(&C.inbox, &imsg)) {
        new_row = yed_buffer_add_line_no_undo(buffer);

        n_added = 0;
        g       = (yed_glyph*)imsg.user_id;
        while (n_added < imsg.user_id_len) {
            glyph_len = yed_get_glyph_len(*g);
            yed_append_to_line_no_undo(buffer, new_row, *g);
            g = ((void*)g) + glyph_len;
            n_added += glyph_len;
        }

        yed_append_to_line_no_undo(buffer, new_row, G(':'));
        yed_append_to_line_no_undo(buffer, new_row, G(' '));

        n_added = 0;
        g       = (yed_glyph*)imsg.contents;
        while (n_added < imsg.content_len) {
            glyph_len = yed_get_glyph_len(*g);
            yed_append_to_line_no_undo(buffer, new_row, *g);
            g = ((void*)g) + glyph_len;
            n_added += glyph_len;
        }
    }
}

/******************************** handlers **********************************/
void ck_pump_handler_client(yed_event *event) {
#define THREAD_SHARED (C.common.ts)

LOG_CMD_ENTER("chatty-kathy-client");

    int status;

    status = GET_STAT();

    PRINT_MSG();

    switch (status) {
        case STAT_CLIENT_STARTING:
            break;
        case STAT_CLIENT_STARTED:
            pthread_create(&tid, NULL, client_run_thread, NULL);
            break;
        case STAT_CLIENT_RUNNING:
            empty_inbox();
            break;

        case STAT_NOT_STARTED:
        case STAT_CONFIG_ERR:
        case STAT_CLIENT_START_ERR:
        case STAT_CLIENT_RUN_ERR:
            goto del;
        default:
            yed_cerr("invalid status");
del:;
            yed_delete_event_handler(ck_pump_client);
            SET_STAT(STAT_NOT_STARTED);
    }

LOG_EXIT();

#undef THREAD_SHARED
}

void ck_pump_handler_server(yed_event *event) {
#define THREAD_SHARED (S.common.ts)

LOG_CMD_ENTER("chatty-kathy-server");

    int status;

    status = GET_STAT();

    PRINT_MSG();

    switch (status) {
        case STAT_SERVER_STARTING:
            break;
        case STAT_SERVER_STARTED:
            pthread_create(&tid, NULL, server_run_thread, NULL);
            break;
        case STAT_SERVER_RUNNING:
            break;

        case STAT_NOT_STARTED:
        case STAT_CONFIG_ERR:
        case STAT_SERVER_START_ERR:
        case STAT_SERVER_RUN_ERR:
            goto del;
        default:
            yed_cerr("invalid status");
del:;
            yed_delete_event_handler(ck_pump_server);
            SET_STAT(STAT_NOT_STARTED);
    }

LOG_EXIT();

#undef THREAD_SHARED
}
/****************************************************************************/
