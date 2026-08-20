#include "vela/http.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void feed_all(my_http_req *r, const char *s)
{
    size_t used = 0;
    my_http_parse_rc rc = my_http_parse(r, s, strlen(s), &used, 8192, 65536, 100);
    if (rc != MY_HTTP_PARSE_DONE) {
        fprintf(stderr, "parse fail rc=%d used=%zu state=%d err=%s s=%s\n",
                (int)rc, used, r->state, r->error, s);
    }
    assert(rc == MY_HTTP_PARSE_DONE);
    assert(used == strlen(s) || used <= strlen(s));
}

int main(void)
{
    my_http_req r;
    my_http_req_init(&r);
    feed_all(&r, "GET /hello HTTP/1.1\r\nHost: x\r\n\r\n");
    assert(strcmp(r.method, "GET") == 0);
    assert(strcmp(r.target, "/hello") == 0);
    assert(r.http_minor == 1);
    assert(r.nheaders == 1);
    assert(!r.has_content_length);
    my_http_req_free(&r);

    my_http_req_init(&r);
    feed_all(&r, "POST /x HTTP/1.1\r\nContent-Length: 4\r\n\r\nabcd");
    assert(r.has_content_length);
    assert(r.content_length == 4);
    assert(r.body_seen == 4);
    assert(r.body_len == 4);
    assert(memcmp(r.body, "abcd", 4) == 0);
    my_http_req_free(&r);

    my_http_req_init(&r);
    feed_all(&r, "POST /c HTTP/1.1\r\nTransfer-Encoding: chunked\r\n\r\n5\r\nhello\r\n0\r\n\r\n");
    assert(r.chunked);
    assert(r.body_seen == 5);
    assert(r.body_len == 5);
    assert(memcmp(r.body, "hello", 5) == 0);
    my_http_req_free(&r);

    my_http_req_init(&r);
    size_t used = 0;
    my_http_parse_rc rc = my_http_parse(&r, "GET / HTTP/1.1\r\nContent-Length: 1\r\nContent-Length: 2\r\n\r\n",
                                       80, &used, 8192, 65536, 100);
    assert(rc == MY_HTTP_PARSE_ERROR);
    my_http_req_free(&r);

    my_http_req_init(&r);
    const char *p1 = "GET /partial";
    rc = my_http_parse(&r, p1, strlen(p1), &used, 8192, 65536, 100);
    assert(rc == MY_HTTP_PARSE_NEED_MORE);
    my_http_req_free(&r);

    printf("test_http_parser OK\n");
    return 0;
}
