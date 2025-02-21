// ------------------------------------------------------------------------------------
// error printer
// ------------------------------------------------------------------------------------

#include "include/error.h"
#include <stdarg.h>
#include <stdio.h>
#include <error.h>
#include <stdlib.h>

// #define DEBUG 時に動作を出力する pr_debugから呼び出される
void _debug(char *file, int line, const char *func, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stdout, "%s:%d:%s(): ", file, line, func);
    vfprintf(stdout, fmt, ap);
    fprintf(stdout, "\n");
}

// エラー時にログを出力し、終了する関数 errorから呼び出される
void _error(char *file, int line, const char *func, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    fprintf(stderr, "[ERROR] %s:%d:%s(): ", file, line, func);
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}

char *user_input;

void error_init(char *input)
{
    user_input = input;
}

// 入力プログラムがおかしいとき、エラー箇所を可視化するプログラム
void error_at(char *error_location, char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    // エラー位置特定
    int error_position = error_location - user_input;
    fprintf(stderr, "%s", user_input);
    fprintf(stderr, "%*s", error_position, " ");
    fprintf(stderr, "^ ");
    vfprintf(stderr, fmt, ap);
    fprintf(stderr, "\n");
    exit(1);
}