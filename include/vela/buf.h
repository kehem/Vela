#ifndef VELA_BUF_H
#define VELA_BUF_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
    size_t pos;
} my_buf_t;

int my_buf_init(my_buf_t *b, size_t cap);
void my_buf_free(my_buf_t *b);
int my_buf_reserve(my_buf_t *b, size_t extra);
int my_buf_append(my_buf_t *b, const void *p, size_t n);
void my_buf_consume(my_buf_t *b, size_t n);
void my_buf_compact(my_buf_t *b);
void my_buf_reset(my_buf_t *b);

#endif
