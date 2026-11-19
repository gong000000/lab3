#!/usr/bin/env bash
# Regression test: reading commands from a file must not duplicate the last line.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/myshell"
WORK="$(mktemp -d)"
PASS=0
FAIL=0

cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

check() {
    local name="$1" expected="$2" file="$3"
    local actual
    actual="$("$BIN" < "$file")"
    if [ "$actual" = "$expected" ]; then
        PASS=$((PASS + 1))
    else
        FAIL=$((FAIL + 1))
        echo "FAIL: $name" >&2
        printf 'expected: <%s>\nactual:   <%s>\n' "$expected" "$actual" >&2
    fi
}

printf 'echo one\n' > "$WORK/one.msh"
check "single line" "one" "$WORK/one.msh"

printf 'echo one\necho two\n' > "$WORK/two.msh"
check "two lines" $'one\ntwo' "$WORK/two.msh"

printf 'echo one\necho two\necho three\n' > "$WORK/three.msh"
check "three lines" $'one\ntwo\nthree' "$WORK/three.msh"

printf 'sleep 0.1 &\necho one\necho two\n' > "$WORK/bg.msh"
check "background job followed by commands" $'one\ntwo' "$WORK/bg.msh"

printf 'for i in 1 2; do\n  echo x$i\ndone\n' > "$WORK/loop.msh"
check "multi-line loop from stdin" $'x1\nx2' "$WORK/loop.msh"

echo
echo "File-input tests passed: $PASS"
echo "File-input tests failed: $FAIL"
[ "$FAIL" -eq 0 ]
