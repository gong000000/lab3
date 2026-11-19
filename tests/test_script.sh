#!/usr/bin/env bash
# Tests for command substitution, if/for/while, functions, and scripts.
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
    timeout 5 "$BIN" -c "$1" >"$OUT" 2>"$ERR"
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
stdout_lacks() {
    if grep -Fq -- "$1" "$OUT"; then fail "$2 (unexpected $1)"; else pass; fi
}
stderr_has() {
    if grep -Fq -- "$1" "$ERR"; then pass; else fail "$2 (missing $1)"; fi
}
file_is() {
    if [ "$(cat "$1" 2>/dev/null)" = "$2" ]; then pass; else fail "$3 (file $1)"; fi
}

# ------------------------------------------------------------------
echo "== command substitution =="
run 'echo $(echo inside)'
status_is 0 "subst status"
stdout_is "subst" "inside"
run 'x=$(echo value); echo $x'
stdout_is "subst assign" "value"
run 'echo "pre $(echo mid) post"'
stdout_is "subst quote" "pre mid post"
run 'echo $(echo $(echo deep))'
stdout_is "subst nested" "deep"
run 'x="a b"; echo "$x"'
stdout_is "quoted assignment" "a b"

# ------------------------------------------------------------------
echo "== if =="
run 'if true; then echo yes; fi'
status_is 0 "if true status"
stdout_is "if true" "yes"
run 'if false; then echo no; else echo yes; fi'
stdout_is "if else" "yes"
run 'if false; then echo one; elif true; then echo two; else echo three; fi'
stdout_is "if elif" "two"
run 'if false; then echo one; elif false; then echo two; else echo three; fi'
stdout_is "if else branch" "three"
run 'if ! false; then echo not; fi'
stdout_is "if not" "not"
run 'if [ 3 -gt 2 ]; then echo gt; fi'
stdout_is "if test" "gt"
run 'if [ -z "" ]; then echo empty; fi'
stdout_is "if test -z" "empty"
run 'if true; then if true; then echo nested; fi; fi'
stdout_is "nested if" "nested"

# ------------------------------------------------------------------
echo "== for =="
run 'for i in a b c; do echo $i; done'
status_is 0 "for status"
stdout_is "for list" "a
b
c"
run 'for i in 1 2 3 4; do if [ $i = 2 ]; then continue; fi; if [ $i = 4 ]; then break; fi; echo $i; done'
stdout_is "for break/continue" "1
3"
run 'for i in; do echo no; done; echo after'
stdout_is "for empty" "after"
run 'f() { for i in "$1" "$2"; do echo $i; done; }; f x y'
stdout_is "for function args" "x
y"
run 'for i in "a b" c; do echo "[$i]"; done'
stdout_is "for quoted word" "[a b]
[c]"

# ------------------------------------------------------------------
echo "== while / until =="
run 'i=; while [ ${i}aa != aaaaaa ]; do i=${i}a; done; echo $i'
stdout_is "while loop" "aaaa"
run 'i=; until [ ${i}aa = aaaaaa ]; do i=${i}a; done; echo $i'
stdout_is "until loop" "aaaa"
run 'i=0; while false; do echo no; done; echo ok'
stdout_is "while false" "ok"
printf 'one\ntwo\nthree\n' > lines.txt
run 'while read line; do echo "[$line]"; done < lines.txt'
status_is 0 "read loop status"
stdout_is "read loop" "[one]
[two]
[three]"
printf 'a b c\n' > fields.txt
run 'read x y z < fields.txt; echo "$x-$y-$z"'
stdout_is "read fields" "a-b-c"
run 'read x < fields.txt; shift; echo ok'
stdout_is "shift" "ok"

# ------------------------------------------------------------------
echo "== functions =="
run 'greet() { echo hello $1; }; greet world'
status_is 0 "function status"
stdout_is "function" "hello world"
run 'f() { echo $#:$1:$2; }; f a b'
stdout_is "function args" "2:a:b"
run 'f() { shift; echo $1/$#; }; f a b c'
stdout_is "function shift" "b/2"
run 'f() { return 7; }; f; echo rc=$?'
stdout_is "function return" "rc=7"
run 'f() { if true; then return 3; fi; echo no; }; f; echo rc=$?'
stdout_is "return in if" "rc=3"
run 'a() { echo a; }; b() { a; echo b; }; b'
stdout_is "nested calls" "a
b"
run 'echo() { /bin/echo shadow "$@"; }; echo hi'
stdout_is "function shadows external" "shadow hi"
run 'f() { echo pipe; }; f | cat'
stdout_is "function in pipeline" "pipe"

# ------------------------------------------------------------------
echo "== scripts =="
cat > script.sh <<'EOS'
    for i in "$1" "$2"; do
        echo "arg:$i"
done
echo "count:$#"
EOS
timeout 5 "$BIN" script.sh one two >"$OUT" 2>"$ERR"
STATUS=$?
status_is 0 "script status"
stdout_is "script args" "arg:one
arg:two
count:2"
cat > source.sh <<'EOS'
sourced=yes
echo "source:$1"
EOS
run 'source source.sh hello; echo "var:$sourced"'
stdout_is "source" "source:hello
var:yes"
cat > multiline.sh <<'EOS'
i=
while [ "$i" != "aaa" ]; do
    i="${i}a"
    echo "$i"
done
EOS
timeout 5 "$BIN" multiline.sh >"$OUT" 2>"$ERR"
STATUS=$?
stdout_is "script while" "a
aa
aaa"
run 'if true
then
  echo multiline
fi'
stdout_is "-c multiline" "multiline"
run 'echo before # comment
echo after'
stdout_is "comments" "before
after"
run 'if true; then echo x; fi # trailing'
stdout_is "trailing comment" "x"
run 'if true; then echo redirected; fi > ifout.txt'
status_is 0 "if redirection status"
file_is ifout.txt "redirected" "if redirection"
run 'for i in a b; do echo $i; done > forout.txt'
status_is 0 "for redirection status"
file_is forout.txt "a
b" "for redirection"

# ------------------------------------------------------------------
echo "== errors =="
run 'if true; then echo x'
status_is 2 "missing fi status"
stderr_has "missing 'fi'" "missing fi message"
run 'for i in a b; do echo x'
status_is 2 "missing done status"
run 'fi'
status_is 2 "stray fi status"
run 'f() { echo x; '
status_is 2 "unterminated function status"
run 'echo $(echo x'
status_is 2 "unterminated substitution"

# ------------------------------------------------------------------
echo
if [ "$FAIL" -eq 0 ]; then
    echo "Script tests passed: $PASS"
    exit 0
fi
echo "Script tests passed: $PASS"
echo "Script tests failed: $FAIL"
exit 1
