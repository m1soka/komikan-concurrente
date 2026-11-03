#ifndef KOMIKAN_QUEUE_H
#define KOMIKAN_QUEUE_H

#include <stdbool.h>
#include <stddef.h>

/* The tournament mutex protects every operation after initialization. */
typedef struct {
    size_t *items;
    size_t capacity;
    size_t head;
    size_t count;
} queue_t;

bool queue_init(queue_t *queue, size_t capacity);
void queue_destroy(queue_t *queue);
bool queue_push(queue_t *queue, size_t id);
bool queue_pop(queue_t *queue, size_t *id);
size_t queue_size(const queue_t *queue);

#endif
