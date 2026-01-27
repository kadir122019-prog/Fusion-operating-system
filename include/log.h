#ifndef LOG_H
#define LOG_H

#include "types.h"

typedef enum log_level {
    LOG_LEVEL_PANIC = 0,
    LOG_LEVEL_ERROR,
    LOG_LEVEL_WARN,
    LOG_LEVEL_INFO,
    LOG_LEVEL_DEBUG
} log_level_t;

void log_init(void);
void log_write(log_level_t level, const char *msg);
void panic(const char *file, int line, const char *msg) __attribute__((noreturn));

#define LOG_PANIC(msg) log_write(LOG_LEVEL_PANIC, (msg))
#define LOG_ERROR(msg) log_write(LOG_LEVEL_ERROR, (msg))
#define LOG_WARN(msg)  log_write(LOG_LEVEL_WARN, (msg))
#define LOG_INFO(msg)  log_write(LOG_LEVEL_INFO, (msg))
#define LOG_DEBUG(msg) log_write(LOG_LEVEL_DEBUG, (msg))

#define PANIC(msg) panic(__FILE__, __LINE__, (msg))

#endif
