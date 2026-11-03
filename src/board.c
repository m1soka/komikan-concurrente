#include "board.h"

#include <stdarg.h>
#include <stdio.h>
#include <string.h>

static bool node_valid(int node)
{
    return node >= 0 && node < BOARD_NODES;
}

static void coordinates(int node, int *row, int *column)
{
    if (node < 25) {
        *row = node / 5;
        *column = node % 5;
    } else if (node == 25 || node == 26) {
        *row = -1;
        *column = node == 25 ? 1 : 3;
    } else {
        *row = -2;
        *column = 2;
    }
}

bool board_adjacent(int from, int to)
{
    static const int throne_edges[][2] = {
        {27, 25}, {27, 26}, {27, 2}, {25, 26},
        {25, 0}, {25, 1}, {26, 3}, {26, 4}
    };
    if (!node_valid(from) || !node_valid(to) || from == to)
        return false;
    if (from < 25 && to < 25) {
        int delta_row = from / 5 - to / 5;
        int delta_column = from % 5 - to % 5;
        return ((delta_row == 1 || delta_row == -1) && delta_column == 0) ||
               ((delta_column == 1 || delta_column == -1) && delta_row == 0);
    }
    for (size_t i = 0; i < sizeof throne_edges / sizeof throne_edges[0]; ++i) {
        if ((from == throne_edges[i][0] && to == throne_edges[i][1]) ||
            (to == throne_edges[i][0] && from == throne_edges[i][1]))
            return true;
    }
    return false;
}

static bool straight_jump(int from, int over, int to)
{
    int from_r, from_c, over_r, over_c, to_r, to_c;
    coordinates(from, &from_r, &from_c);
    coordinates(over, &over_r, &over_c);
    coordinates(to, &to_r, &to_c);
    int first_r = over_r - from_r;
    int first_c = over_c - from_c;
    int second_r = to_r - over_r;
    int second_c = to_c - over_c;
    /* Two drawn consecutive edges, collinear and in the same direction.
       Edge lengths differ only at the central connection with the throne. */
    return first_r * second_c == first_c * second_r &&
           first_r * second_r + first_c * second_c > 0;
}

static int captured_piece(const board_t *board, int from, int to)
{
    for (int over = 0; over < BOARD_NODES; ++over) {
        if (board->cells[over] > 0 && board_adjacent(from, over) &&
            board_adjacent(over, to) && straight_jump(from, over, to))
            return board->cells[over];
    }
    return -1;
}

bool board_init(board_t *board, const int positions[PIECE_COUNT])
{
    if (board == NULL || positions == NULL)
        return false;
    board_t initial;
    for (int i = 0; i < BOARD_NODES; ++i)
        initial.cells[i] = -1;
    initial.captured = 0;
    for (int piece = 0; piece < PIECE_COUNT; ++piece) {
        int node = positions[piece];
        if (!node_valid(node) || initial.cells[node] != -1)
            return false;
        initial.cells[node] = piece;
        initial.positions[piece] = node;
    }
    *board = initial;
    return true;
}

size_t board_moves(const board_t *board, int piece,
                   int destinations[BOARD_NODES])
{
    if (board == NULL || destinations == NULL || piece < 0 ||
        piece >= PIECE_COUNT || !node_valid(board->positions[piece]))
        return 0;
    int from = board->positions[piece];
    size_t count = 0;
    for (int to = 0; to < BOARD_NODES; ++to) {
        if (board->cells[to] == -1 &&
            (board_adjacent(from, to) ||
             (piece == 0 && captured_piece(board, from, to) > 0)))
            destinations[count++] = to;
    }
    return count;
}

board_result_t board_result(const board_t *board)
{
    if (board->captured >= 6)
        return GAME_PUMA;
    int moves[BOARD_NODES];
    if (board_moves(board, 0, moves) == 0)
        return GAME_DOGS;
    return GAME_RUNNING;
}

bool board_apply(board_t *board, int piece, int destination)
{
    if (board == NULL || piece < 0 || piece >= PIECE_COUNT ||
        !node_valid(destination) || !node_valid(board->positions[piece]) ||
        board->cells[destination] != -1 || board_result(board) != GAME_RUNNING)
        return false;
    int from = board->positions[piece];
    int victim = -1;
    if (!board_adjacent(from, destination)) {
        if (piece != 0 || (victim = captured_piece(board, from, destination)) < 1)
            return false;
    }
    board->cells[from] = -1;
    board->cells[destination] = piece;
    board->positions[piece] = destination;
    if (victim > 0) {
        board->cells[board->positions[victim]] = -1;
        board->positions[victim] = -1;
        ++board->captured;
    }
    return true;
}

bool board_valid(const board_t *board)
{
    if (board == NULL || !node_valid(board->positions[0]) ||
        board->captured > DOG_COUNT)
        return false;
    bool seen[PIECE_COUNT] = {false};
    unsigned occupied = 0;
    for (int node = 0; node < BOARD_NODES; ++node) {
        int piece = board->cells[node];
        if (piece == -1)
            continue;
        if (piece < 0 || piece >= PIECE_COUNT || seen[piece] ||
            board->positions[piece] != node)
            return false;
        seen[piece] = true;
        ++occupied;
    }
    unsigned absent = 0;
    for (int piece = 0; piece < PIECE_COUNT; ++piece) {
        if (!seen[piece]) {
            if (piece == 0 || board->positions[piece] != -1)
                return false;
            ++absent;
        }
    }
    return absent == board->captured && occupied + absent == PIECE_COUNT;
}

static void append(char *buffer, size_t capacity, size_t *used,
                   const char *format, ...)
{
    if (*used >= capacity)
        return;
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer + *used, capacity - *used, format, arguments);
    va_end(arguments);
    if (result < 0)
        return;
    size_t available = capacity - *used;
    *used += (size_t)result < available ? (size_t)result : available - 1;
}

static void cell_text(const board_t *board, int node, char text[4])
{
    if (board->cells[node] == -1)
        (void)snprintf(text, 4, " . ");
    else if (board->cells[node] == 0)
        (void)snprintf(text, 4, " P ");
    else
        (void)snprintf(text, 4, "D%02d", board->cells[node]);
}

void board_format(const board_t *board, char *buffer, size_t capacity)
{
    if (buffer == NULL || capacity == 0)
        return;
    buffer[0] = '\0';
    if (board == NULL)
        return;
    size_t used = 0;
    char token[4];
    cell_text(board, 27, token);
    append(buffer, capacity, &used, "              T[27:%s]\n", token);
    cell_text(board, 25, token);
    append(buffer, capacity, &used, "       [25:%s]", token);
    cell_text(board, 26, token);
    append(buffer, capacity, &used, "---[26:%s]\n", token);
    for (int row = 0; row < 5; ++row) {
        for (int column = 0; column < 5; ++column) {
            int node = row * 5 + column;
            cell_text(board, node, token);
            append(buffer, capacity, &used, "[%02d:%s]%s", node, token,
                   column == 4 ? "\n" : "-");
        }
    }
    append(buffer, capacity, &used, "capturados=%u\n", board->captured);
}
