#ifndef VECTOR_H
#define VECTOR_H

#include <stdlib.h>

struct vector {
    void *data;
    size_t elemSize;
    size_t size;
    size_t capacity;
};

#define VECTOR_CHARP_CREATE() vector_create(sizeof(char*))

struct vector *vector_create(size_t elemSize);
void vector_free(struct vector *v, void (*cb)(void*));
void vector_capacity_set(struct vector *v, size_t newCapacity);
void *vector_get(struct vector *v, size_t idx);
void vector_append(struct vector *v, void *data);
void vector_remove(struct vector *v, size_t idx);
void vector_foreach(struct vector *v, void (*cb)(void*));

static char *vector_charp_get(struct vector *v, size_t idx) {
    return ((char **) v->data)[idx];
}

static void vector_charp_append(struct vector *v, char *s) {
    vector_append(v, &s);
}


#endif // VECTOR_H
