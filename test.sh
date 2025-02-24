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
assert 0 'main() {return 0;}'
assert 42 'main() {return 42; }'
assert 9 'main ( ) {return 10-1;}'
assert 18 'main () {return 9+8+5-4;}'
assert 6 'main () {return 10 -9 + 5;}'
assert 2 'main () {return 10 / (4+ (3 - 2)) ;}'
assert 16 'main(){return 1* 8 * (4 / 2);}'
assert 1 'main(){return -1 + 2;}'
assert 10 'main(){return +9+(-1)+2;}'
assert 1  'main(){return -(+1-2);}'
assert 0 'main(){return 0 == 1;}'
assert 1 'main(){return 0 ==0;}'
assert 0 'main() {return 0 != 0;}'
assert 1 'main() {return 0 != 1;}'
assert 0 'main() {return 1 < 1;}'
assert 1 'main() {return 0 < 1;}'
assert 0 'main() {return 10 <= 1;}'
assert 1 'main() {return 1 <= 1;}'
assert 0 'main(){ return   0 > 1;}'
assert 1 'main() { return 0 <= 1;}'
assert 0 ' main() {return 1 > 1;}'
assert 1 'main() {return 1 > 0;}'
assert 0 'main() {return 0 >= 1;}'
assert 1 'main() {return 1 >= 1;}'
assert 4 'main() {a= 5; b = -1;return a+b;}'
assert 1 ' main() {return 10 >= 1 ;}'
assert 10 'main() {a=5; b=2; return a*b/2+5;}'
assert 1 'main () {a = 1;return 1;}'
assert 3 'main() {FOGE = 10; HUGA = 5; PIYO = FOGE / HUGA; return PIYO + 1;}'
assert 1 'main() {foge = 1; huga = 2; piyo = 1; return foge * huga - piyo;}'
assert 10 'main() {a=5; b=2; c=a*b/2+5;return c;}'
assert 5 'main() {a = 2; b = 3; a_b = a +b; return a_b; a * 2;}'
assert 2 'main() {returne = 1; return returne + 1;}'
assert 6 'main() {if (0) return 10; else return 6;}'
assert 10 'main(){if(1) return 10; return 6;}'
assert 45 'main(){j=0;for(i=0;i < 10; i = i+1) j = j+i; return j;}'
assert 10 'main(){i=0;j=1;while(i != 10) i = i+j+1; return i;}'
assert 10 'main(){{} return 10;}'
assert 46 'main(){i=0; j=1; while(i != 10) {j = j+i; i = i+1;} return j;}'
assert 5 'return_val() {return 5;} main() {return return_val();}'
assert 194 'fibonacci(a, b) {return a+b;} main() {x = 0; y = 1; i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); y = fibonacci(x, y); } return y; }'
assert_with_outer_code 10 'main() {i = func();return i;}' './test/include_test.o'
assert_with_outer_code 0 'main() {x = 0; y = 1; i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); if (x != func(x)){ return 1;} y = fibonacci(x, y); if (y != func(y)) {return 1;}} return 0; }' './test/print_u64.o' './test/fibonacci.o'
#assert_with_outer_code 0 'fibonacci(a, b) {return a+b;} main() {x = 0; y = 1; i = 0; while(i != 10) { i = i+ 1; x = fibonacci(x, y); if (x != func(x)) return 0; y = fibonacci(x, y); if (y != func(y)) return 1; } return 0; }' './test/print_u64.o'

(
  cd test
  make clean
)

echo OK