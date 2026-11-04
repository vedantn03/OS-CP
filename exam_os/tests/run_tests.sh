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
make synclab >/dev/null || fail T41 "synclab build failed"
make memlab >/dev/null || { echo "FAIL memlab build failed" >&2; exit 1; }
make fslab >/dev/null || { echo "FAIL fslab build failed" >&2; exit 1; }
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

run_page_case() {
    "$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
        --students 12 --duration 300 --seed 21 "$@" >"$TMP/page.out" 2>&1 ||
        return 1
    ! grep -q '^FAIL ' output/invariants.txt
}

run_page_case --frames 24 --page CLOCK || fail T28 "CLOCK run failed"
pass T28

run_page_case --frames 24 --page FIFO || fail T29 "FIFO run failed"
evictions=$(grep 'Evictions' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
[ "$evictions" -gt 0 ] || fail T29 "FIFO had no evictions"
pass T29

run_page_case --frames 24 --write-pct 0 || fail T30 "read-only run failed"
dirty=$(grep 'Dirty Writebacks' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
[ "$dirty" -eq 0 ] || fail T30 "read-only run had dirty writebacks"
pass T30

"$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
    --students 12 --duration 300 --frames 24 --page-trace --seed 21 >"$TMP/page-trace.out" 2>&1 ||
    fail T31 "page-trace run failed"
faults=$(grep 'Page Faults' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
hits=$(grep 'Page Hits' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
trace_lines=$(wc -l < output/pagetrace.txt)
[ "$trace_lines" -eq $((faults + hits)) ] || fail T31 "page trace count mismatch"
pass T31

if "$BIN" --headless --page BOGUS >"$TMP/bad-page.out" 2>&1; then
    fail T32 "invalid page algorithm accepted"
fi
pass T32

if "$BIN" --headless --ws 99 >"$TMP/bad-ws.out" 2>&1; then
    fail T33 "invalid working set accepted"
fi
pass T33

make pagesim >/dev/null || fail T34 "pagesim build failed"
fifo=$(./pagesim --algo FIFO --frames 3 --string "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1" |
    sed -n 's/^faults: //p')
lru=$(./pagesim --algo LRU --frames 3 --string "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1" |
    sed -n 's/^faults: //p')
opt=$(./pagesim --algo OPT --frames 3 --string "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1" |
    sed -n 's/^faults: //p')
[ "$fifo" -eq 15 ] && [ "$lru" -eq 12 ] && [ "$opt" -eq 9 ] || fail T34 "pagesim textbook counts"
pass T34

belady3=$(./pagesim --algo FIFO --frames 3 --string "1 2 3 4 1 2 5 1 2 3 4 5" |
    sed -n 's/^faults: //p')
belady4=$(./pagesim --algo FIFO --frames 4 --string "1 2 3 4 1 2 5 1 2 3 4 5" |
    sed -n 's/^faults: //p')
[ "$belady3" -eq 9 ] && [ "$belady4" -eq 10 ] || fail T35 "Belady counts"
pass T35

"$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
    --students 60 --duration 500 --frames 24 --seed 1 \
    --thrash-high 35 --thrash-control 1 >"$TMP/thrash-control.out" 2>&1 ||
    fail T36 "thrash-control run failed"
deferred=$(grep 'Deferred Arrival Ticks' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
[ "$deferred" -gt 0 ] || fail T36 "thrash control did not defer arrivals"
if grep -q '^FAIL ' output/invariants.txt; then fail T36 "thrash-control invariant failure"; fi
pass T36

"$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
    --students 60 --duration 500 --frames 24 --seed 1 \
    --thrash-high 35 --thrash-control 0 >"$TMP/no-thrash-control.out" 2>&1 ||
    fail T37 "no-control run failed"
deferred=$(grep 'Deferred Arrival Ticks' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
[ "$deferred" -eq 0 ] || fail T37 "disabled thrash control deferred arrivals"
pass T37

make disksim >/dev/null || fail T38 "disksim build failed"
DISK_QUEUE="98 183 37 122 14 124 65 67"
disk_movement() {
    "$ROOT/disksim" --algo "$1" --head 53 --cylinders 200 \
        --string "$DISK_QUEUE" | sed -n 's/^movement: //p'
}
[ "$(disk_movement FIFO)" -eq 640 ] || fail T38 "FIFO movement mismatch"
[ "$(disk_movement SSTF)" -eq 236 ] || fail T38 "SSTF movement mismatch"
[ "$(disk_movement SCAN)" -eq 331 ] || fail T38 "SCAN movement mismatch"
[ "$(disk_movement CSCAN)" -eq 382 ] || fail T38 "C-SCAN movement mismatch"
if "$ROOT/disksim" --algo BOGUS --head 53 --cylinders 200 \
    --string "$DISK_QUEUE" >/dev/null 2>&1; then
    fail T38 "invalid disk algorithm was accepted"
elif [ "$?" -ne 2 ]; then
    fail T38 "invalid disk algorithm exit code"
fi
if "$ROOT/disksim" --algo FIFO --head 53 --cylinders 200 \
    --string "999" >/dev/null 2>&1; then
    fail T38 "out-of-range disk request was accepted"
elif [ "$?" -ne 1 ]; then
    fail T38 "out-of-range disk request exit code"
fi
pass T38

for DISK_ALGO in FIFO LIFO SSTF SCAN CSCAN
do
    "$BIN" --headless --strict --tick-ms 10 --workload INTERACTIVE \
        --students 12 --duration 300 --algo RR --frames 16 --write-pct 40 \
        --disk "$DISK_ALGO" --disk-trace --seed 5 >"$TMP/disk.out" 2>&1 ||
        fail T39 "$DISK_ALGO disk run failed"
    if grep -q '^FAIL ' output/invariants.txt; then
        fail T39 "$DISK_ALGO disk invariant failure"
    fi
    requests=$(grep 'Disk Requests' output/summary.txt | sed 's/.*: *//; s/[^0-9].*//')
    [ "$requests" -gt 0 ] || fail T39 "$DISK_ALGO had no disk requests"
    [ -s output/disktrace.txt ] || fail T39 "$DISK_ALGO trace missing"
    head -1 output/disktrace.txt | grep -q '^# cylinders=' ||
        fail T39 "$DISK_ALGO trace header missing"
done
if "$BIN" --headless --disk BOGUS >"$TMP/bad-disk.out" 2>&1; then
    fail T39 "invalid disk algorithm accepted"
fi
if "$BIN" --headless --disk-start 999 >"$TMP/bad-disk-start.out" 2>&1; then
    fail T39 "invalid disk start accepted"
fi
pass T39

make deadsim >/dev/null || fail T40 "deadsim build failed"
"$PWD/deadsim" safe --file scenarios/banker_textbook.txt >"$TMP/dead-safe.out" ||
    fail T40 "safe textbook state failed"
grep -q '^sequence: 1 3 0 2 4$' "$TMP/dead-safe.out" ||
    fail T40 "textbook safe sequence mismatch"
"$PWD/deadsim" request --file scenarios/banker_textbook.txt --pid 1 --req "1 0 2" >"$TMP/dead-request.out" ||
    fail T40 "textbook resource request failed"
grep -q '^result: granted$' "$TMP/dead-request.out" ||
    fail T40 "textbook request was not granted"
"$PWD/deadsim" detect --file scenarios/detect_textbook_deadlock.txt >"$TMP/dead-detect.out" ||
    fail T40 "textbook detection failed"
grep -q '^deadlocked: 1 2 3 4$' "$TMP/dead-detect.out" ||
    fail T40 "textbook deadlocked set mismatch"
"$PWD/deadsim" recover --file scenarios/detect_textbook_deadlock.txt --policy min-alloc >"$TMP/dead-recover.out" ||
    fail T40 "textbook recovery failed"
grep -q '^aborted: 1$' "$TMP/dead-recover.out" ||
    fail T40 "min-alloc victim mismatch"
"$PWD/deadsim" wfg --nodes 3 --edges "0>1 1>2 2>0" >"$TMP/dead-wfg.out" ||
    fail T40 "wait-for graph failed"
grep -q '^on_cycle: 0 1 2$' "$TMP/dead-wfg.out" ||
    fail T40 "wait-for graph cycle mismatch"
if "$PWD/deadsim" bogus >/dev/null 2>&1; then
    fail T40 "unknown command was accepted"
elif [ "$?" -ne 2 ]; then
    fail T40 "unknown command exit code mismatch"
fi
pass T40

counter_mutex=$(./synclab counter --mode mutex --threads 4 --iters 200000 | sed -n 's/^final: //p')
[ "$counter_mutex" -eq 800000 ] || fail T41 "synclab mutex counter mismatch"
pass T41

./synclab counter --mode sem --threads 4 --iters 100000 >"$TMP/synclab-sem.out" || fail T42 "synclab semaphore counter failed"
grep -q '^correct: yes$' "$TMP/synclab-sem.out" || fail T42 "synclab semaphore counter mismatch"
pass T42

counter_peterson=$(./synclab counter --mode peterson --threads 2 --iters 200000 | sed -n 's/^final: //p')
[ "$counter_peterson" -eq 400000 ] || fail T43 "synclab Peterson counter mismatch"
pass T43

counter_peterson=$(./synclab counter --mode peterson --threads 2 --iters 200000 | sed -n 's/^final: //p')
[ "$counter_peterson" -eq 400000 ] || fail T44 "synclab repeated Peterson counter mismatch"
pass T44

./synclab rw --policy fair --readers 4 --writers 2 --ops 100 --hold-us 100 >"$TMP/synclab-rw.out" || fail T45 "synclab fair rw failed"
grep -q '^torn_reads: 0$' "$TMP/synclab-rw.out" && grep -q '^violations: 0$' "$TMP/synclab-rw.out" && grep -q '^reads_done: 400$' "$TMP/synclab-rw.out" && grep -q '^writes_done: 200$' "$TMP/synclab-rw.out" || fail T45 "synclab fair rw counts mismatch"
pass T45

./synclab phil --mode ordered --n 5 --meals 30 --eat-us 100 >"$TMP/synclab-phil.out" || fail T46 "synclab ordered philosophers failed"
grep -q '^deadlock: no$' "$TMP/synclab-phil.out" && grep -q '^meals_eaten: 150$' "$TMP/synclab-phil.out" || fail T46 "synclab ordered philosopher result mismatch"
pass T46

./synclab phil --mode naive --n 5 --timeout-ms 150 >"$TMP/synclab-naive.out" || fail T47 "synclab naive deadlock demo failed"
grep -q '^deadlock: yes$' "$TMP/synclab-naive.out" || fail T47 "synclab naive deadlock was not detected"
pass T47

if ./synclab counter --mode peterson --threads 3 --iters 10 >/dev/null 2>&1; then
    fail T48 "synclab invalid Peterson thread count accepted"
elif [ "$?" -ne 2 ]; then
    fail T48 "synclab invalid Peterson exit code mismatch"
fi
pass T48

./memlab contiguous --policy first --holes "100 500 200 300 600" --requests "212 417 112 426" >"$TMP/memlab-first.out" || fail T49 "memlab first-fit failed"
grep -q '^allocated: 3$' "$TMP/memlab-first.out" && grep -q '^failed: 1$' "$TMP/memlab-first.out" && grep -q '^external_fragmentation: yes$' "$TMP/memlab-first.out" || fail T49 "memlab first-fit fragmentation mismatch"
pass T49

./memlab contiguous --policy best --holes "100 500 200 300 600" --requests "212 417 112 426" >"$TMP/memlab-best.out" || fail T50 "memlab best-fit failed"
grep -q '^allocated: 4$' "$TMP/memlab-best.out" && grep -q '^failed: 0$' "$TMP/memlab-best.out" || fail T50 "memlab best-fit result mismatch"
pass T50

./memlab contiguous --policy worst --holes "100 500 200 300 600" --requests "212 417 112 426" >"$TMP/memlab-worst.out" || fail T51 "memlab worst-fit failed"
grep -q '^external_fragmentation: yes$' "$TMP/memlab-worst.out" || fail T51 "memlab worst-fit fragmentation mismatch"
pass T51

./memlab buddy --total 1024 --min-block 32 --requests "100 200 60 500" >"$TMP/memlab-buddy.out" || fail T52 "memlab buddy allocator failed"
grep -q '^allocated: 4$' "$TMP/memlab-buddy.out" && grep -q '^internal_fragmentation: 100$' "$TMP/memlab-buddy.out" || fail T52 "memlab buddy rounding mismatch"
pass T52

./memlab slab --page-size 4096 --classes "16 32 64 128 256" --objects "12 24 40 100 200 12" >"$TMP/memlab-slab.out" || fail T53 "memlab slab allocator failed"
grep -q '^allocated: 6$' "$TMP/memlab-slab.out" && grep -q '^internal_fragmentation: 124$' "$TMP/memlab-slab.out" && grep -q '^slabs: 5$' "$TMP/memlab-slab.out" || fail T53 "memlab slab result mismatch"
pass T53

if ./memlab buddy --total 1000 --min-block 32 --requests "12" >/dev/null 2>&1; then
    fail T54 "memlab accepted a non-power-of-two heap"
elif [ "$?" -ne 2 ]; then
    fail T54 "memlab invalid heap exit code mismatch"
fi
pass T54

./fslab --blocks 16 --method contiguous --files "alpha:3,beta:4,gamma:5" >"$TMP/fslab-contiguous.out" || fail T55 "fslab contiguous allocation failed"
grep -q '^directory: alpha(size=3,start=0); beta(size=4,start=3); gamma(size=5,start=7)$' "$TMP/fslab-contiguous.out" && grep -q '^bitmap: 1111111111110000$' "$TMP/fslab-contiguous.out" || fail T55 "fslab contiguous directory or bitmap mismatch"
pass T55

./fslab --blocks 10 --method linked --files "alpha:3,beta:4" >"$TMP/fslab-linked.out" || fail T56 "fslab linked allocation failed"
grep -q '^layout: alpha=0->1->2; beta=3->4->5->6$' "$TMP/fslab-linked.out" && grep -q '^free_blocks: 3$' "$TMP/fslab-linked.out" || fail T56 "fslab linked layout mismatch"
pass T56

./fslab --blocks 10 --method indexed --files "alpha:3,beta:4" >"$TMP/fslab-indexed.out" || fail T57 "fslab indexed allocation failed"
grep -q '^layout: alpha=index:0\[1,2,3\]; beta=index:4\[5,6,7,8\]$' "$TMP/fslab-indexed.out" && grep -q '^free_blocks: 1$' "$TMP/fslab-indexed.out" || fail T57 "fslab index layout mismatch"
pass T57

./fslab --blocks 10 --method indexed --files "alpha:3,beta:4" --free alpha >"$TMP/fslab-free.out" || fail T58 "fslab file release failed"
grep -q '^directory: beta(size=4,index=4)$' "$TMP/fslab-free.out" && grep -q '^bitmap: 0000111110$' "$TMP/fslab-free.out" || fail T58 "fslab free-space update mismatch"
pass T58

./fslab --blocks 5 --method contiguous --files "alpha:3,beta:3" >"$TMP/fslab-full.out" || fail T59 "fslab capacity case failed"
grep -q '^files_allocated: 1$' "$TMP/fslab-full.out" && grep -q '^files_failed: 1$' "$TMP/fslab-full.out" || fail T59 "fslab full-volume result mismatch"
pass T59

if ./fslab --blocks 8 --method bogus --files "alpha:2" >/dev/null 2>&1; then
    fail T60 "fslab accepted an unknown allocation method"
elif [ "$?" -ne 2 ]; then
    fail T60 "fslab invalid method exit code mismatch"
fi
pass T60

make clean >/dev/null
pass T8
printf 'All T1-T60 tests passed.\n'
