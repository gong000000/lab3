#!/usr/bin/env bash
# Manual smoke test for myshell (not part of `make test`; kept for demos).
set -u
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BIN="$ROOT/myshell"
WORK="$(mktemp -d)"
cd "$WORK" || exit 1
trap 'rm -rf "$WORK"' EXIT

printf 'hello world\nsecond line\nHELLO again\n' > sample.txt

echo "--- exit status ---"
"$BIN" -c "exit 7"
echo "status=$?"

echo "--- echo redirection ---"
"$BIN" -c "echo hello > out.txt"
cat out.txt
"$BIN" -c "echo more >> out.txt"
cat out.txt

echo "--- input redirection ---"
"$BIN" -c "cat < sample.txt"

echo "--- pipe ---"
"$BIN" -c "cat sample.txt | grep -n hello"

echo "--- pipe chain ---"
"$BIN" -c "cat sample.txt | grep -i hello | grep -v again"

echo "--- external ---"
"$BIN" -c "date +%Y"

echo "--- builtins ---"
"$BIN" -c "pwd; mkdir -p a/b/c; ls a; rmdir -p a/b/c; ls"

echo "--- cp/mv/rm/ln ---"
"$BIN" -c "mkdir -p a; echo data > a/file.txt; cp -r a b; ls; mv b c2; ln -s c2 link; ls -l; rm -r c2 link a; ls"

echo "--- background ---"
"$BIN" -c "sleep 0.2 & echo started; jobs; wait; echo done"
echo "bg_status=$?"
