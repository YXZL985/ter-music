#ifndef LOGGER_H
#define LOGGER_H

typedef enum {
    LOG_LEVEL_DEBUG = 0,
    LOG_LEVEL_INFO  = 1,
    LOG_LEVEL_WARN  = 2,
    LOG_LEVEL_ERROR = 3
} LogLevel;

void logger_init(void);
void logger_shutdown(void);

void logger_set_enabled(int enabled);
int  logger_is_enabled(void);

void log_debug(const char *module, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void log_info(const char *module, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void log_warn(const char *module, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
void log_error(const char *module, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

#endif
