#define _POSIX_C_SOURCE 200809L
#include "config.h"

#include <ctype.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool reject(char *error, size_t size, const char *format, ...)
{
    if (error != NULL && size != 0) {
        va_list args;
        va_start(args, format);
        (void)vsnprintf(error, size, format, args);
        va_end(args);
    }
    return false;
}

static char *trim(char *text)
{
    while (isspace((unsigned char)*text)) {
        ++text;
    }
    size_t length = strlen(text);
    while (length != 0 && isspace((unsigned char)text[length - 1])) {
        text[--length] = '\0';
    }
    return text;
}

/* Count bytes rather than strlen(fgets(...)): embedded NUL must not hide
   trailing input, including when the final line has no newline. */
static int read_line(FILE *input, char *line, size_t capacity,
                     bool (*interrupted)(void *), void *context)
{
    size_t length = 0;
    int byte;
    for (;;) {
        if (interrupted != NULL && interrupted(context)) return -4;
        byte = fgetc(input);
        if (byte == EOF) break;
        if (byte == '\0') return -2;
        if (byte == '\n') {
            line[length] = '\0';
            return 1;
        }
        if (length == capacity - 1) return -3;
        line[length++] = (char)byte;
    }
    if (ferror(input)) return -1;
    line[length] = '\0';
    return length != 0 ? 1 : 0;
}

static bool decimal(const char *text, uintmax_t max, uintmax_t *value)
{
    if (text == NULL || *text == '\0') {
        return false;
    }
    uintmax_t result = 0;
    for (const unsigned char *cursor = (const unsigned char *)text; *cursor; ++cursor) {
        if (*cursor < '0' || *cursor > '9') {
            return false;
        }
        unsigned digit = (unsigned)(*cursor - '0');
        if (digit > max || result > (max - digit) / 10) {
            return false;
        }
        result = result * 10 + digit;
    }
    *value = result;
    return true;
}

bool parse_positive(const char *text, size_t max, size_t *value)
{
    uintmax_t result;
    if (value == NULL || !decimal(text, max, &result) || result == 0) {
        return false;
    }
    *value = (size_t)result;
    return true;
}

void config_defaults(config_t *config)
{
    *config = (config_t){
        .min_delay_ms = 50,
        .max_delay_ms = 300,
        .seed = 1,
        .arrival_delay_ms = 0,
        .fail_player = 0,
        .fail_after_ms = 100,
        .fail_mode = FAIL_ABANDON
    };
    config->positions[0] = 0;
    for (int piece = 1; piece < PIECE_COUNT; ++piece) {
        config->positions[piece] = 12 + piece;
    }
}

static bool parse_dogs(char *value, int positions[PIECE_COUNT])
{
    char *cursor = value;
    for (int piece = 1; piece < PIECE_COUNT; ++piece) {
        char *comma = strchr(cursor, ',');
        if ((piece < DOG_COUNT && comma == NULL) ||
            (piece == DOG_COUNT && comma != NULL)) {
            return false;
        }
        if (comma != NULL) {
            *comma = '\0';
        }
        uintmax_t number;
        if (!decimal(trim(cursor), BOARD_NODES - 1, &number)) {
            return false;
        }
        positions[piece] = (int)number;
        if (comma != NULL) {
            cursor = comma + 1;
        }
    }
    return true;
}

bool config_load_interruptible(config_t *config, const char *path, char *error,
                               size_t error_size, bool (*interrupted)(void *),
                               void *context)
{
    if (error != NULL && error_size != 0) {
        *error = '\0';
    }
    if (config == NULL || path == NULL) {
        return reject(error, error_size, "Configuración o ruta nula");
    }
    if (interrupted != NULL && interrupted(context)) {
        return reject(error, error_size, "Lectura interrumpida");
    }
    /* O_NONBLOCK prevents a FIFO pathname from blocking before file-type
       validation; the parser accepts regular files only. */
    int descriptor = open(path, O_RDONLY | O_NONBLOCK);
    if (descriptor < 0) {
        return reject(error, error_size, "No se pudo abrir configuración: %s", path);
    }
    struct stat status;
    if (fstat(descriptor, &status) != 0 || !S_ISREG(status.st_mode)) {
        (void)close(descriptor);
        return reject(error, error_size, "La configuración debe ser un archivo regular: %s", path);
    }
    FILE *input = fdopen(descriptor, "r");
    if (input == NULL) {
        (void)close(descriptor);
        return reject(error, error_size, "No se pudo crear lector de configuración: %s", path);
    }
    config_t candidate = *config;
    enum { KEY_MIN, KEY_MAX, KEY_SEED, KEY_PUMA, KEY_DOGS, KEY_ARRIVAL,
           KEY_FAIL_PLAYER, KEY_FAIL_AFTER, KEY_FAIL_MODE, KEY_COUNT };
    const char *keys[KEY_COUNT] = {
        "min_delay_ms", "max_delay_ms", "seed", "puma", "dogs",
        "arrival_delay_ms", "fail_player", "fail_after_ms", "fail_mode"
    };
    bool seen[KEY_COUNT] = {false};
    bool success = true;
    size_t line_number = 0;
    char line[4096];
    int line_status;
    while ((line_status = read_line(input, line, sizeof(line), interrupted, context)) != 0) {
        if (line_number == SIZE_MAX) {
            success = reject(error, error_size, "Demasiadas líneas en configuración");
            break;
        }
        ++line_number;
        if (line_status == -4) {
            success = reject(error, error_size, "Lectura interrumpida");
            break;
        }
        if (line_status < 0) {
            const char *reason = line_status == -2 ? "contiene NUL" :
                                 line_status == -3 ? "demasiado larga" : "error de lectura";
            success = reject(error, error_size, "Línea %zu: %s", line_number, reason);
            break;
        }
        char *comment = strchr(line, '#');
        if (comment != NULL) {
            *comment = '\0';
        }
        char *key = trim(line);
        if (*key == '\0') {
            continue;
        }
        char *equals = strchr(key, '=');
        if (equals == NULL || strchr(equals + 1, '=') != NULL) {
            success = reject(error, error_size, "Línea %zu: se esperaba clave=valor", line_number);
            break;
        }
        *equals = '\0';
        key = trim(key);
        char *value = trim(equals + 1);
        int index = 0;
        while (index < KEY_COUNT && strcmp(key, keys[index]) != 0) {
            ++index;
        }
        if (index == KEY_COUNT || seen[index]) {
            success = reject(error, error_size, "Línea %zu: clave desconocida o duplicada: %s",
                             line_number, key);
            break;
        }
        seen[index] = true;
        if (index == KEY_FAIL_MODE) {
            const char *modes[] = {"abandon", "client_exit_locked", "client_exit_unlocked",
                                  "piece_exit_locked", "piece_exit_unlocked", "piece_cancel_wait"};
            size_t mode = 0;
            while (mode < sizeof(modes) / sizeof(modes[0]) && strcmp(value, modes[mode]) != 0) {
                ++mode;
            }
            if (mode == sizeof(modes) / sizeof(modes[0])) {
                success = reject(error, error_size, "Línea %zu: fail_mode desconocido", line_number);
                break;
            }
            candidate.fail_mode = (failure_mode_t)mode;
            continue;
        }
        if (index == KEY_DOGS) {
            if (!parse_dogs(value, candidate.positions)) {
                success = reject(error, error_size, "Línea %zu: dogs exige 12 nodos separados por comas", line_number);
                break;
            }
            continue;
        }
        uintmax_t maximum = SIZE_MAX;
        if (index == KEY_MIN || index == KEY_MAX) {
            maximum = 300;
        } else if (index == KEY_SEED) {
            maximum = UINT32_MAX;
        } else if (index == KEY_PUMA) {
            maximum = BOARD_NODES - 1;
        } else if (index == KEY_ARRIVAL) {
            maximum = 1000;
        } else if (index == KEY_FAIL_AFTER) {
            maximum = 60000;
        }
        uintmax_t number;
        if (!decimal(value, maximum, &number)) {
            success = reject(error, error_size, "Línea %zu: valor inválido para %s", line_number, key);
            break;
        }
        switch (index) {
        case KEY_MIN: candidate.min_delay_ms = (unsigned)number; break;
        case KEY_MAX: candidate.max_delay_ms = (unsigned)number; break;
        case KEY_SEED: candidate.seed = (uint32_t)number; break;
        case KEY_PUMA: candidate.positions[0] = (int)number; break;
        case KEY_ARRIVAL: candidate.arrival_delay_ms = (unsigned)number; break;
        case KEY_FAIL_PLAYER: candidate.fail_player = (size_t)number; break;
        case KEY_FAIL_AFTER: candidate.fail_after_ms = (unsigned)number; break;
        default: break;
        }
    }
    if (ferror(input)) {
        success = reject(error, error_size, "Error leyendo configuración: %s", path);
    }
    if (fclose(input) != 0 && success) {
        success = reject(error, error_size, "Error cerrando configuración: %s", path);
    }
    if (!success) {
        return false;
    }
    if (candidate.min_delay_ms < 50 || candidate.max_delay_ms > 300 ||
        candidate.min_delay_ms > candidate.max_delay_ms) {
        return reject(error, error_size, "Los retardos deben satisfacer 50 <= min_delay_ms <= max_delay_ms <= 300");
    }
    if (candidate.fail_mode != FAIL_ABANDON && candidate.fail_player == 0) {
        return reject(error, error_size, "fail_mode requiere fail_player distinto de 0");
    }
    bool occupied[BOARD_NODES] = {false};
    for (int piece = 0; piece < PIECE_COUNT; ++piece) {
        int node = candidate.positions[piece];
        if (node < 0 || node >= BOARD_NODES || occupied[node]) {
            return reject(error, error_size, "Posiciones iniciales ilegales o duplicadas");
        }
        occupied[node] = true;
    }
    *config = candidate;
    return true;
}

bool config_load(config_t *config, const char *path, char *error, size_t error_size)
{
    return config_load_interruptible(config, path, error, error_size, NULL, NULL);
}
