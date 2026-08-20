#ifndef VELA_UTIL_H
#define VELA_UTIL_H

#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

#define MY_OK 0
#define MY_ERR -1

void *my_xmalloc(size_t n);
void *my_xcalloc(size_t n);
void *my_xrealloc(void *p, size_t n);
char *my_xstrdup(const char *s);

int my_set_nonblock(int fd);
int my_set_cloexec(int fd);
int my_set_reuseaddr(int fd);
int my_set_reuseport(int fd);
int my_set_nodelay(int fd);

uint64_t my_now_ms(void);
uint64_t my_now_us(void);

int my_parse_bind(const char *spec, char *host, size_t host_sz, uint16_t *port);

ssize_t my_write_all(int fd, const void *buf, size_t n);

#endif
