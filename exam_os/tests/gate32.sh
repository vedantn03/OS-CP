#!/usr/bin/env bash
# Gate for 3.2 (pagesim: FIFO/LRU/CLOCK/OPT, Belady; thrash control; page-fault curves). Hand-written; Copilot must not edit.
cd "$(dirname "$0")/.." || exit 1
fail=0
ok()  { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }
val() { sed -n "s/.*$2 *: *\([0-9][0-9.]*\).*/\1/p" "$1" | head -1; }
g()   { v=$(val "/tmp/g32_$1.sum" "$2"); echo "${v:-0}"; }
chk() { d=$1; shift; if awk "BEGIN{exit !($*)}" 2>/dev/null; then ok "$d"; else bad "$d [$*]"; fi; }
BASE="--headless --strict --tick-ms 10 --workload INTERACTIVE --algo RR"
run() { n=$1; shift
  ./exam_os $BASE "$@" >/tmp/g32_$n.log 2>&1; RC=$?
  cp output/summary.txt /tmp/g32_$n.sum 2>/dev/null; cp output/invariants.txt /tmp/g32_$n.inv 2>/dev/null
  if [ $RC -eq 0 ]; then ok "$n: exit 0"; else bad "$n: exit $RC"; fi
  if grep -q FAIL /tmp/g32_$n.inv 2>/dev/null; then bad "$n: invariant FAIL"; fi; }
ps() { ./pagesim --algo "$1" --frames "$2" --string "$3" 2>/dev/null | sed -n 's/^faults: *\([0-9][0-9]*\).*/\1/p'; }

if bash tests/gate31.sh >/tmp/g32_prev.txt 2>&1; then ok "regression: gate 3.1"; else bad "regression: gate 3.1"; tail -15 /tmp/g32_prev.txt; fi
make clean >/dev/null 2>&1
make >/tmp/g32_make.txt 2>&1 || { echo "FAIL  build"; tail -20 /tmp/g32_make.txt; exit 1; }
make pagesim >/tmp/g32_ps.txt 2>&1 || { echo "FAIL  pagesim build"; tail -20 /tmp/g32_ps.txt; exit 1; }
if grep -qi warning /tmp/g32_make.txt /tmp/g32_ps.txt; then bad "build: warnings"; else ok "build: no warnings"; fi
mkdir -p output

echo "--- pagesim: textbook string, 3 frames"
T="7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1"
chk "FIFO = 15" "$(ps FIFO 3 "$T") == 15"
chk "LRU = 12"  "$(ps LRU 3 "$T") == 12"
chk "OPT = 9"   "$(ps OPT 3 "$T") == 9"
C=$(ps CLOCK 3 "$T"); chk "CLOCK between 9 and 18" "${C:-0} >= 9 && ${C:-99} <= 18"

echo "--- pagesim: Belady string"
B="1 2 3 4 1 2 5 1 2 3 4 5"
chk "FIFO 3 frames = 9"  "$(ps FIFO 3 "$B") == 9"
chk "FIFO 4 frames = 10 (Belady anomaly)" "$(ps FIFO 4 "$B") == 10"
chk "LRU 3 frames = 10"  "$(ps LRU 3 "$B") == 10"
chk "LRU 4 frames = 8"   "$(ps LRU 4 "$B") == 8"
chk "OPT 3 frames = 7"   "$(ps OPT 3 "$B") == 7"
chk "OPT 4 frames = 6"   "$(ps OPT 4 "$B") == 6"
SW=$(./pagesim --algo FIFO --string "$B" --sweep 1-7 2>/dev/null | awk '$1=="frames"{print $2, $4}')
F3=$(echo "$SW" | awk '$1==3{print $2}'); F4=$(echo "$SW" | awk '$1==4{print $2}')
chk "sweep shows FIFO faults rise from 3 to 4 frames" "${F4:-0} > ${F3:-99}"
if ./pagesim --algo LRU --string "$B" --sweep 1-7 2>/dev/null | awk '$1=="frames"{ if (NR>1 && $4>p) b=1; p=$4 } END{exit b}'; then ok "LRU sweep never increases (stack algorithm)"; else bad "LRU sweep never increases"; fi

echo "--- pagesim: file input and bad usage"
printf '%s\n' $T >/tmp/g32_ref.txt
chk "file input refs = 20" "$(./pagesim --algo LRU --frames 3 --file /tmp/g32_ref.txt 2>/dev/null | sed -n 's/^refs: *\([0-9]*\).*/\1/p') == 20"
./pagesim --algo BOGUS --frames 3 --string "1 2" >/dev/null 2>&1; [ $? -ne 0 ] && ok "rejects bad algo" || bad "rejects bad algo"
./pagesim --algo LRU --frames 0 --string "1 2" >/dev/null 2>&1; [ $? -ne 0 ] && ok "rejects 0 frames" || bad "rejects 0 frames"

echo "--- replay a live trace: OPT is a lower bound"
run live --students 12 --duration 300 --frames 24 --page LRU --page-trace --seed 1
cp output/pagetrace.txt /tmp/g32_trace.txt 2>/dev/null
N=$(wc -l < /tmp/g32_trace.txt 2>/dev/null || echo 0)
for A in FIFO LRU CLOCK OPT; do
  eval "L_$A=$(./pagesim --algo $A --frames 24 --file /tmp/g32_trace.txt 2>/dev/null | sed -n 's/^faults: *\([0-9][0-9]*\).*/\1/p')"
done
chk "replay refs match trace" "$(./pagesim --algo OPT --frames 24 --file /tmp/g32_trace.txt 2>/dev/null | sed -n 's/^refs: *\([0-9]*\).*/\1/p') == $N && $N > 100"
chk "OPT <= FIFO, LRU, CLOCK on live trace" "${L_OPT:-999999} <= ${L_FIFO:-0} && ${L_OPT:-999999} <= ${L_LRU:-0} && ${L_OPT:-999999} <= ${L_CLOCK:-0}"
echo "INFO  live-trace faults at 24 frames: FIFO=$L_FIFO LRU=$L_LRU CLOCK=$L_CLOCK OPT=$L_OPT"

echo "--- thrash control (admission control)"
run off --students 60 --duration 500 --frames 24 --seed 1 --thrash-high 35 --thrash-control 0
run on  --students 60 --duration 500 --frames 24 --seed 1 --thrash-high 35 --thrash-control 1
chk "control off: no deferred arrivals" "$(g off 'Deferred Arrival Ticks') == 0"
chk "control on: arrivals were deferred" "$(g on 'Deferred Arrival Ticks') > 0"
grep -q "Deferred Arrival Ticks" /tmp/g32_on.sum && ok "label: Deferred Arrival Ticks" || bad "label: Deferred Arrival Ticks"
rt() { f=$(g $1 "Page Faults"); h=$(g $1 "Page Hits"); awk -v f="$f" -v h="$h" 'BEGIN{t=f+h; if(t>0) printf "%.3f", f/t; else print 0}'; }
echo "INFO  fault rate: control off=$(rt off) control on=$(rt on)"

echo "--- pagecurve.sh"
rm -rf /tmp/g32pc
if bash scripts/pagecurve.sh --frames "8 24 64" --algos "FIFO LRU CLOCK" --seeds "1" --out /tmp/g32pc >/tmp/g32_pc.log 2>&1; then ok "pagecurve exit 0"; else bad "pagecurve exit 0"; tail -10 /tmp/g32_pc.log; fi
H=$(head -1 /tmp/g32pc/pagecurve.csv 2>/dev/null)
[ "$H" = "algo,frames,seed,faults,hits,fault_rate,evictions" ] && ok "pagecurve csv header" || bad "pagecurve csv header [$H]"
chk "pagecurve csv has 9 rows" "$(( $(wc -l < /tmp/g32pc/pagecurve.csv 2>/dev/null || echo 0) - 1 )) == 9"
for A in FIFO LRU CLOCK; do
  R8=$(awk -F, -v a=$A '$1==a && $2==8{print $6}' /tmp/g32pc/pagecurve.csv); R64=$(awk -F, -v a=$A '$1==a && $2==64{print $6}' /tmp/g32pc/pagecurve.csv)
  chk "$A: fault rate at 8 frames > at 64" "${R8:-0} > ${R64:-1}"
done
[ -s /tmp/g32pc/pagecurve.md ] && ok "pagecurve md" || bad "pagecurve md"

echo; if [ $fail -eq 0 ]; then echo "GATE 3.2 PASSED"; else echo "GATE 3.2 FAILED"; fi
exit $fail