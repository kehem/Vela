#include "vela/conn.h"
#include "vela/util.h"

#include <stdio.h>
#include <string.h>

static const char *reason_phrase(int status)
{
    switch (status) {
    case 200:
        return "OK";
    case 201:
        return "Created";
    case 204:
        return "No Content";
    case 400:
        return "Bad Request";
    case 404:
        return "Not Found";
    case 405:
        return "Method Not Allowed";
    case 408:
        return "Request Timeout";
    case 411:
        return "Length Required";
    case 413:
        return "Payload Too Large";
    case 414:
        return "URI Too Long";
    case 431:
        return "Request Header Fields Too Large";
    case 500:
        return "Internal Server Error";
    case 501:
        return "Not Implemented";
    case 503:
        return "Service Unavailable";
    default:
        return "Error";
    }
}

int my_conn_queue_response(my_conn_t *c, int status, const char *reason, const char *ctype,
                           const void *body, size_t blen, int keep)
{
    if (!reason)
        reason = reason_phrase(status);
    if (!ctype)
        ctype = "text/plain; charset=utf-8";
    char hdr[1024];
    int n = snprintf(hdr, sizeof hdr,
                     "HTTP/1.1 %d %s\r\n"
                     "Content-Type: %s\r\n"
                     "Content-Length: %zu\r\n"
                     "Connection: %s\r\n"
                     "Server: vela/0.1\r\n"
                     "\r\n",
                     status, reason, ctype, blen, keep ? "keep-alive" : "close");
    if (n < 0 || (size_t)n >= sizeof hdr)
        return -1;
    if (my_buf_append(&c->out, hdr, (size_t)n) < 0)
        return -1;
    if (blen && body) {
        if (my_buf_append(&c->out, body, blen) < 0)
            return -1;
    }
    c->keep_alive = keep;
    c->close_after = !keep;
    c->state = MY_CS_WRITE;
    return 0;
}
