#ifndef KOMIKAN_BOARD_H
#define KOMIKAN_BOARD_H

#include <stdbool.h>
#include <stddef.h>

#define BOARD_NODES 28
#define DOG_COUNT 12
#define PIECE_COUNT 13

typedef enum { GAME_RUNNING = 0, GAME_PUMA, GAME_DOGS } board_result_t;

typedef struct {
    int cells[BOARD_NODES]; /* -1 empty; 0 puma; 1..12 dogs */
    int positions[PIECE_COUNT]; /* -1 for a captured dog */
    unsigned captured;
} board_t;

/* This module has no internal locks. The game owns one mutex around all access. */
bool board_init(board_t *board, const int positions[PIECE_COUNT]);
bool board_adjacent(int from, int to);
size_t board_moves(const board_t *board, int piece,
                   int destinations[BOARD_NODES]);
bool board_apply(board_t *board, int piece, int destination);
board_result_t board_result(const board_t *board);
bool board_valid(const board_t *board);
void board_format(const board_t *board, char *buffer, size_t capacity);

#endif
