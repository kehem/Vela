#include "vela/metrics.h"

#include <stdio.h>
#include <stdatomic.h>

static atomic_ullong g_requests;
static atomic_ullong g_errors;
static atomic_ullong g_status_2xx, g_status_3xx, g_status_4xx, g_status_5xx;
static atomic_ullong g_lat_us;
static atomic_ullong g_lat_n;
static atomic_int g_conns;

void my_metrics_inc_requests(void) { atomic_fetch_add(&g_requests, 1); }
void my_metrics_inc_errors(void) { atomic_fetch_add(&g_errors, 1); }

void my_metrics_add_status(int code)
{
    if (code >= 500)
        atomic_fetch_add(&g_status_5xx, 1);
    else if (code >= 400)
        atomic_fetch_add(&g_status_4xx, 1);
    else if (code >= 300)
        atomic_fetch_add(&g_status_3xx, 1);
    else
        atomic_fetch_add(&g_status_2xx, 1);
}

void my_metrics_observe_latency_us(uint64_t us)
{
    atomic_fetch_add(&g_lat_us, us);
    atomic_fetch_add(&g_lat_n, 1);
}

void my_metrics_set_conns(int n) { atomic_store(&g_conns, n); }

int my_metrics_render(char *buf, size_t cap)
{
    unsigned long long n = atomic_load(&g_lat_n);
    unsigned long long avg = n ? atomic_load(&g_lat_us) / n : 0;
    return snprintf(buf, cap,
                    "# HELP vela_requests_total Total HTTP requests\n"
                    "# TYPE vela_requests_total counter\n"
                    "vela_requests_total %llu\n"
                    "# TYPE vela_errors_total counter\n"
                    "vela_errors_total %llu\n"
                    "vela_responses{code=\"2xx\"} %llu\n"
                    "vela_responses{code=\"3xx\"} %llu\n"
                    "vela_responses{code=\"4xx\"} %llu\n"
                    "vela_responses{code=\"5xx\"} %llu\n"
                    "vela_active_connections %d\n"
                    "vela_request_latency_avg_us %llu\n",
                    (unsigned long long)atomic_load(&g_requests),
                    (unsigned long long)atomic_load(&g_errors),
                    (unsigned long long)atomic_load(&g_status_2xx),
                    (unsigned long long)atomic_load(&g_status_3xx),
                    (unsigned long long)atomic_load(&g_status_4xx),
                    (unsigned long long)atomic_load(&g_status_5xx), atomic_load(&g_conns), avg);
}
