#!/bin/bash
assert() {
  expected="$1"
  input="$2"

  echo "$input" > tmp.txt

  ./main tmp.txt
  cc -o out out.s
  ./out
  actual="$?"

  if [ "$actual" = "$expected" ]; then
    echo "$input => $actual"
  else
    echo "$input => $expected expected, but got $actual"
    exit 1
  fi
}

assert 0 0
assert 42 42
assert 9 '10-1'
assert 18 '9+8+5-4'
assert 6 '10 -9 + 5'
assert 2 '10 / (4+ (3 - 2)) '
assert 16 '1* 8 * (4 / 2)'
assert 1 '-1 + 2'
assert 10 '+9+(-1)+2'
assert 1  '-(+1-2)'
assert 0 '0 == 1'
assert 1 '0 ==0'
assert 0 '0 != 0'
assert 1 '0 != 1'
assert 1 '0 < 1'
assert 0 '1 < 1'
assert 1 '1 <= 1'
assert 1 '0 <= 1'
assert 0 '10 <= 1'
assert 0 '0 > 1'
assert 0 '1 > 1'
assert 1 '1 > 0'
assert 1 '1 >= 1'
assert 0 '0 >= 1'
assert 1 '10 >= 1'

echo OK