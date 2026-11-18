#!/usr/bin/env bash
# Automated functional tests for myshell.
# Run with: make test
set -u

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/myshell"
WORK="$(mktemp -d)"
OUT="$WORK/.stdout"
ERR="$WORK/.stderr"
STATUS=0
PASS=0
FAIL=0

cleanup() {
    rm -rf "$WORK"
}
trap cleanup EXIT

cd "$WORK" || exit 1
export HOME="$WORK/home"
mkdir -p "$HOME"

run_shell() {
    "$BIN" -c "$1" >"$OUT" 2>"$ERR"
    STATUS=$?
}

pass() {
    PASS=$((PASS + 1))
}

fail() {
    FAIL=$((FAIL + 1))
    echo "FAIL: $1" >&2
    if [ -s "$OUT" ]; then
        echo "--- stdout ---" >&2
        sed 's/^/  /' "$OUT" >&2
    fi
    if [ -s "$ERR" ]; then
        echo "--- stderr ---" >&2
        sed 's/^/  /' "$ERR" >&2
    fi
    echo "--- status: $STATUS ---" >&2
}

assert_status() {
    local expected="$1" name="$2"
    if [ "$STATUS" -eq "$expected" ]; then
        pass
    else
        fail "$name (expected status $expected, got $STATUS)"
    fi
}

assert_stdout() {
    local expected="$1" name="$2"
    if [ "$(cat "$OUT")" = "$expected" ]; then
        pass
    else
        fail "$name (stdout mismatch)"
    fi
}

assert_stdout_contains() {
    local needle="$1" name="$2"
    if grep -Fq -- "$needle" "$OUT"; then
        pass
    else
        fail "$name (missing '$needle' in stdout)"
    fi
}

assert_stderr_contains() {
    local needle="$1" name="$2"
    if grep -Fq -- "$needle" "$ERR"; then
        pass
    else
        fail "$name (missing '$needle' in stderr)"
    fi
}

assert_file_contains() {
    local file="$1" needle="$2" name="$3"
    if grep -Fq -- "$needle" "$file"; then
        pass
    else
        fail "$name (missing '$needle' in $file)"
    fi
}

# ------------------------------------------------------------------
echo "== exit and sequencing =="
run_shell 'echo before; exit 3; echo after'
assert_status 3 "exit status"
assert_stdout "before" "exit stops execution"

run_shell 'true'
assert_status 0 "true built-in"
run_shell 'false'
assert_status 1 "false built-in"
run_shell 'echo first; echo second'
assert_stdout $'first\nsecond' "semicolon sequence"

# ------------------------------------------------------------------
echo "== quoting, expansion, comments =="
run_shell 'echo "a b" c'
assert_stdout "a b c" "double quote"
run_shell "echo 'a b' c"
assert_stdout "a b c" "single quote"
run_shell 'echo "$HOME"'
assert_status 0 "HOME expansion"
if [ -n "$(cat "$OUT")" ]; then pass; else fail "HOME expansion empty"; fi
run_shell 'export MSHTEST=hello; echo $MSHTEST ${MSHTEST}'
assert_stdout "hello hello" "variable expansion"
run_shell 'echo $?; false; echo $?'
assert_stdout $'0\n1' "last status expansion"
run_shell 'echo one # this is a comment'
assert_stdout "one" "comment"
run_shell 'echo ~'
assert_status 0 "tilde expansion"
if [ "$(cat "$OUT")" != "~" ]; then pass; else fail "tilde not expanded"; fi

# ------------------------------------------------------------------
echo "== redirection =="
run_shell 'echo hello > out.txt'
assert_status 0 "output redirection"
assert_file_contains out.txt hello "output file"

run_shell 'echo more >> out.txt; cat out.txt'
assert_stdout $'hello\nmore' "append redirection"

run_shell 'cat < out.txt'
assert_stdout $'hello\nmore' "input redirection"

run_shell 'ls out.txt > list.txt; grep out list.txt'
assert_stdout "out.txt" "redirection with pipeline input"

run_shell 'cat missing.txt 2> err.txt'
assert_status 1 "stderr redirection status"
assert_file_contains err.txt "missing.txt" "stderr redirected"
if [ -s err.txt ]; then pass; else fail "stderr file empty"; fi

run_shell 'ls missing.txt &> both.txt'
assert_status 1 "combined redirection status"
assert_file_contains both.txt "missing.txt" "combined redirection"

run_shell 'ls missing1 2>> err2.txt; ls missing2 2>> err2.txt; wc -l < err2.txt'
assert_stdout "2" "append stderr redirection"

run_shell 'pwd > pwd.txt; cat pwd.txt'
assert_status 0 "builtin output redirection"
if [ -s pwd.txt ]; then pass; else fail "pwd redirection file empty"; fi

run_shell 'ls missing2 > all.txt 2>&1'
assert_status 1 "2>&1 status"
assert_file_contains all.txt "missing2" "2>&1 combined file"

# ------------------------------------------------------------------
echo "== pipes =="
run_shell 'echo -e "alpha\nbeta\ngamma" | grep beta'
assert_stdout "beta" "pipe with built-ins"
run_shell 'echo -e "alpha\nbeta\ngamma" | grep -v beta | grep -c .'
assert_stdout "2" "two-stage pipe"
run_shell 'echo hello | cat | grep -n hello'
assert_stdout "1:hello" "three-stage pipe"

run_shell 'true | false'
assert_status 1 "pipeline status from last command"
run_shell 'false | true'
assert_status 0 "pipeline status ignores earlier status"

run_shell 'definitely-not-a-command; echo $?'
assert_stdout "127" "external command not found"

# ------------------------------------------------------------------
echo "== background jobs =="
start_ns=$(date +%s%N)
run_shell 'sleep 1 & echo started'
end_ns=$(date +%s%N)
assert_stdout "started" "background output"
elapsed_ms=$(( (end_ns - start_ns) / 1000000 ))
if [ "$elapsed_ms" -lt 800 ]; then pass; else fail "background did not return early (${elapsed_ms}ms)"; fi

run_shell 'sleep 0.2 & jobs; wait; echo done'
assert_stdout_contains "Running" "jobs shows running job"
assert_stdout_contains "done" "wait completes"

run_shell 'sleep 0.1 & echo $!'
if grep -Eq '^[0-9]+$' "$OUT" && [ "$(cat "$OUT")" != "0" ]; then pass; else fail "\$! not expanded"; fi

run_shell 'sleep 0.1 & sleep 0.3; jobs'
assert_stdout_contains "Done" "jobs reports completed background job"

run_shell 'exit 5 & echo alive'
assert_stdout "alive" "background exit does not exit shell"
assert_status 0 "background exit status"

# ------------------------------------------------------------------
echo "== pwd and cd =="
run_shell 'pwd'
assert_status 0 "pwd"
run_shell 'cd /tmp; pwd'
assert_stdout "/tmp" "cd changes directory"
run_shell 'cd /tmp; cd /; cd -'
assert_stdout "/tmp" "cd - prints previous directory"
run_shell 'cd /definitely/not/here'
assert_status 1 "cd failure"
run_shell 'cd /tmp; pwd; cd /; pwd'
assert_stdout $'/tmp\n/' "multiple cd"

run_shell 'pwd; cd /tmp | cat; pwd'
if [ "$(sed -n '1p' "$OUT")" = "$(sed -n '2p' "$OUT")" ]; then pass; else fail "cd in pipeline affected parent"; fi

# ------------------------------------------------------------------
echo "== mkdir and rmdir =="
run_shell 'mkdir -p d1/d2/d3; test -d d1/d2/d3'
assert_status 0 "mkdir -p"
run_shell 'mkdir d1'
assert_status 1 "mkdir existing"
run_shell 'mkdir -m 700 moded; stat -c %a moded'
assert_stdout "700" "mkdir -m"
run_shell 'rmdir d1'
assert_status 1 "rmdir non-empty"
run_shell 'rmdir -p d1/d2/d3; test ! -e d1'
assert_status 0 "rmdir -p"

# ------------------------------------------------------------------
echo "== ln =="
run_shell 'echo data > target.txt; ln target.txt hard.txt; cat hard.txt'
assert_stdout "data" "hard link"
run_shell 'ln -s target.txt sym.txt; cat sym.txt'
assert_stdout "data" "symbolic link"
run_shell 'ln -s target.txt sym.txt'
assert_status 1 "ln existing without -f"
run_shell 'ln -sf target.txt sym.txt; cat sym.txt'
assert_status 0 "ln -sf"

# ------------------------------------------------------------------
echo "== cp =="
run_shell 'echo copy-me > src.txt; cp src.txt dst.txt; cmp -s src.txt dst.txt'
assert_status 0 "cp file"
run_shell 'mkdir -p srcdir/sub; echo x > srcdir/sub/f; cp -r srcdir dstdir; cat dstdir/sub/f'
assert_stdout "x" "cp -r"
run_shell 'cp -r srcdir srcdir/inside'
assert_status 1 "cp directory into itself"
run_shell 'cp src.txtd 2>/dev/null'
assert_status 2 "cp missing source"

# ------------------------------------------------------------------
echo "== mv =="
run_shell 'echo move-me > m1.txt; mv m1.txt m2.txt; cat m2.txt'
assert_stdout "move-me" "mv rename"
run_shell 'mkdir -p mdir; mv m2.txt mdir; cat mdir/m2.txt'
assert_stdout "move-me" "mv into directory"
run_shell 'mv mdir mdir2; cat mdir2/m2.txt'
assert_stdout "move-me" "mv directory"
run_shell 'mv mdir2 mdir2/sub'
assert_status 1 "mv directory into itself"

# ------------------------------------------------------------------
echo "== rm =="
run_shell 'echo x > r1.txt; rm r1.txt; test ! -e r1.txt'
assert_status 0 "rm file"
run_shell 'mkdir -p rdir/sub; echo y > rdir/sub/f; rm -r rdir; test ! -e rdir'
assert_status 0 "rm -r"
run_shell 'rm -f missing-file'
assert_status 0 "rm -f missing"
run_shell 'mkdir rdir2; rm rdir2'
assert_status 1 "rm directory without -r"

# ------------------------------------------------------------------
echo "== cat =="
printf 'one\ntwo\n\nthree\n' > catfile.txt
run_shell 'cat catfile.txt'
assert_stdout $'one\ntwo\n\nthree' "cat file"
run_shell 'cat -n catfile.txt'
assert_status 0 "cat -n"
if grep -q $'1\tone' "$OUT" && grep -q $'4\tthree' "$OUT"; then pass; else fail "cat -n numbering"; fi
run_shell 'cat -b catfile.txt'
if grep -q $'3\tthree' "$OUT"; then pass; else fail "cat -b numbering"; fi
run_shell 'cat missing-cat'
assert_status 1 "cat missing file"

# ------------------------------------------------------------------
echo "== grep =="
printf 'Apple pie\nbanana\napple tart\n' > grepfile.txt
run_shell 'grep Apple grepfile.txt'
assert_stdout "Apple pie" "grep basic"
run_shell 'grep -i apple grepfile.txt'
assert_stdout $'Apple pie\napple tart' "grep -i"
run_shell 'grep -iv apple grepfile.txt'
assert_stdout "banana" "grep -v"
run_shell 'grep -n apple grepfile.txt'
assert_stdout $'3:apple tart' "grep -n"
run_shell 'grep -c apple grepfile.txt'
assert_stdout "1" "grep -c"
run_shell 'grep -F "Apple pie" grepfile.txt'
assert_stdout "Apple pie" "grep -F"
run_shell 'grep -E "^(Apple|banana)" grepfile.txt'
assert_stdout $'Apple pie\nbanana' "grep -E"
run_shell 'grep -q apple grepfile.txt'
assert_status 0 "grep -q match"
run_shell 'grep -q zzz grepfile.txt'
assert_status 1 "grep -q no match"
run_shell 'mkdir -p gdir/sub; echo needle > gdir/sub/a.txt; grep -r needle gdir'
assert_stdout_contains "needle" "grep -r"
assert_stdout_contains "gdir/sub/a.txt" "grep -r filename prefix"

# ------------------------------------------------------------------
echo "== ls =="
run_shell 'ls out.txt'
assert_stdout "out.txt" "ls file"
run_shell 'ls -a'
assert_stdout_contains "out.txt" "ls -a"
run_shell 'ls -l out.txt'
assert_status 0 "ls -l"
if grep -Eq '^-[-rwx]{9}' "$OUT"; then pass; else fail "ls -l mode"; fi
run_shell 'mkdir -p ldir/sub; ls -R ldir'
assert_stdout_contains "ldir/sub" "ls -R"

# ------------------------------------------------------------------
echo "== syntax errors =="
run_shell 'echo "unterminated'
assert_status 2 "unterminated quote"
run_shell 'echo a |'
assert_status 2 "trailing pipe"
run_shell 'echo one; echo two'
assert_stdout $'one\ntwo' "shell continues after previous errors"

# ------------------------------------------------------------------
echo
echo "Passed: $PASS"
echo "Failed: $FAIL"
if [ "$FAIL" -ne 0 ]; then
    exit 1
fi
exit 0
