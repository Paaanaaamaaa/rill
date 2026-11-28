#!/usr/bin/env bash
# tests/<dir>/*.rl is run with a flag and compared with a golden file next to it
# tests/exec/*.rl is compiled and run, stdout plus "exit: N" is compared with .out
# --update rewrites the golden files from current output, read the diff before committing
cd "$(dirname "$0")/.." || exit 1
bin=build/rill
pass=0; fail=0
update=0
[ "$1" = "--update" ] && update=1

compare() {
    # $1 source, $2 golden, $3 actual
    if [ "$update" = 1 ]; then printf '%s\n' "$3" > "$2"; return; fi
    if diff -u "$2" <(printf '%s\n' "$3") > /dev/null; then
        pass=$((pass+1))
    else
        fail=$((fail+1)); echo "FAIL: $1"
        diff -u "$2" <(printf '%s\n' "$3") | head -20
    fi
}

check() {
    for src in tests/$1/*.rl; do
        compare "$src" "${src%.rl}.$3" "$("$bin" "$2" "$src" 2>&1)"
    done
}

run_exec() {
    tmp=$(mktemp -d)
    for src in tests/exec/*.rl; do
        if "$bin" "$src" -o "$tmp/prog" > "$tmp/build.log" 2>&1; then
            actual=$("$tmp/prog" 2>&1; echo "exit: $?")
        else
            actual="build failed: $(cat "$tmp/build.log")"
        fi
        compare "$src" "${src%.rl}.out" "$actual"
    done
    rm -rf "$tmp"
}

check run --tokens tokens
check parse --ast ast
check sema --sema sema
check types --types types
check ir --ir ir
check asm --asm s
run_exec
echo "passed: $pass, failed: $fail"
[ "$fail" -eq 0 ]
