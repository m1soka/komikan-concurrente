#include "signals.h"
#include <string.h>

static void *signal_worker(void *argument)
{
    signals_t *signals = argument;
    int received = 0;
    int error = sigwait(&signals->set, &received);
    if (error == 0 && received == SIGINT)
        signals->on_interrupt(signals->context);
    return NULL;
}
int signals_start(signals_t *signals, void (*callback)(void *), void *context)
{
    memset(signals, 0, sizeof(*signals));
    signals->on_interrupt = callback;
    signals->context = context;
    sigemptyset(&signals->set);
    sigaddset(&signals->set, SIGINT);
    sigaddset(&signals->set, SIGUSR1);
    int error = pthread_sigmask(SIG_BLOCK, &signals->set, NULL);
    if (error != 0) return error;
    error = pthread_create(&signals->thread, NULL, signal_worker, signals);
    if (error == 0) signals->started = true;
    return error;
}
void signals_finish(signals_t *signals)
{
    if (signals->started) {
        /* A directed signal wakes sigwait on normal completion, without cancel. */
        (void)pthread_kill(signals->thread, SIGUSR1);
        (void)pthread_join(signals->thread, NULL);
        signals->started = false;
    }
}
