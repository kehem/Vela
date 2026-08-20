#include "vela/config.h"
#include "vela/worker.h"
#include "vela/log.h"

#include <stdio.h>

int main(int argc, char **argv)
{
    my_config_t cfg;
    if (my_config_parse_cli(&cfg, argc, argv) < 0)
        return 2;
    if (!cfg.app) {
        fprintf(stderr, "usage: %s module:attribute [options]\ntry --help\n", argv[0]);
        /* allow no-app for smoke TCP tests via env */
    }
    return my_master_run(&cfg);
}
