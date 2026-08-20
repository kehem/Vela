#ifndef VELA_SERVER_H
#define VELA_SERVER_H

#include "vela/config.h"
#include "vela/event.h"

typedef struct my_listen {
    int fd;
    int is_unix;
    char path[256];
} my_listen_t;

typedef struct my_server {
    my_config_t cfg;
    my_loop_t *loop;
    my_listen_t listens[16];
    int nlisten;
    int nconn;
    int stopping;
    struct my_conn *pool;
    int pool_n;
    int pool_max;
    /* python */
    void *py_app;
    int py_ready;
} my_server_t;

int my_server_init(my_server_t *s, const my_config_t *cfg);
void my_server_fini(my_server_t *s);
int my_server_listen(my_server_t *s);
int my_server_run(my_server_t *s);
void my_server_stop(my_server_t *s);

#endif
