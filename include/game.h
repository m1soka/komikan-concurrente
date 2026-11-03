#ifndef KOMIKAN_GAME_H
#define KOMIKAN_GAME_H

#include "board.h"
#include "config.h"
#include <pthread.h>
#include <stdbool.h>
#include <stdint.h>

typedef struct game game_t;
typedef void (*game_finish_fn)(void *, game_t *, int, const char *);

struct game {
    /* One mutex protects the board, mailboxes and every mutable match field. */
    pthread_mutex_t mutex;
    pthread_cond_t changed;
    board_t board;
    config_t config;
    size_t table_id, match_id, puma_client, dogs_client;
    bool finished;
    int winner; /* 1 puma, 2 dogs, 0 interruption, -1 internal failure */
    const char *reason;
    uint64_t next_sequence;
    struct {
        bool pending;
        int destination;
        uint64_t sequence;
    } intents[PIECE_COUNT];
    game_finish_fn on_finish;
    void *context;
};

bool game_init(game_t *game);
void game_destroy(game_t *game);
/* Call only after joining this slot's previous judge. */
bool game_prepare(game_t *game, const config_t *config, size_t table_id,
                  size_t match_id, size_t puma_client, size_t dogs_client,
                  game_finish_fn on_finish, void *context);
void *game_run(void *argument);
void game_stop(game_t *game);
/* A stale notification cannot affect a newly assigned pair in the same slot. */
void game_abandon(game_t *game, size_t client_id);

#endif
