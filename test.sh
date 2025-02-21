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

mkdir -p out

assert 0 '0;'
assert 42 '42; '
assert 9 '10-1;'
assert 18 '9+8+5-4;'
assert 6 '10 -9 + 5;'
assert 2 '10 / (4+ (3 - 2)) ;'
assert 16 '1* 8 * (4 / 2);'
assert 1 '-1 + 2;'
assert 10 '+9+(-1)+2;'
assert 1  '-(+1-2);'
assert 0 '0 == 1;'
assert 1 '0 ==0;'
assert 0 '0 != 0;'
assert 1 '0 != 1;'
assert 1 '0 < 1;'
assert 0 '1 < 1;'
assert 1 '1 <= 1;'
assert 1 '0 <= 1;'
assert 0 '10 <= 1;'
assert 0 '0 > 1;'
assert 0 '1 > 1;'
assert 1 '1 > 0;'
assert 1 '1 >= 1;'
assert 0 '0 >= 1;'
assert 1 '10 >= 1 ;'
assert 1 'a = 1; 1;'
assert 4 'a= 5; b = -1; a+b;'
assert 10 'a=5; b=2; a*b/2+5;'
assert 1 'foge = 1; huga = 2; piyo = 1; foge * huga - piyo;'
assert 3 'FOGE = 10; HUGA = 5; PIYO = FOGE / HUGA; PIYO + 1;'
assert 10 'a=5; b=2; c=a*b/2+5;return c;'
assert 5 'a = 2; b = 3; a_b = a +b; return a_b; a * 2;'
assert 2 'returne = 1; return returne + 1;'

echo OK