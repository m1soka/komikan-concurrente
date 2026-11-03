#include "config.h"
#include "tournament.h"
#include <stdio.h>

int main(int argc, char **argv)
{
    size_t players, tables;
    if ((argc != 3 && argc != 4) ||
        !parse_positive(argv[1], 1024, &players) ||
        !parse_positive(argv[2], 64, &tables)) {
        fprintf(stderr, "Uso: %s N M [config.txt]\nN: 1..1024; M: 1..64.\n", argv[0]);
        return 2;
    }
    config_t config;
    config_defaults(&config);
    char error[256];
    if (argc == 4 && !config_load(&config, argv[3], error, sizeof(error))) {
        fprintf(stderr, "Configuración inválida: %s\n", error);
        return 2;
    }
    if (config.fail_player > players) {
        fprintf(stderr, "fail_player debe ser 0 o un ID entre 1 y N.\n");
        return 2;
    }
    return tournament_run(players, tables, &config);
}
