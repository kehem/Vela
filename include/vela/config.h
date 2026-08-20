#ifndef VELA_CONFIG_H
#define VELA_CONFIG_H

#include <stddef.h>
#include <stdint.h>

#define MY_MAX_BINDS 16

typedef struct {
    const char *app;
    const char *bind[MY_MAX_BINDS];
    int nbind;
    const char *unix_socket;
    int unix_mode;
    int workers;
    int log_level;
    const char *access_log;
    const char *error_log;
    int timeout_s;
    int keep_alive_s;
    int header_timeout_s;
    int graceful_timeout_s;
    int max_connections;
    size_t max_request_line;
    size_t max_header_size;
    size_t max_headers;
    size_t max_body_size;
    size_t max_ws_frame;
    const char *ssl_cert;
    const char *ssl_key;
    int reload;
    int daemon;
    const char *pid_file;
    const char *user;
    const char *group;
    const char *chdir;
    const char *python_path;
    const char *config_file;
    const char *static_map;
    int backlog;
    int reuseport;
    const char *metrics_path;
} my_config_t;

void my_config_defaults(my_config_t *c);
int my_config_load_file(my_config_t *c, const char *path);
int my_config_parse_cli(my_config_t *c, int argc, char **argv);
void my_config_print_help(const char *argv0);

#endif
