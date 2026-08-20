#include "vela/util.h"

#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

void *my_xmalloc(size_t n)
{
    void *p = malloc(n ? n : 1);
    if (!p)
        abort();
    return p;
}

void *my_xcalloc(size_t n)
{
    void *p = calloc(1, n ? n : 1);
    if (!p)
        abort();
    return p;
}

void *my_xrealloc(void *p, size_t n)
{
    void *q = realloc(p, n ? n : 1);
    if (!q)
        abort();
    return q;
}

char *my_xstrdup(const char *s)
{
    if (!s)
        return NULL;
    size_t n = strlen(s) + 1;
    char *d = my_xmalloc(n);
    memcpy(d, s, n);
    return d;
}

int my_set_nonblock(int fd)
{
    int fl = fcntl(fd, F_GETFL, 0);
    if (fl < 0)
        return -1;
    return fcntl(fd, F_SETFL, fl | O_NONBLOCK);
}

int my_set_cloexec(int fd)
{
    int fl = fcntl(fd, F_GETFD, 0);
    if (fl < 0)
        return -1;
    return fcntl(fd, F_SETFD, fl | FD_CLOEXEC);
}

int my_set_reuseaddr(int fd)
{
    int on = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof on);
}

int my_set_reuseport(int fd)
{
#ifdef SO_REUSEPORT
    int on = 1;
    return setsockopt(fd, SOL_SOCKET, SO_REUSEPORT, &on, sizeof on);
#else
    (void)fd;
    return 0;
#endif
}

int my_set_nodelay(int fd)
{
    int on = 1;
    return setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &on, sizeof on);
}

uint64_t my_now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

uint64_t my_now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

int my_parse_bind(const char *spec, char *host, size_t host_sz, uint16_t *port)
{
    if (!spec || !host || !port)
        return -1;
    const char *colon = strrchr(spec, ':');
    if (!colon)
        return -1;
    size_t hl = (size_t)(colon - spec);
    if (hl >= host_sz || hl == 0)
        return -1;
    memcpy(host, spec, hl);
    host[hl] = 0;
    int p = atoi(colon + 1);
    if (p <= 0 || p > 65535)
        return -1;
    *port = (uint16_t)p;
    return 0;
}

ssize_t my_write_all(int fd, const void *buf, size_t n)
{
    const char *p = buf;
    size_t left = n;
    while (left) {
        ssize_t w = write(fd, p, left);
        if (w < 0) {
            if (errno == EINTR)
                continue;
            return -1;
        }
        p += (size_t)w;
        left -= (size_t)w;
    }
    return (ssize_t)n;
}
