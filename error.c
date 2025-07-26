// ------------------------------------------------------------------------------------
// error printer
// ------------------------------------------------------------------------------------

#include "include/error.h"

#include "include/debug.h"
#include "include/parser.h"

#ifdef SELF_HOST
#include "test/compiler_header.h"
#else
#include <error.h>
#include <execinfo.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // Used for signal write
#endif

#ifndef __GNUC__
#define __attribute__(x) /*NOTHING*/
#endif
void _debug2(char *file, int line, const char *func, char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
void _error_at(char *error_location, size_t error_len, char *file, int line,
               const char *func, char *fmt, ...)
    __attribute__((format(printf, 6, 7)));

// Called by pr_debug when DEBUG is defined
void _debug(char *file, int line, const char *func, char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  fprintf(stdout, "\e[35m");
  fprintf(stdout, "%s:%d:%s(): ", file, line, func);
  vfprintf(stdout, fmt, ap);
  fprintf(stdout, "\n");
  fprintf(stdout, "\e[37m");
}

// Called by pr_debug2 when DEBUG is defined
void _debug2(char *file, int line, const char *func, char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  fprintf(stdout, "\e[90m  ");  // Gray color and indent
  fprintf(stdout, "%s:%d:%s(): ", file, line, func);
  vfprintf(stdout, fmt, ap);
  fprintf(stdout, "\e[0m\n");  // Reset color
}

char *file_name;
char *user_input;

void error_init(char *name, char *input)
{
  file_name = name;
  user_input = input;
}

// Function to output logs and exit on error, called from error
void _error(char *file, int line, const char *func, char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "\e[31m[ERROR] \e[37m");
  _debug2(file, line, func, fmt, ap);
  fprintf(stderr, "\nError while compiling %s\n", file_name);
  exit(1);
}

void _info_at_(size_t log_level, char *location, size_t len, char *file,
               int line, const char *func, char *fmt, va_list ap)
{
  char *color_ansi;
  char *default_ansi = "\e[37m";
  char *info;
  switch (log_level)
  {
    case 0:  // error red
      color_ansi = "\e[31m";
      info = "error";
      break;
    case 1:  // warning yellow
      color_ansi = "\e[33m";
      info = "warning";
      break;
    default: unreachable(); break;
  }
  fprintf(stderr, "%s%s%s\n", color_ansi, info, default_ansi);
  if (len)
  {  // Identify the line of error_location
    // Line start
    char *start_line = location;
    while (user_input < start_line && start_line[-1] != '\n')
      start_line--;
    // Line end
    char *end_line = location;
    while (*end_line != '\n')
      end_line++;
    // Line number
    int line_num = 1;
    for (char *p = user_input; p < start_line; p++)
      if (*p == '\n')
        line_num++;
    // Error position identification
    size_t error_position = location - start_line;
    fprintf(stderr, "%s:%d\n", file_name, line_num);
    fprintf(stderr, "%.*s\n", (int)(end_line - start_line), start_line);
    fprintf(stderr, "%*s", (int)error_position, " ");
    fprintf(stderr, "%s%.*s%s", color_ansi, (int)len,
            "^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~"
            "~~~~~~~~~~~~~~~~~~~~",
            default_ansi);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n%s at %s:%d:%s\n", info, file, line, func);
  }
  else
  {
    fprintf(stderr, "%s\n", file_name);
    fprintf(stderr, "\n%s at %s:%d:%s\n", info, file, line, func);
  }
}

void _error_at(char *error_location, size_t error_len, char *file, int line,
               const char *func, char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  _info_at_(0, error_location, error_len, file, line, func, fmt, args);

#ifndef INTERPRETER
  printf("\n\n--- BackTrace ---\n\n\n");
  print_parse_result(get_funcblock_head());
#endif
  exit(1);
}

// Program to visualize the location when the input program is incorrect.
// If log_level is 0, an error is displayed and the program exits; if 1, a
// warning is displayed and control returns to the original function.
void _info_at(size_t log_level, char *location, size_t len, char *file,
              int line, const char *func, char *fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  _info_at_(log_level, location, len, file, line, func, fmt, args);
}

#ifndef SELF_HOST
#include "include/tokenizer.h"

// Displays backtrace when a signal occurs
void handle_signal(int signum, siginfo_t *info, void *ucontext)
{
  (void)ucontext;
  char *msg_header =
      "\n\n\e[31m--- Segmentation Fault Detected !!! ---\n\n\e[37m";
  write(STDERR_FILENO, msg_header, strlen(msg_header));

  char buffer[256];  // Output buffer
  int len;

  // Display signal number
  len = snprintf(buffer, sizeof(buffer), "Signal: %d\n", signum);
  write(STDERR_FILENO, buffer, len);

  // Display error address (especially useful for SIGSEGV)
  if (info->si_addr != NULL)
  {
    len = snprintf(buffer, sizeof(buffer), "Faulting address: %p\n",
                   info->si_addr);
    write(STDERR_FILENO, buffer, len);
  }

  // Display error code
  len = snprintf(buffer, sizeof(buffer), "Error code (si_code): %d\n",
                 info->si_code);
  write(STDERR_FILENO, buffer, len);

  // Displays stack trace (GNU extension)
  const char *backtrace_msg = "--- Backtrace (Stack Trace) ---\n";
  write(STDERR_FILENO, backtrace_msg, strlen(backtrace_msg));

  void *callstack[128];
  int frames = backtrace(callstack, sizeof(callstack) / sizeof(void *));
  char **strs = backtrace_symbols(callstack, frames);

  if (strs != NULL)
  {
    for (int i = 0; i < frames; ++i)
    {
      write(STDERR_FILENO, strs[i], strlen(strs[i]));
      write(STDERR_FILENO, "\n", 1);
    }
    free(strs);  // backtrace_symbols dynamically allocates memory, so free it
  }
  else
  {
    const char *no_backtrace_msg = "Could not get backtrace.\n";
    write(STDERR_FILENO, no_backtrace_msg, strlen(no_backtrace_msg));
  }

  #ifndef INTERPRETER
  // Display global variables and token stream as much as possible at that time
  const char *print_progress = "\n\n\e[31m--- Program Progress ---\e[37m\n\n";
  write(STDERR_FILENO, print_progress, strlen(print_progress));
  const char *file_name_header = "File Name: ";
  write(STDERR_FILENO, file_name_header, strlen(file_name_header));
  write(STDERR_FILENO, file_name, strlen(file_name));
  write(STDERR_FILENO, "\n", 2);
  const char *token_print = "token: ";
  write(STDERR_FILENO, token_print, strlen(token_print));
  Token *token = get_token();
  for (int i = 0; i < 30; i++)
  {
    if (!token)
      break;
    len = snprintf(buffer, sizeof(buffer), "%.*s", (int)token->len, token->str);
    write(STDERR_FILENO, buffer, len);
    token = token->next;
  }
#endif
  const char *msg_footer = "\n\n\e[31m--- Program Aborting ---\e[37m\n\n";
  write(STDERR_FILENO, msg_footer, strlen(msg_footer));

  // exit cannot be used in interrupts because it is not reentrant
  _exit(EXIT_FAILURE);
}
#endif