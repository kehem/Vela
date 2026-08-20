#ifndef VELA_WS_H
#define VELA_WS_H

#include <stddef.h>
#include <stdint.h>

/* RFC6455 frame parse (unmasking). Returns bytes consumed or -1. */
int my_ws_parse_frame(const uint8_t *data, size_t len, int *fin, int *opcode,
                      uint8_t **payload, size_t *plen, size_t *consumed);

int my_ws_build_frame(uint8_t *out, size_t cap, int fin, int opcode,
                      const uint8_t *payload, size_t plen, size_t *written);

int my_ws_check_upgrade(const char *upgrade, const char *connection,
                        const char *key, char accept_out[32]);

#endif
