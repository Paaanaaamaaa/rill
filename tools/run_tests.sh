#!/usr/bin/env bash
# Runs every tests/run/*.rl through `rill --tokens` and diffs against its .tokens golden file.
# To accept new output after a deliberate lexer change: tools/run_tests.sh --update
cd "$(dirname "$0")/.." || exit 1
bin=build/rill
pass=0; fail=0
for src in tests/run/*.rl; do
    golden="${src%.rl}.tokens"
    if [ "$1" = "--update" ]; then "$bin" --tokens "$src" > "$golden" 2>&1; continue; fi
    if diff -u "$golden" <("$bin" --tokens "$src" 2>&1) > /dev/null; then
        pass=$((pass+1))
    else
        fail=$((fail+1)); echo "FAIL: $src"
        diff -u "$golden" <("$bin" --tokens "$src" 2>&1) | head -20
    fi
done
echo "passed: $pass, failed: $fail"
[ "$fail" -eq 0 ]
