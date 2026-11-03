#ifndef KOMIKAN_SIGNALS_H
#define KOMIKAN_SIGNALS_H
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>

typedef struct {
    sigset_t set;
    pthread_t thread;
    pthread_mutex_t callback_mutex;
    void (*on_interrupt)(void *);
    void *context;
    atomic_bool interrupted;
    bool started;
} signals_t;
/* First action in main: inherited by every thread, including startup. */
int signals_block(void);
/* Start before parsing configuration or allocating tournament resources. */
int signals_start(signals_t *);
bool signals_interrupted(const signals_t *);
/* Bind/unbind only without holding the tournament mutex. Unbind waits for
   callbacks already in progress, so their context may then be destroyed. */
void signals_bind(signals_t *, void (*on_interrupt)(void *), void *context);
void signals_finish(signals_t *);
#endif
