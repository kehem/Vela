#include "vela/conn.h"
#include "vela/server.h"
#include "vela/log.h"
#include "vela/util.h"
#include "vela/python.h"
#include "vela/metrics.h"

#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>

my_conn_t *my_conn_new(my_server_t *srv, int fd)
{
    my_conn_t *c = NULL;
    if (srv->pool) {
        c = srv->pool;
        srv->pool = c->next;
        srv->pool_n--;
        c->next = NULL;
        my_buf_reset(&c->in);
        my_buf_reset(&c->out);
        my_http_req_reset(&c->req);
        c->close_after = 0;
        c->keep_alive = 0;
        c->headers_done = 0;
        c->is_unix = 0;
    } else {
        c = my_xcalloc(sizeof *c);
        my_buf_init(&c->in, 8192);
        my_buf_init(&c->out, 8192);
        my_http_req_init(&c->req);
    }
    c->fd = fd;
    c->srv = srv;
    c->state = MY_CS_READ_HEAD;
    c->ev_mask = 0;
    c->last_active_ms = my_now_ms();
    c->req_start_us = my_now_us();
    return c;
}

static int conn_want(my_conn_t *c, unsigned mask)
{
    if (c->ev_mask == mask)
        return 0;
    if (my_loop_mod(c->srv->loop, c->fd, mask, my_conn_on_event, c) < 0)
        return -1;
    c->ev_mask = mask;
    return 0;
}

void my_conn_free(my_conn_t *c)
{
    if (!c)
        return;
    my_server_t *srv = c->srv;
    if (c->fd >= 0) {
        if (srv && srv->loop)
            my_loop_del(srv->loop, c->fd);
        close(c->fd);
        c->fd = -1;
        if (srv && srv->nconn > 0)
            srv->nconn--;
    }
    if (srv && srv->pool_n < srv->pool_max) {
        c->next = srv->pool;
        srv->pool = c;
        srv->pool_n++;
        return;
    }
    my_buf_free(&c->in);
    my_buf_free(&c->out);
    my_http_req_free(&c->req);
    free(c);
}

static void client_str(my_conn_t *c, char *out, size_t n)
{
    if (c->is_unix) {
        snprintf(out, n, "unix");
        return;
    }
    if (c->peer.ss_family == AF_INET) {
        struct sockaddr_in *in = (struct sockaddr_in *)&c->peer;
        inet_ntop(AF_INET, &in->sin_addr, out, (socklen_t)n);
    } else
        snprintf(out, n, "-");
}

static void finish_request(my_conn_t *c)
{
    my_server_t *s = c->srv;
    const my_config_t *cfg = &s->cfg;
    if (c->req.body_seen > cfg->max_body_size) {
        my_conn_queue_response(c, 413, NULL, NULL, "payload too large\n", 18, 0);
        return;
    }

    /* built-in metrics */
    const char *mp = cfg->metrics_path;
    size_t mpl = mp ? strlen(mp) : 0;
    if (mpl && c->req.target && strncmp(c->req.target, mp, mpl) == 0 &&
        (c->req.target[mpl] == 0 || c->req.target[mpl] == '?')) {
        char buf[2048];
        int n = my_metrics_render(buf, sizeof buf);
        my_conn_queue_response(c, 200, NULL, "text/plain; version=0.0.4", buf, (size_t)n, 1);
        return;
    }

    my_metrics_inc_requests();
    if (my_py_ready()) {
        if (my_py_handle_http(c) == 0)
            return;
        my_metrics_inc_errors();
        my_conn_queue_response(c, 500, NULL, NULL, "Internal Server Error\n", 22, 0);
        return;
    }
    /* no app: small default page */
    const char *body = "vela is running. Load an ASGI app with: vela module:app\n";
    my_conn_queue_response(c, 200, NULL, NULL, body, strlen(body), c->req.keep_alive);
}

static void try_parse(my_conn_t *c)
{
    my_server_t *s = c->srv;
    const my_config_t *cfg = &s->cfg;
    for (;;) {
        size_t avail = c->in.len - c->in.pos;
        if (!avail)
            break;
        size_t used = 0;
        my_http_parse_rc rc =
            my_http_parse(&c->req, (const char *)c->in.data + c->in.pos, avail, &used,
                          cfg->max_request_line, cfg->max_header_size, cfg->max_headers);
        my_buf_consume(&c->in, used);
        if (rc == MY_HTTP_PARSE_ERROR) {
            int st = c->req.status_code ? c->req.status_code : 400;
            char msg[128];
            snprintf(msg, sizeof msg, "%s\n", c->req.error[0] ? c->req.error : "bad request");
            my_conn_queue_response(c, st, NULL, NULL, msg, strlen(msg), 0);
            return;
        }
        if (rc == MY_HTTP_PARSE_DONE) {
            finish_request(c);
            return;
        }
        if (rc == MY_HTTP_PARSE_NEED_MORE)
            break;
    }
}

static void flush_out(my_conn_t *c)
{
    while (c->out.pos < c->out.len) {
        ssize_t w = send(c->fd, c->out.data + c->out.pos, c->out.len - c->out.pos, MSG_NOSIGNAL);
        if (w < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                (void)conn_want(c, MY_EV_WRITE | MY_EV_READ | MY_EV_ET);
                return;
            }
            if (errno == EINTR)
                continue;
            c->state = MY_CS_CLOSED;
            return;
        }
        c->out.pos += (size_t)w;
    }
    my_buf_reset(&c->out);
    double ms = (double)(my_now_us() - c->req_start_us) / 1000.0;
    char cli[64];
    client_str(c, cli, sizeof cli);
    my_access_log(cli, c->req.method, c->req.target, 200, 0, ms);
    my_metrics_observe_latency_us(my_now_us() - c->req_start_us);
    if (c->close_after || !c->req.keep_alive) {
        c->state = MY_CS_CLOSED;
        return;
    }
    my_http_req_reset(&c->req);
    c->state = MY_CS_READ_HEAD;
    c->req_start_us = my_now_us();
    my_loop_mod(c->srv->loop, c->fd, MY_EV_READ | MY_EV_ET, my_conn_on_event, c);
    try_parse(c);
}

static void do_read(my_conn_t *c)
{
    for (;;) {
        if (my_buf_reserve(&c->in, 4096) < 0) {
            c->state = MY_CS_CLOSED;
            return;
        }
        ssize_t n = read(c->fd, c->in.data + c->in.len, c->in.cap - c->in.len);
        if (n < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK)
                break;
            if (errno == EINTR)
                continue;
            c->state = MY_CS_CLOSED;
            return;
        }
        if (n == 0) {
            c->state = MY_CS_CLOSED;
            return;
        }
        c->in.len += (size_t)n;
        c->last_active_ms = my_now_ms();
        if (c->in.len > c->srv->cfg.max_header_size + c->srv->cfg.max_body_size) {
            my_conn_queue_response(c, 413, NULL, NULL, "too large\n", 10, 0);
            return;
        }
    }
    try_parse(c);
}

void my_conn_on_event(my_loop_t *loop, int fd, unsigned events, void *ud)
{
    (void)loop;
    (void)fd;
    my_conn_t *c = ud;
    if (events & (MY_EV_ERR | MY_EV_HUP)) {
        if (!(events & MY_EV_READ)) {
            c->state = MY_CS_CLOSED;
            my_conn_free(c);
            return;
        }
    }
    if (events & MY_EV_READ)
        do_read(c);
    if (c->state == MY_CS_WRITE)
        flush_out(c);
    if (c->state == MY_CS_CLOSED)
        my_conn_free(c);
}
