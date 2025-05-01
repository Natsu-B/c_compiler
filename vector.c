// ------------------------------------------------------------------------------------
// vector like storage
// ------------------------------------------------------------------------------------

#include "include/vector.h"

#include <stdbool.h>
#include <string.h>

#include "include/error.h"

struct Vector
{
  size_t capacity;
  size_t len;
  void **data;
};

Vector *vector_new()
{
  Vector *vec = calloc(1, sizeof(Vector));
  return vec;
}

bool vector_has_data(Vector *vec)
{
  return vec->len;
}

size_t vector_size(Vector *vec)
{
  if (!vec)
    return 0;
  return vec->len;
}

void vector_push(Vector *vec, void *data)
{
  if (vec->capacity < vec->len + 1)
    vec->data = realloc(vec->data, sizeof(void *) * (vec->capacity += 8));
  vec->data[vec->len++] = data;
}

// lenは1始まり
void vector_insert(Vector *vec, size_t len, void *data)
{
  if (vec->len + 1 < len)
    error_exit("Invalid vector insertion");
  size_t len_max = (vec->len < len ? len : vec->len);
  if (vec->capacity < len_max)
    vec->data = realloc(vec->data, vec->capacity + 8);
  vec->data[len - 1] = data;
}

void *vector_pop(Vector *vec)
{
  if (vec->len == 0)
    error_exit("Vector size is 0");
  return vec->data[--vec->len];
}

void *vector_shift(Vector *vec)
{
  if (vec->len == 0)
    error_exit("Vector size is 0");
  void *data = vec->data[0];
  if (vec->len > 1)
    memmove(&vec->data[0], &vec->data[1], sizeof(void *) * (vec->len - 1));
  vec->len--;
  return data;
}

void *vector_peek(Vector *vec)
{
  if (vec->len == 0)
    error_exit("Vector size is 0");
  return vec->data[vec->len - 1];
}

// 指定できるlocationは1始まり
void *vector_peek_at(Vector *vec, size_t location)
{
  if (vec->len < location || location == 0)
    error_exit(
        "vector_peek_at() tried to access index %lu, but the vector contains "
        "only %lu elements",
        location, vec->len);
  return vec->data[location - 1];
}

void *vector_pop_at(Vector *vec, size_t location)
{
  if (vec->len < location || location == 0)
    error_exit(
        "vector_pop_at() tried to access index %lu, but the vector contains "
        "only %lu elements",
        location, vec->len);
  void *return_data = vec->data[location - 1];
  if (vec->len > 1)
    memmove(&vec->data[location - 1], &vec->data[location],
            vec->len-- - location);
  return return_data;
}

void vector_free(Vector *vec)
{
  if (vec->data)
    free(vec->data);
  free(vec);
}