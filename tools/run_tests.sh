#!/usr/bin/env bash
# tests/run/*.rl are checked with --tokens against .tokens
# tests/parse/*.rl are checked with --ast against .ast
# --update rewrites the golden files from current output, read the diff before committing
cd "$(dirname "$0")/.." || exit 1
bin=build/rill
pass=0; fail=0

check() {
    for src in tests/$1/*.rl; do
        golden="${src%.rl}.$3"
        actual=$("$bin" "$2" "$src" 2>&1)
        if [ "$update" = 1 ]; then printf '%s\n' "$actual" > "$golden"; continue; fi
        if diff -u "$golden" <(printf '%s\n' "$actual") > /dev/null; then
            pass=$((pass+1))
        else
            fail=$((fail+1)); echo "FAIL: $src"
            diff -u "$golden" <(printf '%s\n' "$actual") | head -20
        fi
    done
}

[ "$1" = "--update" ] && update=1
check run --tokens tokens
check parse --ast ast
check sema --sema sema
check types --types types
echo "passed: $pass, failed: $fail"
[ "$fail" -eq 0 ]
