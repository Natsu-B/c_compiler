// ------------------------------------------------------------------------------------
// vector like storage
// ------------------------------------------------------------------------------------

#include "include/vector.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <stdbool.h>
#include <string.h>
#endif

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

// len starts from 1
void vector_insert(Vector *vec, size_t len, void *data)
{
  if (len > vec->len + 1 || len == 0)
    error_exit("Invalid vector insertion.");

  if (vec->capacity < vec->len + 1)
  {
    vec->capacity += 8;
    vec->data = realloc(vec->data, sizeof(void *) * vec->capacity);
  }

  if (len <= vec->len)
  {
    memmove(&vec->data[len], &vec->data[len - 1],
            sizeof(void *) * (vec->len - (len - 1)));
  }

  vec->data[len - 1] = data;
  vec->len++;
}

void *vector_pop(Vector *vec)
{
  if (vec->len == 0)
    error_exit("Vector size is 0.");
  return vec->data[--vec->len];
}

void *vector_shift(Vector *vec)
{
  if (vec->len == 0)
    error_exit("Vector size is 0.");
  void *data = vec->data[0];
  if (vec->len > 1)
    memmove(&vec->data[0], &vec->data[1], sizeof(void *) * (vec->len - 1));
  vec->len--;
  return data;
}

void *vector_peek(Vector *vec)
{
  if (vec->len == 0)
    error_exit("Vector size is 0.");
  return vec->data[vec->len - 1];
}

// The specifiable location starts from 1
void *vector_peek_at(Vector *vec, size_t location)
{
  if (vec->len < location || location == 0)
    error_exit(
        "vector_peek_at() tried to access index %ld, but the vector contains "
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
  if (location < vec->len)
  {
    memmove(&vec->data[location - 1], &vec->data[location],
            sizeof(void *) * (vec->len - location));
  }
  vec->len--;
  return return_data;
}

void *vector_replace_at(Vector *vec, size_t location, void *data)
{
  if (vec->len < location || location == 0)
    error_exit(
        "vector_replace_at() tried to access index %lu, but the vector "
        "contains "
        "only %lu elements",
        location, vec->len);
  void *return_data = vec->data[location - 1];
  vec->data[location - 1] = data;
  return return_data;
}

// Returns true if the two argument vectors are equal
bool vector_compare(Vector *vec1, Vector *vec2)
{
  if (vector_size(vec1) != vector_size(vec2))
    return false;
  if (vector_size(vec1) == 0)
    return true;
  return memcmp(vec1->data, vec2->data, vector_size(vec1) * sizeof(void *)) ==
         0;
}

void vector_free(Vector *vec)
{
  if (vec->data)
    free(vec->data);
  free(vec);
}
