#ifndef VELA_HTTP_H
#define VELA_HTTP_H

#include <stddef.h>
#include <stdint.h>

#define MY_HTTP_MAX_HEADERS 128

typedef enum {
    MY_HTTP_PARSE_NEED_MORE = 0,
    MY_HTTP_PARSE_DONE = 1,
    MY_HTTP_PARSE_ERROR = -1
} my_http_parse_rc;

typedef enum {
    MY_HS_REQ_LINE = 0,
    MY_HS_HEADER,
    MY_HS_BODY,
    MY_HS_CHUNK_SIZE,
    MY_HS_CHUNK_DATA,
    MY_HS_CHUNK_CRLF,
    MY_HS_TRAILER,
    MY_HS_DONE,
    MY_HS_ERROR
} my_http_state;

typedef struct {
    const char *name;
    size_t nlen;
    const char *value;
    size_t vlen;
} my_http_hdr;

typedef struct {
    my_http_state state;
    char method[16];
    char *target;
    size_t target_len;
    char *target_cap_ptr;
    size_t target_cap;
    int http_major;
    int http_minor;
    my_http_hdr headers[MY_HTTP_MAX_HEADERS];
    int nheaders;
    int chunked;
    int keep_alive;
    int has_content_length;
    uint64_t content_length;
    uint64_t body_seen;
    uint64_t chunk_left;
    size_t header_bytes;
    int status_code; /* on error */
    char error[64];
    /* storage for copied request-line target and header block */
    char *store;
    size_t store_len;
    size_t store_cap;
    /* assembled request body (reused across keep-alive requests) */
    char *body;
    size_t body_len;
    size_t body_cap;
} my_http_req;

void my_http_req_init(my_http_req *r);
void my_http_req_reset(my_http_req *r);
void my_http_req_free(my_http_req *r);

/* Feed data. Consumes *nused bytes. Returns NEED_MORE/DONE/ERROR. */
my_http_parse_rc my_http_parse(my_http_req *r, const char *data, size_t len, size_t *nused,
                               size_t max_line, size_t max_header, size_t max_headers);

const char *my_http_header(const my_http_req *r, const char *name);
int my_http_header_eq(const my_http_hdr *h, const char *name);

#endif
