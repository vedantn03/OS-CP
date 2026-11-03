#!/usr/bin/env bash
# Independent acceptance gate. Usage: bash tests/gate.sh 2.0|2.1|2.2a|2.2b
# Cumulative: each stage re-checks the earlier ones. Hand-written; Copilot must not edit it.
cd "$(dirname "$0")/.." || exit 1
case "${1:-}" in 2.0) R=0;; 2.1) R=1;; 2.2a) R=2;; 2.2b) R=3;; *) echo "usage: gate.sh 2.0|2.1|2.2a|2.2b"; exit 2;; esac
fail=0
ok()    { echo "PASS  $1"; }
bad()   { echo "FAIL  $1"; fail=1; }
has()   { d=$1; p=$2; shift 2; if grep -q -- "$p" "$@" 2>/dev/null; then ok "$d"; else bad "$d"; fi; }
hasnt() { d=$1; p=$2; shift 2; if grep -n -- "$p" "$@" >/tmp/gate_hits.txt 2>/dev/null; then bad "$d"; head -3 /tmp/gate_hits.txt; else ok "$d"; fi; }
val()   { sed -n "s/.*$2 *: *\([0-9][0-9.]*\).*/\1/p" "$1" | head -1; }
g()     { v=$(val "/tmp/gate_$1.sum" "$2"); echo "${v:-0}"; }
chk()   { d=$1; shift; if awk "BEGIN{exit !($*)}" 2>/dev/null; then ok "$d"; else bad "$d   [$*]"; fi; }
run()   { n=$1; shift
          ./exam_os --headless --strict --tick-ms 10 "$@" >"/tmp/gate_$n.log" 2>&1; RC=$?
          cp output/summary.txt "/tmp/gate_$n.sum" 2>/dev/null; cp output/invariants.txt "/tmp/gate_$n.inv" 2>/dev/null
          if [ $RC -eq 0 ]; then ok "$n: exit 0"; else bad "$n: exit $RC"; fi
          if [ ! -s "/tmp/gate_$n.inv" ]; then bad "$n: no invariants.txt"
          elif grep -q FAIL "/tmp/gate_$n.inv"; then bad "$n: invariant FAIL"; else ok "$n: invariants"; fi; }

make clean >/dev/null 2>&1
make >/tmp/gate_make.txt 2>&1 || { echo "FAIL  build"; tail -20 /tmp/gate_make.txt; exit 1; }
if grep -qi warning /tmp/gate_make.txt; then bad "build: warnings"; else ok "build: no warnings"; fi
mkdir -p output
LEG=""; [ $R -ge 2 ] && LEG="--workload LEGACY"

echo "--- 2.0 infrastructure"
has   "sim_sleep_ticks defined"        "sim_sleep_ticks" src/clock.c
has   "uses nanosleep"                 "nanosleep" src/clock.c
hasnt "no usleep(tick_ms) in main/memory/io/interrupt" "usleep((useconds_t)g_config.tick_ms" src/main.c src/memory.c src/io_buffer.c src/interrupt.c
has   "tick_cv in SystemState"         "tick_cv" include/shared.h
has   "tick thread broadcasts"         "pthread_cond_broadcast" src/main.c
has   "RNG is per-thread or locked"    "_Thread_local\|__thread\|pthread_mutex" src/rng.c
arr() { sed -n 's/.*\(PID [0-9]* added to ready queue (remaining=[0-9]* ticks)\).*/\1/p' output/system_log.txt; }
run a $LEG --students 20 --duration 60 --seed 5; arr >/tmp/gate_a.arr
run b $LEG --students 20 --duration 60 --seed 5; arr >/tmp/gate_b.arr
run c $LEG --students 20 --duration 60 --seed 6; arr >/tmp/gate_c.arr
[ -s /tmp/gate_a.arr ] && ok "arrival lines present" || bad "arrival lines present"
cmp -s /tmp/gate_a.arr /tmp/gate_b.arr && ok "same seed -> same arrivals" || bad "same seed -> same arrivals"
cmp -s /tmp/gate_a.arr /tmp/gate_c.arr && bad "different seed -> different arrivals" || ok "different seed -> different arrivals"

if [ $R -ge 1 ]; then
echo "--- 2.1 tick-accurate scheduler"
has   "wait_for_tick in scheduler"     "wait_for_tick" src/scheduler.c
hasnt "no usleep in scheduler"         "usleep" src/scheduler.c
has   "busy_ticks field"               "busy_ticks" include/shared.h
has   "report: Busy Ticks"             "Busy Ticks" src/logger.c
has   "invariant I7"                   "I7" src/invariants.c
has   "invariant I8"                   "I8" src/invariants.c
for A in RR PRIORITY; do
  run t$A $LEG --trace --students 3 --duration 150 --algo $A --seed 1
  B=$(g t$A "Busy Ticks"); I=$(g t$A "Idle Ticks"); D=$(g t$A "Dispatches")
  chk "$A: busy+idle close to duration" "($B+$I) >= 135 && ($B+$I) <= 155"
  chk "$A: avg dispatch length > 3"     "$D > 0 && $B/$D > 3"
  if head -1 output/trace.csv | grep -q "^tick_start,tick_end,pid,reason"; then ok "$A: trace header"; else bad "$A: trace header"; fi
  if awk -F, 'NR>1 && $3>0 && $2-$1>=3 {f=1} END{exit !f}' output/trace.csv; then ok "$A: multi-tick dispatch in trace"; else bad "$A: multi-tick dispatch in trace"; fi
done
fi

if [ $R -ge 2 ]; then
echo "--- 2.2a workload model"
has "WORKLOAD key parsed"      "WORKLOAD" src/config.c
has "demand_min in headers"    "demand_min" include/shared.h
has "report: Workload"         "Workload" src/logger.c
for A in RR PRIORITY; do
  run w$A --workload INTERACTIVE --students 12 --duration 300 --algo $A --seed 21
  chk "$A: completed >= 10"  "$(g w$A 'Completed Exams') >= 10"
  chk "$A: timeouts <= 2"    "$(g w$A 'Timeouts Fired') <= 2"
  if [ $R -eq 2 ]; then chk "$A: submissions <= 20 (no random producer)" "$(g w$A 'Total Submissions') <= 20"; fi
done
run p1 --workload INTERACTIVE --students 6 --duration 300 --algo RR --seed 5
run p2 --workload INTERACTIVE --students 6 --duration 600 --algo RR --seed 5
chk "submissions do not scale with run length" "$(g p2 'Total Submissions') <= 1.3*$(g p1 'Total Submissions') + 1"
fi

if [ $R -ge 3 ]; then
echo "--- 2.2b bursts, WAITING, wake-ups"
has "invariant I9"            "I9" src/invariants.c
has "wake_sleepers"           "wake_sleepers" src/scheduler.c
has "timeout scan covers WAITING" "WAITING" src/interrupt.c
has "report: Blocked Waits"   "Blocked Waits" src/logger.c
for A in RR PRIORITY; do
  BW=$(g w$A "Blocked Waits"); WK=$(g w$A "Wakeups"); S=$(g w$A "Total Submissions")
  B=$(g w$A "Busy Ticks"); D=$(g w$A "Dispatches")
  chk "$A: blocked waits > 0"            "$BW > 0"
  chk "$A: wakeups in (0, blocked]"      "$WK > 0 && $WK <= $BW"
  chk "$A: submissions in [30,75]"       "$S >= 30 && $S <= 75"
  chk "$A: avg dispatch length > 2"      "$D > 0 && $B/$D > 2"
done
chk "PRIORITY: real context switches >= 10" "$(g wPRIORITY 'Context Switches') >= 10"
run lim --workload INTERACTIVE --limit 15 --students 12 --duration 300 --seed 21
chk "limit run: timeouts > 0" "$(g lim 'Timeouts Fired') > 0"
dup=$(grep -o 'PID [0-9]* terminated' output/system_log.txt | sort | uniq -d | wc -l)
[ "$dup" -eq 0 ] && ok "no PID terminated twice" || bad "PID terminated twice"
fi

echo; if [ $fail -eq 0 ]; then echo "GATE $1 PASSED"; else echo "GATE $1 FAILED"; fi
exit $fail
