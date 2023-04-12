#include "vector.h"

#include <stdlib.h>
#include <string.h>

struct vector *vector_create(size_t elemSize) {
    struct vector *v = malloc(sizeof(struct vector));
    memset(v, 0, sizeof(*v));
    v->elemSize = elemSize;
    vector_capacity_set(v, 4);
    return v;
}

void vector_free(struct vector *v, void (*cb)(void*)) {
    vector_foreach(v, cb);
    free(v->data);
    free(v);
}

void *vector_get(struct vector *v, size_t idx) {
    if (idx >= v->size) {
        return NULL;
    }
    return v->data + idx * v->elemSize;
}

void vector_capacity_set(struct vector *v, size_t newCapacity) {
    if (newCapacity >= v->size && v->capacity != newCapacity) {
        void *newdata = malloc(newCapacity * v->elemSize);
        memcpy(newdata, v->data, v->size * v->elemSize);
        free(v->data);
        v->data = newdata;
        v->capacity = newCapacity;
    }
}

void vector_append(struct vector *v, void *data) {
    if (v->capacity < v->size + 1) {
        vector_capacity_set(v, 2 * v->capacity);
    }
    memcpy(v->data + v->size * v->elemSize, data, v->elemSize);
    v->size++;
}

void vector_remove(struct vector *v, size_t idx) {
    if (idx < v->size) {
        memmove(v->data + (idx + 1) * v->elemSize, v->data + idx * v->elemSize, (v->size - idx - 1) * v->elemSize);
        v->size--;
    }
}

void vector_foreach(struct vector *v, void (*cb)(void*)) {
    if (cb) {
        for (size_t i = 0; i < v->size; i++) {
            cb(v->data + i * v->elemSize);
        }
    }
}
