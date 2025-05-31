#!/bin/bash
assert_print() {
  input="$1"

  echo "$input" > out/tmp.c
  gcc -o out/gcc out/tmp.c
  ./out/gcc > out/gcc.txt
  expected="$?"
  ./main -i out/tmp.c -o out/out.s
  gcc -z noexecstack -o out/out out/out.s
  ./out/out > out/out.txt
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
    cmp -s "out/out.txt" "out/gcc.txt"
    if [$? -neq 0]; then
      echo "ERROR: Compiler output does not match GCC output"
      exit 1
    fi
  else
    echo "$input => $expected expected, but got $actual"
    exit 1
  fi
}

assert() {
  input="$1"

  echo "$input" > out/tmp.c
  gcc -o out/gcc out/tmp.c
  ./out/gcc
  expected="$?"
  ./main -i out/tmp.c -o out/out.s
  gcc -z noexecstack -o out/out out/out.s
  ./out/out
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected, but got $actual"
    exit 1
  fi
}

assert_with_outer_code() {
  expected="$1"
  input="$2"
  shift 2
  linkcode=("$@")

  echo "$input" > out/tmp.c
  ./main -i out/tmp.c -o out/out.s
  cc -c out/out.s -o out/out.o
  cc -z noexecstack -o out/out out/out.o "${linkcode[@]}"
  ./out/out
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected, but got $actual"
    exit 1
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
assert_with_outer_code 10 'int main() {int i = func();return i;}' './test/include_test.o'
assert_with_outer_code 0 'int main() {int x = 0;int y = 1; int i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); if (x != func1(x)){ return 1;} y = fibonacci(x, y); if (y != func1(y)) {return 1;}} return 0; }' './test/print_u64.o' './test/fibonacci.o'
assert_with_outer_code 0 'int fibonacci(int a,int b) {return a+b;}int main() {int x = 0; int y = 1; int i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); if (x != func1(x)) return 0; y = fibonacci(x, y); if (y != func1(y)) return 1; } return 0; }' './test/print_u64.o'
assert 'int main() {int i = 4;int *x = &i; return *x;}'
assert 'int main() {int K = 6; return *&K;}'
assert_with_outer_code 5 'int main() {int x = 0; for(int i= 0; i < 10 ; i =i+1) { x = x+ i; if (x !=func1(x)) return 1; }return 5;}' './test/print_u64.o'
assert 'int main() {int x = 3; int *y = &x; *y = 0; return x;}'
assert 'int main() {int x = 0; int *y = &x; int **z = &y;*y = 2; **z = 3; return x;}'
assert_with_outer_code 3 'int main() {int *p; alloc(&p, 1, 3); int *q = p+1; return *q;}' './test/alloc2.o'
assert_with_outer_code 1 'int main() {int *p; alloc(&p, 1, 3); int *q = p + 1; q = q-1; return *q;}' './test/alloc2.o'
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
assert 'typedef int *hoge; int main() {hoge x; *x = 1; return *x;}'
assert 'typedef int hoge[2]; int main() {hoge x; x[1] = 10; x[0] = 2; return x[1];}'
assert 'struct HOGE { int x; int y; }; int main() {struct HOGE x; return sizeof(x);}'
assert '#include <stdbool.h>
struct HOGE { int x; int y; }; struct FUGA { bool x; bool y; }; typedef struct FUGA HOGE; int main() {HOGE x; return sizeof(x);}'
assert 'typedef int hoge; int main() {hoge* a; *a = 1; return *a;}'
assert '#include <stdbool.h>
struct HOGE { int x; int y; }; struct FUGA { bool x; bool y; }; typedef struct FUGA HOGE; int main() {HOGE x; return sizeof(x.x);}'
assert 'int main() { int x = 0; for(int i = 0; i <= 10; i = i+ 1) { x = x + i; if (x > 10) break; } return x;}'
assert 'int main() { int x = 0; goto end; { x = 1; } x = 2; return x; end: return x; }'
assert 'typedef int foo; int main(){ foo(i); i = 999; return i; }'

(
  cd test
  make clean
)

echo OK