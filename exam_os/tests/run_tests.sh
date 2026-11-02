#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"
BIN=./exam_os
TMP=${TMPDIR:-/tmp}/exam_os_tests.$$
trap 'rm -rf "$TMP"' EXIT INT TERM
mkdir -p "$TMP"

pass() { printf 'PASS %s\n' "$1"; }
fail() { printf 'FAIL %s: %s\n' "$1" "$2" >&2; exit 1; }

make clean >/dev/null
make >/dev/null || fail T1 "build failed"
pass T1

[ -x "$BIN" ] || fail T2 "binary was not created"
pass T2

"$BIN" --headless --students 1 --frames 2 --duration 1 --buffer 2 >"$TMP/headless.out" 2>&1 || true
grep -q "Simulation complete" "$TMP/headless.out" || fail T3 "headless run did not complete"
pass T3

if "$BIN" --headless --students 0 --duration 1 >"$TMP/invalid.out" 2>&1; then
    fail T4 "invalid configuration was accepted"
fi
grep -q "Invalid configuration" "$TMP/invalid.out" || fail T4 "invalid configuration was not reported"
pass T4

"$BIN" --headless --students 2 --frames 2 --duration 2 --seed 7 >"$TMP/seed.out" 2>&1 || fail T5 "seeded run failed"
grep -q "Output files" "$TMP/seed.out" || fail T5 "report output was not announced"
pass T5

[ -f output/summary.txt ] || fail T6 "summary report missing"
[ -f output/system_log.txt ] || fail T6 "system log missing"
[ -f output/submissions.txt ] || fail T6 "submission output missing"
pass T6

grep -q "Hit Rate" output/summary.txt || fail T7 "summary metrics missing"
grep -q "EXAM OS SIMULATION LOG" output/system_log.txt || fail T7 "log header missing"
pass T7

make clean >/dev/null
pass T8
printf 'All T1-T8 tests passed.\n'
