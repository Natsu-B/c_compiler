#!/bin/sh
COMPILER=${1:-./main}

show_compiler_output_and_exit() {
  local message="$1"
  local stdout_file="$2"
  local input_code="$3"

  if [ -s "$stdout_file" ]; then
    echo "--- Compiler (./main) debug stdout ---"
    cat "$stdout_file"
    echo "--------------------------------------"
  fi

  echo
  echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"
  echo "!! TEST FAILED for input: '$input_code'"
  echo "!! Reason: $message"
  echo "!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!"

  exit 1
}

assert_print() {
  input="$1"
  local compiler_stdout="out/compiler.stdout"

  echo "$input" > out/tmp.c
  if ! gcc -I./test -include gcc.h -o out/gcc out/tmp.c; then
    echo "ERROR: Reference build with GCC failed for input: '$input'"
    exit 1
  fi
  ./out/gcc > out/gcc.txt
  expected_exit_code="$?"

  if ! "$COMPILER" -i out/tmp.c -o out/out.s > "$compiler_stdout"; then
    show_compiler_output_and_exit "COMPILATION FAILED" "$compiler_stdout" "$input"
  fi

  if ! gcc -z noexecstack -o out/out out/out.s; then
    show_compiler_output_and_exit "LINKING FAILED" "$compiler_stdout" "$input"
  fi

  ./out/out > out/out.txt
  actual_exit_code="$?"

  if [ "$actual_exit_code" != "$expected_exit_code" ]; then
    show_compiler_output_and_exit "EXIT CODE MISMATCH: Expected $expected_exit_code, but got $actual_exit_code" "$compiler_stdout" "$input"
  fi
  if ! cmp -s "out/out.txt" "out/gcc.txt"; then
    show_compiler_output_and_exit "OUTPUT MISMATCH" "$compiler_stdout" "$input"
  fi

  echo "$input => $actual_exit_code"
}

assert() {
  input="$1"
  local compiler_stdout="out/compiler.stdout"

  echo "$input" > out/tmp.c

  gcc -I./test -include gcc.h -o out/gcc out/tmp.c
  ./out/gcc
  expected="$?"

  if ! "$COMPILER" -i out/tmp.c -o out/out.s > "$compiler_stdout"; then
    show_compiler_output_and_exit "COMPILATION FAILED" "$compiler_stdout" "$input"
  fi

  if ! gcc -z noexecstack -o out/out out/out.s; then
    show_compiler_output_and_exit "LINKING FAILED" "$compiler_stdout" "$input"
  fi

  ./out/out
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    show_compiler_output_and_exit "$expected expected, but got $actual" "$compiler_stdout" "$input"
  fi
}

assert_with_outer_code() {
  input="$1"
  shift
  linkcode=("$@")
  local compiler_stdout="out/compiler.stdout"

  echo "$input" > out/tmp.c

  gcc -I./test -include gcc.h -o out/gcc out/tmp.c "${linkcode[@]}"
  ./out/gcc
  expected="$?"

  if ! "$COMPILER" -i out/tmp.c -o out/out.s > "$compiler_stdout"; then
    show_compiler_output_and_exit "COMPILATION FAILED" "$compiler_stdout" "$input"
  fi

  if ! gcc -z noexecstack -o out/out out/out.s "${linkcode[@]}"; then
    show_compiler_output_and_exit "LINKING FAILED" "$compiler_stdout" "$input"
  fi

  ./out/out
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    show_compiler_output_and_exit "$expected expected, but got $actual" "$compiler_stdout" "$input"
  fi
}

mkdir -p out

(
  cd test
  make
)
assert 'int main() {return 0;}'
assert 'int main() {return 42; }'
assert 'int main ( ) {return 10-1;}'
assert 'int main () {return 9+8+5-4;}'
assert 'int main () {return 10 -9 + 5;}'
assert 'int main () {return 10 / (4+ (3 - 2)) ;}'
assert 'int main(){return 1* 8 * (4 / 2);}'
assert 'int main(){return -1 + 2;}'
assert 'int main(){return +9+(-1)+2;}'
assert  'int main(){return -(+1-2);}'
assert 'int main(){return 0 == 1;}'
assert 'int main(){return 0 ==0;}'
assert 'int main() {return 0 != 0;}'
assert 'int main() {return 0 != 1;}'
assert 'int main() {return 1 < 1;}'
assert 'int main() {return 0 < 1;}'
assert 'int main() {return 10 <= 1;}'
assert 'int main() {return 1 <= 1;}'
assert 'int main(){ return   0 > 1;}'
assert 'int main() { return 0 <= 1;}'
assert 'int main() {return 1 > 1;}'
assert 'int main() {return 1 > 0;}'
assert 'int main() {return 0 >= 1;}'
assert 'int main() {return 1 >= 1;}'
assert 'int main() {int a= 5; int b = -1;return a+b;}'
assert 'int main() {return 10 >= 1 ;}'
assert 'int main() {int a=5; int b=2; return a*b/2+5;}'
assert 'int main () {int a = 1;return 1;}'
assert 'int main() {int FOGE = 10; int HUGA = 5; int PIYO = FOGE / HUGA; return PIYO + 1;}'
assert 'int main() {int foge = 1; int huga = 2; int piyo = 1; return foge * huga - piyo;}'
assert 'int main() {int a=5; int b=2; int c=a*b/2+5;return c;}'
assert 'int main() {int a = 2; int b = 3; int a_b = a +b; return a_b; a * 2;}'
assert 'int main() {int returne = 1; return returne + 1;}'
assert 'int main() {if (0) return 10; else return 6;}'
assert 'int main(){if(1) return 10; return 6;}'
assert 'int main(){int j=0;for(int i=0;i < 10; i = i+1) j = j+i; return j;}'
assert 'int main(){int i=0;int j=1;while(i != 10) i = i+j+1; return i;}'
assert 'int main(){{} return 10;}'
assert 'int main(){int i=0; int j=1; while(i != 10) {j = j+i; i = i+1;} return j;}'
assert 'int return_val() {return 5;}int main() {return return_val();}'
assert 'int fibonacci(int a, int b) {return a+b;}int main() {int x = 0; int y = 1; int i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); y = fibonacci(x, y); } return y; }'
assert_with_outer_code 'int main() {int i = func();return i;}' './test/include_test.o'
assert_with_outer_code 'int main() {int x = 0;int y = 1; int i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); if (x != func1(x)){ return 1;} y = fibonacci(x, y); if (y != func1(y)) {return 1;}} return 0; }' './test/print_u64.o' './test/fibonacci.o'
assert_with_outer_code 'int fibonacci(int a,int b) {return a+b;}int main() {int x = 0; int y = 1; int i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); if (x != func1(x)) return 0; y = fibonacci(x, y); if (y != func1(y)) return 1; } return 0; }' './test/print_u64.o'
assert 'int main() {int i = 4;int *x = &i; return *x;}'
assert 'int main() {int K = 6; return *&K;}'
assert_with_outer_code 'int main() {int x = 0; for(int i= 0; i < 10 ; i =i+1) { x = x+ i; if (x !=func1(x)) return 1; }return 5;}' './test/print_u64.o'
assert 'int main() {int x = 3; int *y = &x; *y = 0; return x;}'
assert 'int main() {int x = 0; int *y = &x; int **z = &y;*y = 2; **z = 3; return x;}'
assert_with_outer_code 'int main() {int *p; alloc(&p, 1, 3); int *q = p+1; return *q;}' './test/alloc2.o'
assert_with_outer_code 'int main() {int *p; alloc(&p, 1, 3); int *q = p + 1; q = q-1; return *q;}' './test/alloc2.o'
assert 'int main() {int x; long z; if (sizeof(x) != 4) return 1; int* y; if (sizeof(y) != 8) return 1; if (sizeof(*y) != 4) return 1; if (sizeof(x + 1) != 4) return 1; if (sizeof(z) != 4) return 1; if (sizeof(sizeof(1)) != 8)return 1;  return 0;}'
assert 'int main() {int x[2]; int y; long z; *(x+1) = 1; *x = 2; y = 5; return (y - *(x+1)) / *x;}'
assert 'int main() {int x[2]; int y = 1; x[0] = y; x[1] = y + 1; return x[1] + x[0];}'
assert 'int main() {int i = 0; {int i = 1; return i;}}'
assert 'int main() {int i = 0; {int x = 1;} return i;}'
assert 'int main() {int i = 0; {int i = 1; } return i;}'
assert 'int i; int main() {int i = 5; return i;}'
assert 'int i; int main() {int j = 5; return i;}'
assert 'int i; int main() {i= 5; return i;}'
assert 'int main() {long i = 0; i = i+ 1; return i;}'
assert 'int main() {char i; i= 3; return i;}'
assert 'int main() {char i[4]; i[3] = 10; i[2] = 0; int j = 2; return i[2] + i[3] +j;}'
assert 'int main() {char i; char a = 100; char b = 3; char c = 4; i = a * b /c; return i;}'
assert 'int hoge; int main() {int x = 2; x = hoge; return x;}'
assert 'int main() {char *i = "abc"; return i[0];}'
assert 'int main() {char*i = "abc"; return i[2];}'
assert 'int i; int main() {int i = 1; {int i = 2; {int i = 3; return i;}}}'
assert 'int i; int main() {int i = 1; {int i = 2; {int i = 3;} return i;}}'
assert 'int i; int main() {int i = 1; {int i = 2; {int i = 3;}} return i;}'
assert_print 'int main() {char* hoge = "%s\n"; char* tmp = "Hello World!!!"; printf(hoge, tmp); return 0;}'
assert_print 'int main() {char* tmp = "Hello World!!!"; printf("%s\n", tmp); return 0;}'
assert 'int main() {int hoge = 10; hoge /* = 0*/; return hoge; }'
assert 'int main() {int hoge = 15; // hoge = 10;
return hoge;}'
assert 'int x; int y; int main() {return x;}'
assert '#include <stdbool.h>
short h; int main() { short i; bool j; if (sizeof(h) != 2) return 0; if (sizeof(i) != 2) return 1; /*if (sizeof(j) != 1) return 2;*/ return 10; }'
assert '#include <stdbool.h>
bool f; int main() {if (f) return 1; f = 1; if (f) f; else return 2; f = f + 1; if (f) f; else return 4; return 3;}'
assert 'typedef int hoge; int main() {hoge a = 1; return a;}'
assert 'int main() { typedef long long fuga; fuga x; return sizeof(x); }'
assert 'typedef int *hoge; int main() {int y; hoge x = &y; *x = 1; return *x;}'
assert 'typedef int hoge[2]; int main() {hoge x; x[1] = 10; x[0] = 2; return x[1];}'
assert 'struct HOGE { int x; int y; }; int main() {struct HOGE x; return sizeof(x);}'
assert '#include <stdbool.h>
struct HOGE { int x; int y; }; struct FUGA { bool x; bool y; }; typedef struct FUGA HOGE; int main() {HOGE x; return sizeof(x);}'
assert 'typedef int hoge; int b; int main() {hoge* a = &b; *a = 1; return *a;}'
assert '#include <stdbool.h>
struct HOGE { int x; int y; }; struct FUGA { bool x; bool y; }; typedef struct FUGA HOGE; int main() {HOGE x; return sizeof(x.x);}'
assert 'int main() { int x = 0; for(int i = 0; i <= 10; i = i+ 1) { x = x + i; if (x > 10) break; } return x;}'
assert 'int main() { int x = 0; goto end; { x = 1; } x = 2; return x; end: return x; }'
assert 'enum tmp { a, b, c = 8, d,}; int main() {return b + d;}'
assert 'enum tmp { a, b = a + 1, c = 2 * 2, d,}; int main() {return b + d;}'
assert 'int main () { int x = 0; int y = 0; return ++x + y++;}'
assert 'int main () { int x = 0; int y = 0; return x-- - --y;}'
assert 'int main() {int x = 0; switch (x) { case 0: x = x + 10; break; case 1: x--; break;default: break;} return x;}'
assert 'int main() {int x = 0; switch (x) { case 0: x = x + 10; break; case 1: x--; break;default: break;} switch(x) {case 0: return 255; default: return x;}}'
assert 'int main() {int x = 1; void *y = &x; return 0;}'
assert 'int main() {int i = 0; do {i =i+ 5;}while(0); return i;}'
assert 'int main(){int r=0;int i=0;for(;i<1;i++)if(1)while(r==0)do{r++;if(r<3)continue;r=r+5;if(r>7)break;}while(r<10);return r;}'
assert 'int main() {int k[3]; k[0] = 0; k[1] = 1; k[2] = 2; int *ptr = k; int t = *++ptr; int s = *ptr++; return t + s;}'
assert 'struct HOGE; struct HOGE { int x; int y; }; int main() {struct HOGE x; return sizeof(x);}'
assert 'enum tmp; enum tmp { a, b, c = 8, d,}; int main() {return b + d;}'
assert 'int main() {int x =0; return x ? 10 : 1;}'
assert 'int y() { exit(11); return 0;} int a() {exit(2); return 1;} int main() {int x = 1; int z = 0; if (x || y()) if (z && a()) return 5; else return 10;}'
assert 'int main() { int x = 1; if (x | 0) x++; if (x&0) x--; if (x^1) x = x + 10; return x + 5;}'
assert 'int main() { int x = 10; x = x << 5; x = x >> 1; return x;}'
assert 'int main() {int x= 1; x += 100; x -= 2; x/=3; x <<= 1; x >>= 2; x*= 10; return x;}'
assert 'int main() {int x =111; x %= 5; int y = 199; x += y % 9; return x;}'
assert 'int add(int a, int b); int main() {int i = 0; int j = 1; return add(i, j);} int add(int a, int b) {return a + b;}'
assert 'int sub(int, int); int main() {int sub(int,int); int i = 100; int y = 90; return sub (i, y);} int sub(int a, int b) {return a - b;}'
assert 'enum tmp {tmp1, tmp2, tmp3, tmp4, tmp5, tmp6, tmp7, tmp8, tmp9, tmp10 }; int main() {return tmp8;}'
assert 'int foo(); int main() { return foo(); } int foo() { return 123; }'
assert_with_outer_code 'int main() { int x = piyo(1, 2, 3, 4, 5, 6, 7, 8); return x; }' './test/piyo.o'
assert_with_outer_code 'int main() { int g = 7; int x = piyo(1, 2, 3, 4, 5, 6, g, 8); return x; }' './test/piyo.o'
assert_print 'int hoge(int a, int b, int c, int d, int e, int f, int g, int h){if (g!=7||h!=8)return 1;printf("%d,%d,%d,%d,%d,%d,%d,%d",a,b,c,d,e,f,g,h);return h;} int main() { int g = 7; int x = hoge(1, 2, 3, 4, 5, 6, g, 8); return x; }'
assert_print 'int hoge(int a, int b, int c, int d, int e, int f, int g, int h){if (g!=7||h!=8)return 1; printf("%d,%d,%d,%d,%d,%d,%d,%d",a,b,c,d,e,f,g,h);return g;} int main() { int g = 7; int x = hoge(1, 2, 3, 4, 5, 6, g, 8); return x; }'
assert 'int main() { typedef int hoge; return sizeof(hoge); }'
assert 'int main() { typedef int hoge; return sizeof(hoge*); }'
assert 'int main() { typedef int hoge[2]; return sizeof(hoge); }'
assert 'int main() { typedef struct HOGE {int x; int y;} hoge; return sizeof(hoge); }'
assert 'int main() { typedef struct HOGE {int x; int y;} hoge; return sizeof(hoge*); }'
assert 'int main() { return sizeof(int); }'
assert 'int main() { return sizeof(int*); }'
assert 'int main() { return sizeof(int**) + 1; }'
assert 'int main() { return sizeof(int[2]) + 2; }'
assert 'int main() { struct HOGE {int x; int y;}; return sizeof(struct HOGE); }'
assert 'int main() { return sizeof(int (*[10])(int, int)); }'
assert 'typedef long hoge; hoge x;int main() { typedef int hoge; return sizeof(hoge) + sizeof(x); }'
assert 'typedef long hoge; hoge x;int main() { typedef hoge fuga; return sizeof(fuga) + sizeof(x); }'
assert 'int main() { return sizeof(int[2][3]); }'
assert 'typedef int foo; int main(){ foo(i); i = 999; return i; }'
assert 'struct tmp {int hoge; long piyo; long x; long y; long z; long a; long b; long c;}; int main() { struct tmp x; x.hoge = 199; x.piyo = 100; return x.hoge; }'
assert 'struct tmp {int hoge; long piyo; long x; long y; long z; long a; long b; long c;}; int main() { struct tmp x; x.hoge = 199; x.piyo = 100; return x.hoge - x.piyo + sizeof(struct tmp); }'
assert 'struct tmp {int *hoge; long* piyo;}; int main() { int y; long z; struct tmp x; x.hoge = &y; *x.hoge = 199; x.piyo = &z; *x.piyo = 100; return *x.hoge - *x.piyo + sizeof(struct tmp); }'
assert 'int main() {int x = -1; x >>= 9; x <<=8; return x;}'
assert 'int main() {unsigned y = -1/* 4294967295 */; y >>= 30; y <<= 1; return y;}'
assert 'int main() {int x = -1; x >>= 9; x <<=8; unsigned y = -1; y >>= 30; y <<= 1; return x + y;}'
assert 'int main() {_Bool x = 1; _Bool tmp; _Bool *y = &tmp; *y = 10; if (memcmp(&x, y, 1)) return 100; return 89;}'
assert 'int main() {int a= 0; int b = 3; b += (a++, b); b += a++, b; b++, b++; return (a, b);}'
assert 'int main() {int x = 100; void* a = &x; return *(int*)a;}'
assert_print 'int printf(char *str, ...); int main() {for (int i = 0; i < 10; i++) printf("Hello World!!! %d\n", i); return 0;}'
assert 'int main() {int k[3]; for (int i = 0; i < 3; i++) k[i] = i; int *ptr = k; int t = *++ptr; int s = *ptr++; return t + s;}'
assert_print 'int foo(); int printf(char*tmp, ...); int main() {printf(__func__); return foo();} int foo() {return printf(__func__); }'
assert_print '#define HOGE "world!!!\n"
int foo();int printf(char*tmp, ...); int main() {printf("hello" "c" "world!!!"); return foo();} int foo() {return printf("hello" HOGE); }'
assert 'struct HOGE {int x;}; int main() {struct HOGE {int x;}; return sizeof(struct HOGE);}'
assert 'int foo; int main() {extern int foo; return foo;}'
assert "int main() {return 'a' + '\n' + '\0';}"
assert_print '__asm__("gcc_predef_start: \n"" .incbin \"include/gcc_predef.h\"\n""gcc_predef_end: \n");extern char gcc_predef_start;extern char gcc_predef_end; int main() {printf("%.*s", (int)(&gcc_predef_end - &gcc_predef_start), &gcc_predef_start); __asm__("nop"); return 0;}'
assert 'int foo; extern int foo;extern int foo; int main() {extern int foo; return foo;}'
assert 'extern int foo; int foo; extern int foo;extern int foo; int main() {extern int foo; return foo;}'
assert 'int a = 187; int *b = &a; char* c = "abc"; int main() {return a + *b + c[1];}'
assert '_Bool a = 187; int main() {return a;}'
assert 'int main () {int a[3][4];return sizeof(a);}'
assert 'int main () {int a[3][4];return sizeof(a[0][0]);}'
assert 'int main () {int a[3][4];return sizeof(a[1]);}'
assert 'int main () {int a[3][4];return sizeof(a) + sizeof(a[1]) + sizeof(a[0][0]);}'
assert 'int main () {int a[3][4]; a[1][2] = 10; a[2][3] = 99; return a[2][3] + sizeof(a);}'
assert 'int main () {int a[3][4]; a[1][2] = 10; a[2][3] = 99; return a[1][2] + sizeof(a);}'
assert 'int a[3][4];int main () {a[1][2] = 10; a[2][3] = 99; return a[2][3] + sizeof(a);}'
assert 'int main() {int a[3][4]; *(*(a+1)+2) = 10; return *(*(a+1)+2);}'
assert 'int main() {int a[3][4]; *(*(a+1)+2) = 10; return a[1][2];}'
assert 'int main() {int a[] = {1, 2, 3}; return a[0];}'
assert 'int main() {int a[] = {1, 2, 3}; return a[2];}'
assert 'int main() {int a[] = {1, 2, 3}; return sizeof(a);}'
assert 'int main() {int a[5] = {1, 2}; return a[0];}'
assert 'int main() {int a[5] = {1, 2}; return a[1];}'
assert 'int main() {int a[5] = {1, 2}; return a[2];}'
assert 'int main() {int a[2][5] = {{1, 2,3, 4, 5}, {4, 5},}; return sizeof(a);}'
assert 'int main() {int a[2][5] = {{1, 2,3, 4, 5}, {6, 7},}; return a[0][4];}'
assert 'int main() {int a[3][5] = {{1, 2,3, 4, 5}, {6, 7, 12, 13, 14},{11, 10, 9, 8}}; return a[2][1];}'
assert 'int main() {int a[2][3] = {{1, 2,3}, {4, 5},}; return a[0][2];}'
assert 'int main() {int a[2][3] = {{1, 2,3}, {4, 5},}; return a[1][2];}'
assert 'int main() { struct {int a; int b;} x; return sizeof(x); }'
assert 'int main() { struct {int a; int b;} x; x.a = 10; x.b = 20; return x.a + x.b; }'
assert 'struct {union{int x; int y;}; long z;} x; int main() {x.z = 10; x.x = 1; x.y = 2; return x.x + x.y + x.z;}'
assert_print 'int hoge() {printf("hello world!!!%s:%d:%s",__FILE__, __LINE__, __func__); return 0;} int main() { printf("Hello World!!!%s:%d:%s",__FILE__,__LINE__,__func__); return hoge(); }'
assert 'int arr[] = {1, 2, 3}; int main() { return arr[1]; }'
assert 'int arr[5] = {10, 20, 30}; int main() { return arr[0] + arr[2]; }'
assert 'int a[2][5] = {{1, 2,3, 4, 5}, {6, 7},}; int main() {return a[0][4] * 8 + a[1][3];}'
assert 'int a[2][5] = {{6, 7},{1, 2,3, 4, 5}}; int main() {return a[0][4] * 8 + a[1][3];}'
assert_print 'int fibonacci1(){for (int i = 0, a = 0, b = 1; i < 10; i++, b = a + b)printf("%d, ", i ? a = b - a : 0);printf("...\n");return 0;}int main(){printf("Hello World!!!"); return fibonacci1();}'
assert_print 'int main() {}'
assert 'int main() {int x = 1; switch (x) { case 0: x = x + 10; break; default: break; case 1: x--; break;} return x;}'

# assert 'int func() {static int x = 0; x++; return x;} int main() {if (func() != 1) return 1; if (func() != 2) return 1; if (func() != 3) return 1; return 0;}'
# sleep 0.5
# assert '#include "../test/compiler_header.h"
# int foo(int x, ...);int main(){ return foo(1, 2, 4, 7, 8, 9, 11, 15, 18, 20, 19, 0); } int foo(int x, ...){ va_list ap; va_start(ap, x); int tmp = x; int result; while (tmp) { result = tmp; tmp = va_arg(ap, int); } va_end(ap); return result; }'

(
  cd test
  make clean
)

echo OK
