#include "vela/config.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

int main(void)
{
    my_config_t c;
    char *argv[] = {"vela", "app:application", "--workers", "4", "--bind", "0.0.0.0:9000",
                    "--log-level", "debug"};
    assert(my_config_parse_cli(&c, 8, argv) == 0);
    assert(c.workers == 4);
    assert(c.nbind == 1);
    assert(strcmp(c.bind[0], "0.0.0.0:9000") == 0);
    assert(strcmp(c.app, "app:application") == 0);

    FILE *f = fopen("/tmp/vela-test.toml", "w");
    fprintf(f, "workers = 2\nbind = \"127.0.0.1:8001\"\nkeep_alive = 9\n");
    fclose(f);
    my_config_defaults(&c);
    assert(my_config_load_file(&c, "/tmp/vela-test.toml") == 0);
    assert(c.workers == 2);
    assert(c.keep_alive_s == 9);
    unlink("/tmp/vela-test.toml");
    printf("test_config OK\n");
    return 0;
}
