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

"$BIN" --headless --workload LEGACY --students 10 --duration 80 --limit 10 --seed 1 >"$TMP/timeout.out" 2>&1 || fail T9 "timeout run failed"
grep -Eq 'Timeouts Fired[[:space:]]*:[[:space:]]*[1-9][0-9]*' output/summary.txt || fail T9 "no timeout was fired"
grep -q "\[PARTIAL\]" output/submissions.txt || fail T9 "partial submission missing"
if grep -o "PID [0-9]* terminated" output/system_log.txt | sort | uniq -d | grep -q .; then
    fail T9 "duplicate termination log found"
fi
pass T9

for ALGO in RR PRIORITY
do
    "$BIN" --headless --students 20 --duration 100 --algo "$ALGO" --seed 3 >"$TMP/$ALGO.out" 2>&1 ||
        fail T10 "$ALGO run failed"
    dispatches=$(grep 'Dispatches' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
    switches=$(grep 'Context Switches' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
    [ "$dispatches" -gt 0 ] || fail T10 "$ALGO had no dispatches"
    [ "$dispatches" -ge "$switches" ] || fail T10 "$ALGO dispatch/switch count invalid"
done
pass T10

run_t11() {
    "$BIN" --headless --students 40 --duration 100 --arrival-batch 10 --arrival-interval 5 --seed 4 >"$TMP/t11.out" 2>&1 ||
        fail T11 "configurable arrival run failed"
}
run_t11
pass T11

if "$BIN" --headless --students 0 --duration 1 >"$TMP/t12.out" 2>&1; then
    fail T12 "invalid configuration was accepted"
fi
grep -q "Invalid configuration" "$TMP/t12.out" || fail T12 "invalid configuration was not reported"
pass T12

for CASE in \
    "--algo RR --students 20 --duration 100" \
    "--algo PRIORITY --students 20 --duration 100" \
    "--page FIFO --frames 8 --students 20 --duration 100" \
    "--demo --buffer 8 --students 20 --duration 100" \
    "--limit 10 --students 10 --duration 80"
do
    "$BIN" --headless --strict --workload LEGACY --seed 11 $CASE >"$TMP/t13.out" 2>&1 ||
        fail T13 "strict run failed: $CASE"
    if grep -q '^FAIL ' output/invariants.txt; then
        fail T13 "invariant failure: $CASE: $(grep '^FAIL ' output/invariants.txt)"
    fi
done
pass T13

total=$(grep 'Total Submissions' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
records=$(grep -c '^\[' output/submissions.txt || true)
[ "$records" -eq "$total" ] || fail T14 "submission records=$records total=$total"
pass T14

for WORKLOAD in LEGACY INTERACTIVE
do
    "$BIN" --headless --strict --workload "$WORKLOAD" --students 10 --duration 40 --seed 11 >"$TMP/workload.out" 2>&1 ||
        fail T15 "$WORKLOAD workload failed"
    grep -q '^PASS I[1-6]' output/invariants.txt ||
        fail T15 "$WORKLOAD invariant failure"
done
pass T15

for ALGO in RR PRIORITY
do
    "$BIN" --headless --strict --tick-ms 10 --students 3 --duration 150 --algo "$ALGO" --seed 1 >"$TMP/t16-$ALGO.out" 2>&1 ||
        fail T16 "$ALGO tick-driven run failed"
    dispatches=$(grep 'Dispatches' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
    [ "$dispatches" -gt 0 ] || fail T16 "$ALGO had no dispatches"
done
pass T16

for ALGO in RR PRIORITY
do
    "$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
        --students 12 --duration 300 --algo "$ALGO" --seed 21 >"$TMP/t17-$ALGO.out" 2>&1 ||
        fail T17 "$ALGO interactive run failed"
    if grep -q '^FAIL ' output/invariants.txt; then
        fail T17 "$ALGO interactive invariant failure"
    fi
    completed=$(grep 'Completed Exams' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
    [ "$completed" -ge 10 ] || fail T17 "$ALGO completed fewer than 10 exams"
done
pass T17

"$BIN" --headless --strict --tick-ms 10 --workload LEGACY \
    --students 5 --duration 100 --seed 2 >"$TMP/t18.out" 2>&1 ||
    fail T18 "legacy run failed"
pass T18

for ALGO in RR PRIORITY
do
    "$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
        --students 12 --duration 300 --algo "$ALGO" --seed 21 >"$TMP/t19-$ALGO.out" 2>&1 ||
        fail T19 "$ALGO waiting workload failed"
    blocked=$(grep 'Blocked Waits' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
    wakeups=$(grep 'Wakeups' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
    completed=$(grep 'Completed Exams' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
    [ "$blocked" -gt 0 ] || fail T19 "$ALGO had no blocked waits"
    [ "$wakeups" -gt 0 ] || fail T19 "$ALGO had no wakeups"
    [ "$wakeups" -le "$blocked" ] || fail T19 "$ALGO wakeups exceeded blocked waits"
    [ "$completed" -ge 10 ] || fail T19 "$ALGO completed fewer than 10 exams"
done
pass T19

"$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE --limit 15 \
    --students 12 --duration 300 --seed 21 >"$TMP/t20.out" 2>&1 ||
    fail T20 "interactive timeout run failed"
timeouts=$(grep 'Timeouts Fired' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
[ "$timeouts" -gt 0 ] || fail T20 "interactive timeout run had no timeouts"
if grep -o "PID [0-9]* terminated" output/system_log.txt | sort | uniq -d | grep -q .; then
    fail T20 "duplicate termination log found"
fi
pass T20

for ALGO in FCFS SJF SRTF
do
    "$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
        --students 12 --duration 300 --algo "$ALGO" --seed 21 >"$TMP/t21-$ALGO.out" 2>&1 ||
        fail T21 "$ALGO interactive run failed"
    if grep -q '^FAIL ' output/invariants.txt; then
        fail T21 "$ALGO interactive invariant failure"
    fi
    completed=$(grep 'Completed Exams' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
    [ "$completed" -ge 8 ] || fail T21 "$ALGO completed fewer than 8 exams"
done
pass T21

"$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
    --students 12 --duration 300 --algo RR --switch-cost 2 --seed 21 >"$TMP/t22.out" 2>&1 ||
    fail T22 "switch-cost run failed"
overhead=$(grep 'Overhead Ticks' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
[ "$overhead" -gt 0 ] || fail T22 "switch cost produced no overhead ticks"
pass T22

for ALGO in PRIO MLFQ
do
    "$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
        --students 12 --duration 300 --algo "$ALGO" --seed 21 >"$TMP/t23-$ALGO.out" 2>&1 ||
        fail T23 "$ALGO interactive run failed"
    if grep -q '^FAIL ' output/invariants.txt; then
        fail T23 "$ALGO interactive invariant failure"
    fi
    completed=$(grep 'Completed Exams' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
    [ "$completed" -ge 8 ] || fail T23 "$ALGO completed fewer than 8 exams"
done
pass T23

"$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
    --students 12 --duration 300 --algo PRIO --preempt 0 --seed 21 >"$TMP/t24.out" 2>&1 ||
    fail T24 "PRIO non-preemptive run failed"
pass T24

"$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
    --students 12 --duration 300 --algo PRIO --aging 0 --seed 21 >"$TMP/t25.out" 2>&1 ||
    fail T25 "PRIO no-aging run failed"
pass T25

"$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
    --students 12 --duration 300 --algo MLFQ --mlfq-boost 0 --seed 21 >"$TMP/t26.out" 2>&1 ||
    fail T26 "MLFQ no-boost run failed"
pass T26

if "$BIN" --headless --algo BOGUS >"$TMP/t27.out" 2>&1; then
    fail T27 "invalid algorithm was accepted"
fi
pass T27

make clean >/dev/null
pass T8
printf 'All T1-T27 tests passed.\n'
