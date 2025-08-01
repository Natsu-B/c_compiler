#ifndef VECTOR_C_COMPILER
#define VECTOR_C_COMPILER

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
#include <stdbool.h>
#include <stdlib.h>
#endif

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
void *vector_pop_at(Vector *vec, size_t location);
void *vector_replace_at(Vector *vec, size_t location, void *data);
bool vector_compare(Vector *vec1, Vector *vec2);
void vector_free(Vector *vec);

#endif