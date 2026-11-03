#!/usr/bin/env bash
# Gate for 2.4. Hand-written; Copilot must not edit.
cd "$(dirname "$0")/.." || exit 1
fail=0
ok()  { echo "PASS  $1"; }
bad() { echo "FAIL  $1"; fail=1; }
make clean >/dev/null 2>&1
make >/tmp/g24_make.txt 2>&1 || { echo "FAIL  build"; tail -10 /tmp/g24_make.txt; exit 1; }
mkdir -p output
if [ -z "$(git status --porcelain -- src include 2>/dev/null)" ]; then ok "simulator sources untouched"; else bad "simulator sources modified"; fi
rm -rf /tmp/g24out
if bash scripts/compare.sh --seeds "1 2" --algos "RR SRTF MLFQ" --out /tmp/g24out >/tmp/g24_cmp.log 2>&1; then ok "compare.sh exit 0"; else bad "compare.sh exit 0"; tail -15 /tmp/g24_cmp.log; fi
[ -s /tmp/g24out/compare.csv ] && ok "csv exists" || bad "csv exists"
H=$(head -1 /tmp/g24out/compare.csv 2>/dev/null)
[ "$H" = "algo,seed,avg_wait,avg_turnaround,avg_response,completed,timeouts,ctx_switches,preemptions,cpu_util,throughput" ] && ok "csv header" || bad "csv header [$H]"
N=$(( $(wc -l < /tmp/g24out/compare.csv 2>/dev/null) - 1 ))
[ "$N" -eq 6 ] && ok "csv has 6 runs" || bad "csv has 6 runs [$N]"
if awk -F, 'NR>1 { if ($3=="" || $6+0<1) e=1 } END{exit e}' /tmp/g24out/compare.csv 2>/dev/null; then ok "csv values sane"; else bad "csv values sane"; fi
[ -s /tmp/g24out/compare.md ] && ok "md exists" || bad "md exists"
for A in RR SRTF MLFQ; do
  grep -q "^| $A |" /tmp/g24out/compare.md 2>/dev/null && ok "md row $A" || bad "md row $A"
done
grep -q "Avg Wait" /tmp/g24out/compare.md 2>/dev/null && ok "md header" || bad "md header"
U=$(grep "^| \(RR\|SRTF\|MLFQ\) |" /tmp/g24out/compare.md 2>/dev/null | awk -F'|' '{print $3+0}' | sort -u | wc -l)
[ "$U" -ge 2 ] && ok "algorithms differ in avg wait" || bad "algorithms differ in avg wait [$U]"
./exam_os --headless --strict --tick-ms 10 --trace --workload INTERACTIVE --students 6 --duration 150 --algo RR --seed 3 >/dev/null 2>&1
bash scripts/gantt.sh output/trace.csv >/tmp/g24_gantt.txt 2>&1 && ok "gantt.sh exit 0" || bad "gantt.sh exit 0"
R=$(grep -c '^P[0-9]' /tmp/g24_gantt.txt)
[ "$R" -ge 3 ] && ok "gantt has >= 3 process rows" || bad "gantt process rows [$R]"
grep -q "^CPU" /tmp/g24_gantt.txt && ok "gantt CPU row" || bad "gantt CPU row"
grep -q "legend" /tmp/g24_gantt.txt && ok "gantt legend" || bad "gantt legend"
M=$(awk '{ if (length($0)>m) m=length($0) } END{print m+0}' /tmp/g24_gantt.txt)
[ "$M" -le 160 ] && ok "gantt width <= 160" || bad "gantt width [$M]"
echo; if [ $fail -eq 0 ]; then echo "GATE 2.4 PASSED"; else echo "GATE 2.4 FAILED"; fi
exit $fail