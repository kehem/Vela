#include "vela/http.h"
#include "vela/util.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdio.h>
#include <stdint.h>

void my_http_req_init(my_http_req *r)
{
    memset(r, 0, sizeof *r);
    r->state = MY_HS_REQ_LINE;
    r->http_major = 1;
    r->http_minor = 1;
    r->keep_alive = 1;
}

void my_http_req_free(my_http_req *r)
{
    free(r->store);
    free(r->target);
    free(r->body);
    memset(r, 0, sizeof *r);
}

void my_http_req_reset(my_http_req *r)
{
    char *store = r->store;
    size_t store_cap = r->store_cap;
    char *body = r->body;
    size_t body_cap = r->body_cap;
    char *target = r->target;
    size_t target_cap = r->target_cap;
    memset(r, 0, sizeof *r);
    r->store = store;
    r->store_cap = store_cap;
    r->body = body;
    r->body_cap = body_cap;
    r->target = target;
    r->target_cap = target_cap;
    r->state = MY_HS_REQ_LINE;
    r->http_major = 1;
    r->http_minor = 1;
    r->keep_alive = 1;
}

static int body_append(my_http_req *r, const char *p, size_t n)
{
    if (!n)
        return 0;
    if (r->body_len + n > r->body_cap) {
        size_t nc = r->body_cap ? r->body_cap : 4096;
        while (nc < r->body_len + n) {
            if (nc > (SIZE_MAX / 2))
                return -1;
            nc *= 2;
        }
        r->body = my_xrealloc(r->body, nc);
        r->body_cap = nc;
    }
    memcpy(r->body + r->body_len, p, n);
    r->body_len += n;
    return 0;
}

static int store_append(my_http_req *r, const char *p, size_t n)
{
    if (r->store_len + n + 1 > r->store_cap) {
        size_t nc = r->store_cap ? r->store_cap * 2 : 512;
        while (nc < r->store_len + n + 1)
            nc *= 2;
        r->store = my_xrealloc(r->store, nc);
        r->store_cap = nc;
    }
    memcpy(r->store + r->store_len, p, n);
    r->store_len += n;
    r->store[r->store_len] = 0;
    return 0;
}

static int is_tchar(unsigned char c)
{
    return isalnum(c) || strchr("!#$%&'*+-.^_`|~", c) != NULL;
}

int my_http_header_eq(const my_http_hdr *h, const char *name)
{
    size_t n = strlen(name);
    if (h->nlen != n)
        return 0;
    for (size_t i = 0; i < n; i++) {
        if (tolower((unsigned char)h->name[i]) != tolower((unsigned char)name[i]))
            return 0;
    }
    return 1;
}

const char *my_http_header(const my_http_req *r, const char *name)
{
    for (int i = 0; i < r->nheaders; i++) {
        if (my_http_header_eq(&r->headers[i], name))
            return r->headers[i].value;
    }
    return NULL;
}

static int parse_req_line(my_http_req *r, const char *line, size_t n)
{
    /* METHOD SP target SP HTTP/x.y */
    size_t i = 0;
    while (i < n && is_tchar((unsigned char)line[i]))
        i++;
    if (i == 0 || i >= sizeof r->method || i >= n || line[i] != ' ')
        return -1;
    memcpy(r->method, line, i);
    r->method[i] = 0;
    i++;
    size_t t0 = i;
    while (i < n && line[i] != ' ')
        i++;
    if (i == t0 || i >= n)
        return -1;
    size_t tlen = i - t0;
    if (tlen + 1 > r->target_cap) {
        r->target = my_xrealloc(r->target, tlen + 1);
        r->target_cap = tlen + 1;
    }
    memcpy(r->target, line + t0, tlen);
    r->target[tlen] = 0;
    r->target_len = tlen;
    i++;
    if (i + 8 > n || memcmp(line + i, "HTTP/", 5) != 0)
        return -1;
    i += 5;
    if (i >= n || !isdigit((unsigned char)line[i]))
        return -1;
    r->http_major = line[i] - '0';
    i++;
    if (i >= n || line[i] != '.')
        return -1;
    i++;
    if (i >= n || !isdigit((unsigned char)line[i]))
        return -1;
    r->http_minor = line[i] - '0';
    i++;
    if (i != n)
        return -1;
    if (r->http_major != 1 || (r->http_minor != 0 && r->http_minor != 1))
        return -1;
    r->keep_alive = r->http_minor == 1;
    return 0;
}

static int parse_header_line(my_http_req *r, const char *line, size_t n, size_t max_headers)
{
    if ((size_t)r->nheaders >= max_headers || r->nheaders >= MY_HTTP_MAX_HEADERS) {
        snprintf(r->error, sizeof r->error, "too many headers");
        r->status_code = 431;
        return -1;
    }
    size_t i = 0;
    while (i < n && is_tchar((unsigned char)line[i]))
        i++;
    if (i == 0 || i >= n || line[i] != ':')
        return -1;
    size_t nlen = i;
    i++;
    while (i < n && (line[i] == ' ' || line[i] == '\t'))
        i++;
    size_t v0 = i;
    size_t vlen = n - v0;
    while (vlen > 0 && (line[v0 + vlen - 1] == ' ' || line[v0 + vlen - 1] == '\t'))
        vlen--;

    size_t off = r->store_len;
    store_append(r, line, nlen);
    store_append(r, "", 1);
    size_t voff = r->store_len;
    store_append(r, line + v0, vlen);
    store_append(r, "", 1);

    my_http_hdr *h = &r->headers[r->nheaders++];
    h->name = r->store + off;
    h->nlen = nlen;
    h->value = r->store + voff;
    h->vlen = vlen;

    if (my_http_header_eq(h, "content-length")) {
        if (r->chunked) {
            snprintf(r->error, sizeof r->error, "cl and chunked");
            r->status_code = 400;
            return -1;
        }
        if (r->has_content_length) {
            snprintf(r->error, sizeof r->error, "dup content-length");
            r->status_code = 400;
            return -1;
        }
        uint64_t cl = 0;
        if (vlen == 0)
            return -1;
        for (size_t k = 0; k < vlen; k++) {
            unsigned char c = (unsigned char)h->value[k];
            if (!isdigit(c))
                return -1;
            uint64_t next = cl * 10 + (uint64_t)(c - '0');
            if (next / 10 != cl) {
                snprintf(r->error, sizeof r->error, "cl overflow");
                return -1;
            }
            cl = next;
        }
        r->content_length = cl;
        r->has_content_length = 1;
    } else if (my_http_header_eq(h, "transfer-encoding")) {
        if (vlen >= 7 && !strncasecmp(h->value, "chunked", 7)) {
            if (r->has_content_length) {
                snprintf(r->error, sizeof r->error, "cl and chunked");
                r->status_code = 400;
                return -1;
            }
            r->chunked = 1;
        }
    } else if (my_http_header_eq(h, "connection")) {
        if (vlen >= 5 && !strncasecmp(h->value, "close", 5))
            r->keep_alive = 0;
        else if (vlen >= 10 && !strncasecmp(h->value, "keep-alive", 10))
            r->keep_alive = 1;
    }
    return 0;
}

static const char *find_crlf(const char *p, size_t n, size_t *idx)
{
    const void *hit = memchr(p, '\n', n);
    if (!hit)
        return NULL;
    size_t i = (size_t)((const char *)hit - p);
    if (i > 0 && p[i - 1] == '\r') {
        *idx = i - 1;
        return p + (i - 1);
    }
    *idx = i;
    return (const char *)hit;
}

my_http_parse_rc my_http_parse(my_http_req *r, const char *data, size_t len, size_t *nused,
                               size_t max_line, size_t max_header, size_t max_headers)
{
    size_t off = 0;
    *nused = 0;
    while (off < len) {
        if (r->state == MY_HS_DONE)
            break;
        if (r->state == MY_HS_REQ_LINE || r->state == MY_HS_HEADER) {
            size_t cr;
            const char *nl = find_crlf(data + off, len - off, &cr);
            if (!nl)
                break;
            size_t linelen = cr;
            int crlf = (data[off + cr] == '\r');
            size_t adv = cr + (crlf ? 2 : 1);
            r->header_bytes += adv;
            if (r->header_bytes > max_header) {
                snprintf(r->error, sizeof r->error, "headers too large");
                r->status_code = 431;
                r->state = MY_HS_ERROR;
                return MY_HTTP_PARSE_ERROR;
            }
            if (r->state == MY_HS_REQ_LINE) {
                if (linelen > max_line) {
                    snprintf(r->error, sizeof r->error, "request line too long");
                    r->status_code = 414;
                    r->state = MY_HS_ERROR;
                    return MY_HTTP_PARSE_ERROR;
                }
                if (linelen == 0) {
                    /* ignore leading CRLF */
                    off += adv;
                    *nused = off;
                    continue;
                }
                if (parse_req_line(r, data + off, linelen) < 0) {
                    snprintf(r->error, sizeof r->error, "bad request line");
                    r->status_code = 400;
                    r->state = MY_HS_ERROR;
                    return MY_HTTP_PARSE_ERROR;
                }
                r->state = MY_HS_HEADER;
            } else if (r->state == MY_HS_HEADER) {
                if (linelen == 0) {
                    if (r->chunked)
                        r->state = MY_HS_CHUNK_SIZE;
                    else if (r->has_content_length) {
                        if (r->content_length == 0)
                            r->state = MY_HS_DONE;
                        else
                            r->state = MY_HS_BODY;
                    } else
                        r->state = MY_HS_DONE;
                } else {
                    if (parse_header_line(r, data + off, linelen, max_headers) < 0) {
                        r->state = MY_HS_ERROR;
                        if (!r->status_code)
                            r->status_code = 400;
                        return MY_HTTP_PARSE_ERROR;
                    }
                }
            }
            off += adv;
            *nused = off;
            if (r->state == MY_HS_DONE)
                return MY_HTTP_PARSE_DONE;
            continue;
        }
        if (r->state == MY_HS_BODY) {
            uint64_t need = r->content_length - r->body_seen;
            size_t take = len - off;
            if ((uint64_t)take > need)
                take = (size_t)need;
            if (body_append(r, data + off, take) < 0) {
                r->state = MY_HS_ERROR;
                r->status_code = 413;
                return MY_HTTP_PARSE_ERROR;
            }
            r->body_seen += take;
            off += take;
            *nused = off;
            if (r->body_seen >= r->content_length) {
                r->state = MY_HS_DONE;
                return MY_HTTP_PARSE_DONE;
            }
            return MY_HTTP_PARSE_NEED_MORE;
        }
        if (r->state == MY_HS_CHUNK_SIZE) {
            size_t cr;
            const char *nl = find_crlf(data + off, len - off, &cr);
            if (!nl)
                return MY_HTTP_PARSE_NEED_MORE;
            uint64_t sz = 0;
            size_t i = 0;
            if (cr == 0) {
                r->state = MY_HS_ERROR;
                return MY_HTTP_PARSE_ERROR;
            }
            for (; i < cr; i++) {
                char c = data[off + i];
                if (c == ';')
                    break;
                int v;
                if (c >= '0' && c <= '9')
                    v = c - '0';
                else if (c >= 'a' && c <= 'f')
                    v = c - 'a' + 10;
                else if (c >= 'A' && c <= 'F')
                    v = c - 'A' + 10;
                else {
                    r->state = MY_HS_ERROR;
                    snprintf(r->error, sizeof r->error, "bad chunk size");
                    r->status_code = 400;
                    return MY_HTTP_PARSE_ERROR;
                }
                if (sz > (UINT64_MAX >> 4)) {
                    r->state = MY_HS_ERROR;
                    return MY_HTTP_PARSE_ERROR;
                }
                sz = (sz << 4) | (uint64_t)v;
            }
            int crlf = data[off + cr] == '\r';
            off += cr + (crlf ? 2 : 1);
            *nused = off;
            r->chunk_left = sz;
            if (sz == 0)
                r->state = MY_HS_TRAILER;
            else
                r->state = MY_HS_CHUNK_DATA;
            continue;
        }
        if (r->state == MY_HS_CHUNK_DATA) {
            size_t take = len - off;
            if ((uint64_t)take > r->chunk_left)
                take = (size_t)r->chunk_left;
            if (body_append(r, data + off, take) < 0) {
                r->state = MY_HS_ERROR;
                r->status_code = 413;
                return MY_HTTP_PARSE_ERROR;
            }
            r->chunk_left -= take;
            r->body_seen += take;
            off += take;
            *nused = off;
            if (r->chunk_left == 0)
                r->state = MY_HS_CHUNK_CRLF;
            else
                return MY_HTTP_PARSE_NEED_MORE;
            continue;
        }
        if (r->state == MY_HS_CHUNK_CRLF) {
            if (len - off < 2)
                return MY_HTTP_PARSE_NEED_MORE;
            if (data[off] != '\r' || data[off + 1] != '\n') {
                r->state = MY_HS_ERROR;
                r->status_code = 400;
                return MY_HTTP_PARSE_ERROR;
            }
            off += 2;
            *nused = off;
            r->state = MY_HS_CHUNK_SIZE;
            continue;
        }
        if (r->state == MY_HS_TRAILER) {
            size_t cr;
            const char *nl = find_crlf(data + off, len - off, &cr);
            if (!nl)
                return MY_HTTP_PARSE_NEED_MORE;
            int crlf = data[off + cr] == '\r';
            if (cr == 0) {
                off += crlf ? 2 : 1;
                *nused = off;
                r->state = MY_HS_DONE;
                return MY_HTTP_PARSE_DONE;
            }
            off += cr + (crlf ? 2 : 1);
            *nused = off;
            continue;
        }
        break;
    }
    if (r->state == MY_HS_DONE)
        return MY_HTTP_PARSE_DONE;
    return MY_HTTP_PARSE_NEED_MORE;
}
