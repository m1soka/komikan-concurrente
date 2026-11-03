#ifndef KOMIKAN_CONFIG_H
#define KOMIKAN_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "board.h"

typedef struct {
    unsigned min_delay_ms;
    unsigned max_delay_ms;
    uint32_t seed;
    int positions[PIECE_COUNT];
    unsigned arrival_delay_ms;
    unsigned fail_player;
    unsigned fail_after_ms;
} config_t;

void config_defaults(config_t *config);
/* Leaves config unchanged on failure. error may be NULL when error_size is 0. */
bool config_load(config_t *config, const char *path, char *error, size_t error_size);
/* Decimal digits only, in the inclusive interval [1, max]. */
bool parse_positive(const char *text, size_t max, size_t *value);

#endif
