#define _POSIX_C_SOURCE 200809L
#include "config.h"
#include "queue.h"

#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool load_text(config_t *config, const char *text)
{
    char path[] = "/tmp/komikan-config-XXXXXX";
    int descriptor = mkstemp(path);
    assert(descriptor >= 0);
    FILE *file = fdopen(descriptor, "w");
    assert(file != NULL);
    assert(fputs(text, file) >= 0);
    assert(fclose(file) == 0);
    char error[256];
    bool result = config_load(config, path, error, sizeof(error));
    assert(unlink(path) == 0);
    if (!result) {
        assert(error[0] != '\0');
    }
    return result;
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
    assert(load_text(&config, "# vacío\n\n"));
    assert(load_text(&config, "seed=0\nfail_player=0\narrival_delay_ms=0\nfail_after_ms=0\n"));
    assert(config.seed == 0 && config.fail_player == 0);
    char error[256];
    assert(!config_load(&config, "/ruta/inexistente/komikan-config", error, sizeof(error)));
    assert(!config_load(NULL, "nada", error, sizeof(error)));
    assert(!config_load(&config, NULL, error, sizeof(error)));
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
}

int main(void)
{
    test_numbers();
    test_config();
    test_queue();
    puts("OK config/queue: validación estricta, overflow, transacciones y FIFO circular");
    return EXIT_SUCCESS;
}
