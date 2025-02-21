#ifndef ERROR_C_COMPILER
#define ERROR_C_COMPILER

//#define DEBUG 1

// デバッグ用のprintf管理
// DEBUG が定義されてなければ何も出力されず、
// DEBUG 0 ならpr_debugのみ出力され
// DEBUG 1 ならpr_debug、 pr_debug2ともに出力される
// pr_debug2 の方は長い出力も行う
#ifdef DEBUG
#define pr_debug(fmt, ...) \
    _debug(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)
#if DEBUG
#define pr_debug2(fmt, ...) \
    pr_debug(fmt, ##__VA_ARGS__)
#endif // DEBUG=(true)
#else
#define pr_debug(fmt, ...)
#endif // DEBUG
#ifndef pr_debug2
#define pr_debug2(fmt, ...)
#endif // pr_debug2

#define error_exit(fmt, ...) \
    _error(__FILE__, __LINE__, __func__, fmt, ##__VA_ARGS__)

extern void _debug(char *file, int line, const char *func, char *fmt, ...);

extern void _error(char *file, int line, const char *func, char *fmt, ...);

extern void error_init(char *input);

extern void error_at(char *error_location, char *fmt, ...);

#endif // ERROR_C_COMPILER