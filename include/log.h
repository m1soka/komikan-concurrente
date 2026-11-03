#ifndef KOMIKAN_LOG_H
#define KOMIKAN_LOG_H
/* A complete event (including a board snapshot) is serialized in one call. */
void log_event(const char *format, ...)
    __attribute__((format(printf, 1, 2)));
#endif
