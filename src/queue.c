#include "queue.h"

#include <stdint.h>
#include <stdlib.h>

bool queue_init(queue_t *queue, size_t capacity)
{
    if (queue == NULL) {
        return false;
    }
    *queue = (queue_t){0};
    if (capacity == 0 || capacity > SIZE_MAX / sizeof(*queue->items)) {
        return false;
    }
    queue->items = calloc(capacity, sizeof(*queue->items));
    if (queue->items == NULL) {
        return false;
    }
    queue->capacity = capacity;
    return true;
}

void queue_destroy(queue_t *queue)
{
    if (queue != NULL) {
        free(queue->items);
        *queue = (queue_t){0};
    }
}

bool queue_push(queue_t *queue, size_t id)
{
    if (queue == NULL || queue->items == NULL || queue->count == queue->capacity) {
        return false;
    }
    size_t tail = (queue->head + queue->count) % queue->capacity;
    queue->items[tail] = id;
    ++queue->count;
    return true;
}

bool queue_pop(queue_t *queue, size_t *id)
{
    if (queue == NULL || queue->count == 0 || id == NULL) {
        return false;
    }
    *id = queue->items[queue->head];
    queue->head = (queue->head + 1) % queue->capacity;
    --queue->count;
    return true;
}

size_t queue_size(const queue_t *queue)
{
    return queue == NULL ? 0 : queue->count;
}
