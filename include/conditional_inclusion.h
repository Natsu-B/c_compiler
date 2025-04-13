#ifndef CONDITIONAL_INCLUSION_C_COMPILER
#define CONDITIONAL_INCLUSION_C_COMPILER

#include "vector.h"

typedef enum
{
  token_if,
  token_ifdef,
  token_ifndef,
} if_directive;

void conditional_inclusion(if_directive type, Vector *conditional_list);

#endif