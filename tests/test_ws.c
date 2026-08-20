#include "vela/ws.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    uint8_t frame[64];
    size_t w = 0;
    const uint8_t msg[] = "hi";
    assert(my_ws_build_frame(frame, sizeof frame, 1, 1, msg, 2, &w) == 0);
    int fin, op;
    uint8_t *payload;
    size_t plen, cons;
    assert(my_ws_parse_frame(frame, w, &fin, &op, &payload, &plen, &cons) == 1);
    assert(fin == 1 && op == 1 && plen == 2 && cons == w);
    assert(payload[0] == 'h' && payload[1] == 'i');

    char acc[32];
    /* RFC 6455 example key */
    int ok = my_ws_check_upgrade("websocket", "Upgrade", "dGhlIHNhbXBsZSBub25jZQ==", acc);
    assert(ok);
    assert(strcmp(acc, "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=") == 0);
    printf("test_ws OK\n");
    return 0;
}
