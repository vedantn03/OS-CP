#!/usr/bin/env bash
# Gate for 3.1 (locality paging model, CLOCK, dirty pages, thrash counters, trace). Hand-written; Copilot must not edit.
cd "$(dirname "$0")/.." || exit 1
fail=0
ok()  { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }
val() { sed -n "s/.*$2 *: *\([0-9][0-9.]*\).*/\1/p" "$1" | head -1; }
g()   { v=$(val "/tmp/g31_$1.sum" "$2"); echo "${v:-0}"; }
chk() { d=$1; shift; if awk "BEGIN{exit !($*)}" 2>/dev/null; then ok "$d"; else bad "$d [$*]"; fi; }
has() { d=$1; p=$2; shift 2; if grep -q -- "$p" "$@" 2>/dev/null; then ok "$d"; else bad "$d"; fi; }
BASE="--headless --strict --tick-ms 10 --workload INTERACTIVE --students 12 --duration 300 --algo RR"
run() { n=$1; shift
  ./exam_os $BASE "$@" >/tmp/g31_$n.log 2>&1; RC=$?
  cp output/summary.txt /tmp/g31_$n.sum 2>/dev/null; cp output/invariants.txt /tmp/g31_$n.inv 2>/dev/null
  if [ $RC -eq 0 ]; then ok "$n: exit 0"; else bad "$n: exit $RC"; fi
  if grep -q FAIL /tmp/g31_$n.inv 2>/dev/null; then bad "$n: invariant FAIL"; fi; }
rate() { f=$(g $1 "Page Faults"); h=$(g $1 "Page Hits"); awk -v f="$f" -v h="$h" 'BEGIN{t=f+h; if(t>0) printf "%.4f", f/t; else print 0}'; }

make clean >/dev/null 2>&1
make >/tmp/g31_make.txt 2>&1 || { echo "FAIL  build"; tail -20 /tmp/g31_make.txt; exit 1; }
if grep -qi warning /tmp/g31_make.txt; then bad "build: warnings"; else ok "build: no warnings"; fi
mkdir -p output
if bash tests/gate.sh 2.2b >/tmp/g31_prev.txt 2>&1; then ok "regression: gate 2.2b"; else bad "regression: gate 2.2b"; tail -15 /tmp/g31_prev.txt; fi

echo "--- source and CLI checks"
has "invariant I11" "I11" src/invariants.c
has "memory_write_trace exists" "memory_write_trace" src/memory.c
for A in "--page BOGUS" "--ws 99" "--pages 100" "--locality 101" "--write-pct 101" "--thrash-high 0" "--thrash-high 101"; do
  ./exam_os --headless $A >/dev/null 2>&1; RC=$?
  if [ $RC -ne 0 ]; then ok "rejects: $A"; else bad "rejects: $A"; fi
done

echo "--- policies under memory pressure (24 frames, 3 seeds)"
for P in LRU FIFO CLOCK; do
  : >/tmp/g31_$P.f
  for S in 1 2 3; do
    run $P$S --frames 24 --page $P --seed $S
    chk "$P/$S: evictions > 0" "$(g $P$S Evictions) > 0"
    g $P$S "Page Faults" >>/tmp/g31_$P.f
  done
  eval "SUM_$P=$(awk '{s+=$1} END{print s+0}' /tmp/g31_$P.f)"
done
chk "policies differ in total faults" "(${SUM_LRU} != ${SUM_FIFO}) || (${SUM_FIFO} != ${SUM_CLOCK})"
echo "INFO  total faults over 3 seeds: LRU=$SUM_LRU FIFO=$SUM_FIFO CLOCK=$SUM_CLOCK"

echo "--- fault rate falls as frames grow"
run c8  --frames 8  --page LRU --seed 1
run c64 --frames 64 --page LRU --seed 1
R8=$(rate c8); R64=$(rate c64)
chk "rate(8 frames) >= rate(64 frames) + 0.15" "$R8 >= $R64 + 0.15"
echo "INFO  fault rate: 8 frames=$R8  64 frames=$R64"

echo "--- dirty pages"
run d30 --frames 24 --page LRU --seed 1 --write-pct 30
run d0  --frames 24 --page LRU --seed 1 --write-pct 0
chk "dirty writebacks > 0 with writes" "$(g d30 'Dirty Writebacks') > 0"
chk "dirty writebacks <= evictions" "$(g d30 'Dirty Writebacks') <= $(g d30 Evictions)"
chk "no writes -> no dirty writebacks" "$(g d0 'Dirty Writebacks') == 0"

echo "--- thrash detection"
run t4 --frames 4 --page LRU --seed 1 --thrash-high 35
chk "thrash episodes at 4 frames >= 1" "$(g t4 'Thrash Episodes') >= 1"
chk "more episodes at 4 frames than at 64" "$(g t4 'Thrash Episodes') >= $(g c64 'Thrash Episodes')"

echo "--- report labels"
for L in "Page Replacement" "Evictions" "Dirty Writebacks" "Page Fault Rate" "Thrash Episodes"; do
  has "label: $L" "$L" /tmp/g31_t4.sum
done

echo "--- page trace"
run tr --frames 24 --page LRU --page-trace --seed 1
NL=$(wc -l < output/pagetrace.txt 2>/dev/null || echo 0)
BADL=$(grep -vc '^[0-9][0-9]*:[0-9][0-9]*$' output/pagetrace.txt 2>/dev/null)
TOT=$(( $(g tr 'Page Faults') + $(g tr 'Page Hits') ))
chk "trace lines == faults + hits" "$NL == $TOT && $NL > 100"
chk "trace lines all pid:page" "${BADL:-1} == 0"

echo; if [ $fail -eq 0 ]; then echo "GATE 3.1 PASSED"; else echo "GATE 3.1 FAILED"; fi
exit $fail