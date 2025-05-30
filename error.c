// ------------------------------------------------------------------------------------
// error printer
// ------------------------------------------------------------------------------------

#include "include/error.h"

#include <error.h>
#include <execinfo.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>  // signalのwriteのために利用

#ifndef __GNUC__
#define __attribute__(x) /*NOTHING*/
#endif
void _debug2(char *file, int line, const char *func, char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
void _error_at(char *error_location, size_t error_len, char *file, int line,
               const char *func, char *fmt, ...)
    __attribute__((format(printf, 6, 7)));

// #define DEBUG 時に動作を出力する pr_debugから呼び出される
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

// #define DEBUG 時に動作を出力する pr_debug2から呼び出される
void _debug2(char *file, int line, const char *func, char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  fprintf(stdout, "%s:%d:%s(): ", file, line, func);
  vfprintf(stdout, fmt, ap);
  fprintf(stdout, "\n");
}

char *file_name;
char *user_input;

void error_init(char *name, char *input)
{
  file_name = name;
  user_input = input;
}

// エラー時にログを出力し、終了する関数 errorから呼び出される
[[noreturn]]
void _error(char *file, int line, const char *func, char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  fprintf(stderr, "\e[31m[ERROR] \e[37m");
  _debug2(file, line, func, fmt, ap);
  fprintf(stderr, "\n error while compiling %s\n", file_name);
  exit(1);
}

// 入力プログラムがおかしいとき、エラー箇所を可視化するプログラム
[[noreturn]]
void _error_at(char *error_location, size_t error_len, char *file, int line,
               const char *func, char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  if (error_len)
  {  // error_locationの行を特定
    // 行開始
    char *start_line = error_location;
    while (user_input < start_line && start_line[-1] != '\n')
      start_line--;
    // 行終了
    char *end_line = error_location;
    while (*end_line != '\n')
      end_line++;
    // 何行目か
    int line_num = 1;
    for (char *p = user_input; p < start_line; p++)
      if (*p == '\n')
        line_num++;
    // エラー位置特定
    size_t error_position = error_location - start_line;
    fprintf(stderr, "%s:%d\n", file_name, line_num);
    fprintf(stderr, "%.*s\n", (int)(end_line - start_line), start_line);
    fprintf(stderr, "%*s", (int)error_position, " ");
    fprintf(stderr, "\e[31m%.*s \e[37m", (int)error_len, "^");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n error at %s:%d:%s\n", file, line, func);
  }
  else
  {
    fprintf(stderr, "%s\n", file_name);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n error at %s:%d:%s\n", file, line, func);
  }
  exit(1);
}

// signal が発生したときにバックトレースを出す
void handle_signal(int signum, siginfo_t *info, void *ucontext)
{
  (void)ucontext;
  char *msg_header =
      "\n\n\e[31m--- Segmentation Fault Detected !!! ---\n\n\e[37m";
  write(STDERR_FILENO, msg_header, strlen(msg_header));

  char buffer[256];  // 出力用バッファ
  int len;

  // シグナル番号の表示
  len = snprintf(buffer, sizeof(buffer), "Signal: %d\n", signum);
  write(STDERR_FILENO, buffer, len);

  // エラーアドレスの表示 (SIGSEGVの場合に特に有用)
  if (info->si_addr != NULL)
  {
    len = snprintf(buffer, sizeof(buffer), "Faulting address: %p\n",
                   info->si_addr);
    write(STDERR_FILENO, buffer, len);
  }

  // エラーコードの表示
  len = snprintf(buffer, sizeof(buffer), "Error code (si_code): %d\n",
                 info->si_code);
  write(STDERR_FILENO, buffer, len);

  // スタックトレースの表示（GNU拡張）
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
    free(strs);  // backtrace_symbols は動的にメモリを確保するので解放する
  }
  else
  {
    const char *no_backtrace_msg = "Could not get backtrace.\n";
    write(STDERR_FILENO, no_backtrace_msg, strlen(no_backtrace_msg));
  }

  // その時のグローバル変数やトークン列をできる限り表示
  const char *print_progress = "\n\n\e[31m--- Program Progress ---\e[37m\n\n";
  write(STDERR_FILENO, print_progress, strlen(print_progress));
  const char *file_name_header = "File Name: ";
  write(STDERR_FILENO, file_name_header, strlen(file_name_header));
  write(STDERR_FILENO, file_name, strlen(file_name));
  write(STDERR_FILENO, "\n", 2);

  const char *msg_footer = "\n\n\e[31m--- Program Aborting ---\e[37m\n\n";
  write(STDERR_FILENO, msg_footer, strlen(msg_footer));

  // 割り込みではリエントラントでないのでexitが使えない
  _exit(EXIT_FAILURE);
}