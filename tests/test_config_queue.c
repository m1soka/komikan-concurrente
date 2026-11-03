#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "queue.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool load_bytes_interruptible(config_t *config, const void *data, size_t size,
                                     bool (*interrupted)(void *), void *context)
{
    char path[] = "/tmp/komikan-config-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    FILE *file = fdopen(descriptor, "w");
    assert(file != NULL);
    assert(fwrite(data, 1, size, file) == size);
    assert(fclose(file) == 0);
    char error[256];
    bool result = config_load_interruptible(config, path, error, sizeof(error),
                                            interrupted, context);
    assert(unlink(path) == 0);
    if (!result) {
        assert(error[0] != '\0');
    }
    return result;
}

static bool load_bytes(config_t *config, const void *data, size_t size)
{
    return load_bytes_interruptible(config, data, size, NULL, NULL);
}

static bool load_text(config_t *config, const char *text)
{
    return load_bytes(config, text, strlen(text));
}

static void test_numbers(void)
{
    size_t number = 0;
    assert(parse_positive("1", 100, &number) && number == 1);
    assert(parse_positive("00042", 100, &number) && number == 42);
    assert(parse_positive("100", 100, &number) && number == 100);
    const char *invalid[] = {"", "0", "-1", "+2", " 1", "1 ", "1x", "101",
                             "9999999999999999999999999999999999999999999999999"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        assert(!parse_positive(invalid[i], 100, &number));
    }
    char maximum[64];
    (void)snprintf(maximum, sizeof(maximum), "%zu", SIZE_MAX);
    assert(parse_positive(maximum, SIZE_MAX, &number) && number == SIZE_MAX);
    size_t length = strlen(maximum);
    maximum[length] = '0';
    maximum[length + 1] = '\0';
    assert(!parse_positive(maximum, SIZE_MAX, &number));
    assert(!parse_positive("1", 0, &number));
    assert(!parse_positive(NULL, SIZE_MAX, &number));
    assert(!parse_positive("1", SIZE_MAX, NULL));
}

static void test_config(void)
{
    config_t config;
    config_defaults(&config);
    assert(config.min_delay_ms == 50 && config.max_delay_ms == 300);
    assert(config.positions[0] == 0 && config.positions[12] == 24);
    assert(load_text(&config, "# comentario\n min_delay_ms = 60\nmax_delay_ms=70\nseed=4294967295 # max\n"
                              "puma=27\ndogs=0, 1,2,3,4,5,6,7,8,9,10,11\n"
                              "arrival_delay_ms=1000\nfail_player=2\nfail_after_ms=60000"));
    assert(config.min_delay_ms == 60 && config.max_delay_ms == 70);
    assert(config.seed == UINT32_MAX && config.positions[0] == 27);
    assert(config.positions[12] == 11 && config.arrival_delay_ms == 1000);
    assert(config.fail_player == 2 && config.fail_after_ms == 60000);
    assert(config.fail_mode == FAIL_ABANDON);
    char largest_id[100];
    (void)snprintf(largest_id, sizeof(largest_id), "fail_player=%zu\n", SIZE_MAX);
    assert(load_text(&config, largest_id) && config.fail_player == SIZE_MAX);

    const char *invalid[] = {
        "unknown=1\n", "seed=1\nseed=2\n", "seed\n", "seed=1=2\n", "=3\n",
        "min_delay_ms=49\n", "max_delay_ms=301\n", "min_delay_ms=71\nmax_delay_ms=70\n",
        "seed=-1\n", "seed=4294967296\n", "seed=nan\n", "seed=\n", "puma=28\n",
        "puma=0\n", /* puma overlaps custom dogs */
        "dogs=0,1,2\n", "dogs=0,1,2,3,4,5,6,7,8,9,10,11,12\n",
        "dogs=0,0,2,3,4,5,6,7,8,9,10,11\n", "dogs=0,1,2,3,4,5,6,7,8,9,10,-1\n",
        "dogs=0,1,2,3,4,5,6,7,8,9,10,28\n", "dogs=0,1,2,3,4,5,6,7,8,9,10,\n",
        "arrival_delay_ms=1001\n", "fail_after_ms=60001\n", "fail_player=-1\n",
        "fail_player=99999999999999999999999999999999999999\n"
    };
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        config_t before = config;
        assert(!load_text(&config, invalid[i]));
        assert(memcmp(&config, &before, sizeof(config)) == 0);
    }
    char long_line[5000];
    memset(long_line, 'x', sizeof(long_line) - 1);
    long_line[sizeof(long_line) - 1] = '\0';
    assert(!load_text(&config, long_line));
    /* A complete boundary-sized line is legal with or without a final newline. */
    memset(long_line, ' ', 4095);
    long_line[4095] = '\0';
    assert(load_text(&config, long_line));
    long_line[4095] = '\n';
    long_line[4096] = '\0';
    assert(load_text(&config, long_line));
    const char nul_at_eof[] = "seed=3\0hidden";
    const char nul_in_line[] = "seed=3\0hidden\n";
    const char nul_in_comment[] = "# comment\0hidden\nseed=4\n";
    config_t before_binary = config;
    assert(!load_bytes(&config, nul_at_eof, sizeof(nul_at_eof) - 1));
    assert(!load_bytes(&config, nul_in_line, sizeof(nul_in_line) - 1));
    assert(!load_bytes(&config, nul_in_comment, sizeof(nul_in_comment) - 1));
    assert(memcmp(&config, &before_binary, sizeof(config)) == 0);
    assert(load_text(&config, "# vacío\n\n"));
    assert(load_text(&config, "seed=0\nfail_player=0\narrival_delay_ms=0\nfail_after_ms=0\n"));
    assert(config.seed == 0 && config.fail_player == 0);
    char error[256];
    assert(!config_load(&config, "/ruta/inexistente/komikan-config", error, sizeof(error)));
    /* Special files must be rejected before attempting to read their data. */
    config_t before_read = config;
    assert(!config_load(&config, "/tmp", error, sizeof(error)));
    assert(memcmp(&config, &before_read, sizeof(config)) == 0);
    assert(!config_load(NULL, "nada", error, sizeof(error)));
    assert(!config_load(&config, NULL, error, sizeof(error)));
}

typedef struct {
    size_t polls;
    size_t interrupt_after;
} cancellation_t;

static bool interrupt_reader(void *context)
{
    cancellation_t *cancellation = context;
    return cancellation->polls++ >= cancellation->interrupt_after;
}

static void test_interruptible_config(void)
{
    config_t config;
    config_defaults(&config);
    config_t before = config;
    const char input[] = "seed=2026\nmin_delay_ms=100\nmax_delay_ms=200\n";
    cancellation_t cancellation = {0, 0};
    assert(!load_bytes_interruptible(&config, input, sizeof(input) - 1,
                                     interrupt_reader, &cancellation));
    assert(memcmp(&config, &before, sizeof(config)) == 0);
    cancellation = (cancellation_t){0, 15};
    assert(!load_bytes_interruptible(&config, input, sizeof(input) - 1,
                                     interrupt_reader, &cancellation));
    assert(cancellation.polls == 16);
    assert(memcmp(&config, &before, sizeof(config)) == 0);
    cancellation = (cancellation_t){0, SIZE_MAX};
    assert(load_bytes_interruptible(&config, input, sizeof(input) - 1,
                                    interrupt_reader, &cancellation));
    assert(config.seed == 2026 && config.max_delay_ms == 200);

    char path[] = "/tmp/komikan-fifo-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    assert(close(descriptor) == 0);
    assert(unlink(path) == 0);
    assert(mkfifo(path, 0600) == 0);
    char error[256];
    assert(!config_load(&config, path, error, sizeof(error)));
    assert(strstr(error, "archivo regular") != NULL);
    assert(unlink(path) == 0);
    assert(!config_load(&config, "/dev/null", error, sizeof(error)));
}

static void test_failure_modes(void)
{
    const char *modes[] = {"abandon", "client_exit_locked", "client_exit_unlocked",
                          "piece_exit_locked", "piece_exit_unlocked", "piece_cancel_wait"};
    config_t config;
    for (size_t mode = 0; mode < sizeof(modes) / sizeof(modes[0]); ++mode) {
        config_defaults(&config);
        char text[150];
        (void)snprintf(text, sizeof(text), "fail_mode=%s\nfail_player=1\n", modes[mode]);
        assert(load_text(&config, text));
        assert(config.fail_mode == (failure_mode_t)mode);
        assert(config.fail_player == 1);
    }
    config_defaults(&config);
    assert(!load_text(&config, "fail_mode=unknown\nfail_player=1\n"));
    assert(!load_text(&config, "fail_mode=client_exit_locked\n"));
    assert(!load_text(&config, "fail_mode=piece_exit_locked\nfail_player=0\n"));
    assert(!load_text(&config, "fail_mode=abandon\nfail_mode=abandon\n"));
    assert(load_text(&config, "fail_mode=abandon\n"));
}

static void test_queue(void)
{
    queue_t queue;
    size_t id;
    assert(!queue_init(&queue, 0));
    assert(!queue_init(&queue, SIZE_MAX));
    assert(queue_init(&queue, 3));
    assert(queue_size(&queue) == 0);
    assert(!queue_pop(&queue, &id));
    assert(queue_push(&queue, 11));
    assert(queue_push(&queue, 22));
    assert(queue_push(&queue, 33));
    assert(!queue_push(&queue, 44));
    assert(queue_pop(&queue, &id) && id == 11);
    assert(queue_push(&queue, 44)); /* wrap-around */
    assert(queue_pop(&queue, &id) && id == 22);
    assert(queue_pop(&queue, &id) && id == 33);
    assert(queue_pop(&queue, &id) && id == 44);
    assert(!queue_pop(&queue, &id));
    for (size_t cycle = 0; cycle < 10000; ++cycle) {
        assert(queue_push(&queue, cycle));
        assert(queue_pop(&queue, &id) && id == cycle);
    }
    queue_destroy(&queue);
    queue_destroy(&queue);
    assert(queue_size(&queue) == 0);
    assert(!queue_push(&queue, 1));
    assert(!queue_pop(&queue, &id));
    assert(!queue_init(NULL, 3));
    assert(!queue_push(NULL, 1));
    assert(!queue_pop(NULL, &id));
    queue_destroy(NULL);
}

int main(void)
{
    test_numbers();
    test_config();
    test_interruptible_config();
    test_failure_modes();
    test_queue();
    puts("OK config/queue: validación estricta, overflow, transacciones y FIFO circular");
    return EXIT_SUCCESS;
}
