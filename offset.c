#include "include/offset.h"

typedef struct offset offset;

struct offset
{
  offset *next;  // One level shallower nest
  size_t offset;
};

offset *current_offset;
size_t max_offset;

void init_nest()
{
  if (current_offset)
    free(current_offset);
  current_offset = calloc(1, sizeof(offset));
  max_offset = 0;
}

void offset_enter_nest()
{
  offset *new = calloc(1, sizeof(offset));
  new->next = current_offset;
  new->offset = current_offset->offset;
  current_offset = new;
}

void offset_exit_nest()
{
  offset *tmp = current_offset;
  current_offset = tmp->next;
  free(tmp);
}

size_t calculate_offset(size_t size, size_t align)
{
  current_offset->offset = (current_offset->offset + align - 1) / align * align;
  current_offset->offset += size;
  if (current_offset->offset > max_offset)
    max_offset = current_offset->offset;
  return current_offset->offset;
}

size_t get_max_offset()
{
  return max_offset;
}