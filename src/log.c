#include "log.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

static pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;
void log_event(const char *format, ...)
{
    va_list arguments;
    pthread_mutex_lock(&output_mutex);
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    putchar('\n');
    fflush(stdout);
    pthread_mutex_unlock(&output_mutex);
}
