// ------------------------------------------------------------------------------------
// error printer
// ------------------------------------------------------------------------------------

#include "include/error.h"
#include <stdarg.h>
#include <stdio.h>
#include <error.h>
#include <stdlib.h>

#ifndef __STDC__
#define __attribute__(x) /*NOTHING*/
#endif
void _debug2(char *file, int line, const char *func, char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
void error_at(char *error_location, size_t error_len, char *fmt, ...)
    __attribute__((format(printf, 3, 4)));

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

// エラー時にログを出力し、終了する関数 errorから呼び出される
[[noreturn]]
void _error(char *file, int line, const char *func, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "\e[31m[ERROR] \e[37m");
    _debug2(file, line, func, fmt, ap);
    exit(1);
}

char *file_name;
char *user_input;

void error_init(char *name, char *input)
{
    file_name = name;
    user_input = input;
}

// 入力プログラムがおかしいとき、エラー箇所を可視化するプログラム
[[noreturn]]
void error_at(char *error_location, size_t error_len, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    // error_locationの行を特定
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
    fprintf(stderr, "\n");
    exit(1);
}