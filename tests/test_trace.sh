#!/usr/bin/env bash
# Tests for the ptrace-based trace builtin.
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
    timeout 10 "$BIN" -c "$1" >"$OUT" 2>"$ERR"
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
stdout_has() {
    if grep -Fq -- "$1" "$OUT"; then pass; else fail "$2 (missing $1)"; fi
}
stdout_lacks() {
    if grep -Fq -- "$1" "$OUT"; then fail "$2 (unexpected $1)"; else pass; fi
}
stderr_has() {
    if grep -Fq -- "$1" "$ERR"; then pass; else fail "$2 (missing $1)"; fi
}

# ------------------------------------------------------------------
echo "== basic tracing =="
run 'trace -e write /bin/echo hi'
status_is 0 "trace echo status"
stdout_has "hi" "traced program output"
stdout_has "write(1" "write syscall"
stdout_has "= 3" "write return value"
stdout_lacks "openat(" "filter excludes openat"

run 'trace -e execve /bin/true'
status_is 0 "trace true status"
stdout_has 'execve("/bin/true"' "execve path"
stdout_has "= 0" "execve return"

run 'trace -e openat /bin/cat /etc/hostname'
status_is 0 "trace cat status"
stdout_has 'openat(' "openat syscall"
stdout_has '"/etc/hostname"' "openat path string"

run 'trace -e read /bin/cat /etc/hostname'
stdout_has "read(" "read syscall"

# ------------------------------------------------------------------
echo "== filters =="
run 'trace -e write,exit_group /bin/true'
stdout_has "exit_group(" "multiple filter names"
stdout_lacks "write(" "unmatched filtered call"

run 'trace -e nosuchsyscall /bin/true'
status_is 0 "unknown filter status"
stdout_lacks "execve(" "unknown filter excludes all"

run 'trace -e trace=write /bin/echo x'
stdout_has "write(1" "trace= prefix filter"

# ------------------------------------------------------------------
echo "== summary =="
run 'trace -c -e write /bin/echo hi'
status_is 0 "summary status"
stdout_has "calls" "summary header"
stdout_has "write" "summary syscall name"
stdout_has "total" "summary total"
stdout_has "100.0%" "summary percentage"

run 'trace -c /bin/false'
status_is 1 "summary child status"

# ------------------------------------------------------------------
echo "== follow =="
run 'trace -f -e write /bin/sh -c "echo child"'
status_is 0 "follow status"
stdout_has "child" "followed child output"
stdout_has "write(1" "followed write"
if grep -Eq '^\[ *[0-9]+\]' "$OUT"; then pass; else fail "follow pid prefix"; fi

# ------------------------------------------------------------------
echo "== errors =="
run 'trace /definitely/not/a/command'
status_is 127 "missing command status"
stderr_has "No such file" "missing command message"

run 'trace'
status_is 2 "usage status"
stderr_has "usage: trace" "usage message"

run 'trace -e'
status_is 2 "missing filter argument status"

# ------------------------------------------------------------------
echo
if [ "$FAIL" -eq 0 ]; then
    echo "Trace tests passed: $PASS"
    exit 0
fi
echo "Trace tests passed: $PASS"
echo "Trace tests failed: $FAIL"
exit 1
