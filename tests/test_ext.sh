#!/usr/bin/env bash
# Tests for explain, &&/||, glob, undo/trash, serve, and watch.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/myshell"
WORK="$(mktemp -d)"
OUT="$WORK/out"
ERR="$WORK/err"
PASS=0
FAIL=0
BASE=$((24000 + ($$ % 500) * 10))

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
stderr_has() {
    if grep -Fq -- "$1" "$ERR"; then pass; else fail "$2 (missing $1)"; fi
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
echo "== undo / trash =="
run 'echo keep > undo.txt; rm undo.txt; undo; cat undo.txt'
status_is 0 "rm undo status"
stdout_has "undo: restored" "rm undo message"
stdout_has "keep" "rm undo content"

run 'echo old > target.txt; echo new > source.txt; mv source.txt target.txt; undo; cat target.txt; cat source.txt'
status_is 0 "mv undo status"
stdout_has "old" "mv undo old content"
stdout_has "new" "mv undo source content"

run 'echo old2 > target2.txt; echo new2 > source2.txt; cp -f source2.txt target2.txt; undo; cat target2.txt'
status_is 0 "cp -f undo status"
stdout_has "old2" "cp -f undo content"

run 'echo permanent > p.txt; rm --no-trash p.txt; test ! -e p.txt'
status_is 0 "rm --no-trash"

# ------------------------------------------------------------------
echo "== serve =="
mkdir -p www/sub
echo hello-http > www/index.txt
PORT=$BASE
run "serve -1 -p $PORT -d $WORK/www & sleep 0.5; curl -s http://127.0.0.1:$PORT/index.txt; wait"
status_is 0 "serve GET status"
stdout_has "hello-http" "serve GET body"

PORT=$((BASE + 1))
run "serve -1 -p $PORT -d $WORK/www & sleep 0.5; curl -s -o /dev/null -w '%{http_code}' --path-as-is http://127.0.0.1:$PORT/../etc/passwd; wait"
status_is 0 "serve traversal status"
stdout_has "403" "serve traversal blocked"

# ------------------------------------------------------------------
echo "== watch =="
run 'watch -n 1 -c 1 echo watched'
status_is 0 "watch status"
stdout_has "Every 1s: echo watched" "watch header"
stdout_has "watched" "watch command output"

echo
echo "Extension tests passed: $PASS"
echo "Extension tests failed: $FAIL"
[ "$FAIL" -eq 0 ]
