#ifndef ERROR_C_COMPILER
#define ERROR_C_COMPILER

#ifdef SELF_HOST
#include "../test/compiler_header.h"
#else
// #define _GNU_SOURCE
#include <signal.h>
#include <stddef.h>
#endif

#define DEBUG 0

// Debug printf management
// If DEBUG is not defined or 0, nothing is output.
// If DEBUG is 1, both pr_debug and pr_debug2 are output.
// pr_debug2 outputs longer messages.
#if DEBUG
#define pr_debug(...) _debug(__FILE__, __LINE__, __func__, __VA_ARGS__)
#define pr_debug2(...) _debug2(__FILE__, __LINE__, __func__, __VA_ARGS__)
#else
#define pr_debug(fmt, ...) ((void)0)
#define pr_debug2(fmt, ...) ((void)0)
#endif  // DEBUG

#define unreachable() error_exit("unreachable")
#define unimplemented() error_exit("unimplemented")

#define error_exit(...) _error(__FILE__, __LINE__, __func__, __VA_ARGS__)

#define error_at(error_location, error_len, ...)                          \
  _error_at(error_location, error_len, __FILE__, __LINE__, __func__,      \
            __VA_ARGS__)

#define warn_at(warning_location, warning_len, ...)                        \
  _info_at(1, warning_location, warning_len, __FILE__, __LINE__, __func__, \
           __VA_ARGS__)

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
