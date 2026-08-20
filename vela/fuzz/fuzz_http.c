#include "vela/http.h"
#include <stdint.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
    my_http_req r;
    my_http_req_init(&r);
    size_t used = 0;
    (void)my_http_parse(&r, (const char *)data, size, &used, 8192, 65536, 100);
    my_http_req_free(&r);
    return 0;
}
