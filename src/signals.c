#include "signals.h"
#include <string.h>

static void notify_interrupt(signals_t *signals)
{
    atomic_store(&signals->interrupted, true);
    pthread_mutex_lock(&signals->callback_mutex);
    if (signals->on_interrupt != NULL)
        signals->on_interrupt(signals->context);
    pthread_mutex_unlock(&signals->callback_mutex);
}

static void *signal_worker(void *argument)
{
    signals_t *signals = argument;
    for (;;) {
        int received = 0;
        int error = sigwait(&signals->set, &received);
        if (error != 0) {
            /* The configured set is valid. An unexpected wait failure must
               still wake the server instead of silently leaving it running. */
            notify_interrupt(signals);
            return NULL;
        }
        if (received == SIGINT) notify_interrupt(signals);
        if (received == SIGUSR1) {
            /* Preserve a SIGINT already pending during normal teardown. */
            sigset_t pending;
            if (sigpending(&pending) == 0 && sigismember(&pending, SIGINT) == 1)
                notify_interrupt(signals);
            return NULL;
        }
    }
}

int signals_block(void)
{
    sigset_t set;
    sigemptyset(&set);
    sigaddset(&set, SIGINT);
    sigaddset(&set, SIGUSR1);
    return pthread_sigmask(SIG_BLOCK, &set, NULL);
}

int signals_start(signals_t *signals)
{
    memset(signals, 0, sizeof(*signals));
    atomic_init(&signals->interrupted, false);
    sigemptyset(&signals->set);
    sigaddset(&signals->set, SIGINT);
    sigaddset(&signals->set, SIGUSR1);
    int error = pthread_mutex_init(&signals->callback_mutex, NULL);
    if (error != 0) return error;
    error = pthread_create(&signals->thread, NULL, signal_worker, signals);
    if (error == 0) signals->started = true;
    else pthread_mutex_destroy(&signals->callback_mutex);
    return error;
}

bool signals_interrupted(const signals_t *signals)
{
    return atomic_load(&signals->interrupted);
}

void signals_bind(signals_t *signals, void (*callback)(void *), void *context)
{
    pthread_mutex_lock(&signals->callback_mutex);
    signals->on_interrupt = callback;
    signals->context = context;
    if (callback != NULL && signals_interrupted(signals)) callback(context);
    pthread_mutex_unlock(&signals->callback_mutex);
}

void signals_finish(signals_t *signals)
{
    if (signals->started) {
        (void)pthread_kill(signals->thread, SIGUSR1);
        (void)pthread_join(signals->thread, NULL);
        pthread_mutex_destroy(&signals->callback_mutex);
        signals->started = false;
    }
}
