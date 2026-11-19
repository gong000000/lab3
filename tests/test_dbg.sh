#!/usr/bin/env bash
# Tests for the dbg mini debugger: ELF symbols, breakpoints, step, bt, PIE,
# memory dump, finish, and SIGSEGV reporting.
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
stdout_lacks() {
    if grep -Fq -- "$1" "$OUT"; then fail "$2 (unexpected $1)"; else pass; fi
}
stderr_has() {
    if grep -Fq -- "$1" "$ERR"; then pass; else fail "$2 (missing $1)"; fi
}
run_script() {
    timeout 10 "$BIN" -c "dbg ./$1" < "$2" >"$OUT" 2>"$ERR"
    STATUS=$?
}

cat > target.c <<'EOS'
#include <stdio.h>
__attribute__((noinline)) int compute(int x) { return x * 2; }
int main(void) {
    int v = compute(21);
    printf("v=%d\n", v);
    return 0;
}
EOS
gcc -g -O0 -fno-omit-frame-pointer -no-pie target.c -o target
gcc -g -O0 -fno-omit-frame-pointer target.c -o target_pie
cat > segv.c <<'EOS'
int main(void) { volatile int *p = 0; *p = 42; return 0; }
EOS
gcc -g -O0 -fno-omit-frame-pointer -no-pie segv.c -o segv

# ------------------------------------------------------------------
echo "== breakpoint / registers / step =="
cat > cmds1 <<'EOS'
break main
run
regs
bt
step
continue
quit
EOS
run_script target cmds1
status_is 0 "basic status"
stdout_has "Breakpoint 1 at" "breakpoint address"
stdout_has "(main)" "breakpoint symbol"
stdout_has "Breakpoint 1," "breakpoint hit"
stdout_has "rip=0x" "register rip"
stdout_has "rsp=0x" "register rsp"
stdout_has "#0" "backtrace frame 0"
stdout_has "main" "backtrace main"
stdout_has "v=42" "program output"
stdout_has "Inferior exited with status 0" "exit status"

# ------------------------------------------------------------------
echo "== backtrace through a call =="
cat > cmds2 <<'EOS'
break compute
run
bt
x/4x $rsp
continue
quit
EOS
run_script target cmds2
status_is 0 "compute status"
stdout_has "Breakpoint 1, 0x" "compute breakpoint hit"
stdout_has "#0" "compute frame 0"
stdout_has "compute ()" "compute symbol"
stdout_has "#1" "compute frame 1"
stdout_has "main ()" "caller main"
stdout_has "0x" "memory dump"
stdout_has "v=42" "compute continue"

# ------------------------------------------------------------------
echo "== finish =="
cat > cmds3 <<'EOS'
break compute
run
finish
bt
continue
quit
EOS
run_script target cmds3
status_is 0 "finish status"
stdout_has "Run till return at 0x" "finish return address"
stdout_has "Breakpoint 2" "finish temporary breakpoint"
stdout_has "#0" "finish backtrace"
stdout_has "main ()" "finish in main"
stdout_has "v=42" "finish continues"

# ------------------------------------------------------------------
echo "== info / delete / run without breakpoint =="
cat > cmds4 <<'EOS'
break main
info break
delete 1
info break
run
quit
EOS
run_script target cmds4
status_is 0 "info/delete status"
stdout_has "breakpoint" "info breakpoint list"
stdout_has "No breakpoints" "delete removes breakpoint"
stdout_has "v=42" "run without breakpoint"
stdout_has "Inferior exited with status 0" "run without breakpoint exit"

# ------------------------------------------------------------------
echo "== PIE =="
cat > cmds5 <<'EOS'
break main
run
regs
quit
EOS
run_script target_pie cmds5
status_is 0 "PIE status"
stdout_has "PIE" "PIE detected"
stdout_has "Breakpoint 1 at" "PIE breakpoint"
stdout_has "Breakpoint 1," "PIE breakpoint hit"
stdout_has "(main)" "PIE main symbol"
stdout_has "rip=0x" "PIE registers"

# ------------------------------------------------------------------
echo "== SIGSEGV =="
cat > cmds6 <<'EOS'
run
continue
quit
EOS
run_script segv cmds6
status_is 0 "SIGSEGV status"
stdout_has "SIGSEGV" "SIGSEGV reported"
stdout_has "#0" "SIGSEGV backtrace"
stdout_has "main ()" "SIGSEGV main frame"
stdout_has "Inferior terminated by signal 11" "SIGSEGV termination"

# ------------------------------------------------------------------
echo "== errors =="
cat > cmds7 <<'EOS'
break nosuchsymbol
quit
EOS
run_script target cmds7
status_is 0 "unknown symbol status"
stderr_has "unknown symbol" "unknown symbol message"

timeout 5 "$BIN" -c 'dbg' >"$OUT" 2>"$ERR"
STATUS=$?
status_is 2 "usage status"
stderr_has "usage: dbg" "usage message"

# ------------------------------------------------------------------
echo
if [ "$FAIL" -eq 0 ]; then
    echo "Debugger tests passed: $PASS"
    exit 0
fi
echo "Debugger tests passed: $PASS"
echo "Debugger tests failed: $FAIL"
exit 1
