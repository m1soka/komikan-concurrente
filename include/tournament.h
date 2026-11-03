#ifndef KOMIKAN_TOURNAMENT_H
#define KOMIKAN_TOURNAMENT_H
#include "config.h"
#include "signals.h"
#include <stddef.h>
int tournament_run(size_t players, size_t max_tables, const config_t *config,
                   signals_t *signals);
#endif
