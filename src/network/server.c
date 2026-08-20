#include "vela/server.h"
#include "vela/conn.h"
#include "vela/log.h"
#include "vela/util.h"
#include "vela/python.h"
#include "vela/metrics.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>

static void on_accept(my_loop_t *loop, int fd, unsigned events, void *ud);

int my_server_init(my_server_t *s, const my_config_t *cfg)
{
    memset(s, 0, sizeof *s);
    s->cfg = *cfg;
    s->loop = my_loop_create();
    if (!s->loop)
        return -1;
    s->pool_max = 4096;
    return 0;
}

void my_server_fini(my_server_t *s)
{
    while (s->pool) {
        my_conn_t *c = s->pool;
        s->pool = c->next;
        c->srv = NULL;
        c->fd = -1;
        my_buf_free(&c->in);
        my_buf_free(&c->out);
        my_http_req_free(&c->req);
        free(c);
    }
    s->pool_n = 0;
    for (int i = 0; i < s->nlisten; i++) {
        if (s->listens[i].fd >= 0) {
            close(s->listens[i].fd);
            if (s->listens[i].is_unix && s->listens[i].path[0])
                unlink(s->listens[i].path);
        }
    }
    my_loop_destroy(s->loop);
    s->loop = NULL;
}

static int listen_tcp(my_server_t *s, const char *spec)
{
    char host[256];
    uint16_t port;
    if (my_parse_bind(spec, host, sizeof host, &port) < 0) {
        MY_ERROR("invalid bind %s", spec);
        return -1;
    }
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    my_set_reuseaddr(fd);
    my_set_reuseport(fd);
#ifdef TCP_DEFER_ACCEPT
    {
        int d = 1;
        (void)setsockopt(fd, IPPROTO_TCP, TCP_DEFER_ACCEPT, &d, sizeof d);
    }
#endif
#ifdef TCP_FASTOPEN
    {
        int q = 1024;
        (void)setsockopt(fd, IPPROTO_TCP, TCP_FASTOPEN, &q, sizeof q);
    }
#endif
    {
        int buf = 1024 * 1024;
        (void)setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &buf, sizeof buf);
        (void)setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &buf, sizeof buf);
    }
    struct sockaddr_in addr;
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    if (!strcmp(host, "*") || !strcmp(host, "0.0.0.0"))
        addr.sin_addr.s_addr = INADDR_ANY;
    else if (inet_pton(AF_INET, host, &addr.sin_addr) != 1) {
        close(fd);
        MY_ERROR("invalid address %s", host);
        return -1;
    }
    if (bind(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        MY_ERROR("bind %s: %s", spec, strerror(errno));
        close(fd);
        return -1;
    }
    if (listen(fd, s->cfg.backlog) < 0) {
        MY_ERROR("listen: %s", strerror(errno));
        close(fd);
        return -1;
    }
    my_listen_t *l = &s->listens[s->nlisten++];
    l->fd = fd;
    l->is_unix = 0;
    MY_INFO("listening on %s", spec);
    return 0;
}

static int listen_unix(my_server_t *s, const char *path)
{
    unlink(path);
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0)
        return -1;
    struct sockaddr_un un;
    memset(&un, 0, sizeof un);
    un.sun_family = AF_UNIX;
    if (strlen(path) >= sizeof un.sun_path) {
        close(fd);
        return -1;
    }
    memcpy(un.sun_path, path, strlen(path) + 1);
    if (bind(fd, (struct sockaddr *)&un, sizeof un) < 0) {
        MY_ERROR("unix bind %s: %s", path, strerror(errno));
        close(fd);
        return -1;
    }
    chmod(path, (mode_t)s->cfg.unix_mode);
    if (listen(fd, s->cfg.backlog) < 0) {
        close(fd);
        return -1;
    }
    my_listen_t *l = &s->listens[s->nlisten++];
    l->fd = fd;
    l->is_unix = 1;
    snprintf(l->path, sizeof l->path, "%s", path);
    MY_INFO("listening on unix:%s", path);
    return 0;
}

int my_server_listen(my_server_t *s)
{
    for (int i = 0; i < s->cfg.nbind; i++) {
        if (listen_tcp(s, s->cfg.bind[i]) < 0)
            return -1;
    }
    if (s->cfg.unix_socket) {
        if (listen_unix(s, s->cfg.unix_socket) < 0)
            return -1;
    }
    for (int i = 0; i < s->nlisten; i++) {
        if (my_loop_add(s->loop, s->listens[i].fd, MY_EV_READ, on_accept, s) < 0)
            return -1;
    }
    return 0;
}

static void on_accept(my_loop_t *loop, int fd, unsigned events, void *ud)
{
    (void)loop;
    (void)events;
    my_server_t *s = ud;
    for (int n = 0; n < 256; n++) {
        struct sockaddr_storage ss;
        socklen_t sl = sizeof ss;
        int cfd = accept4(fd, (struct sockaddr *)&ss, &sl, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            MY_ERROR("accept: %s", strerror(errno));
            break;
        }
        if (s->nconn >= s->cfg.max_connections) {
            close(cfd);
            continue;
        }
        my_set_nodelay(cfd);
        my_conn_t *c = my_conn_new(s, cfd);
        c->peer = ss;
        c->peerlen = sl;
        s->nconn++;
        my_metrics_set_conns(s->nconn);
        if (my_loop_add(s->loop, cfd, MY_EV_READ | MY_EV_ET, my_conn_on_event, c) < 0)
            my_conn_free(c);
        else
            c->ev_mask = MY_EV_READ | MY_EV_ET;
    }
}

static void on_tick(my_loop_t *loop, void *ud)
{
    (void)loop;
    my_server_t *s = ud;
    if (s->stopping)
        my_loop_stop(s->loop);
}

int my_server_run(my_server_t *s)
{
    my_loop_set_tick(s->loop, on_tick, s, 200);
    return my_loop_run(s->loop);
}

void my_server_stop(my_server_t *s)
{
    s->stopping = 1;
    my_loop_stop(s->loop);
}
