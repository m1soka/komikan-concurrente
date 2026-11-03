#include "log.h"
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>

static pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;
static void unlock_output(void *unused)
{
    (void)unused;
    pthread_mutex_unlock(&output_mutex);
}

void log_event(const char *format, ...)
{
    va_list arguments;
    int previous_state;
    /* printf/fflush may be cancellation points. Restore only after unlocking
       so deferred cancellation cannot strand the shared output mutex. */
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, &previous_state);
    pthread_mutex_lock(&output_mutex);
    pthread_cleanup_push(unlock_output, NULL);
    va_start(arguments, format);
    vprintf(format, arguments);
    va_end(arguments);
    putchar('\n');
    fflush(stdout);
    pthread_cleanup_pop(1);
    pthread_setcancelstate(previous_state, NULL);
}
