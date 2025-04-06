#ifndef VECTOR_C_COMPILER
#define VECTOR_C_COMPILER

#include <stdlib.h>
#include <stdbool.h>

typedef struct Vector Vector;

Vector *vector_new();
bool vector_has_data(Vector *vec);
size_t vector_size(Vector *vec);
void vector_push(Vector *vec, void *data);
void vector_insert(Vector *vec, size_t len, void *data);
void *vector_pop(Vector *vec);
void *vector_shift(Vector *vec);
void *vector_peek(Vector *vec);
void *vector_peek_at(Vector *vec, size_t location);
void vector_free(Vector *vec);

#endif