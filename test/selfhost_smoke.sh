#!/bin/sh
set -eu

compiler=${1:-./main2}
cc=${2:-gcc}

tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM

check_case() {
  name=$1
  source=$2
  expected=$3

  c_file="$tmpdir/$name.c"
  s_file="$tmpdir/$name.s"
  bin_file="$tmpdir/$name.bin"

  printf '%s\n' "$source" > "$c_file"

  if ! timeout 5 "$compiler" -i "$c_file" -o "$s_file" >/dev/null 2>&1; then
    echo "[FAIL] $name: compiler did not finish successfully"
    return 1
  fi

  if [ ! -s "$s_file" ]; then
    echo "[FAIL] $name: compiler produced empty assembly"
    return 1
  fi

  "$cc" -no-pie "$s_file" -o "$bin_file" >/dev/null 2>&1
  set +e
  "$bin_file"
  actual=$?
  set -e
  if [ "$actual" -ne "$expected" ]; then
    echo "[FAIL] $name: expected $expected, got $actual"
    return 1
  fi

  echo "[OK] $name => $actual"
}

check_case "minimal" 'int main(){return 0;}' 0
check_case "ret18" 'int main(){return 9+8+5-4;}' 18
check_case "div_nested" 'int main(){return 10 / (4 + (3 - 2));}' 2
check_case "fibloop" 'int fib(int a,int b){return a+b;} int main(){int x=0; int y=1; for(int i=0;i<3;i=i+1){ x=fib(x,y); y=fib(x,y);} return y;}' 13
check_case "call8" 'int piyo(int a,int b,int c,int d,int e,int f,int g,int h){return a+b+c+d+e+f+g+h;} int main(){return piyo(1,2,3,4,5,6,7,8);}' 36
check_case "nested_break_continue" 'int main(){int r=0; for(int i=0;i<3;i=i+1){ while(1){ if(i==0){ r=r+1; break; } if(i==1){ r=r+2; continue; } r=r+4; break; } if(i==1) break; } return r;}' 3
