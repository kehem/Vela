#ifndef VELA_LOG_H
#define VELA_LOG_H

#include <stdarg.h>

typedef enum {
    MY_LOG_DEBUG = 0,
    MY_LOG_INFO,
    MY_LOG_WARN,
    MY_LOG_ERROR,
    MY_LOG_CRIT
} my_log_level_t;

void my_log_init(my_log_level_t level, const char *error_path, const char *access_path);
void my_log_set_level(my_log_level_t level);
my_log_level_t my_log_parse_level(const char *s);
void my_log_close(void);

void my_log(my_log_level_t level, const char *fmt, ...);
int my_access_enabled(void);
void my_access_log(const char *client, const char *method, const char *path,
                   int status, unsigned long long bytes, double ms);

#define MY_DEBUG(...) my_log(MY_LOG_DEBUG, __VA_ARGS__)
#define MY_INFO(...)  my_log(MY_LOG_INFO, __VA_ARGS__)
#define MY_WARN(...)  my_log(MY_LOG_WARN, __VA_ARGS__)
#define MY_ERROR(...) my_log(MY_LOG_ERROR, __VA_ARGS__)
#define MY_CRIT(...)  my_log(MY_LOG_CRIT, __VA_ARGS__)

#endif
