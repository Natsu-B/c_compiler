#ifndef _MY_COMPILER_HEADER__
#define _MY_COMPILER_HEADER__

#define bool _Bool
#define true 1
#define false 0
#define NULL ((void *)0)

typedef unsigned long size_t;

typedef long time_t;
struct tm
{
  int tm_sec;
  int tm_min;
  int tm_hour;
  int tm_mday;
  int tm_mon;
  int tm_year;
  int tm_wday;
  int tm_yday;
  int tm_isdst;
  long int tm_gmtoff;
  char *tm_zone;
};

typedef __builtin_va_list __gnuc_va_list;
typedef __gnuc_va_list va_list;
#define va_start(v, l) __builtin_va_start(v, l)
#define va_end(v) __builtin_va_end(v)
#define va_copy(d, s) __builtin_va_copy(d, s)
#define va_arg(ap, x) __builtin_va_arg(ap, x)
int *__errno_location();
#define errno (*__errno_location())

struct _IO_FILE;
typedef struct _IO_FILE FILE;
#ifndef __MYCC__
extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;
#endif
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

int memcmp(void *s1, void *s2, size_t n);
void *memcpy(void *dest, void *src, size_t n);
void *memmove(void *dest, void *src, size_t n);
int strcmp(char *s1, char *s2);
int strncmp(char *s1, char *s2, size_t n);
size_t strlen(char *s);
char *strcpy(char *dst, char *src);
char *strncpy(char *dst, char *src, size_t dsize);
char *strchr(char *s, int c);
char *strstr(char *haystack, char *needle);
void *malloc(size_t size);
void free(void *ptr);
void *calloc(size_t n, size_t size);
void *realloc(void *ptr, size_t size);
int printf(char *fmt, ...);
int fprintf(FILE *stream, char *fmt, ...);
int vfprintf(FILE *stream, char *format, va_list ap);
int snprintf(char *str, size_t size, char *fmt, ...);
long strtol(char *nptr, char **endptr, int base);
long long strtoll(char *nptr, char **endptr, int base);
void exit(int status);
FILE *fopen(char *pathname, char *mode);
int fclose(FILE *stream);
size_t fread(void *ptr, size_t size, size_t nmemb, FILE *stream);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
char *strerror(int errnum);
int isspace(int c);
int isdigit(int c);
time_t time(time_t *timer);
struct tm *localtime(time_t *timer);
char *asctime(struct tm *tp);

#endif