#ifndef VELA_CONN_H
#define VELA_CONN_H

#include "vela/buf.h"
#include "vela/http.h"
#include "vela/event.h"
#include "vela/config.h"

#include <stdint.h>
#include <netinet/in.h>

struct my_server;

typedef enum {
    MY_CS_READ_HEAD = 0,
    MY_CS_READ_BODY,
    MY_CS_PROCESS,
    MY_CS_WRITE,
    MY_CS_CLOSING,
    MY_CS_CLOSED
} my_conn_state;

typedef struct my_conn {
    int fd;
    my_conn_state state;
    my_buf_t in;
    my_buf_t out;
    my_http_req req;
    struct sockaddr_storage peer;
    socklen_t peerlen;
    uint64_t last_active_ms;
    uint64_t req_start_us;
    int keep_alive;
    int close_after;
    int is_unix;
    unsigned ev_mask;
    struct my_server *srv;
    struct my_conn *next;
    /* ASGI body leftover after headers */
    int headers_done;
} my_conn_t;

my_conn_t *my_conn_new(struct my_server *srv, int fd);
void my_conn_free(my_conn_t *c);
void my_conn_on_event(my_loop_t *loop, int fd, unsigned events, void *ud);
int my_conn_queue_response(my_conn_t *c, int status, const char *reason,
                           const char *ctype, const void *body, size_t blen, int keep);

#endif
