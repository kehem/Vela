#include "vela/config.h"
#include "vela/log.h"
#include "vela/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void my_config_defaults(my_config_t *c)
{
    memset(c, 0, sizeof *c);
    c->workers = 1;
    c->log_level = MY_LOG_INFO;
    c->timeout_s = 60;
    c->keep_alive_s = 5;
    c->header_timeout_s = 10;
    c->graceful_timeout_s = 30;
    c->max_connections = 10000;
    c->max_request_line = 8192;
    c->max_header_size = 65536;
    c->max_headers = 100;
    c->max_body_size = 16ull * 1024 * 1024;
    c->max_ws_frame = 1024 * 1024;
    c->backlog = 16384;
    c->reuseport = 1;
    c->unix_mode = 0660;
    c->metrics_path = "/metrics";
}

void my_config_print_help(const char *argv0)
{
    printf("Usage: %s APP [OPTIONS]\n\n", argv0);
    printf("  APP                         Python ASGI target, e.g. myapp.asgi:application\n\n");
    printf("Options:\n");
    printf("  --bind ADDR                 TCP bind host:port (repeatable). Default 127.0.0.1:8000\n");
    printf("  --unix-socket PATH          Listen on a Unix domain socket\n");
    printf("  --workers N                 Worker processes (default 1)\n");
    printf("  --log-level LEVEL           debug|info|warning|error|critical\n");
    printf("  --access-log PATH           Access log file or -\n");
    printf("  --error-log PATH            Error log file\n");
    printf("  --timeout SEC               Request timeout (default 60)\n");
    printf("  --keep-alive SEC            Keep-alive timeout (default 5)\n");
    printf("  --max-connections N         Per-worker connection cap\n");
    printf("  --max-request-size N        Max request body bytes\n");
    printf("  --ssl-cert PATH             TLS certificate (PEM)\n");
    printf("  --ssl-key PATH              TLS private key (PEM)\n");
    printf("  --reload                    Dev auto-reload (not for production)\n");
    printf("  --daemon                    Daemonize\n");
    printf("  --pid-file PATH             Write master PID\n");
    printf("  --user NAME                 Drop privileges\n");
    printf("  --group NAME                Drop privileges\n");
    printf("  --working-directory DIR     chdir before serving\n");
    printf("  --python-path PATH          Prepend to sys.path (':' separated)\n");
    printf("  --config FILE               Load TOML-ish key=value config\n");
    printf("  --static MAP                native static map /url=/dir\n");
    printf("  --help                      This help\n");
    printf("  --version                   Version string\n");
}

int my_config_load_file(my_config_t *c, const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return -1;
    char line[1024];
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p == '#' || *p == '\n' || *p == 0 || *p == '[')
            continue;
        char *eq = strchr(p, '=');
        if (!eq)
            continue;
        *eq = 0;
        char *k = p;
        char *v = eq + 1;
        while (*v == ' ' || *v == '\t' || *v == '"' || *v == '[')
            v++;
        char *e = v + strlen(v);
        while (e > v && (e[-1] == '\n' || e[-1] == '\r' || e[-1] == ' ' || e[-1] == '"' ||
                         e[-1] == ']'))
            *--e = 0;
        char *kt = k + strlen(k);
        while (kt > k && (kt[-1] == ' ' || kt[-1] == '\t'))
            *--kt = 0;
        if (!strcmp(k, "workers"))
            c->workers = atoi(v);
        else if (!strcmp(k, "bind")) {
            if (c->nbind < MY_MAX_BINDS)
                c->bind[c->nbind++] = my_xstrdup(v);
        } else if (!strcmp(k, "keep_alive"))
            c->keep_alive_s = atoi(v);
        else if (!strcmp(k, "request_timeout"))
            c->timeout_s = atoi(v);
        else if (!strcmp(k, "max_connections"))
            c->max_connections = atoi(v);
        else if (!strcmp(k, "unix_socket"))
            c->unix_socket = my_xstrdup(v);
        else if (!strcmp(k, "log_level"))
            c->log_level = my_log_parse_level(v);
    }
    fclose(f);
    return 0;
}

int my_config_parse_cli(my_config_t *c, int argc, char **argv)
{
    my_config_defaults(c);
    /* pre-scan --config */
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--config") && i + 1 < argc) {
            if (my_config_load_file(c, argv[i + 1]) < 0) {
                fprintf(stderr, "cannot load config %s\n", argv[i + 1]);
                return -1;
            }
            c->config_file = argv[i + 1];
        }
    }
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        if (!strcmp(a, "--help") || !strcmp(a, "-h")) {
            my_config_print_help(argv[0]);
            exit(0);
        }
        if (!strcmp(a, "--version")) {
            printf("vela 0.1.0\n");
            exit(0);
        }
        if (a[0] != '-') {
            c->app = a;
            continue;
        }
#define NEED if (i + 1 >= argc) { fprintf(stderr, "missing value for %s\n", a); return -1; }
        if (!strcmp(a, "--bind")) {
            NEED;
            if (c->nbind < MY_MAX_BINDS)
                c->bind[c->nbind++] = argv[++i];
        } else if (!strcmp(a, "--unix-socket")) {
            NEED;
            c->unix_socket = argv[++i];
        } else if (!strcmp(a, "--workers")) {
            NEED;
            c->workers = atoi(argv[++i]);
        } else if (!strcmp(a, "--log-level")) {
            NEED;
            c->log_level = my_log_parse_level(argv[++i]);
        } else if (!strcmp(a, "--access-log")) {
            NEED;
            c->access_log = argv[++i];
        } else if (!strcmp(a, "--error-log")) {
            NEED;
            c->error_log = argv[++i];
        } else if (!strcmp(a, "--timeout")) {
            NEED;
            c->timeout_s = atoi(argv[++i]);
        } else if (!strcmp(a, "--keep-alive")) {
            NEED;
            c->keep_alive_s = atoi(argv[++i]);
        } else if (!strcmp(a, "--max-connections")) {
            NEED;
            c->max_connections = atoi(argv[++i]);
        } else if (!strcmp(a, "--max-request-size")) {
            NEED;
            c->max_body_size = (size_t)strtoull(argv[++i], NULL, 10);
        } else if (!strcmp(a, "--ssl-cert")) {
            NEED;
            c->ssl_cert = argv[++i];
        } else if (!strcmp(a, "--ssl-key")) {
            NEED;
            c->ssl_key = argv[++i];
        } else if (!strcmp(a, "--reload")) {
            c->reload = 1;
        } else if (!strcmp(a, "--daemon")) {
            c->daemon = 1;
        } else if (!strcmp(a, "--pid-file")) {
            NEED;
            c->pid_file = argv[++i];
        } else if (!strcmp(a, "--user")) {
            NEED;
            c->user = argv[++i];
        } else if (!strcmp(a, "--group")) {
            NEED;
            c->group = argv[++i];
        } else if (!strcmp(a, "--working-directory")) {
            NEED;
            c->chdir = argv[++i];
        } else if (!strcmp(a, "--python-path")) {
            NEED;
            c->python_path = argv[++i];
        } else if (!strcmp(a, "--config")) {
            i++;
        } else if (!strcmp(a, "--static")) {
            NEED;
            c->static_map = argv[++i];
        } else {
            fprintf(stderr, "unknown option: %s\n", a);
            return -1;
        }
#undef NEED
    }
    if (c->nbind == 0 && !c->unix_socket)
        c->bind[c->nbind++] = "127.0.0.1:8000";
    if (c->workers < 1)
        c->workers = 1;
    return 0;
}
