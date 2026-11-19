#!/usr/bin/env bash
# Tests for explain, &&/||, and glob (core transparent-shell extensions).
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/myshell"
WORK="$(mktemp -d)"
OUT="$WORK/out"
ERR="$WORK/err"
PASS=0
FAIL=0

cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

cd "$WORK" || exit 1
export HOME="$WORK/home"
mkdir -p "$HOME"

run() {
    "$BIN" -c "$1" >"$OUT" 2>"$ERR"
    STATUS=$?
}

pass() { PASS=$((PASS + 1)); }
fail() {
    FAIL=$((FAIL + 1))
    echo "FAIL: $1" >&2
    echo "--- stdout ---" >&2
    sed 's/^/  /' "$OUT" >&2
    echo "--- stderr ---" >&2
    sed 's/^/  /' "$ERR" >&2
    echo "--- status: $STATUS ---" >&2
}
status_is() {
    if [ "$STATUS" -eq "$1" ]; then pass; else fail "$2 (status $STATUS)"; fi
}
stdout_is() {
    if [ "$(cat "$OUT")" = "$2" ]; then pass; else fail "$1 (stdout)"; fi
}
stdout_has() {
    if grep -Fq -- "$1" "$OUT"; then pass; else fail "$2 (missing $1)"; fi
}

# ------------------------------------------------------------------
echo "== explain =="
run "explain 'cat a | grep x > o 2>&1 &'"
status_is 0 "explain status"
stdout_has "[pipeline 1] background" "explain background"
stdout_has "stdout → o (truncate)" "explain stdout"
stdout_has "stderr → stdout" "explain stderr dup"

# ------------------------------------------------------------------
echo "== && / || =="
run 'true && echo yes'
status_is 0 "AND success status"
stdout_is "AND success" "yes"
run 'false && echo no'
status_is 1 "AND failure status"
stdout_is "AND failure" ""
run 'false || echo yes'
status_is 0 "OR success status"
stdout_is "OR success" "yes"
run 'true || echo no'
status_is 0 "OR skip status"
stdout_is "OR skip" ""
run 'false && echo no || echo fallback'
stdout_is "AND/OR fallback" "fallback"

# ------------------------------------------------------------------
echo "== glob =="
printf 'a\n' > g1.txt
printf 'b\n' > g2.txt
run 'echo g*.txt'
stdout_is "glob expansion" "g1.txt g2.txt"
run 'echo "g*.txt"'
stdout_is "quoted glob literal" "g*.txt"
run 'ls g*.txt'
stdout_is "glob with builtin" $'g1.txt\ng2.txt'

# ------------------------------------------------------------------
echo "== pipestat =="
run "pipestat 'echo hello | cat | wc -c'"
status_is 0 "pipestat status"
stdout_has "[1] echo hello: 6 bytes" "pipestat stage 1 bytes"
stdout_has "[3] wc -c" "pipestat stage 3"
run "pipestat 'echo hello | grep zzz'"
status_is 1 "pipestat failure status"
stdout_has "status 1" "pipestat failure report"

# ------------------------------------------------------------------
echo
echo "Extension tests passed: $PASS"
echo "Extension tests failed: $FAIL"
[ "$FAIL" -eq 0 ]
