#include "vela/event.h"
#include "vela/log.h"
#include "vela/util.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/epoll.h>

#define MY_MAX_EVENTS 256
#define MY_MAX_FDS 65536

typedef struct {
    my_event_cb cb;
    void *ud;
    int used;
} my_slot;

struct my_loop {
    int epfd;
    int running;
    int wakeup[2];
    my_timer_cb tick;
    void *tick_ud;
    int tick_ms;
    uint64_t last_tick;
    my_slot *slots;
    int slot_cap;
};

static int grow_slots(my_loop_t *l, int fd)
{
    if (fd < l->slot_cap)
        return 0;
    int n = l->slot_cap ? l->slot_cap : 64;
    while (n <= fd) {
        if (n > MY_MAX_FDS)
            return -1;
        n *= 2;
    }
    my_slot *s = calloc((size_t)n, sizeof *s);
    if (!s)
        return -1;
    if (l->slots)
        memcpy(s, l->slots, (size_t)l->slot_cap * sizeof *s);
    free(l->slots);
    l->slots = s;
    l->slot_cap = n;
    return 0;
}

my_loop_t *my_loop_create(void)
{
    my_loop_t *l = calloc(1, sizeof *l);
    if (!l)
        return NULL;
    l->epfd = epoll_create1(EPOLL_CLOEXEC);
    if (l->epfd < 0) {
        free(l);
        return NULL;
    }
    if (pipe(l->wakeup) < 0) {
        close(l->epfd);
        free(l);
        return NULL;
    }
    my_set_nonblock(l->wakeup[0]);
    my_set_nonblock(l->wakeup[1]);
    my_set_cloexec(l->wakeup[0]);
    my_set_cloexec(l->wakeup[1]);
    struct epoll_event ev = {.events = EPOLLIN, .data.fd = l->wakeup[0]};
    if (epoll_ctl(l->epfd, EPOLL_CTL_ADD, l->wakeup[0], &ev) < 0) {
        close(l->wakeup[0]);
        close(l->wakeup[1]);
        close(l->epfd);
        free(l);
        return NULL;
    }
    l->tick_ms = 250;
    return l;
}

void my_loop_destroy(my_loop_t *loop)
{
    if (!loop)
        return;
    close(loop->wakeup[0]);
    close(loop->wakeup[1]);
    close(loop->epfd);
    free(loop->slots);
    free(loop);
}

static uint32_t to_epoll(unsigned events)
{
    uint32_t e = 0;
    if (events & MY_EV_READ)
        e |= EPOLLIN;
    if (events & MY_EV_WRITE)
        e |= EPOLLOUT;
    if (events & MY_EV_ET)
        e |= EPOLLET;
    e |= EPOLLRDHUP;
    return e;
}

int my_loop_add(my_loop_t *loop, int fd, unsigned events, my_event_cb cb, void *ud)
{
    if (grow_slots(loop, fd) < 0)
        return -1;
    struct epoll_event ev = {.events = to_epoll(events), .data.fd = fd};
    if (epoll_ctl(loop->epfd, EPOLL_CTL_ADD, fd, &ev) < 0)
        return -1;
    loop->slots[fd].cb = cb;
    loop->slots[fd].ud = ud;
    loop->slots[fd].used = 1;
    return 0;
}

int my_loop_mod(my_loop_t *loop, int fd, unsigned events, my_event_cb cb, void *ud)
{
    if (fd >= loop->slot_cap || !loop->slots[fd].used)
        return -1;
    struct epoll_event ev = {.events = to_epoll(events), .data.fd = fd};
    if (epoll_ctl(loop->epfd, EPOLL_CTL_MOD, fd, &ev) < 0)
        return -1;
    loop->slots[fd].cb = cb;
    loop->slots[fd].ud = ud;
    return 0;
}

int my_loop_del(my_loop_t *loop, int fd)
{
    if (fd < loop->slot_cap && loop->slots[fd].used) {
        loop->slots[fd].used = 0;
        loop->slots[fd].cb = NULL;
        loop->slots[fd].ud = NULL;
    }
    if (epoll_ctl(loop->epfd, EPOLL_CTL_DEL, fd, NULL) < 0) {
        if (errno == ENOENT || errno == EBADF)
            return 0;
        return -1;
    }
    return 0;
}

void my_loop_stop(my_loop_t *loop)
{
    loop->running = 0;
    char x = 1;
    (void)write(loop->wakeup[1], &x, 1);
}

int my_loop_alive(const my_loop_t *loop) { return loop && loop->running; }

int my_loop_set_tick(my_loop_t *loop, my_timer_cb cb, void *ud, int interval_ms)
{
    loop->tick = cb;
    loop->tick_ud = ud;
    loop->tick_ms = interval_ms > 0 ? interval_ms : 250;
    return 0;
}

int my_loop_run(my_loop_t *loop)
{
    struct epoll_event evs[MY_MAX_EVENTS];
    loop->running = 1;
    loop->last_tick = my_now_ms();
    while (loop->running) {
        int n = epoll_wait(loop->epfd, evs, MY_MAX_EVENTS, loop->tick_ms);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            MY_ERROR("epoll_wait: %s", strerror(errno));
            return -1;
        }
        for (int i = 0; i < n; i++) {
            int fd = evs[i].data.fd;
            if (fd == loop->wakeup[0]) {
                char buf[32];
                while (read(loop->wakeup[0], buf, sizeof buf) > 0) {
                }
                continue;
            }
            if (fd >= loop->slot_cap || !loop->slots[fd].used || !loop->slots[fd].cb)
                continue;
            unsigned e = 0;
            uint32_t ge = evs[i].events;
            if (ge & (EPOLLIN | EPOLLRDHUP | EPOLLHUP | EPOLLPRI))
                e |= MY_EV_READ;
            if (ge & EPOLLOUT)
                e |= MY_EV_WRITE;
            if (ge & EPOLLERR)
                e |= MY_EV_ERR;
            if (ge & (EPOLLHUP | EPOLLRDHUP))
                e |= MY_EV_HUP;
            loop->slots[fd].cb(loop, fd, e, loop->slots[fd].ud);
        }
        uint64_t now = my_now_ms();
        if (loop->tick && now - loop->last_tick >= (uint64_t)loop->tick_ms) {
            loop->last_tick = now;
            loop->tick(loop, loop->tick_ud);
        }
    }
    return 0;
}
