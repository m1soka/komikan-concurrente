#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include "log.h"
#include <errno.h>
#include <inttypes.h>
#include <string.h>
#include <time.h>

typedef struct {
    game_t *game;
    int piece;
    uint32_t random_state;
} piece_argument_t;

typedef struct {
    game_t *game;
    int piece;
    bool mutex_owned, expected_exit;
} piece_cleanup_t;

static struct timespec after_ms(unsigned milliseconds)
{
    struct timespec time;
    (void)clock_gettime(CLOCK_MONOTONIC, &time);
    time.tv_sec += (time_t)(milliseconds / 1000U);
    time.tv_nsec += (long)(milliseconds % 1000U) * 1000000L;
    if (time.tv_nsec >= 1000000000L) {
        time.tv_sec++;
        time.tv_nsec -= 1000000000L;
    }
    return time;
}

static uint32_t random_next(uint32_t *state)
{
    uint32_t value = *state;
    value ^= value << 13;
    value ^= value >> 17;
    value ^= value << 5;
    *state = value;
    return value;
}

/* Caller holds game->mutex. Reasons are string literals with static lifetime. */
static void finish_locked(game_t *game, int winner, const char *reason)
{
    if (!game->finished) {
        game->finished = true;
        game->winner = winner;
        game->reason = reason;
    }
    (void)pthread_cond_broadcast(&game->changed);
}

bool game_init(game_t *game)
{
    memset(game, 0, sizeof(*game));
    int error = pthread_mutex_init(&game->mutex, NULL);
    if (error != 0) return false;
    pthread_condattr_t attributes;
    error = pthread_condattr_init(&attributes);
    if (error != 0) {
        (void)pthread_mutex_destroy(&game->mutex);
        return false;
    }
    error = pthread_condattr_setclock(&attributes, CLOCK_MONOTONIC);
    if (error == 0) error = pthread_cond_init(&game->changed, &attributes);
    (void)pthread_condattr_destroy(&attributes);
    if (error != 0) {
        (void)pthread_mutex_destroy(&game->mutex);
        return false;
    }
    game->finished = true;
    return true;
}

void game_destroy(game_t *game)
{
    (void)pthread_cond_destroy(&game->changed);
    (void)pthread_mutex_destroy(&game->mutex);
}

bool game_prepare(game_t *game, const config_t *config, size_t table_id,
                  size_t match_id, size_t puma_client, size_t dogs_client,
                  game_finish_fn on_finish, void *context)
{
    /* This is validation, not initialization of the shared board. Only the
       judge writes that board, after the assignment's metadata is ready. */
    bool seen[BOARD_NODES] = {false};
    bool valid = true;
    for (int piece = 0; piece < PIECE_COUNT; piece++) {
        int node = config->positions[piece];
        if (node < 0 || node >= BOARD_NODES || seen[node]) {
            valid = false;
            break;
        }
        seen[node] = true;
    }
    (void)pthread_mutex_lock(&game->mutex);
    if (valid) {
        game->config = *config;
        game->table_id = table_id;
        game->match_id = match_id;
        game->puma_client = puma_client;
        game->dogs_client = dogs_client;
        game->finished = false;
        game->board_ready = false;
        game->winner = 0;
        game->reason = "en_curso";
        game->next_sequence = 0;
        memset(game->intents, 0, sizeof(game->intents));
        memset(game->piece_failed, 0, sizeof(game->piece_failed));
        memset(game->piece_failure_request, 0, sizeof(game->piece_failure_request));
        game->participant_failure_winner = 0;
        game->on_finish = on_finish;
        game->context = context;
    }
    (void)pthread_mutex_unlock(&game->mutex);
    return valid;
}

void game_stop(game_t *game)
{
    (void)pthread_mutex_lock(&game->mutex);
    finish_locked(game, 0, "SIGINT");
    (void)pthread_mutex_unlock(&game->mutex);
}

void game_abandon(game_t *game, size_t client_id)
{
    (void)pthread_mutex_lock(&game->mutex);
    if (client_id == game->puma_client) finish_locked(game, 2, "abandono");
    else if (client_id == game->dogs_client) finish_locked(game, 1, "abandono");
    (void)pthread_mutex_unlock(&game->mutex);
}

void game_participant_failed(game_t *game, size_t client_id)
{
    (void)pthread_mutex_lock(&game->mutex);
    if (!game->finished && game->participant_failure_winner == 0) {
        if (client_id == game->puma_client) game->participant_failure_winner = 2;
        else if (client_id == game->dogs_client) game->participant_failure_winner = 1;
    }
    (void)pthread_cond_broadcast(&game->changed);
    (void)pthread_mutex_unlock(&game->mutex);
}

bool game_inject_piece_failure(game_t *game, size_t client_id,
                               game_failure_mode_t mode)
{
    bool injected = false;
    if (mode < GAME_FAIL_EXIT_LOCKED || mode > GAME_FAIL_CANCEL_WAIT) return false;
    (void)pthread_mutex_lock(&game->mutex);
    int first = -1, last = -1;
    if (client_id == game->puma_client) first = last = 0;
    else if (client_id == game->dogs_client) {
        first = 1;
        last = DOG_COUNT;
    }
    for (int piece = first; !game->finished && piece >= 0 && piece <= last; piece++) {
        if (!game->board_ready || game->board.positions[piece] >= 0) {
            game->piece_failure_request[piece] = mode;
            injected = true;
            break;
        }
    }
    (void)pthread_cond_broadcast(&game->changed);
    (void)pthread_mutex_unlock(&game->mutex);
    return injected;
}

/* Cancellation is disabled across ownership transitions. At every enabled
   cancellation point, mutex_owned precisely describes what this thread owns.
   In particular, POSIX cond_wait reacquires the mutex before running cleanup. */
static void piece_lock(piece_cleanup_t *cleanup)
{
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    (void)pthread_mutex_lock(&cleanup->game->mutex);
    cleanup->mutex_owned = true;
    (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
}

static void piece_unlock(piece_cleanup_t *cleanup)
{
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    (void)pthread_mutex_unlock(&cleanup->game->mutex);
    cleanup->mutex_owned = false;
    (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
}

static void piece_cleanup(void *argument)
{
    piece_cleanup_t *cleanup = argument;
    game_t *game = cleanup->game;
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    if (!cleanup->mutex_owned) (void)pthread_mutex_lock(&game->mutex);
    bool captured = game->board_ready && game->board.positions[cleanup->piece] < 0;
    if (!cleanup->expected_exit && !game->finished && !captured)
        game->piece_failed[cleanup->piece] = true;
    /* A terminated piece cannot leave a request owned by a nonexistent worker. */
    game->intents[cleanup->piece].pending = false;
    (void)pthread_cond_broadcast(&game->changed);
    (void)pthread_mutex_unlock(&game->mutex);
    cleanup->mutex_owned = false;
}

/* Called with the board mutex owned; only this piece exits/cancels itself.
   Pieces only submit requests, so these failure points cannot interrupt a
   partially written board. The judge remains the board's sole writer. */
static void inject_requested_failure(piece_cleanup_t *cleanup)
{
    game_t *game = cleanup->game;
    game_failure_mode_t mode = game->piece_failure_request[cleanup->piece];
    game->piece_failure_request[cleanup->piece] = 0;
    if (mode == GAME_FAIL_EXIT_LOCKED) pthread_exit(NULL);
    if (mode == GAME_FAIL_EXIT_UNLOCKED) {
        piece_unlock(cleanup);
        pthread_exit(NULL);
    }
    if (mode == GAME_FAIL_CANCEL_WAIT) {
        (void)pthread_cancel(pthread_self());
        /* With deferred cancellation this is the cancellation point. Its
           cleanup handler receives the mutex reacquired by cond_wait. */
        (void)pthread_cond_wait(&game->changed, &game->mutex);
        pthread_testcancel();
    }
}

static void piece_body(piece_argument_t *piece, piece_cleanup_t *cleanup)
{
    game_t *game = piece->game;
    piece_lock(cleanup);
    while (!game->finished && game->board.positions[piece->piece] >= 0) {
        inject_requested_failure(cleanup);
        unsigned range = game->config.max_delay_ms - game->config.min_delay_ms + 1U;
        unsigned delay = game->config.min_delay_ms + random_next(&piece->random_state) % range;
        struct timespec deadline = after_ms(delay);
        int error = 0;
        /* Spurious/movement wakeups keep the original deadline. cond_wait
           releases the mutex; no piece sleeps while retaining the board. */
        while (!game->finished && game->board.positions[piece->piece] >= 0 &&
               game->piece_failure_request[piece->piece] == 0 &&
               error != ETIMEDOUT) {
            error = pthread_cond_timedwait(&game->changed, &game->mutex, &deadline);
            if (error != 0 && error != ETIMEDOUT) {
                finish_locked(game, -1, "error_condicion");
                break;
            }
        }
        if (game->finished || game->board.positions[piece->piece] < 0) break;
        inject_requested_failure(cleanup);
        int destinations[BOARD_NODES];
        size_t count = board_moves(&game->board, piece->piece, destinations);
        if (count == 0) continue;
        game->intents[piece->piece].destination =
            destinations[random_next(&piece->random_state) % count];
        game->intents[piece->piece].sequence = ++game->next_sequence;
        game->intents[piece->piece].pending = true;
        (void)pthread_cond_broadcast(&game->changed);
        while (!game->finished && game->intents[piece->piece].pending &&
               game->piece_failure_request[piece->piece] == 0) {
            error = pthread_cond_wait(&game->changed, &game->mutex);
            if (error != 0) {
                finish_locked(game, -1, "error_condicion");
                break;
            }
        }
        if (!game->finished && game->board.positions[piece->piece] >= 0)
            inject_requested_failure(cleanup);
    }
    cleanup->expected_exit = game->finished || game->board.positions[piece->piece] < 0;
    piece_unlock(cleanup);
}

static void *piece_run(void *argument)
{
    piece_argument_t *piece = argument;
    piece_cleanup_t cleanup = {piece->game, piece->piece, false, false};
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    (void)pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED, NULL);
    /* The wrapper executes cleanup after ordinary return too, distinguishing
       a valid captured/finished exit from an unexpected early body return. */
    pthread_cleanup_push(piece_cleanup, &cleanup);
    (void)pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
    piece_body(piece, &cleanup);
    pthread_cleanup_pop(1);
    return NULL;
}

static int oldest_intent(const game_t *game)
{
    int selected = -1;
    for (int piece = 0; piece < PIECE_COUNT; piece++) {
        if (game->intents[piece].pending &&
            (selected < 0 || game->intents[piece].sequence <
                             game->intents[selected].sequence)) selected = piece;
    }
    return selected;
}

static void check_victory_locked(game_t *game)
{
    board_result_t result = board_result(&game->board);
    if (result == GAME_PUMA) finish_locked(game, 1, "capturas");
    if (result == GAME_DOGS) finish_locked(game, 2, "inmovilizacion");
}

static void check_failures_locked(game_t *game)
{
    if (game->participant_failure_winner != 0)
        finish_locked(game, game->participant_failure_winner, "fallo_cliente");
    for (int piece = 0; piece < PIECE_COUNT; piece++) {
        if (game->piece_failed[piece]) {
            finish_locked(game, piece == 0 ? 2 : 1, "fallo_pieza");
            break;
        }
    }
}

void *game_run(void *argument)
{
    game_t *game = argument;
    /* Infrastructure threads terminate cooperatively. They are not player
       workers and must not be cancelled halfway through board writes/joins. */
    (void)pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL);
    pthread_t threads[PIECE_COUNT];
    piece_argument_t arguments[PIECE_COUNT];
    size_t created = 0;
    char snapshot[2048];
    (void)pthread_mutex_lock(&game->mutex);
    game->board_ready = board_init(&game->board, game->config.positions);
    if (!game->board_ready) {
        finish_locked(game, -1, "error_tablero");
        (void)strcpy(snapshot, "tablero_no_inicializado\n");
    } else {
        check_failures_locked(game);
        check_victory_locked(game);
        board_format(&game->board, snapshot, sizeof(snapshot));
    }
    (void)pthread_cond_broadcast(&game->changed);
    (void)pthread_mutex_unlock(&game->mutex);
    log_event("MESA inicio mesa=%zu partida=%zu puma=%zu perros=%zu\n%s",
              game->table_id, game->match_id, game->puma_client,
              game->dogs_client, snapshot);

    for (int piece = 0; piece < PIECE_COUNT; piece++) {
        (void)pthread_mutex_lock(&game->mutex);
        bool finished = game->finished;
        (void)pthread_mutex_unlock(&game->mutex);
        if (finished) break;
        uint32_t seed = game->config.seed ^ (uint32_t)game->match_id * UINT32_C(2654435761)
                        ^ (uint32_t)(piece + 1) * UINT32_C(2246822519);
        arguments[piece] = (piece_argument_t){game, piece, seed ? seed : UINT32_C(1)};
        int error = pthread_create(&threads[piece], NULL, piece_run, &arguments[piece]);
        if (error != 0) {
            (void)pthread_mutex_lock(&game->mutex);
            finish_locked(game, -1, "error_pthread_create");
            (void)pthread_mutex_unlock(&game->mutex);
            log_event("ERROR mesa=%zu pieza=%d pthread_create=%s",
                      game->table_id, piece, strerror(error));
            break;
        }
        created++;
    }

    (void)pthread_mutex_lock(&game->mutex);
    while (!game->finished) {
        check_failures_locked(game);
        if (game->finished) break;
        int piece = oldest_intent(game);
        if (piece < 0) {
            int error = pthread_cond_wait(&game->changed, &game->mutex);
            if (error != 0) finish_locked(game, -1, "error_condicion");
            continue;
        }
        int destination = game->intents[piece].destination;
        int origin = game->board.positions[piece];
        uint64_t sequence = game->intents[piece].sequence;
        unsigned before = game->board.captured;
        /* Revalidate against the current board: two independent requests can
           claim the same empty destination, but only the first can succeed. */
        bool accepted = board_apply(&game->board, piece, destination);
        game->intents[piece].pending = false;
        if (accepted) {
            if (!board_valid(&game->board)) finish_locked(game, -1, "error_invariante");
            else check_victory_locked(game);
            board_format(&game->board, snapshot, sizeof(snapshot));
        }
        unsigned captures = game->board.captured - before;
        (void)pthread_cond_broadcast(&game->changed);
        (void)pthread_mutex_unlock(&game->mutex);
        log_event("MESA movimiento mesa=%zu partida=%zu sec=%" PRIu64
                  " pieza=%d origen=%d destino=%d aceptado=%d captura=%u%s%s",
                  game->table_id, game->match_id, sequence, piece, origin,
                  destination, accepted ? 1 : 0, captures,
                  accepted ? "\n" : "", accepted ? snapshot : "");
        (void)pthread_mutex_lock(&game->mutex);
    }
    (void)pthread_cond_broadcast(&game->changed);
    (void)pthread_mutex_unlock(&game->mutex);
    for (size_t index = 0; index < created; index++) {
        int error = pthread_join(threads[index], NULL);
        if (error != 0) {
            /* A valid joinable thread owned by this judge must join. Report
               an OS/API anomaly instead of reporting successful validation. */
            log_event("ERROR mesa=%zu pthread_join=%s", game->table_id, strerror(error));
            (void)pthread_mutex_lock(&game->mutex);
            game->winner = -1;
            game->reason = "error_pthread_join";
            (void)pthread_mutex_unlock(&game->mutex);
        }
    }
    (void)pthread_mutex_lock(&game->mutex);
    int winner = game->winner;
    const char *reason = game->reason;
    unsigned captured = game->board.captured;
    (void)pthread_mutex_unlock(&game->mutex);
    log_event("MESA fin mesa=%zu partida=%zu ganador=%d motivo=%s capturados=%u piezas_unidas=%zu",
              game->table_id, game->match_id, winner, reason, captured, created);
    game->on_finish(game->context, game, winner, reason);
    return NULL;
}
