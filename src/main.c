#include "config.h"
#include "log.h"
#include "signals.h"
#include "tournament.h"
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static bool interrupted(void *context)
{
    return signals_interrupted(context);
}

int main(int argc, char **argv)
{
    int error = signals_block();
    if (error != 0) {
        fprintf(stderr, "No se pudo bloquear SIGINT: %s\n", strerror(error));
        return 1;
    }
    log_event("SERVIDOR señales_bloqueadas");
    signals_t signals;
    error = signals_start(&signals);
    if (error != 0) {
        fprintf(stderr, "No se pudo iniciar sigwait: %s\n", strerror(error));
        return 1;
    }
    int result = 2;
    size_t players, tables;
    if (signals_interrupted(&signals)) goto finish;
    if ((argc != 3 && argc != 4) ||
        !parse_positive(argv[1], SIZE_MAX, &players) ||
        !parse_positive(argv[2], SIZE_MAX, &tables)) {
        fprintf(stderr, "Uso: %s N M [config.txt]\nN y M: enteros positivos representables en size_t.\n", argv[0]);
        goto finish;
    }
    config_t config;
    config_defaults(&config);
    char message[256];
    if (argc == 4 && !config_load_interruptible(&config, argv[3], message,
                                               sizeof(message), interrupted, &signals)) {
        fprintf(stderr, "Configuración inválida: %s\n", message);
        goto finish;
    }
    if (config.fail_player > players) {
        fprintf(stderr, "fail_player debe ser 0 o un ID entre 1 y N.\n");
        goto finish;
    }
    if (!signals_interrupted(&signals))
        result = tournament_run(players, tables, &config, &signals);
finish:
    signals_finish(&signals);
    if (signals_interrupted(&signals) && result != 1) result = 130;
    return result;
}
