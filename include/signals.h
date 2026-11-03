#ifndef KOMIKAN_SIGNALS_H
#define KOMIKAN_SIGNALS_H
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
typedef struct {
    sigset_t set;
    pthread_t thread;
    void (*on_interrupt)(void *);
    void *context;
    bool started;
} signals_t;
/* Blocks SIGINT/SIGUSR1 before any worker is created. */
int signals_start(signals_t *, void (*on_interrupt)(void *), void *context);
void signals_finish(signals_t *);
#endif
