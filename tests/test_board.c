#include "board.h"

#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>

static board_t make_board(const int positions[PIECE_COUNT])
{
    board_t board;
    assert(board_init(&board, positions));
    assert(board_valid(&board));
    return board;
}

static bool contains(const board_t *board, int piece, int destination)
{
    int moves[BOARD_NODES];
    size_t count = board_moves(board, piece, moves);
    for (size_t i = 0; i < count; ++i) {
        if (moves[i] == destination)
            return true;
    }
    return false;
}

static void test_topology(void)
{
    unsigned edges = 0;
    for (int from = 0; from < BOARD_NODES; ++from) {
        assert(!board_adjacent(from, from));
        for (int to = 0; to < BOARD_NODES; ++to) {
            assert(board_adjacent(from, to) == board_adjacent(to, from));
            if (board_adjacent(from, to))
                ++edges;
        }
    }
    assert(edges == 96); /* 40 square-grid edges and eight throne edges */
    assert(board_adjacent(0, 1));
    assert(board_adjacent(0, 5));
    assert(!board_adjacent(0, 6));
    assert(!board_adjacent(4, 5));
    assert(board_adjacent(27, 25));
    assert(board_adjacent(27, 26));
    assert(board_adjacent(27, 2));
    assert(board_adjacent(25, 26));
    assert(board_adjacent(25, 0));
    assert(board_adjacent(25, 1));
    assert(board_adjacent(26, 3));
    assert(board_adjacent(26, 4));
    assert(!board_adjacent(25, 2));
    assert(!board_adjacent(26, 2));
    assert(!board_adjacent(-1, 0));
    assert(!board_adjacent(28, 27));
}

static void test_initialization_and_validation(void)
{
    int positions[PIECE_COUNT] = {0, 10, 11, 12, 13, 14, 15, 16, 17,
                                18, 19, 20, 21};
    board_t board = make_board(positions);
    board_t original = board;
    positions[1] = 0;
    assert(!board_init(&board, positions));
    assert(memcmp(&board, &original, sizeof board) == 0);
    positions[1] = -1;
    assert(!board_init(&board, positions));
    positions[1] = BOARD_NODES;
    assert(!board_init(&board, positions));
    assert(!board_init(NULL, positions));
    assert(!board_init(&board, NULL));
    board.cells[27] = 0;
    assert(!board_valid(&board));
    board = original;
    board.positions[1] = 8;
    assert(!board_valid(&board));
    board = original;
    board.captured = 1;
    assert(!board_valid(&board));
    assert(!board_valid(NULL));
}

static void test_moves_and_capture(void)
{
    int positions[PIECE_COUNT] = {0, 1, 10, 11, 12, 13, 14, 15,
                                16, 17, 18, 19, 20};
    board_t board = make_board(positions);
    assert(contains(&board, 0, 2));
    assert(contains(&board, 0, 5));
    assert(!contains(&board, 0, 6));
    assert(!contains(&board, 1, 0)); /* a dog cannot capture the puma */
    assert(!contains(&board, 1, 3)); /* dogs cannot jump */
    board_t original = board;
    assert(!board_apply(&board, 1, 0));
    assert(!board_apply(&board, 0, 6));
    assert(!board_apply(&board, -1, 2));
    assert(!board_apply(&board, PIECE_COUNT, 2));
    assert(!board_apply(&board, 0, -1));
    assert(!board_apply(&board, 0, BOARD_NODES));
    assert(memcmp(&board, &original, sizeof board) == 0);
    assert(board_apply(&board, 0, 2));
    assert(board.positions[0] == 2);
    assert(board.positions[1] == -1);
    assert(board.cells[0] == -1 && board.cells[1] == -1);
    assert(board.cells[2] == 0 && board.captured == 1);
    assert(board_valid(&board));
    assert(board_moves(&board, 1, (int[BOARD_NODES]){0}) == 0);
    assert(!board_apply(&board, 1, 6));
    assert(board_apply(&board, 0, 3));
    assert(board_valid(&board));
}

static void test_throne(void)
{
    int positions[PIECE_COUNT] = {27, 25, 10, 11, 12, 13, 14, 15,
                                16, 17, 18, 19, 20};
    board_t board = make_board(positions);
    assert(contains(&board, 0, 0)); /* diagonal apex -> 25 -> grid 0 */
    assert(!contains(&board, 0, 1)); /* a jump cannot turn at 25 */
    assert(contains(&board, 0, 2));
    assert(board_apply(&board, 0, 0));
    assert(board.positions[1] == -1 && board_valid(&board));

    positions[0] = 7;
    positions[1] = 2;
    board = make_board(positions);
    assert(contains(&board, 0, 27)); /* central edge has no extra intersection */
    assert(board_apply(&board, 0, 27));
    assert(board.positions[1] == -1 && board_valid(&board));

    positions[0] = 27;
    positions[1] = 2;
    board = make_board(positions);
    assert(contains(&board, 0, 7));
    assert(board_apply(&board, 0, 7));
    assert(board_valid(&board));
}

static void test_victory(void)
{
    int positions[PIECE_COUNT] = {24, 19, 23, 14, 22, 0, 1, 2,
                                3, 4, 5, 6, 7};
    board_t board = make_board(positions);
    assert(board_result(&board) == GAME_DOGS);
    assert(!board_apply(&board, 5, 10)); /* final state is immutable */
    positions[3] = 8;
    board = make_board(positions);
    assert(board_result(&board) == GAME_RUNNING);
    assert(board_apply(&board, 0, 14));
    assert(board_valid(&board));

    int winning[PIECE_COUNT] = {0, 10, 11, 12, 13, 14, 1, 16,
                              17, 18, 19, 20, 21};
    board = make_board(winning);
    for (int dog = 1; dog <= 5; ++dog) {
        board.cells[board.positions[dog]] = -1;
        board.positions[dog] = -1;
        ++board.captured;
    }
    assert(board_valid(&board));
    assert(board_result(&board) == GAME_RUNNING);
    assert(board_apply(&board, 0, 2));
    assert(board.captured == 6 && board_result(&board) == GAME_PUMA);
    assert(board_valid(&board));
    assert(!board_apply(&board, 0, 3));
}

typedef struct {
    board_t *board;
    pthread_mutex_t *mutex;
    int piece;
    bool moved;
} contender_t;

static void *contend(void *argument)
{
    contender_t *contender = argument;
    assert(pthread_mutex_lock(contender->mutex) == 0);
    contender->moved = board_apply(contender->board, contender->piece, 6);
    assert(pthread_mutex_unlock(contender->mutex) == 0);
    return NULL;
}

static void test_contested_cell(void)
{
    const int positions[PIECE_COUNT] = {27, 5, 7, 10, 11, 12, 13,
                                      14, 15, 16, 17, 18, 19};
    for (int repetition = 0; repetition < 100; ++repetition) {
        board_t board = make_board(positions);
        pthread_mutex_t mutex;
        pthread_t threads[2];
        assert(pthread_mutex_init(&mutex, NULL) == 0);
        contender_t contenders[2] = {
            {&board, &mutex, 1, false}, {&board, &mutex, 2, false}
        };
        assert(pthread_create(&threads[0], NULL, contend, &contenders[0]) == 0);
        assert(pthread_create(&threads[1], NULL, contend, &contenders[1]) == 0);
        assert(pthread_join(threads[0], NULL) == 0);
        assert(pthread_join(threads[1], NULL) == 0);
        assert(contenders[0].moved != contenders[1].moved);
        assert(board_valid(&board));
        assert(board.cells[6] == 1 || board.cells[6] == 2);
        assert(pthread_mutex_destroy(&mutex) == 0);
    }
}

static void test_format(void)
{
    const int positions[PIECE_COUNT] = {27, 0, 1, 2, 3, 4, 5, 6,
                                      7, 8, 9, 10, 11};
    board_t board = make_board(positions);
    char output[1024];
    board_format(&board, output, sizeof output);
    assert(strstr(output, "T[27: P ]") != NULL);
    assert(strstr(output, "[00:D01]") != NULL);
    assert(strstr(output, "capturados=0") != NULL);
    char tiny[4] = {'x', 'x', 'x', 'x'};
    board_format(&board, tiny, sizeof tiny);
    assert(tiny[3] == '\0');
    board_format(&board, NULL, 0);
    board_format(NULL, tiny, sizeof tiny);
    assert(tiny[0] == '\0');
}

int main(void)
{
    test_topology();
    test_initialization_and_validation();
    test_moves_and_capture();
    test_throne();
    test_victory();
    test_contested_cell();
    test_format();
    puts("board: 7 grupos OK (incluye 100 competencias por una casilla)");
    return 0;
}
