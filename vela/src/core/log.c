#include "vela/log.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <sys/time.h>
#include <pthread.h>
#include <strings.h>

static my_log_level_t g_level = MY_LOG_INFO;
static FILE *g_err;
static FILE *g_acc;
static pthread_mutex_t g_mu = PTHREAD_MUTEX_INITIALIZER;

void my_log_init(my_log_level_t level, const char *error_path, const char *access_path)
{
    g_level = level;
    g_err = stderr;
    g_acc = NULL;
    if (error_path && strcmp(error_path, "-") != 0) {
        FILE *f = fopen(error_path, "a");
        if (f)
            g_err = f;
    }
    if (access_path && strcmp(access_path, "-") != 0) {
        FILE *f = fopen(access_path, "a");
        if (f)
            g_acc = f;
        else
            g_acc = stdout;
    } else if (access_path) {
        g_acc = stdout;
    }
}

void my_log_set_level(my_log_level_t level) { g_level = level; }

my_log_level_t my_log_parse_level(const char *s)
{
    if (!s)
        return MY_LOG_INFO;
    if (!strcasecmp(s, "debug"))
        return MY_LOG_DEBUG;
    if (!strcasecmp(s, "info"))
        return MY_LOG_INFO;
    if (!strcasecmp(s, "warning") || !strcasecmp(s, "warn"))
        return MY_LOG_WARN;
    if (!strcasecmp(s, "error"))
        return MY_LOG_ERROR;
    if (!strcasecmp(s, "critical") || !strcasecmp(s, "crit"))
        return MY_LOG_CRIT;
    return MY_LOG_INFO;
}

void my_log_close(void)
{
    if (g_err && g_err != stderr)
        fclose(g_err);
    if (g_acc && g_acc != stdout && g_acc != stderr)
        fclose(g_acc);
    g_err = stderr;
    g_acc = NULL;
}

static const char *lvl_name(my_log_level_t l)
{
    switch (l) {
    case MY_LOG_DEBUG:
        return "DEBUG";
    case MY_LOG_INFO:
        return "INFO";
    case MY_LOG_WARN:
        return "WARN";
    case MY_LOG_ERROR:
        return "ERROR";
    default:
        return "CRIT";
    }
}

void my_log(my_log_level_t level, const char *fmt, ...)
{
    if (level < g_level)
        return;
    struct timeval tv;
    gettimeofday(&tv, NULL);
    struct tm tm;
    localtime_r(&tv.tv_sec, &tm);
    char ts[64];
    strftime(ts, sizeof ts, "%Y-%m-%d %H:%M:%S", &tm);

    pthread_mutex_lock(&g_mu);
    fprintf(g_err ? g_err : stderr, "%s.%03d [%s] pid=%d ", ts, (int)(tv.tv_usec / 1000),
            lvl_name(level), (int)getpid());
    va_list ap;
    va_start(ap, fmt);
    vfprintf(g_err ? g_err : stderr, fmt, ap);
    va_end(ap);
    fputc('\n', g_err ? g_err : stderr);
    fflush(g_err ? g_err : stderr);
    pthread_mutex_unlock(&g_mu);
}

int my_access_enabled(void) { return g_acc != NULL; }

void my_access_log(const char *client, const char *method, const char *path, int status,
                   unsigned long long bytes, double ms)
{
    FILE *f = g_acc;
    if (!f)
        return;
    pthread_mutex_lock(&g_mu);
    fprintf(f, "%s - %s %s HTTP/1.1 %d %llu %.1fms\n", client ? client : "-",
            method ? method : "-", path ? path : "-", status, bytes, ms);
    fflush(f);
    pthread_mutex_unlock(&g_mu);
}
