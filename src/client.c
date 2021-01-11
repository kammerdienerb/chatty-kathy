#include "client.h"
#include "message.h"

ck_client C;

#define THREAD_SHARED (C.common.ts)


static int identify(void) {
    int  status;
    long len;
    long net_len;

    len     = strlen(C.id);
    net_len = htonl(len);
    status  = send_all(C.common.sock_fd, &net_len, sizeof(net_len));
    if (status <= 0) { goto out; }
    status = send_all(C.common.sock_fd, C.id, len);
    if (status <= 0) { goto out; }
out:;
    return status;
}

static int read_message(void) {
    int          status;
    long         net_id_len;
    long         net_msg_len;
    long         len;
    char        *buff;
    ck_imessage  imsg;

    status = recv_all(C.common.sock_fd, &net_id_len, sizeof(net_id_len));
    if (status <= 0) { errno = 0; goto out; }
    len = ntohl(net_id_len);
    if (len == 0) { goto out; }
    imsg.user_id_len = len;

    status = recv_all(C.common.sock_fd, imsg.user_id, len);
    if (status <= 0) { errno = 0; goto out; }

    status = recv_all(C.common.sock_fd, &net_msg_len, sizeof(net_msg_len));
    if (status <= 0) { errno = 0; goto out; }
    len = ntohl(net_msg_len);
    if (len == 0) { goto out; }

    buff = malloc(len);
    status = recv_all(C.common.sock_fd, buff, len);
    if (status <= 0) { errno = 0; free(buff); goto out; }
    imsg.content_len = len;
    imsg.contents    = buff;
    fifo_put(&C.inbox, &imsg);

out:;
    return status;
}

static int handle_emessages(void) {
    int     status;
    uint8_t msg_type;

    status = recv_all(C.common.sock_fd, &msg_type, sizeof(msg_type));

    if (status == 0) {
        goto out;
    }
    if (status < 0) {
        if (errno == EAGAIN) {
            status = 1;
            errno  = 0;
            goto out;
        }
        SET_ERR(STAT_CLIENT_RUN_ERR, "recv error -- errno = %d", errno);
        status = -errno;
        errno  = 0;
        goto out;
    }

    switch (msg_type) {
        case EMSG_PROTOCOL_IDENTIFY:
            status = identify();
            if (status < 0) {
                status = errno;
                errno  = 0;
                goto out;
            }
            break;
        case EMSG_PROTOCOL_DENY_DUP:
            SET_ERR(STAT_CLIENT_RUN_ERR, "%s already has a session on the server", C.id);
            status = -1;
            goto out;
        case EMSG_CHAT:
            status = read_message();
            if (status < 0) {
                status = errno;
                errno  = 0;
                goto out;
            }
            break;
        default:
            SET_MSG("I don't know this message type %u", msg_type);
            status = -1;
            goto out;
    }

out:;
    if (status == 0) {
        SET_MSG("disconnected from server");
    }
    return status;
}

static int empty_outbox(void) {
    int         status;
    uint8_t     msg_kind;
    ck_imessage imsg;
    long        len;

    status = 0;

    while (fifo_get(&C.outbox, &imsg)) {
        /* Send the EMSG kind. */
        msg_kind = EMSG_CHAT;
        status   = send_all(C.common.sock_fd, &msg_kind, sizeof(msg_kind));
        if (status < 0) {
            status = errno;
            errno  = 0;
            goto out;
        }

        /* Send the length. */
        len    = htonl(imsg.content_len);
        status = send_all(C.common.sock_fd, &len, sizeof(len));
        if (status < 0) {
            status = errno;
            errno  = 0;
            goto out;
        }

        /* Send the contents. */
        status = send_all(C.common.sock_fd, imsg.contents, imsg.content_len);
        if (status < 0) {
            status = errno;
            errno  = 0;
            goto out;
        }

        free(imsg.contents);
    }

out:;
    return status;
}

int run_client(void) {
    int         status;

    SET_STAT(STAT_CLIENT_RUNNING);

    /* First pass to identify self to server. */
    status = handle_emessages();
    if (status <= 0) { goto err; }

    for (;;) {
        status = handle_emessages();
        if (status <= 0) { goto err; }
        status = empty_outbox();
        if (status < 0) { goto err; }
    }

err:;
    SET_STAT(STAT_NOT_STARTED);

    close(C.common.sock_fd);

    return 0;
}

int start_client(void) {
    int              err;
    struct addrinfo *servinfo;
    struct addrinfo *it;
    struct timeval   tv;
    char             name[INET6_ADDRSTRLEN];

    C.outbox = fifo_make(ck_imessage);
    C.inbox  = fifo_make(ck_imessage);

    if ((err = getaddrinfo(C.common.server, C.common.port_str, &C.common.hints, &servinfo)) != 0) {
        SET_ERR(STAT_CLIENT_START_ERR,
                "getaddrinfo: %s: %s", gai_strerror(err), C.common.server);
        return 0;
    }

    for (it = servinfo; it != NULL; it = it->ai_next) {
        if ((C.common.sock_fd = socket(it->ai_family, it->ai_socktype, it->ai_protocol)) == -1) {
            continue;
        }

        if (connect(C.common.sock_fd, it->ai_addr, it->ai_addrlen) == -1) {
            close(C.common.sock_fd);
            continue;
        }

        break;
    }

    if (it == NULL) {
        SET_ERR(STAT_CLIENT_START_ERR, "failed to connect");
        return 0;
    }

    tv.tv_sec  = 1;
    tv.tv_usec = 0;
    setsockopt(C.common.sock_fd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&tv, sizeof tv);

    inet_ntop(it->ai_family, get_in_addr((struct sockaddr *)it->ai_addr), name, sizeof(name));

    SET_STAT(STAT_CLIENT_STARTED);
    SET_MSG("the client has been started and connect to %s", name);

    freeaddrinfo(servinfo);

    return 1;
}
