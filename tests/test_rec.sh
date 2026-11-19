#!/usr/bin/env bash
# Tests for rec/replay session recording and standalone HTML export.
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
stderr_has() {
    if grep -Fq -- "$1" "$ERR"; then pass; else fail "$2 (missing $1)"; fi
}
file_has() {
    if grep -Fq -- "$2" "$1"; then pass; else fail "$3 (file $1 missing $2)"; fi
}

printf 'hello\nworld\n' > a.txt

# ------------------------------------------------------------------
echo "== record / replay =="
cat > cmds <<'EOS'
rec start rec.bin
echo hello
cat a.txt | grep hello
false
ls /definitely-not-here
rec status
rec stop
exit
EOS
timeout 10 "$BIN" < cmds >"$OUT" 2>"$ERR"
STATUS=$?
status_is 0 "record session status"
stdout_has "recording to rec.bin" "recording starts"
stdout_has "hello" "command output captured"
stderr_has "No such file or directory" "stderr captured"
if [ -s rec.bin ]; then pass; else fail "record file exists"; fi
if [ "$(head -c 7 rec.bin)" = "MSHREC1" ]; then pass; else fail "record magic"; fi

timeout 10 "$BIN" -c 'replay rec.bin -s 0' >"$OUT" 2>"$ERR"
STATUS=$?
status_is 0 "replay status"
stdout_has "=== replay: rec.bin" "replay header"
stdout_has '$ echo hello' "replay command"
stdout_has "hello" "replay stdout"
stdout_has "[status=1" "replay false status"
stdout_has '$ rec stop' "replay stop command"
stderr_has "No such file or directory" "replay stderr"

# ------------------------------------------------------------------
echo "== HTML export =="
timeout 10 "$BIN" -c 'rec export rec.bin session.html' >"$OUT" 2>"$ERR"
STATUS=$?
status_is 0 "export status"
stdout_has "wrote session.html" "export message"
file_has session.html "<!doctype html>" "HTML doctype"
file_has session.html "myshell session recording" "HTML title"
file_has session.html '$ echo hello' "HTML command"
file_has session.html "hello" "HTML output"
file_has session.html "status=1" "HTML status"
if [ "$(grep -c 'class="entry"' session.html)" -ge 5 ]; then
    pass
else
    fail "HTML timeline entries"
fi

# ------------------------------------------------------------------
echo "== diagnostics -x =="
cat > cmdsx <<'EOS'
rec start -x recx.bin
cat a.txt | grep hello
rec stop
exit
EOS
timeout 20 "$BIN" < cmdsx >"$OUT" 2>"$ERR"
STATUS=$?
status_is 0 "diagnostics record status"
timeout 10 "$BIN" -c 'rec export recx.bin recx.html' >"$OUT" 2>"$ERR"
STATUS=$?
status_is 0 "diagnostics export status"
file_has recx.html "explain / trace" "diagnostics details"
file_has recx.html "trace -c summary" "trace summary label"
file_has recx.html "calls" "trace summary table"

# ------------------------------------------------------------------
echo "== errors / auto close =="
cat > cmds2 <<'EOS'
rec start auto.bin
echo auto
exit
EOS
timeout 10 "$BIN" < cmds2 >"$OUT" 2>"$ERR"
STATUS=$?
status_is 0 "auto close status"
timeout 10 "$BIN" -c 'replay auto.bin -s 0' >"$OUT" 2>"$ERR"
STATUS=$?
stdout_has '$ echo auto' "auto close replay"

timeout 5 "$BIN" -c 'rec stop' >"$OUT" 2>"$ERR"
STATUS=$?
status_is 1 "stop without start"
stderr_has "not recording" "stop error"

timeout 5 "$BIN" -c 'rec' >"$OUT" 2>"$ERR"
STATUS=$?
status_is 2 "rec usage status"
stderr_has "usage: rec" "rec usage message"

timeout 5 "$BIN" -c 'replay' >"$OUT" 2>"$ERR"
STATUS=$?
status_is 2 "replay usage status"

timeout 5 "$BIN" -c 'replay missing.rec -s 0' >"$OUT" 2>"$ERR"
STATUS=$?
status_is 1 "missing record status"

timeout 5 "$BIN" -c 'rec export missing.rec no.html' >"$OUT" 2>"$ERR"
STATUS=$?
status_is 1 "missing export status"

# ------------------------------------------------------------------
echo
if [ "$FAIL" -eq 0 ]; then
    echo "Recording tests passed: $PASS"
    exit 0
fi
echo "Recording tests passed: $PASS"
echo "Recording tests failed: $FAIL"
exit 1
