#ifndef VELA_EVENT_H
#define VELA_EVENT_H

#include <stddef.h>
#include <stdint.h>

#define MY_EV_READ  1u
#define MY_EV_WRITE 2u
#define MY_EV_ERR   4u
#define MY_EV_HUP   8u
#define MY_EV_ET    16u

typedef struct my_loop my_loop_t;

typedef void (*my_event_cb)(my_loop_t *loop, int fd, unsigned events, void *ud);

my_loop_t *my_loop_create(void);
void my_loop_destroy(my_loop_t *loop);
int my_loop_add(my_loop_t *loop, int fd, unsigned events, my_event_cb cb, void *ud);
int my_loop_mod(my_loop_t *loop, int fd, unsigned events, my_event_cb cb, void *ud);
int my_loop_del(my_loop_t *loop, int fd);
int my_loop_run(my_loop_t *loop);
void my_loop_stop(my_loop_t *loop);
int my_loop_alive(const my_loop_t *loop);

typedef void (*my_timer_cb)(my_loop_t *loop, void *ud);
int my_loop_set_tick(my_loop_t *loop, my_timer_cb cb, void *ud, int interval_ms);

#endif
