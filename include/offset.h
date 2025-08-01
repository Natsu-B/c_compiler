#ifndef OFFSET_C_COMPILER
#define OFFSET_C_COMPILER

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
#include <stdlib.h>
#endif

void init_nest();
void offset_enter_nest();
void offset_exit_nest();
size_t calculate_offset(size_t size, size_t align);
size_t get_max_offset();

#endif