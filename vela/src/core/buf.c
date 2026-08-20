#include "vela/buf.h"
#include "vela/util.h"

#include <string.h>
#include <stdint.h>
#include <stdlib.h>

int my_buf_init(my_buf_t *b, size_t cap)
{
    memset(b, 0, sizeof *b);
    if (cap) {
        b->data = my_xmalloc(cap);
        b->cap = cap;
    }
    return 0;
}

void my_buf_free(my_buf_t *b)
{
    free(b->data);
    memset(b, 0, sizeof *b);
}

int my_buf_reserve(my_buf_t *b, size_t extra)
{
    size_t need = b->len + extra;
    if (need <= b->cap)
        return 0;
    size_t ncap = b->cap ? b->cap : 4096;
    while (ncap < need) {
        if (ncap > (SIZE_MAX / 2))
            return -1;
        ncap *= 2;
    }
    b->data = my_xrealloc(b->data, ncap);
    b->cap = ncap;
    return 0;
}

int my_buf_append(my_buf_t *b, const void *p, size_t n)
{
    if (my_buf_reserve(b, n) < 0)
        return -1;
    memcpy(b->data + b->len, p, n);
    b->len += n;
    return 0;
}

void my_buf_consume(my_buf_t *b, size_t n)
{
    if (n > b->len - b->pos)
        n = b->len - b->pos;
    b->pos += n;
    if (b->pos == b->len) {
        b->pos = 0;
        b->len = 0;
    } else if (b->pos > 4096 && b->pos * 2 > b->len) {
        my_buf_compact(b);
    }
}

void my_buf_compact(my_buf_t *b)
{
    if (b->pos == 0)
        return;
    size_t live = b->len - b->pos;
    memmove(b->data, b->data + b->pos, live);
    b->len = live;
    b->pos = 0;
}

void my_buf_reset(my_buf_t *b)
{
    b->len = 0;
    b->pos = 0;
}
