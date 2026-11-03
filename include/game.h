#ifndef KOMIKAN_GAME_H
#define KOMIKAN_GAME_H

#include "board.h"
#include "config.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct game game_t;
typedef void (*game_finish_fn)(void *, game_t *, int, const char *);
typedef enum {
    GAME_FAIL_EXIT_LOCKED = 1,
    GAME_FAIL_EXIT_UNLOCKED,
    GAME_FAIL_CANCEL_WAIT
} game_failure_mode_t;

struct game {
    /* One mutex protects the board, mailboxes and every mutable match field. */
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    board_t board;
    config_t config;
    size_t table_id, match_id, puma_client, dogs_client;
    bool finished, board_ready;
    int winner; /* 1 puma, 2 dogs, 0 interruption, -1 internal failure */
    const char *reason;
    uint64_t next_sequence;
    struct {
        bool pending;
        int destination;
        uint64_t sequence;
    } intents[PIECE_COUNT];
    bool piece_failed[PIECE_COUNT];
    game_failure_mode_t piece_failure_request[PIECE_COUNT];
    int participant_failure_winner;
    game_finish_fn on_finish;
    void *context;
};

bool game_init(game_t *game);
void game_destroy(game_t *game);
/* Call only after joining this slot's previous judge. Prepare metadata only;
   the new judge initializes the shared board before creating any piece. */
bool game_prepare(game_t *game, const config_t *config, size_t table_id,
                  size_t match_id, size_t puma_client, size_t dogs_client,
                  game_finish_fn on_finish, void *context);
void *game_run(void *argument);
void game_stop(game_t *game);
/* A stale notification cannot affect a newly assigned pair in the same slot. */
void game_abandon(game_t *game, size_t client_id);
/* Cleanup handlers report unexpected exits; the judge adjudicates the result.
   These calls are safe before the judge initializes its board. */
void game_participant_failed(game_t *game, size_t client_id);
/* Controlled failure injection: pthread_exit inside/outside the piece lock or
   deferred cancellation at cond_wait. No asynchronous cancellation is used. */
bool game_inject_piece_failure(game_t *game, size_t client_id,
                               game_failure_mode_t mode);

#endif
