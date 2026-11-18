#!/usr/bin/env bash
# Network extension tests for netserv/netcli.
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/myshell"
WORK="$(mktemp -d)"
OUT="$WORK/out"
ERR="$WORK/err"
SCRIPT="$WORK/script"
BASE=$((22000 + ($$ % 1000) * 10))
PASS=0
FAIL=0

cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

run_script() {
    timeout 30 "$BIN" < "$SCRIPT" > "$OUT" 2> "$ERR"
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

check_status() {
    if [ "$STATUS" -eq "$1" ]; then pass; else fail "$2 (status $STATUS)"; fi
}
check_contains() {
    if grep -Fq -- "$2" "$1"; then pass; else fail "$3 (missing $2)"; fi
}

# ------------------------------------------------------------------
echo "== netserv: one-shot, echo =="
PORT=$BASE
cat > "$SCRIPT" <<EOF
netserv -1 -p $PORT &
sleep 0.5
netcli -p $PORT 127.0.0.1 echo hello
wait
EOF
run_script
check_status 0 "one-shot server status"
check_contains "$OUT" "listening on 127.0.0.1:$PORT" "server banner"
check_contains "$OUT" "hello" "remote echo"

# ------------------------------------------------------------------
echo "== netserv: token authentication =="
PORT=$((BASE + 1))
cat > "$SCRIPT" <<EOF
netserv -1 -p $PORT -s secret &
sleep 0.5
netcli -p $PORT -s wrong 127.0.0.1 echo nope
echo rc=\$?
wait
EOF
run_script
check_status 0 "auth test shell status"
check_contains "$ERR" "authentication failed" "auth failure"
check_contains "$OUT" "rc=147" "auth failure status"

PORT=$((BASE + 2))
cat > "$SCRIPT" <<EOF
netserv -1 -p $PORT -s secret &
sleep 0.5
netcli -p $PORT -s secret 127.0.0.1 echo ok
wait
EOF
run_script
check_status 0 "auth success shell status"
check_contains "$OUT" "ok" "auth success"

# ------------------------------------------------------------------
echo "== netserv: pipe, redirection, persistent session =="
PORT=$((BASE + 3))
cat > "$SCRIPT" <<EOF
netserv -p $PORT &
sleep 0.5
netcli -p $PORT 127.0.0.1 'echo hi | grep hi'
netcli -p $PORT 127.0.0.1 'echo data > /tmp/msh_net_test.\$\$; cat /tmp/msh_net_test.\$\$'
netcli -p $PORT 127.0.0.1 pwd
printf 'echo one\\necho two\\nexit\\n' | netcli -i -p $PORT 127.0.0.1
kill \$!
EOF
run_script
check_status 0 "persistent server status"
check_contains "$OUT" "hi" "remote pipe"
check_contains "$OUT" "data" "remote redirection"
check_contains "$OUT" "one" "interactive netcli 1"
check_contains "$OUT" "two" "interactive netcli 2"

# ------------------------------------------------------------------
echo "== netserv: timeout and background rejection =="
PORT=$((BASE + 4))
cat > "$SCRIPT" <<EOF
netserv -1 -p $PORT -t 1 &
sleep 0.5
netcli -p $PORT 127.0.0.1 'sleep 5'
echo rc=\$?
wait
EOF
run_script
check_status 0 "timeout test shell status"
check_contains "$ERR" "timed out" "remote timeout"
check_contains "$OUT" "rc=124" "timeout status"

PORT=$((BASE + 5))
cat > "$SCRIPT" <<EOF
netserv -1 -p $PORT &
sleep 0.5
netcli -p $PORT 127.0.0.1 'sleep 1 & echo bad'
echo rc=\$?
wait
EOF
run_script
check_status 0 "background rejection shell status"
check_contains "$ERR" "background jobs are not supported" "background rejection"
check_contains "$OUT" "rc=144" "background rejection status"

# ------------------------------------------------------------------
echo "== netserv: concurrent clients =="
PORT=$((BASE + 6))
cat > "$SCRIPT" <<EOF
netserv -n 2 -p $PORT &
sleep 0.5
netcli -p $PORT 127.0.0.1 'echo client-A' > "$WORK/a.out" 2> "$WORK/a.err" &
netcli -p $PORT 127.0.0.1 'echo client-B' > "$WORK/b.out" 2> "$WORK/b.err" &
wait
EOF
run_script
check_status 0 "concurrent clients server status"
if grep -Fq "client-A" "$WORK/a.out" && grep -Fq "client-B" "$WORK/b.out"; then
    pass
else
    fail "concurrent clients"
fi

# ------------------------------------------------------------------
echo "== netserv: non-loopback bind requires a token =="
PORT=$((BASE + 7))
"$BIN" -c "netserv -a 0.0.0.0 -p $PORT" > "$OUT" 2> "$ERR"
STATUS=$?
check_status 1 "non-loopback without token"
check_contains "$ERR" "token" "non-loopback token check"

echo
echo "Network tests passed: $PASS"
echo "Network tests failed: $FAIL"
[ "$FAIL" -eq 0 ]
