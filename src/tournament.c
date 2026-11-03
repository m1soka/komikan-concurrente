#include "tournament.h"
#include "game.h"
#include "log.h"
#include "queue.h"
#include "signals.h"
#include <errno.h>
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
    bool assigned, done;
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
    size_t active, peak, matches, completed;
    bool creation_done, stop, interrupted, fatal;
    queue_t waiting;
    client_t *clients;
    slot_t *slots;
};

static struct timespec deadline_ms(size_t milliseconds)
{
    struct timespec deadline;
    (void)clock_gettime(CLOCK_MONOTONIC, &deadline);
    deadline.tv_sec += (time_t)(milliseconds / 1000);
    deadline.tv_nsec += (long)(milliseconds % 1000) * 1000000L;
    if (deadline.tv_nsec >= 1000000000L) {
        deadline.tv_sec++;
        deadline.tv_nsec -= 1000000000L;
    }
    return deadline;
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

static void *client_run(void *argument)
{
    client_t *client = argument;
    tournament_t *tournament = client->tournament;
    struct timespec arrival = deadline_ms((client->id - 1) *
                                         tournament->config.arrival_delay_ms);
    pthread_mutex_lock(&tournament->mutex);
    int error = 0;
    if (tournament->config.arrival_delay_ms > 0) {
        while (!tournament->stop && error != ETIMEDOUT) {
            error = pthread_cond_timedwait(&tournament->changed, &tournament->mutex, &arrival);
            if (error != 0 && error != ETIMEDOUT) {
                tournament->stop = tournament->fatal = true;
                pthread_cond_broadcast(&tournament->changed);
                break;
            }
        }
    }
    if (tournament->stop) {
        pthread_mutex_unlock(&tournament->mutex);
        return NULL;
    }
    if (!queue_push(&tournament->waiting, client->id)) {
        tournament->stop = tournament->fatal = true;
        pthread_cond_broadcast(&tournament->changed);
        pthread_mutex_unlock(&tournament->mutex);
        return NULL;
    }
    client->arrival_order = ++tournament->arrived;
    pthread_cond_broadcast(&tournament->changed);
    pthread_mutex_unlock(&tournament->mutex);
    log_event("LLEGADA jugador=%zu orden=%zu", client->id, client->arrival_order);
    pthread_mutex_lock(&tournament->mutex);
    while (!client->assigned && !client->done && !tournament->stop)
        pthread_cond_wait(&tournament->changed, &tournament->mutex);
    if (client->assigned && !client->done && !tournament->stop &&
        tournament->config.fail_player == client->id) {
        struct timespec failure = deadline_ms(tournament->config.fail_after_ms);
        error = 0;
        while (!client->done && !tournament->stop && error != ETIMEDOUT) {
            error = pthread_cond_timedwait(&tournament->changed, &tournament->mutex, &failure);
            if (error != 0 && error != ETIMEDOUT) {
                tournament->stop = tournament->fatal = true;
                pthread_cond_broadcast(&tournament->changed);
                break;
            }
        }
        if (!client->done && !tournament->stop && error == ETIMEDOUT) {
            game_t *game = client->game;
            pthread_mutex_unlock(&tournament->mutex);
            log_event("ABANDONO jugador=%zu", client->id);
            game_abandon(game, client->id);
            pthread_mutex_lock(&tournament->mutex);
        }
    }
    while (!client->done && !tournament->stop)
        pthread_cond_wait(&tournament->changed, &tournament->mutex);
    pthread_mutex_unlock(&tournament->mutex);
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
            pthread_cond_broadcast(&tournament->changed);
            pthread_mutex_unlock(&tournament->mutex);
            log_event("PAREJA partida=%zu mesa=%zu puma=%zu perros=%zu ordenes=%zu,%zu activas=%zu",
                      slot->match, available + 1, first, second,
                      puma->arrival_order, dogs->arrival_order, active);
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
                           tournament->arrived == tournament->created;
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
        pthread_cond_wait(&tournament->changed, &tournament->mutex);
    }
    pthread_mutex_unlock(&tournament->mutex);
    return NULL;
}

int tournament_run(size_t players, size_t max_tables, const config_t *config)
{
    tournament_t tournament = {0};
    tournament.n = players;
    tournament.limit = max_tables;
    tournament.config = *config;
    tournament.slots_count = players / 2 < max_tables ? players / 2 : max_tables;
    tournament.clients = calloc(players, sizeof(*tournament.clients));
    tournament.slots = calloc(tournament.slots_count ? tournament.slots_count : 1,
                              sizeof(*tournament.slots));
    bool queue_ready = queue_init(&tournament.waiting, players);
    bool mutex_ready = false, condition_ready = false;
    size_t initialized_games = 0;
    int result = 1;
    signals_t signals = {0};
    pthread_t matcher;
    if (!tournament.clients || !tournament.slots || !queue_ready) goto cleanup;
    if (pthread_mutex_init(&tournament.mutex, NULL) != 0) goto cleanup;
    mutex_ready = true;
    pthread_condattr_t attributes;
    if (pthread_condattr_init(&attributes) != 0) goto cleanup;
    int error = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (error == 0) error = pthread_cond_init(&tournament.changed, &attributes);
    pthread_condattr_destroy(&attributes);
    if (error != 0) goto cleanup;
    condition_ready = true;
    for (; initialized_games < tournament.slots_count; initialized_games++)
        if (!game_init(&tournament.slots[initialized_games].game)) goto cleanup;
    error = signals_start(&signals, on_interrupt, &tournament);
    if (error != 0) {
        log_event("ERROR señales pthread_create/mask=%s", strerror(error));
        goto cleanup;
    }
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
    (void)pthread_join(matcher, NULL);
    for (size_t index = 0; index < tournament.created; index++)
        (void)pthread_join(tournament.clients[index].thread, NULL);
    /* Join the signal waiter before reading flags or destroying its context. */
    signals_finish(&signals);
    log_event("RESUMEN creados=%zu llegados=%zu partidas=%zu completadas=%zu pico_mesas=%zu limite=%zu en_cola=%zu",
              tournament.created, tournament.arrived, tournament.matches,
              tournament.completed, tournament.peak, tournament.limit,
              queue_size(&tournament.waiting));
    result = tournament.fatal ? 1 : tournament.interrupted ? 130 : 0;
cleanup:
    signals_finish(&signals);
    for (size_t index = 0; index < initialized_games; index++)
        game_destroy(&tournament.slots[index].game);
    if (condition_ready) pthread_cond_destroy(&tournament.changed);
    if (mutex_ready) pthread_mutex_destroy(&tournament.mutex);
    if (queue_ready) queue_destroy(&tournament.waiting);
    free(tournament.slots);
    free(tournament.clients);
    if (result == 1) log_event("SERVIDOR error: recursos liberados tras fallo");
    return result;
}
