#include "server.h"
#include "message.h"

#include <yed/tree.h>
#include <pthread.h>

typedef char *str_t;

typedef struct {
    char id[ID_MAX];
    int  fd;
} ck_user;

#define inline static inline
use_tree(str_t, ck_user);
#undef inline

ck_server            S;
tree(str_t, ck_user) users;
pthread_mutex_t      user_mtx;

#define THREAD_SHARED (S.common.ts)

static ck_user *get_user(char *id) {
    ck_user                 *user;
    tree_it(str_t, ck_user)  it;

    user = NULL;

    pthread_mutex_lock(&user_mtx);
    it = tree_lookup(users, id);
    if (tree_it_good(it)) {
        user = &tree_it_val(it);
    }
    pthread_mutex_unlock(&user_mtx);

    return user;
}

static ck_user *add_user(char *id) {
    ck_user                 *user;
    tree_it(str_t, ck_user)  it;
    ck_user                  new_user;

    user = NULL;

    memset(&new_user, 0, sizeof(new_user));

    pthread_mutex_lock(&user_mtx);
    it = tree_lookup(users, id);
    if (!tree_it_good(it)) {
        it   = tree_insert(users, id, new_user);
        user = &tree_it_val(it);
        strcpy(user->id, id);
    }
    pthread_mutex_unlock(&user_mtx);

    return user;
}

static void del_user(char *id) {
    pthread_mutex_lock(&user_mtx);
    tree_delete(users, id);
    pthread_mutex_unlock(&user_mtx);
}

int start_server(void) {
    int              err;
    struct addrinfo *servinfo;
    struct addrinfo *it;

    S.common.hints.ai_flags = AI_PASSIVE;

    if ((err = getaddrinfo(NULL, S.common.port_str, &S.common.hints, &servinfo)) != 0) {
        SET_ERR(STAT_SERVER_START_ERR,
                "getaddrinfo: %s", gai_strerror(err));
        return 0;
    }

    for (it = servinfo; it != NULL; it = it->ai_next) {
        if ((S.common.sock_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol)) == -1) {
            continue;
        }

        if (setsockopt(S.common.sock_fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes)) == -1) {
            SET_ERR(STAT_SERVER_START_ERR, "setsockopt");
            return 0;
        }

        if (bind(S.common.sock_fd, it->ai_addr, it->ai_addrlen) == -1) {
            close(S.common.sock_fd);
            continue;
        }

        break;
    }

    freeaddrinfo(servinfo);

    if (it == NULL)  {
        SET_ERR(STAT_SERVER_START_ERR, "failed to bind");
        return 0;
    }

    if (listen(S.common.sock_fd, get_backlog()) == -1) {
        SET_ERR(STAT_SERVER_START_ERR, "failed to listen");
        return 0;
    }

    S.outbox = fifo_make(ck_imessage);
    users    = tree_make_c(str_t, ck_user, strcmp);
    pthread_mutex_init(&user_mtx, NULL);

    SET_STAT(STAT_SERVER_STARTED);
    SET_MSG("the server has been started");

    return 1;
}

static int handle_emessage(ck_user *user, uint8_t msg_kind) {
    int          status;
    long         net_len;
    long         len;
    char        *buff;
    ck_imessage  imsg;

    status = 0;

    switch (msg_kind) {
        case EMSG_CHAT:
            status = recv_all(user->fd, &net_len, sizeof(net_len));
            if (status <= 0) { errno = 0; goto out; }
            len = ntohl(net_len);
            if (len == 0) { goto out; }

            buff = malloc(len);
            status = recv_all(user->fd, buff, len);
            if (status <= 0) { errno = 0; free(buff); goto out; }
            imsg.content_len = len;
            imsg.contents    = buff;

            imsg.user_id_len = strlen(user->id);
            memcpy(imsg.user_id, user->id, imsg.user_id_len);
            fifo_put(&S.outbox, &imsg);
            break;
    }

out:;
    return status;
}

static void server_handle_user(ck_user *user) {
    int     status;
    uint8_t msg_kind;


    SET_MSG("%s has joined the chat", user->id);

    for (;;) {
        /* Get the EMSG kind. */
        status = recv_all(user->fd, &msg_kind, sizeof(msg_kind));
        if (status == 0) { break; }
        if (status < 0) {
            SET_ERR(STAT_SERVER_RUN_ERR, "recv error -- %d", errno);
            errno = 0;
            break;
        }

        status = handle_emessage(user, msg_kind);
        if (status == 0) { break; }
        if (status < 0) {
            SET_ERR(STAT_SERVER_RUN_ERR, "recv error -- %d", errno);
            errno = 0;
            break;
        }
    }
}

static void *server_connection_thread(void *arg) {
    int         sock_fd;
    ck_emessage msg;
    int         n_read;
    long        id_net_len;
    long        id_len;
    char        id_buff[ID_MAX + 1];
    ck_user    *user;

    sock_fd = (int)(long)arg;

    msg.kind = EMSG_PROTOCOL_IDENTIFY;
    if (send_all(sock_fd, &msg, sizeof(msg)) == -1) {
        SET_ERR(STAT_SERVER_RUN_ERR, "send error -- %d", errno);
        errno = 0;
        goto out;
    }

    n_read = recv_all(sock_fd, &id_net_len, sizeof(id_net_len));

    if (n_read == 0) { goto out; }
    if (n_read < 0) {
        SET_ERR(STAT_CLIENT_RUN_ERR, "recv error -- %d", errno);
        errno = 0;
        goto out;
    }

    id_len = ntohl(id_net_len);

    n_read = recv_all(sock_fd, id_buff, id_len);

    if (n_read == 0) { goto out; }
    if (n_read < 0) {
        SET_ERR(STAT_CLIENT_RUN_ERR, "recv error -- %d", errno);
        errno = 0;
        goto out;
    }

    id_buff[id_len] = 0;

    if (get_user(id_buff)) {
        msg.kind = EMSG_PROTOCOL_DENY_DUP;
        if (send_all(sock_fd, &msg, sizeof(msg)) == -1) {
            SET_ERR(STAT_SERVER_RUN_ERR, "send error -- %d", errno);
            errno = 0;
        }
        goto out_no_del;
    }

    user     = add_user(id_buff);
    user->fd = sock_fd;
    server_handle_user(user);

out:;
    del_user(id_buff);
out_no_del:;
    close(sock_fd);
    return NULL;
}

static void *server_dispatch_thread(void *arg) {
    ck_imessage              imsg;
    tree_it(str_t, ck_user)  user_it;
    ck_user                 *user;
    int                      status;
    uint8_t                  msg_kind;
    long                     net_id_len;
    long                     net_msg_len;

    msg_kind = EMSG_CHAT;

    for (;;) {
        usleep(100000);
        pthread_mutex_lock(&user_mtx);
        while (fifo_get(&S.outbox, &imsg)) {
            net_id_len  = htonl(imsg.user_id_len);
            net_msg_len = htonl(imsg.content_len);
            tree_traverse(users, user_it) {
                user = &tree_it_val(user_it);

                status = send_all(user->fd, &msg_kind, sizeof(msg_kind));
                if (status <= 0) { continue; }

                status = send_all(user->fd, &net_id_len, sizeof(net_id_len));
                if (status <= 0) { continue; }

                status = send_all(user->fd, imsg.user_id, imsg.user_id_len);
                if (status <= 0) { continue; }

                status = send_all(user->fd, &net_msg_len, sizeof(net_msg_len));
                if (status <= 0) { continue; }

                status = send_all(user->fd, imsg.contents, imsg.content_len);
                if (status <= 0) { continue; }
            }
        }
        pthread_mutex_unlock(&user_mtx);
    }

    return NULL;
}

int run_server(void) {
    struct sockaddr_storage conn_addr;
    socklen_t               sin_size;
    char                    s[INET6_ADDRSTRLEN];
    int                     conn_sock_fd;
    pthread_t               conn_tid;
    pthread_t               disp_tid;

    SET_STAT(STAT_SERVER_RUNNING);
    SET_MSG("waiting for connections...");

    for (;;) {
        sin_size = sizeof(conn_addr);

        conn_sock_fd = accept(S.common.sock_fd, (struct sockaddr*)&conn_addr, &sin_size);
        if (conn_sock_fd == -1) {
            SET_MSG("accept error");
            continue;
        }

        inet_ntop(conn_addr.ss_family,
                  get_in_addr((struct sockaddr *)&conn_addr), s, sizeof(s));

        SET_MSG("connection from %s", s);
        pthread_create(&conn_tid, NULL, server_connection_thread, (void*)(long)conn_sock_fd);
        pthread_create(&disp_tid, NULL, server_dispatch_thread,   NULL);
    }

    return 1;
}
