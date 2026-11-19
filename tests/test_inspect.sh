#!/usr/bin/env bash
# Tests for inspect: /proc identity, fds, maps, namespaces, jobs, limits.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/myshell"
WORK="$(mktemp -d)"
OUT="$WORK/out"
ERR="$WORK/err"
PASS=0
FAIL=0

cleanup() { rm -rf "$WORK"; }
trap cleanup EXIT
cd "$WORK" || exit 1
export HOME="$WORK/home"
mkdir -p "$HOME"

run() { "$BIN" -c "$1" >"$OUT" 2>"$ERR"; STATUS=$?; }
pass() { PASS=$((PASS + 1)); }
fail() {
    FAIL=$((FAIL + 1))
    echo "FAIL: $1" >&2
    sed 's/^/  /' "$OUT" >&2
    sed 's/^/  /' "$ERR" >&2
}
status_is() { if [ "$STATUS" -eq "$1" ]; then pass; else fail "$2 (status $STATUS)"; fi; }
stdout_has() { if grep -Fq -- "$1" "$OUT"; then pass; else fail "$2 (missing $1)"; fi; }
stdout_lacks() { if grep -Fq -- "$1" "$OUT"; then fail "$2 (unexpected $1)"; else pass; fi; }

echo "== identity =="
run 'inspect'
status_is 0 "inspect status"
stdout_has "shell:" "shell section"
stdout_has "pid=" "shell pid"
stdout_has "uid=" "shell uid"
stdout_has "umask=" "shell umask"
stdout_has "status (/proc/" "proc status"
stdout_has "VmRSS:" "status rss"
stdout_has "limits:" "rlimits"
stdout_has "NOFILE" "nofile limit"

echo "== fds =="
run 'inspect -f'
status_is 0 "inspect fds status"
stdout_has "fds (/proc/" "fds header"
stdout_has " -> " "fd symlink target"

echo "== maps / namespaces =="
run 'inspect -m'
stdout_has "maps (/proc/" "maps header"
stdout_has "regions," "maps region summary"
run 'inspect -n'
stdout_has "namespaces:" "namespaces header"
stdout_has "mnt" "mnt namespace"
stdout_has "pid" "pid namespace"

echo "== jobs =="
run 'inspect -j'
status_is 0 "inspect jobs status"
stdout_has "jobs:" "jobs header"
stdout_has "(none)" "no jobs"

echo "== explicit pid =="
run 'inspect $$'
status_is 0 "inspect self pid status"
stdout_has "status (/proc/" "self status"
stdout_has "fds (/proc/" "self fds"

echo "== process tree =="
run 'inspect -t'
status_is 0 "inspect tree status"
stdout_has "process tree" "tree header"
stdout_has "myshell" "tree root"
run 'sleep 2 & inspect -t; wait'
stdout_has "sleep" "tree child process"

echo "== errors =="
run 'inspect -z'
status_is 2 "bad option status"
run 'inspect notapid'
status_is 2 "bad pid status"
run 'inspect 999999'
status_is 0 "missing pid status"
stdout_has "status: unavailable" "missing pid status message"

echo
if [ "$FAIL" -eq 0 ]; then
    echo "Inspect tests passed: $PASS"
    exit 0
fi
echo "Inspect tests passed: $PASS"
echo "Inspect tests failed: $FAIL"
exit 1
