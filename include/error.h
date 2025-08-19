#ifndef ERROR_C_COMPILER
#define ERROR_C_COMPILER

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
// #define _GNU_SOURCE
#include <signal.h>
#include <stddef.h>
#endif

#define DEBUG 1

// Debug printf management
// If DEBUG is not defined, nothing is output.
// If DEBUG is 0, only pr_debug is output.
// If DEBUG is 1, both pr_debug and pr_debug2 are output.
// pr_debug2 outputs longer messages.
#ifdef DEBUG
#define pr_debug(fmt, ...) \
  _debug(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#if DEBUG
#define pr_debug2(fmt, ...) \
  _debug2(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#endif  // DEBUG=(true)
#else
#define pr_debug(fmt, ...)
#endif  // DEBUG
#ifndef pr_debug2
#define pr_debug2(fmt, ...)
#endif  // pr_debug2

#define unreachable() error_exit("unreachable")
#define unimplemented() error_exit("unimplemented")

#define error_exit(fmt, ...) \
  _error(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

#define error_at(error_location, error_len, fmt, ...)                     \
  _error_at(error_location, error_len, __FILE__, __LINE__, __func__, fmt, \
            ##__VA_ARGS__)

#define warn_at(warning_location, warning_len, fmt, ...)                   \
  _info_at(1, warning_location, warning_len, __FILE__, __LINE__, __func__, \
           fmt, ##__VA_ARGS__)

void _debug(char *file, int line, const char *func, char *fmt, ...);
void _debug2(char *file, int line, const char *func, char *fmt, ...);

void _error(char *file, int line, const char *func, char *fmt, ...);

void error_init(char *name, char *input);
void _error_at(char *error_location, size_t error_len, char *file, int line,
               const char *func, char *fmt, ...);
void _info_at(size_t log_level, char *location, size_t len, char *file,
              int line, const char *func, char *fmt, ...);

#ifndef SELF_HOST
void handle_signal(int signum, siginfo_t *info, void *ucontext);
#endif

#endif  // ERROR_C_COMPILER