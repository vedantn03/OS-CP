#!/usr/bin/env bash
# Gate for 7.1: synclab (counter/Peterson, readers-writers, dining philosophers). Hand-written; Copilot must not edit.
cd "$(dirname "$0")/.." || exit 1
ROOT=$(cd .. && pwd); D=$ROOT/docs; R=$ROOT/README.md
fail=0
ok()    { echo "PASS  $1"; }
bad()   { echo "FAIL  $1"; fail=1; }
has()   { d=$1; p=$2; shift 2; if grep -q -- "$p" "$@" 2>/dev/null; then ok "$d"; else bad "$d"; fi; }
eq()    { d=$1; got=$2; want=$3; if [ "$got" = "$want" ]; then ok "$d"; else bad "$d [got '$got', want '$want']"; fi; }
chk()   { d=$1; shift; if awk "BEGIN{exit !($*)}" 2>/dev/null; then ok "$d"; else bad "$d [$*]"; fi; }
rc()    { "$@" >/dev/null 2>&1; echo $?; }
field() { sed -n "s/^$1: *//p" | head -1; }
sl()    { ./synclab "$@" 2>/dev/null; }

if make test >/tmp/g71_test.txt 2>&1; then ok "regression: make test"; else bad "regression: make test"; tail -8 /tmp/g71_test.txt; fi
make clean >/dev/null 2>&1
make >/tmp/g71_make.txt 2>&1 || { echo "FAIL  build"; tail -20 /tmp/g71_make.txt; exit 1; }
make synclab >/tmp/g71_sl.txt 2>&1 || { echo "FAIL  synclab build"; tail -20 /tmp/g71_sl.txt; exit 1; }
if grep -qi warning /tmp/g71_make.txt /tmp/g71_sl.txt; then bad "build: warnings"; else ok "build: no warnings"; fi
if [ -z "$(git -C "$ROOT" status --porcelain -- exam_os/src exam_os/include)" ]; then ok "simulator sources untouched"; else bad "simulator sources modified"; fi

echo "--- source checks"
[ -s tools/synclab.c ] && ok "tools/synclab.c exists" || bad "tools/synclab.c exists"
has "Makefile builds synclab"      "synclab" Makefile
has "synclab git-ignored"          "synclab" ../.gitignore
has "uses C11 atomics"             "_Atomic" tools/synclab.c
has "uses timed mutex lock"        "pthread_mutex_timedlock" tools/synclab.c
has "uses a barrier"               "pthread_barrier_wait" tools/synclab.c
has "uses semaphores"              "sem_wait" tools/synclab.c
chk "run_tests.sh has >= 6 synclab checks" "$(grep -c synclab tests/run_tests.sh) >= 6"

echo "--- counter: mutual exclusion"
for M in mutex sem; do
  O=$(sl counter --mode $M --threads 4 --iters 200000)
  eq "$M: expected" "$(echo "$O" | field expected)" 800000
  eq "$M: final"    "$(echo "$O" | field final)" 800000
  eq "$M: correct"  "$(echo "$O" | field correct)" yes
  eq "$M: lost_updates" "$(echo "$O" | field lost_updates)" 0
done
for i in 1 2 3; do
  O=$(sl counter --mode peterson --threads 2 --iters 300000)
  eq "peterson run $i: final" "$(echo "$O" | field final)" 600000
  eq "peterson run $i: correct" "$(echo "$O" | field correct)" yes
done
O=$(sl counter --mode none --threads 4 --iters 1000000)
E=$(echo "$O" | field expected); F=$(echo "$O" | field final); LU=$(echo "$O" | field lost_updates)
chk "none: final + lost_updates == expected" "${F:-0} + ${LU:-0} == ${E:-1} && ${E:-0} == 4000000"
echo "INFO  unsynchronized counter lost ${LU:-?} of ${E:-?} updates (expected > 0 on a multicore machine)"
eq "peterson with 3 threads -> exit 2" "$(rc ./synclab counter --mode peterson --threads 3 --iters 1000)" 2
eq "bad mode -> exit 2"      "$(rc ./synclab counter --mode bogus --threads 2 --iters 1000)" 2
eq "iters 0 -> exit 2"       "$(rc ./synclab counter --mode mutex --threads 2 --iters 0)" 2
eq "threads 17 -> exit 2"    "$(rc ./synclab counter --mode mutex --threads 17 --iters 10)" 2
eq "unknown command -> 2"    "$(rc ./synclab bogus)" 2

echo "--- readers-writers"
for P in reader writer fair; do
  O=$(sl rw --policy $P --readers 4 --writers 2 --ops 150 --hold-us 100)
  eq "rw $P: policy"      "$(echo "$O" | field policy)" $P
  eq "rw $P: reads_done"  "$(echo "$O" | field reads_done)" 600
  eq "rw $P: writes_done" "$(echo "$O" | field writes_done)" 300
  eq "rw $P: torn_reads"  "$(echo "$O" | field torn_reads)" 0
  eq "rw $P: violations"  "$(echo "$O" | field violations)" 0
  chk "rw $P: at most one writer at a time" "$(echo "$O" | field max_writers) <= 1"
  echo "INFO  rw $P: max_readers=$(echo "$O" | field max_readers)"
done
O=$(sl rw --policy none --readers 4 --writers 2 --ops 150 --hold-us 100)
eq "rw none (unsafe control): reads_done" "$(echo "$O" | field reads_done)" 600
echo "INFO  rw none (no locking): torn_reads=$(echo "$O" | field torn_reads) violations=$(echo "$O" | field violations)"
eq "rw bad policy -> 2"   "$(rc ./synclab rw --policy bogus --readers 2 --writers 1 --ops 10 --hold-us 10)" 2
eq "rw readers 0 -> 2"    "$(rc ./synclab rw --policy fair --readers 0 --writers 1 --ops 10 --hold-us 10)" 2
eq "rw writers 0 -> 2"    "$(rc ./synclab rw --policy fair --readers 2 --writers 0 --ops 10 --hold-us 10)" 2
eq "rw ops 0 -> 2"        "$(rc ./synclab rw --policy fair --readers 2 --writers 1 --ops 0 --hold-us 10)" 2
eq "rw hold-us -1 -> 2"   "$(rc ./synclab rw --policy fair --readers 2 --writers 1 --ops 10 --hold-us -1)" 2

echo "--- dining philosophers"
O=$(sl phil --mode naive --n 5 --timeout-ms 150)
eq "naive: deadlock"     "$(echo "$O" | field deadlock)" yes
eq "naive: stuck"        "$(echo "$O" | field stuck)" 5
eq "naive: meals_eaten"  "$(echo "$O" | field meals_eaten)" 0
O=$(sl phil --mode naive --n 2 --timeout-ms 150)
eq "naive n=2: deadlock" "$(echo "$O" | field deadlock)" yes
for M in ordered waiter; do
  O=$(sl phil --mode $M --n 5 --meals 40 --eat-us 200)
  eq "$M: deadlock"    "$(echo "$O" | field deadlock)" no
  eq "$M: meals_eaten" "$(echo "$O" | field meals_eaten)" 200
  eq "$M: violations"  "$(echo "$O" | field violations)" 0
  eq "$M: stuck"       "$(echo "$O" | field stuck)" 0
done
eq "ordered n=2 meals" "$(sl phil --mode ordered --n 2 --meals 20 --eat-us 100 | field meals_eaten)" 40
eq "phil n=1 -> 2"        "$(rc ./synclab phil --mode ordered --n 1 --meals 5)" 2
eq "phil n=33 -> 2"       "$(rc ./synclab phil --mode ordered --n 33 --meals 5)" 2
eq "phil bad mode -> 2"   "$(rc ./synclab phil --mode bogus --n 5 --meals 5)" 2
eq "phil timeout 0 -> 2"  "$(rc ./synclab phil --mode naive --n 5 --timeout-ms 0)" 2
eq "phil meals 0 -> 2"    "$(rc ./synclab phil --mode ordered --n 5 --meals 0)" 2

echo "--- ThreadSanitizer on every correct mode"
if make synclab_tsan >/tmp/g71_tsb.txt 2>&1 && ! grep -qi warning /tmp/g71_tsb.txt; then
  PR=$(./synclab_tsan counter --mode mutex --threads 2 --iters 1000 2>&1)
  if echo "$PR" | grep -q "unexpected memory mapping"; then
    echo "SKIP  ThreadSanitizer cannot run here (memory mapping); try: sudo sysctl vm.mmap_rnd_bits=28"
  else
    W=0
    while IFS= read -r A; do n=$(./synclab_tsan $A 2>&1 | grep -c "WARNING: ThreadSanitizer"); W=$((W+n)); done <<'TSAN_EOF'
counter --mode mutex --threads 4 --iters 50000
counter --mode sem --threads 4 --iters 50000
counter --mode peterson --threads 2 --iters 50000
rw --policy reader --readers 3 --writers 2 --ops 40 --hold-us 50
rw --policy writer --readers 3 --writers 2 --ops 40 --hold-us 50
rw --policy fair --readers 3 --writers 2 --ops 40 --hold-us 50
phil --mode ordered --n 5 --meals 10 --eat-us 100
phil --mode waiter --n 5 --meals 10 --eat-us 100
phil --mode naive --n 5 --timeout-ms 100
TSAN_EOF
    eq "ThreadSanitizer warnings in correct modes" "$W" 0
  fi
else bad "synclab_tsan builds without warnings"; fi

echo "--- documentation sync"
SM=$D/SYLLABUS_MAP.md
col() { awk -F'|' -v t="$1" -v c="$2" '{a=$2; gsub(/^ +| +$/,"",a); if (a==t) {x=$c; gsub(/^ +| +$/,"",x); print x; exit}}' "$SM"; }
rowcheck() { s=$(col "$1" 3); w=$(col "$1" 4); e=$(col "$1" 5)
  if [ "$s" = "$2" ] && echo "$w" | grep -q 'exam_os/tools/synclab.c' && [ ${#e} -ge 5 ]; then ok "SYLLABUS_MAP: $1 = $2"; else bad "SYLLABUS_MAP: $1 (status '$s', needs $2, a synclab.c citation and evidence)"; fi; }
rowcheck "Peterson's solution" LAB
rowcheck "Readers-writers and dining philosophers" LAB
rowcheck "Deadlock prevention" PARTIAL
CITED=$(grep -oE 'exam_os/tools/synclab\.c::[A-Za-z0-9_]+' "$SM" | sort -u)
chk "SYLLABUS_MAP cites >= 3 synclab functions" "$(echo "$CITED" | grep -c .) >= 3"
BADF=""; for c in $CITED; do fn=${c##*::}; grep -qw -- "$fn" tools/synclab.c || BADF="$BADF $c"; done
if [ -z "$BADF" ]; then ok "cited synclab functions exist"; else bad "unknown synclab functions cited:$BADF"; fi
for K in Peterson readers-writers synclab; do if grep -qiF -- "$K" $D/LIMITATIONS.md; then ok "LIMITATIONS mentions $K"; else bad "LIMITATIONS mentions $K"; fi; done
STALE=$(grep -inE 'peterson|readers-writers|dining philosophers|deadlock prevention' $R $D/VIVA.md $D/ARCHITECTURE.md $D/EXPERIMENTS.md $D/DEMO.md 2>/dev/null | grep -iE 'not implemented|isn.t implemented|no implementation' | head -3)
if [ -z "$STALE" ]; then ok "no stale 'not implemented' claims in README/VIVA/ARCHITECTURE/EXPERIMENTS/DEMO"; else bad "stale claims: $STALE"; fi
has "EXPERIMENTS: Synchronization lab section" '^## Synchronization lab' $D/EXPERIMENTS.md
for F in $D/EXPERIMENTS.md $D/PROJECT_STATE.md $R; do if grep -qF synclab "$F"; then ok "synclab mentioned in $(basename $F)"; else bad "synclab mentioned in $(basename $F)"; fi; done
has "PROJECT_STATE lists gate71.sh" "gate71.sh" $D/PROJECT_STATE.md
MAXT=$(grep -oE '\bT[0-9]+\b' tests/run_tests.sh | tr -d T | sort -n | tail -1)
has "PROJECT_STATE mentions T$MAXT (highest test)" "T$MAXT" $D/PROJECT_STATE.md

echo; if [ $fail -eq 0 ]; then echo "GATE 7.1 PASSED"; else echo "GATE 7.1 FAILED"; fi
exit $fail