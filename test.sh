#!/bin/bash
assert() {
  expected="$1"
  input="$2"

  echo "$input" > out/tmp.txt
  ./main out/tmp.txt out/out.s
  cc -o out/out out/out.s
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

  echo "$input" > out/tmp.txt
  ./main out/tmp.txt out/out.s
  cc -c out/out.s -o out/out.o
  cc -o out/out out/out.o "${linkcode[@]}"
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
assert 0 'int main() {return 0;}'
assert 42 'int main() {return 42; }'
assert 9 'int main ( ) {return 10-1;}'
assert 18 'int main () {return 9+8+5-4;}'
assert 6 'int main () {return 10 -9 + 5;}'
assert 2 'int main () {return 10 / (4+ (3 - 2)) ;}'
assert 16 'int main(){return 1* 8 * (4 / 2);}'
assert 1 'int main(){return -1 + 2;}'
assert 10 'int main(){return +9+(-1)+2;}'
assert 1  'int main(){return -(+1-2);}'
assert 0 'int main(){return 0 == 1;}'
assert 1 'int main(){return 0 ==0;}'
assert 0 'int main() {return 0 != 0;}'
assert 1 'int main() {return 0 != 1;}'
assert 0 'int main() {return 1 < 1;}'
assert 1 'int main() {return 0 < 1;}'
assert 0 'int main() {return 10 <= 1;}'
assert 1 'int main() {return 1 <= 1;}'
assert 0 'int main(){ return   0 > 1;}'
assert 1 'int main() { return 0 <= 1;}'
assert 0 'int main() {return 1 > 1;}'
assert 1 'int main() {return 1 > 0;}'
assert 0 'int main() {return 0 >= 1;}'
assert 1 'int main() {return 1 >= 1;}'
assert 4 'int main() {int a= 5; int b = -1;return a+b;}'
assert 1 'int main() {return 10 >= 1 ;}'
assert 10 'int main() {int a=5; int b=2; return a*b/2+5;}'
assert 1 'int main () {int a = 1;return 1;}'
assert 3 'int main() {int FOGE = 10; int HUGA = 5; int PIYO = FOGE / HUGA; return PIYO + 1;}'
assert 1 'int main() {int foge = 1; int huga = 2; int piyo = 1; return foge * huga - piyo;}'
assert 10 'int main() {int a=5; int b=2; int c=a*b/2+5;return c;}'
assert 5 'int main() {int a = 2; int b = 3; int a_b = a +b; return a_b; a * 2;}'
assert 2 'int main() {int returne = 1; return returne + 1;}'
assert 6 'int main() {if (0) return 10; else return 6;}'
assert 10 'int main(){if(1) return 10; return 6;}'
assert 45 'int main(){int j=0;for(int i=0;i < 10; i = i+1) j = j+i; return j;}'
assert 10 'int main(){int i=0;int j=1;while(i != 10) i = i+j+1; return i;}'
assert 10 'int main(){{} return 10;}'
assert 46 'int main(){int i=0; int j=1; while(i != 10) {j = j+i; i = i+1;} return j;}'
assert 5 'int return_val() {return 5;}int main() {return return_val();}'
assert 194 'int fibonacci(int a, int b) {return a+b;}int main() {int x = 0; int y = 1; int i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); y = fibonacci(x, y); } return y; }'
assert_with_outer_code 10 'int main() {int i = func();return i;}' './test/include_test.o'
assert_with_outer_code 0 'int main() {int x = 0;int y = 1; int i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); if (x != func(x)){ return 1;} y = fibonacci(x, y); if (y != func(y)) {return 1;}} return 0; }' './test/print_u64.o' './test/fibonacci.o'
assert_with_outer_code 0 'int fibonacci(int a,int b) {return a+b;}int main() {int x = 0; int y = 1; int i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); if (x != func(x)) return 0; y = fibonacci(x, y); if (y != func(y)) return 1; } return 0; }' './test/print_u64.o'
assert 4 'int main() {int i = 4;int *x = &i; return *x;}'
assert 6 'int main() {int K = 6; return *&K;}'
assert_with_outer_code 5 'int main() {int x = 0; for(int i= 0; i < 10 ; i =i+1) { x = x+ i; if (x !=func(x)) return 1; }return 5;}' './test/print_u64.o'
assert 0 'int main() {int x = 3; int *y = &x; *y = 0; return x;}'
assert 3 'int main() {int x = 0; int *y = &x; int **z = &y;*y = 2; **z = 3; return x;}'

(
  cd test
  make clean
)

echo OK