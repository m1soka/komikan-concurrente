#ifndef KOMIKAN_CONFIG_H
#define KOMIKAN_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"

typedef enum {
    FAIL_ABANDON = 0,
    FAIL_CLIENT_EXIT_LOCKED,
    FAIL_CLIENT_EXIT_UNLOCKED,
    FAIL_PIECE_EXIT_LOCKED,
    FAIL_PIECE_EXIT_UNLOCKED,
    FAIL_PIECE_CANCEL_WAIT
} failure_mode_t;

typedef struct {
    unsigned min_delay_ms;
    unsigned max_delay_ms;
    uint32_t seed;
    int positions[PIECE_COUNT];
    unsigned arrival_delay_ms;
    size_t fail_player;
    unsigned fail_after_ms;
    failure_mode_t fail_mode;
} config_t;

void config_defaults(config_t *config);
/* Leaves config unchanged on failure. error may be NULL when error_size is 0. */
bool config_load(config_t *config, const char *path, char *error, size_t error_size);
/* Predicate is polled while reading; cancellation leaves config unchanged. */
bool config_load_interruptible(config_t *config, const char *path, char *error,
                               size_t error_size, bool (*interrupted)(void *),
                               void *context);
/* Decimal digits only, in the inclusive interval [1, max]. */
bool parse_positive(const char *text, size_t max, size_t *value);

#endif
