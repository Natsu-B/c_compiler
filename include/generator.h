#ifndef GENERATOR_C_COMPILER
#define GENERATOR_C_COMPILER

#include "common.h"

void generator(IRProgram *program, char *output_filename);

#define error_exit_with_guard(fmt, ...)                                     \
  do                                                                        \
  {                                                                         \
    output_file("error detected at %s:%d:%s() exit...", __FILE__, __LINE__, \
                __func__);                                                  \
    error_exit(fmt, ##__VA_ARGS__);                                         \
  } while (0)

#if DEBUG
#define output_debug(fmt, ...) \
  output_file("# %s:%d:%s()" fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__);
#define output_debug2(fmt, ...) \
  output_file("# %s:%d:%s()" fmt, __FILE__, __LINE__, __func__, ##__VA_ARGS__);
#elif defined(DEBUG)
#define output_debug(fmt, ...) output_file("# " fmt, ##__VA_ARGS__);
#define output_debug2(fmt, ...) output_file("# " fmt, ##__VA_ARGS__);
#else
#define output_debug(fmt, ...)
#define output_debug2(fmt, ...)
#endif

#define output_file(fmt, ...)               \
  do                                        \
  {                                         \
    fprintf(fout, fmt "\n", ##__VA_ARGS__); \
  } while (0)

extern FILE *fout;

#endif  // GENERATOR_C_COMPILER