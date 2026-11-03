#!/usr/bin/env bash
# Gate for 2.3. Usage: bash tests/gate23.sh a|b   (hand-written; Copilot must not edit)
cd "$(dirname "$0")/.." || exit 1
S=${1:-}; [ "$S" = a ] || [ "$S" = b ] || { echo "usage: gate23.sh a|b"; exit 2; }
fail=0
ok()  { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }
val() { sed -n "s/.*$2 *: *\([0-9][0-9.]*\).*/\1/p" "$1" | head -1; }
chk() { d=$1; shift; if awk "BEGIN{exit !($*)}" 2>/dev/null; then ok "$d"; else bad "$d [$*]"; fi; }
if bash tests/gate.sh 2.2b >/tmp/g23_prev.txt 2>&1; then ok "regression: gate 2.2b"; else bad "regression: gate 2.2b"; tail -15 /tmp/g23_prev.txt; fi
ALGOS="FCFS SJF SRTF RR PRIORITY"; [ "$S" = b ] && ALGOS="$ALGOS PRIO MLFQ"
for A in $ALGOS; do
  ./exam_os --headless --strict --tick-ms 10 --workload INTERACTIVE --students 12 --duration 300 --algo $A --seed 21 >/tmp/g23_$A.log 2>&1; RC=$?
  cp output/summary.txt /tmp/g23_$A.sum 2>/dev/null; cp output/invariants.txt /tmp/g23_$A.inv 2>/dev/null
  if [ $RC -eq 0 ]; then ok "$A: exit 0"; else bad "$A: exit $RC"; fi
  if grep -q FAIL /tmp/g23_$A.inv 2>/dev/null; then bad "$A: invariant FAIL"; else ok "$A: invariants"; fi
  C=$(val /tmp/g23_$A.sum "Completed Exams"); T=$(val /tmp/g23_$A.sum "Timeouts Fired")
  chk "$A: completed >= 8" "${C:-0} >= 8"
  chk "$A: timeouts <= 4" "${T:-99} <= 4"
done
W=""; for A in $ALGOS; do W="$W $(val /tmp/g23_$A.sum 'Avg Waiting Time')"; done
N=$(echo $W | tr ' ' '\n' | sort -u | wc -l)
chk "policies give different waiting times" "$N >= 3"
grep -q "Preemptions" /tmp/g23_SRTF.sum 2>/dev/null && ok "report: Preemptions" || bad "report: Preemptions"
grep -q "Scheduler" /tmp/g23_FCFS.sum 2>/dev/null && ok "report: Scheduler name" || bad "report: Scheduler name"
if [ "$S" = b ]; then
  grep -q "MLFQ Demotions" /tmp/g23_MLFQ.sum 2>/dev/null && ok "report: MLFQ Demotions" || bad "report: MLFQ Demotions"
  grep -q "MLFQ Boosts" /tmp/g23_MLFQ.sum 2>/dev/null && ok "report: MLFQ Boosts" || bad "report: MLFQ Boosts"
fi
./exam_os --headless --strict --tick-ms 10 --workload INTERACTIVE --students 12 --duration 300 --algo RR --switch-cost 2 --seed 21 >/dev/null 2>&1
cp output/summary.txt /tmp/g23_sw.sum 2>/dev/null
chk "switch cost produces overhead ticks" "$(val /tmp/g23_sw.sum 'Overhead Ticks') > 0"
echo; if [ $fail -eq 0 ]; then echo "GATE 2.3$S PASSED"; else echo "GATE 2.3$S FAILED"; fi
exit $fail