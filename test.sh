#!/bin/bash
assert() {
  expected="$1"
  input="$2"

  echo $input > tmp.txt

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
assert 9 "10-1"
assert 18 "9+8+5-4"

echo OK