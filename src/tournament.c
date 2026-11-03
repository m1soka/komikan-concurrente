#include "tournament.h"
#include "game.h"
#include "log.h"
#include "queue.h"
#include "signals.h"
#include <limits.h>
#include <errno.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

typedef struct tournament tournament_t;
typedef struct {
    tournament_t *tournament;
    pthread_t thread;
    size_t id, arrival_order;
    bool assigned, done, registered, failed;
    game_t *game;
} client_t;
typedef enum { SLOT_FREE, SLOT_RUNNING, SLOT_FINISHED } slot_state_t;
typedef struct {
    game_t game;
    pthread_t judge;
    slot_state_t state;
    size_t players[2], match;
} slot_t;
struct tournament {
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    config_t config;
    size_t n, limit, slots_count, created, arrived;
    size_t active, peak, matches, completed, failed_before_arrival;
    bool creation_done, stop, interrupted, fatal;
    queue_t waiting;
    client_t *clients;
    slot_t *slots;
};

static bool deadline_ms(size_t milliseconds, struct timespec *deadline)
{
    if (clock_gettime(CLOCK_MONOTONIC, deadline) != 0 || deadline->tv_sec < 0)
        return false;
    uintmax_t maximum = (time_t)-1 > 0 ? (uintmax_t)(time_t)-1
        : UINTMAX_MAX >> (sizeof(uintmax_t) * CHAR_BIT - sizeof(time_t) * CHAR_BIT + 1);
    uintmax_t seconds = milliseconds / 1000;
    long nanos = deadline->tv_nsec + (long)(milliseconds % 1000) * 1000000L;
    if (nanos >= 1000000000L) { seconds++; nanos -= 1000000000L; }
    if ((uintmax_t)deadline->tv_sec > maximum || seconds > maximum - (uintmax_t)deadline->tv_sec)
        return false;
    deadline->tv_sec = (time_t)((uintmax_t)deadline->tv_sec + seconds);
    deadline->tv_nsec = nanos;
    return true;
}
/* Never nests the tournament lock with a board or output lock. */
static void request_stop(tournament_t *tournament, bool interrupted, bool fatal)
{
    pthread_mutex_lock(&tournament->mutex);
    tournament->stop = true;
    tournament->interrupted |= interrupted;
    tournament->fatal |= fatal;
    pthread_cond_broadcast(&tournament->changed);
    pthread_mutex_unlock(&tournament->mutex);
}
static void on_interrupt(void *context)
{
    request_stop(context, true, false);
    log_event("SERVIDOR SIGINT recibido; cierre cooperativo");
}

typedef struct {
    client_t *client;
    bool mutex_owned;
    bool normal_return;
} client_cleanup_t;

static void client_lock(client_cleanup_t *cleanup)
{
    pthread_mutex_lock(&cleanup->client->tournament->mutex);
    cleanup->mutex_owned = true;
}

static void client_unlock(client_cleanup_t *cleanup)
{
    pthread_mutex_unlock(&cleanup->client->tournament->mutex);
    cleanup->mutex_owned = false;
}

/* Cancellation is disabled in clients. pthread_exit still runs this handler.
   It knows whether this very thread owns the mutex; it never unlocks another
   thread's mutex or attempts to recover a partially modified board. */
static void client_cleanup(void *argument)
{
    client_cleanup_t *cleanup = argument;
    client_t *client = cleanup->client;
    tournament_t *tournament = client->tournament;
    game_t *game = NULL;
    bool failed = false;
    if (!cleanup->normal_return) {
        if (!cleanup->mutex_owned) client_lock(cleanup);
        if (!client->done && !tournament->stop) {
            client->failed = client->done = true;
            failed = true;
            if (!client->registered) tournament->failed_before_arrival++;
            if (client->assigned) game = client->game;
            else {
                /* Preserve the relative FIFO order of every other client. */
                size_t count = queue_size(&tournament->waiting), id;
                for (size_t index = 0; index < count; index++) {
                    (void)queue_pop(&tournament->waiting, &id);
                    if (id != client->id) (void)queue_push(&tournament->waiting, id);
                }
            }
            pthread_cond_broadcast(&tournament->changed);
        }
    }
    if (cleanup->mutex_owned) client_unlock(cleanup);
    if (failed) log_event("FALLO_CLIENTE jugador=%zu tipo=pthread_exit", client->id);
    if (game != NULL) game_participant_failed(game, client->id);
}

static int client_wait(client_cleanup_t *cleanup, const struct timespec *deadline)
{
    tournament_t *tournament = cleanup->client->tournament;
    int error = deadline ? pthread_cond_timedwait(&tournament->changed, &tournament->mutex, deadline)
                         : pthread_cond_wait(&tournament->changed, &tournament->mutex);
    if (error != 0 && error != ETIMEDOUT) {
        tournament->stop = tournament->fatal = true;
        pthread_cond_broadcast(&tournament->changed);
    }
    return error;
}

static void client_body(client_cleanup_t *cleanup)
{
    client_t *client = cleanup->client;
    tournament_t *tournament = client->tournament;
    struct timespec arrival;
    if (!deadline_ms((client->id - 1) * tournament->config.arrival_delay_ms, &arrival)) {
        request_stop(tournament, false, true);
        return;
    }
    client_lock(cleanup);
    int error = 0;
    if (tournament->config.arrival_delay_ms > 0) {
        while (!tournament->stop && error != ETIMEDOUT)
            error = client_wait(cleanup, &arrival);
    }
    if (tournament->stop) {
        client_unlock(cleanup);
        return;
    }
    if (!queue_push(&tournament->waiting, client->id)) {
        tournament->stop = tournament->fatal = true;
        pthread_cond_broadcast(&tournament->changed);
        client_unlock(cleanup);
        return;
    }
    client->registered = true;
    client->arrival_order = ++tournament->arrived;
    pthread_cond_broadcast(&tournament->changed);
    client_unlock(cleanup);
    log_event("LLEGADA jugador=%zu orden=%zu", client->id, client->arrival_order);
    client_lock(cleanup);
    while (!client->assigned && !client->done && !tournament->stop)
        (void)client_wait(cleanup, NULL);
    if (client->assigned && !client->done && !tournament->stop &&
        tournament->config.fail_player == client->id) {
        struct timespec failure;
        if (!deadline_ms(tournament->config.fail_after_ms, &failure)) {
            tournament->stop = tournament->fatal = true;
            pthread_cond_broadcast(&tournament->changed);
            client_unlock(cleanup);
            return;
        }
        error = 0;
        while (!client->done && !tournament->stop && error != ETIMEDOUT)
            error = client_wait(cleanup, &failure);
        if (!client->done && !tournament->stop && error == ETIMEDOUT) {
            game_t *game = client->game;
            failure_mode_t mode = tournament->config.fail_mode;
            if (mode == FAIL_CLIENT_EXIT_LOCKED) pthread_exit(NULL);
            client_unlock(cleanup);
            if (mode == FAIL_CLIENT_EXIT_UNLOCKED) pthread_exit(NULL);
            if (mode == FAIL_ABANDON) {
                log_event("ABANDONO jugador=%zu", client->id);
                game_abandon(game, client->id);
            } else {
                game_failure_mode_t piece_mode = mode == FAIL_PIECE_EXIT_LOCKED ? GAME_FAIL_EXIT_LOCKED
                    : mode == FAIL_PIECE_EXIT_UNLOCKED ? GAME_FAIL_EXIT_UNLOCKED : GAME_FAIL_CANCEL_WAIT;
                (void)game_inject_piece_failure(game, client->id, piece_mode);
            }
            client_lock(cleanup);
        }
    }
    while (!client->done && !tournament->stop)
        (void)client_wait(cleanup, NULL);
    client_unlock(cleanup);
}

static void *client_run(void *argument)
{
    /* No asynchronous cancellation and no forced cancellation of clients.
       Controlled pthread_exit is handled before the thread can disappear. */
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    client_cleanup_t cleanup = {.client = argument};
    pthread_cleanup_push(client_cleanup, &cleanup);
    client_body(&cleanup);
    if (!cleanup.mutex_owned) client_lock(&cleanup);
    cleanup.normal_return = cleanup.client->done || cleanup.client->tournament->stop;
    client_unlock(&cleanup);
    pthread_cleanup_pop(1);
    return NULL;
}

/* Called only once by a judge, after all of that game's pieces have joined. */
static void on_game_finished(void *context, game_t *game, int winner, const char *reason)
{
    tournament_t *tournament = context;
    /* Keep RESULTADO ahead of a new PAREJA that might reuse this table. */
    log_event("RESULTADO partida=%zu mesa=%zu ganador=%d motivo=%s",
              game->match_id, game->table_id, winner, reason);
    pthread_mutex_lock(&tournament->mutex);
    slot_t *slot = &tournament->slots[game->table_id - 1];
    slot->state = SLOT_FINISHED;
    tournament->active--;
    tournament->completed++;
    for (size_t side = 0; side < 2; side++)
        tournament->clients[slot->players[side] - 1].done = true;
    if (winner < 0) tournament->stop = tournament->fatal = true;
    pthread_cond_broadcast(&tournament->changed);
    pthread_mutex_unlock(&tournament->mutex);
}

static void *matchmaker_run(void *argument)
{
    tournament_t *tournament = argument;
    pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    bool notified_stop = false;
    pthread_mutex_lock(&tournament->mutex);
    for (;;) {
        /* A slot is reusable only after joining its previous judge. */
        size_t finished = tournament->slots_count;
        size_t available = tournament->slots_count;
        for (size_t index = 0; index < tournament->slots_count; index++) {
            if (tournament->slots[index].state == SLOT_FINISHED) finished = index;
            if (tournament->slots[index].state == SLOT_FREE) available = index;
        }
        if (finished != tournament->slots_count) {
            pthread_t judge = tournament->slots[finished].judge;
            pthread_mutex_unlock(&tournament->mutex);
            int error = pthread_join(judge, NULL);
            pthread_mutex_lock(&tournament->mutex);
            if (error != 0) tournament->stop = tournament->fatal = true;
            tournament->slots[finished].state = SLOT_FREE;
            continue;
        }
        if (tournament->stop && !notified_stop) {
            notified_stop = true;
            for (size_t index = 0; index < tournament->created; index++)
                if (!tournament->clients[index].assigned) tournament->clients[index].done = true;
            size_t unused;
            while (queue_pop(&tournament->waiting, &unused)) { }
            pthread_cond_broadcast(&tournament->changed);
            pthread_mutex_unlock(&tournament->mutex);
            /* Only this matcher prepares slots. Stop never races a prepare. */
            for (size_t index = 0; index < tournament->slots_count; index++)
                game_stop(&tournament->slots[index].game);
            pthread_mutex_lock(&tournament->mutex);
            continue;
        }
        if (!tournament->stop && queue_size(&tournament->waiting) >= 2 &&
            available != tournament->slots_count) {
            slot_t *slot = &tournament->slots[available];
            size_t first, second;
            (void)queue_pop(&tournament->waiting, &first);
            (void)queue_pop(&tournament->waiting, &second);
            slot->players[0] = first;
            slot->players[1] = second;
            slot->match = tournament->matches + 1;
            pthread_mutex_unlock(&tournament->mutex);
            /* Prepare without holding the global lock; stale abandonment can
               briefly take the board mutex, but checks client IDs. */
            bool prepared = game_prepare(&slot->game, &tournament->config,
                                         available + 1, slot->match, first, second,
                                         on_game_finished, tournament);
            pthread_mutex_lock(&tournament->mutex);
            if (!prepared) {
                tournament->clients[first - 1].done = true;
                tournament->clients[second - 1].done = true;
                tournament->stop = tournament->fatal = true;
                pthread_cond_broadcast(&tournament->changed);
                continue;
            }
            if (tournament->stop) {
                tournament->clients[first - 1].done = true;
                tournament->clients[second - 1].done = true;
                pthread_cond_broadcast(&tournament->changed);
                continue;
            }
            tournament->matches++;
            slot->state = SLOT_RUNNING;
            tournament->active++;
            if (tournament->active > tournament->peak) tournament->peak = tournament->active;
            size_t active = tournament->active;
            client_t *puma = &tournament->clients[first - 1];
            client_t *dogs = &tournament->clients[second - 1];
            puma->assigned = dogs->assigned = true;
            puma->game = dogs->game = &slot->game;
            bool puma_failed = puma->failed, dogs_failed = dogs->failed;
            pthread_cond_broadcast(&tournament->changed);
            pthread_mutex_unlock(&tournament->mutex);
            log_event("PAREJA partida=%zu mesa=%zu puma=%zu perros=%zu ordenes=%zu,%zu activas=%zu",
                      slot->match, available + 1, first, second,
                      puma->arrival_order, dogs->arrival_order, active);
            if (puma_failed) game_participant_failed(&slot->game, first);
            if (dogs_failed) game_participant_failed(&slot->game, second);
            int error = pthread_create(&slot->judge, NULL, game_run, &slot->game);
            if (error != 0) {
                log_event("ERROR juez pthread_create=%s", strerror(error));
                /* No judge was started: mark the slot free directly. */
                pthread_mutex_lock(&tournament->mutex);
                tournament->active--;
                slot->state = SLOT_FREE;
                puma->done = dogs->done = true;
                tournament->stop = tournament->fatal = true;
                pthread_cond_broadcast(&tournament->changed);
                pthread_mutex_unlock(&tournament->mutex);
            }
            pthread_mutex_lock(&tournament->mutex);
            continue;
        }
        bool all_arrived = tournament->creation_done &&
                           tournament->arrived + tournament->failed_before_arrival == tournament->created;
        if (!tournament->stop && all_arrived && queue_size(&tournament->waiting) == 1) {
            size_t odd;
            (void)queue_pop(&tournament->waiting, &odd);
            tournament->clients[odd - 1].done = true;
            pthread_cond_broadcast(&tournament->changed);
            pthread_mutex_unlock(&tournament->mutex);
            log_event("SIN_RIVAL jugador=%zu", odd);
            pthread_mutex_lock(&tournament->mutex);
            continue;
        }
        if (tournament->creation_done && tournament->active == 0 &&
            (tournament->stop || (all_arrived && queue_size(&tournament->waiting) == 0)))
            break;
        if (pthread_cond_wait(&tournament->changed, &tournament->mutex) != 0)
            tournament->stop = tournament->fatal = true;
    }
    pthread_mutex_unlock(&tournament->mutex);
    return NULL;
}

int tournament_run(size_t players, size_t max_tables, const config_t *config,
                   signals_t *signals)
{
    tournament_t tournament = {0};
    tournament.n = players;
    tournament.limit = max_tables;
    tournament.config = *config;
    tournament.slots_count = players / 2 < max_tables ? players / 2 : max_tables;
    if (players == 0 || max_tables == 0 ||
        players > SIZE_MAX / sizeof(*tournament.clients) ||
        tournament.slots_count > SIZE_MAX / sizeof(*tournament.slots) ||
        players > SIZE_MAX / sizeof(size_t) ||
        (config->arrival_delay_ms != 0 && players - 1 > SIZE_MAX / config->arrival_delay_ms)) {
        log_event("ERROR capacidad: tamaños o plazos no representables");
        return 2;
    }
    bool queue_ready = false, mutex_ready = false, condition_ready = false;
    bool signals_bound = false;
    size_t initialized_games = 0;
    int result = 1;
    pthread_t matcher;
    if (signals_interrupted(signals)) { result = 130; goto cleanup; }
    tournament.clients = calloc(players, sizeof(*tournament.clients));
    if (signals_interrupted(signals)) { result = 130; goto cleanup; }
    if (!tournament.clients) goto cleanup;
    tournament.slots = calloc(tournament.slots_count ? tournament.slots_count : 1,
                              sizeof(*tournament.slots));
    if (signals_interrupted(signals)) { result = 130; goto cleanup; }
    if (!tournament.slots) goto cleanup;
    queue_ready = queue_init(&tournament.waiting, players);
    if (signals_interrupted(signals)) { result = 130; goto cleanup; }
    if (!queue_ready) goto cleanup;
    if (pthread_mutex_init(&tournament.mutex, NULL) != 0) goto cleanup;
    mutex_ready = true;
    pthread_condattr_t attributes;
    if (pthread_condattr_init(&attributes) != 0) goto cleanup;
    int error = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (error == 0) error = pthread_cond_init(&tournament.changed, &attributes);
    pthread_condattr_destroy(&attributes);
    if (error != 0) goto cleanup;
    condition_ready = true;
    /* The signal waiter has been alive since main, before config/allocation.
       Binding applies any interruption already observed during startup. */
    signals_bind(signals, on_interrupt, &tournament);
    signals_bound = true;
    for (; initialized_games < tournament.slots_count; initialized_games++) {
        if (signals_interrupted(signals)) { result = 130; goto cleanup; }
        if (!game_init(&tournament.slots[initialized_games].game)) goto cleanup;
    }
    if (signals_interrupted(signals)) { result = 130; goto cleanup; }
    error = pthread_create(&matcher, NULL, matchmaker_run, &tournament);
    if (error != 0) {
        log_event("ERROR emparejador pthread_create=%s", strerror(error));
        goto cleanup;
    }
    for (size_t index = 0; index < players; index++) {
        client_t *client = &tournament.clients[index];
        client->id = index + 1;
        client->tournament = &tournament;
        pthread_mutex_lock(&tournament.mutex);
        bool stop = tournament.stop;
        if (!stop) tournament.created++;
        pthread_mutex_unlock(&tournament.mutex);
        if (stop) break;
        error = pthread_create(&client->thread, NULL, client_run, client);
        if (error != 0) {
            pthread_mutex_lock(&tournament.mutex);
            tournament.created--;
            pthread_mutex_unlock(&tournament.mutex);
            log_event("ERROR cliente=%zu pthread_create=%s", client->id, strerror(error));
            request_stop(&tournament, false, true);
            break;
        }
    }
    pthread_mutex_lock(&tournament.mutex);
    tournament.creation_done = true;
    pthread_cond_broadcast(&tournament.changed);
    pthread_mutex_unlock(&tournament.mutex);
    bool join_failed = pthread_join(matcher, NULL) != 0;
    for (size_t index = 0; index < tournament.created; index++)
        if (pthread_join(tournament.clients[index].thread, NULL) != 0) join_failed = true;
    /* Exclude callbacks before inspecting non-atomic flags or freeing context. */
    signals_bind(signals, NULL, NULL);
    signals_bound = false;
    tournament.fatal |= join_failed;
    log_event("RESUMEN creados=%zu llegados=%zu partidas=%zu completadas=%zu pico_mesas=%zu limite=%zu en_cola=%zu",
              tournament.created, tournament.arrived, tournament.matches,
              tournament.completed, tournament.peak, tournament.limit,
              queue_size(&tournament.waiting));
    result = tournament.fatal ? 1 : tournament.interrupted ? 130 : 0;
cleanup:
    if (signals_bound) signals_bind(signals, NULL, NULL);
    for (size_t index = 0; index < initialized_games; index++)
        game_destroy(&tournament.slots[index].game);
    if (condition_ready) pthread_cond_destroy(&tournament.changed);
    if (mutex_ready) pthread_mutex_destroy(&tournament.mutex);
    if (queue_ready) queue_destroy(&tournament.waiting);
    free(tournament.slots);
    free(tournament.clients);
    if (result == 1) log_event("SERVIDOR error: cierre tras fallo de recursos");
    return result;
}
