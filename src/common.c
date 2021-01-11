#include "common.h"
#include "thread_shared.h"

const int yes = 1;

extern char* yed_get_var(char*);
extern int   yed_get_var_as_int(char*, int*);

int init_common(ck_common *common) {
#define THREAD_SHARED (common->ts)

    common->port = get_port();

    if (common->port == -1) {
        SET_ERR(STAT_CONFIG_ERR, "invalid port -- check 'chatty-kathy-port'");
        return 0;
    }

    snprintf(common->port_str, sizeof(common->port_str),
             "%d", common->port);

    if (!get_server(common->server)) {
        SET_ERR(STAT_CONFIG_ERR, "invalid server -- check 'chatty-kathy-server'");
        return 0;
    }

    common->sock_fd = -1;

    memset(&common->hints, 0, sizeof(common->hints));
    common->hints.ai_family   = AF_UNSPEC;
    common->hints.ai_socktype = SOCK_STREAM;

    return 1;

#undef THREAD_SHARED
}



int fifo_get(ck_fifo *fifo, void *dst) {
    void *item;
    int   r;

    pthread_mutex_lock(&fifo->mtx);
    item = array_last(fifo->items);
    if (item == NULL) {
        r = 0;
    } else {
        memcpy(dst, item, fifo->items.elem_size);
        array_pop(fifo->items);
        r = 1;
    }
    pthread_mutex_unlock(&fifo->mtx);

    return r;
}

void fifo_put(ck_fifo *fifo, void *item) {
    pthread_mutex_lock(&fifo->mtx);
    _array_insert(&fifo->items, 0, item);
    pthread_mutex_unlock(&fifo->mtx);
}


int get_port(void) {
    int port;

    if (!yed_get_var_as_int("chatty-kathy-port", &port)) {
        port = -1;
    }

    return port;
}

int get_server(char *server) {
    char *serv_var;

    if (server == NULL)                                          { return 0; }
    if ((serv_var = yed_get_var("chatty-kathy-server")) == NULL) { return 0; }

    strcpy(server, serv_var);

    return 1;
}

int get_backlog(void) {
    int backlog;

    if (!yed_get_var_as_int("chatty-kathy-server-max-backlog", &backlog)) {
        backlog = 10;
    }

    return backlog;
}

/* get sockaddr, IPv4 or IPv6 */
void *get_in_addr(struct sockaddr *sa) {
    if (sa->sa_family == AF_INET) {
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }

    return &(((struct sockaddr_in6*)sa)->sin6_addr);
}

int recv_all(int fd, void *buff, int n) {
    int total_read;
    int n_read;

    total_read = 0;

    while (total_read < n) {
        n_read = recv(fd, buff, n, 0);
        if (n_read <= 0) {
            return n_read;
        }
        total_read += n_read;
    }

    return total_read;
}

int send_all(int fd, void *buff, int n) {
    int total_sent;
    int n_sent;

    total_sent = 0;

    while (total_sent < n) {
        n_sent = send(fd, buff, n, MSG_NOSIGNAL);
        if (n_sent <= 0) {
            return n_sent;
        }
        total_sent += n_sent;
    }

    return total_sent;
}
