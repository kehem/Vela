#ifndef VELA_METRICS_H
#define VELA_METRICS_H

#include <stdint.h>
#include <stddef.h>

void my_metrics_inc_requests(void);
void my_metrics_inc_errors(void);
void my_metrics_add_status(int code);
void my_metrics_observe_latency_us(uint64_t us);
void my_metrics_set_conns(int n);
int my_metrics_render(char *buf, size_t cap);

#endif
