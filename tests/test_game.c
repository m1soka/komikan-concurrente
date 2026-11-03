#define _POSIX_C_SOURCE 200809L
#include "game.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

typedef struct {
    unsigned calls;
    int winner;
    const char *reason;
} result_t;

static void finished(void *context, game_t *game, int winner, const char *reason)
{
    result_t *result = context;
    (void)game;
    result->calls++;
    result->winner = winner;
    result->reason = reason;
}

static void launch_and_join(game_t *game)
{
    pthread_t judge;
    assert(pthread_create(&judge, NULL, game_run, game) == 0);
    assert(pthread_join(judge, NULL) == 0);
}

/* Observe a predicate rather than guessing how long startup needs. The
   deadline bounds a broken test; it does not prescribe a random game result. */
static void wait_for_activity(game_t *game)
{
    struct timespec deadline;
    assert(clock_gettime(CLOCK_MONOTONIC, &deadline) == 0);
    deadline.tv_sec += 5;
    assert(pthread_mutex_lock(&game->mutex) == 0);
    while (game->next_sequence == 0 && !game->finished) {
        int error = pthread_cond_timedwait(&game->changed, &game->mutex, &deadline);
        assert(error == 0 || error == ETIMEDOUT);
        if (error == ETIMEDOUT) break;
    }
    assert(game->next_sequence > 0);
    assert(pthread_mutex_unlock(&game->mutex) == 0);
}

int main(void)
{
    config_t config;
    config_defaults(&config);
    game_t game;
    result_t result = {0};
    assert(game_init(&game));

    /* Abandonments are deterministic even before the judge starts. */
    assert(game_prepare(&game, &config, 1, 1, 1, 2, finished, &result));
    game_abandon(&game, 1);
    launch_and_join(&game);
    assert(result.calls == 1 && result.winner == 2);
    assert(strcmp(result.reason, "abandono") == 0);

    /* The same slot can be reused; late notifications from the old pair have
       no effect on a new pair, and dogs abandoning awards the puma. */
    assert(game_prepare(&game, &config, 1, 2, 3, 4, finished, &result));
    game_abandon(&game, 1);
    game_abandon(&game, 2);
    assert(!game.finished);
    game_abandon(&game, 4);
    launch_and_join(&game);
    assert(result.calls == 2 && result.winner == 1);
    assert(strcmp(result.reason, "abandono") == 0);

    /* Stop is idempotent and preserves the first terminal result. */
    assert(game_prepare(&game, &config, 1, 3, 5, 6, finished, &result));
    game_stop(&game);
    game_stop(&game);
    game_abandon(&game, 5);
    launch_and_join(&game);
    assert(result.calls == 3 && result.winner == 0);
    assert(strcmp(result.reason, "SIGINT") == 0);

    /* Exercise actual piece activity, then terminate and join it. No move
       sequence or natural winner is assumed: stop preserves an earlier win. */
    config.positions[0] = 27;
    assert(game_prepare(&game, &config, 1, 4, 7, 8, finished, &result));
    pthread_t judge;
    assert(pthread_create(&judge, NULL, game_run, &game) == 0);
    wait_for_activity(&game);
    game_stop(&game);
    assert(pthread_join(judge, NULL) == 0);
    assert(result.calls == 4);
    assert(result.winner >= 0 && result.winner <= 2);
    assert(strcmp(result.reason, "SIGINT") == 0 ||
           strcmp(result.reason, "capturas") == 0 ||
           strcmp(result.reason, "inmovilizacion") == 0);
    assert(board_valid(&game.board));
    game_destroy(&game);
    puts("test_game: abandono, reutilizacion, cierre y actividad: OK");
    return 0;
}
