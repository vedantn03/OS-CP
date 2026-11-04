#!/usr/bin/env bash
# Gate for 6.1: documentation truthfulness and demo reproducibility. Hand-written; Copilot must not edit.
cd "$(dirname "$0")/.." || exit 1
ROOT=$(cd .. && pwd); D=$ROOT/docs; R=$ROOT/README.md
BT=$(printf '\140\140\140')
fail=0
ok()   { echo "PASS  $1"; }
bad()  { echo "FAIL  $1"; fail=1; }
has()  { d=$1; p=$2; f=$3; if grep -qE -- "$p" "$f" 2>/dev/null; then ok "$d"; else bad "$d"; fi; }
hasf() { d=$1; p=$2; f=$3; if grep -qF -- "$p" "$f" 2>/dev/null; then ok "$d"; else bad "$d"; fi; }
chk()  { d=$1; shift; if awk "BEGIN{exit !($*)}" 2>/dev/null; then ok "$d"; else bad "$d [$*]"; fi; }
nlines() { wc -l < "$1" 2>/dev/null || echo 0; }

if make test >/tmp/g61_test.txt 2>&1; then ok "regression: make test"; else bad "regression: make test"; tail -8 /tmp/g61_test.txt; fi
make clean >/dev/null 2>&1
make >/tmp/g61_make.txt 2>&1 || { echo "FAIL  build"; tail -20 /tmp/g61_make.txt; exit 1; }
make pagesim disksim deadsim >/tmp/g61_tools.txt 2>&1 || { echo "FAIL  tool builds"; tail -20 /tmp/g61_tools.txt; exit 1; }
mkdir -p output
if [ -z "$(git -C "$ROOT" status --porcelain -- exam_os/src exam_os/include exam_os/tools exam_os/scripts exam_os/tests exam_os/scenarios exam_os/Makefile)" ]; then ok "code, scripts and tests untouched (docs only)"; else bad "code, scripts or tests modified"; fi

echo "--- files and sizes"
DOCS="ARCHITECTURE SYLLABUS_MAP EXPERIMENTS DEMO VIVA LIMITATIONS PROJECT_STATE"
declare -A MIN=([ARCHITECTURE]=80 [SYLLABUS_MAP]=50 [EXPERIMENTS]=40 [DEMO]=60 [VIVA]=120 [LIMITATIONS]=25 [PROJECT_STATE]=60)
ALL="$R"
for n in $DOCS; do
  ALL="$ALL $D/$n.md"
  if [ -s "$D/$n.md" ]; then chk "$n.md has >= ${MIN[$n]} lines" "$(nlines $D/$n.md) >= ${MIN[$n]}"; else bad "$n.md exists"; fi
done
if [ -s "$R" ]; then chk "README.md has >= 60 lines" "$(nlines $R) >= 60"; else bad "README.md exists"; fi
for f in compare pagecurve belady disk deadlock; do if [ -s "$D/results/$f.md" ]; then ok "results/$f.md exists"; else bad "results/$f.md exists"; fi; done

echo "--- banned (untrue or unfinished) text"
for P in "SIGALRM" "Hardware Timer" "C++ STL" "Priyanka-Singh27" "TODO" "TBD" "FIXME" "lorem ipsum" "real-time operating system"; do
  if grep -qiF -- "$P" $ALL 2>/dev/null; then bad "banned phrase: $P"; grep -niF -- "$P" $ALL | head -2; else ok "no banned phrase: $P"; fi
done
if grep -qx "EOF" $ALL 2>/dev/null; then bad "stray EOF line"; else ok "no stray EOF line"; fi

echo "--- README"
has  "README: What this is"          '^## What this is' $R
has  "README: Quick start"           '^## Quick start' $R
has  "README: What you can demonstrate" '^## What you can demonstrate' $R
has  "README: Documentation"         '^## Documentation' $R
has  "README: Testing"               '^## Testing' $R
has  "README: Origin and credit"     '^## Origin and credit' $R
has  "README says not a kernel"      'not a (real )?kernel' $R
hasf "README names original author"  "Priyanka Singh" $R
hasf "README names the MIT license"  "MIT" $R
hasf "README mentions WSL"           "WSL" $R
hasf "README mentions make test"     "make test" $R
hasf "README mentions gate_all.sh"   "gate_all.sh" $R
for n in $DOCS; do hasf "README links docs/$n.md" "docs/$n.md" $R; done
hasf "README links docs/results/"    "docs/results/" $R

echo "--- links, paths and function citations resolve"
for f in $ALL; do
  b=$(basename "$f")
  BL=$(grep -o '](\([^)]*\))' "$f" | sed 's/^](//; s/)$//; s/#.*//' | grep -v -e '^$' -e '://' -e '^mailto:' | while read -r l; do [ -e "$(dirname "$f")/$l" ] || echo "$l"; done)
  if [ -z "$BL" ]; then ok "links resolve: $b"; else bad "broken links in $b: $(echo $BL | head -c 200)"; fi
  BP=$(grep -oE '(exam_os|docs)/[A-Za-z0-9_./-]*[A-Za-z0-9_/]' "$f" | sort -u | while read -r p; do case "$p" in exam_os/output/*) continue;; esac; [ -e "$ROOT/$p" ] || echo "$p"; done)
  if [ -z "$BP" ]; then ok "cited paths exist: $b"; else bad "missing paths cited in $b: $(echo $BP | head -c 300)"; fi
  BF=$(grep -oE 'exam_os/(src|tools)/[A-Za-z0-9_]+\.c::[A-Za-z0-9_]+' "$f" | sort -u | while IFS= read -r c; do file=${c%%::*}; fn=${c##*::}; grep -qw -- "$fn" "$ROOT/$file" 2>/dev/null || echo "$c"; done)
  if [ -z "$BF" ]; then ok "cited functions exist: $b"; else bad "unknown functions cited in $b: $(echo $BF | head -c 300)"; fi
done
cf() { grep -oE 'exam_os/(src|tools)/[A-Za-z0-9_]+\.c::[A-Za-z0-9_]+' "$1" | sort -u | wc -l; }
chk "VIVA cites >= 25 functions"          "$(cf $D/VIVA.md) >= 25"
chk "SYLLABUS_MAP cites >= 20 functions"  "$(cf $D/SYLLABUS_MAP.md) >= 20"
chk "ARCHITECTURE cites >= 12 functions"  "$(cf $D/ARCHITECTURE.md) >= 12"

echo "--- SYLLABUS_MAP: status vocabulary and required rows"
SM=$D/SYLLABUS_MAP.md
chk "six unit headings" "$(grep -cE '^## Unit [1-6]' $SM) == 6"
BADST=$(awk -F'|' 'NF>=5 { s=$3; gsub(/^ +| +$/,"",s); if (s!="Status" && s !~ /^:?-+:?$/ && s!="IMPLEMENTED" && s!="LAB" && s!="PARTIAL" && s!="NOT IMPLEMENTED" && s!="EXCLUDED") print s }' $SM | head -3)
if [ -z "$BADST" ]; then ok "every status is from the allowed vocabulary"; else bad "unknown status values: $BADST"; fi
need() {
  topic=$1; want=$2
  row=$(awk -F'|' -v t="$topic" '{a=$2; gsub(/^ +| +$/,"",a); if (a==t) {print; exit}}' "$SM")
  if [ -z "$row" ]; then bad "row missing: $topic"; return; fi
  got=$(echo "$row" | awk -F'|' '{s=$3; gsub(/^ +| +$/,"",s); print s}')
  where=$(echo "$row" | awk -F'|' '{s=$4; gsub(/^ +| +$/,"",s); print s}')
  evid=$(echo "$row" | awk -F'|' '{s=$5; gsub(/^ +| +$/,"",s); print s}')
  if [ "$got" != "$want" ]; then bad "$topic: status '$got', required '$want'"; return; fi
  case "$want" in
    IMPLEMENTED|LAB|PARTIAL) if echo "$where" | grep -q 'exam_os/' && [ ${#evid} -ge 5 ]; then ok "$topic: $want"; else bad "$topic: needs an exam_os/ path and evidence"; fi;;
    *) if [ ${#where} -ge 15 ]; then ok "$topic: $want"; else bad "$topic: needs a reason of >= 15 characters"; fi;;
  esac
}
need "Shell programming" PARTIAL
need "Kernel structures (monolithic, microkernel, hybrid, modular)" EXCLUDED
need "Process states" IMPLEMENTED
need "Process control block" IMPLEMENTED
need "Process creation with fork/exec/wait" EXCLUDED
need "Threads (POSIX threads)" IMPLEMENTED
need "Scheduling criteria and metrics" IMPLEMENTED
need "Preemptive and non-preemptive scheduling" IMPLEMENTED
need "FCFS" IMPLEMENTED
need "SJF" IMPLEMENTED
need "Round Robin" IMPLEMENTED
need "Priority scheduling with aging" IMPLEMENTED
need "Multilevel Feedback Queue" IMPLEMENTED
need "Critical section and mutual exclusion (mutexes)" IMPLEMENTED
need "Semaphores" IMPLEMENTED
need "Producer-consumer (bounded buffer)" IMPLEMENTED
need "Peterson's solution" "NOT IMPLEMENTED"
need "Readers-writers and dining philosophers" "NOT IMPLEMENTED"
need "Deadlock prevention" "NOT IMPLEMENTED"
need "Deadlock avoidance (Banker's algorithm)" LAB
need "Deadlock detection and recovery" LAB
need "Deadlock handling inside the live simulation" "NOT IMPLEMENTED"
need "Paging and page tables" IMPLEMENTED
need "Demand paging and page faults" IMPLEMENTED
need "FIFO page replacement" IMPLEMENTED
need "LRU page replacement" IMPLEMENTED
need "Clock (second chance) page replacement" IMPLEMENTED
need "Optimal page replacement" LAB
need "Belady's anomaly" LAB
need "Thrashing detection and control" IMPLEMENTED
need "Swapping" "NOT IMPLEMENTED"
need "Contiguous allocation (first, best, worst fit) and fragmentation" "NOT IMPLEMENTED"
need "Segmentation" "NOT IMPLEMENTED"
need "Buddy and slab allocation" "NOT IMPLEMENTED"
need "I/O buffering" IMPLEMENTED
need "Disk scheduling (FIFO, LIFO, SSTF, SCAN, C-SCAN)" IMPLEMENTED
need "File concept, directories and allocation methods" "NOT IMPLEMENTED"
need "File system implementation and free-space management" "NOT IMPLEMENTED"
need "Linux kernel architecture and boot process" EXCLUDED
need "System call interface" "NOT IMPLEMENTED"
need "Interrupt vector table and handlers" IMPLEMENTED
need "Android architecture (HAL, ART, AOSP)" EXCLUDED

echo "--- ARCHITECTURE"
A=$D/ARCHITECTURE.md
chk "ARCHITECTURE has >= 3 mermaid diagrams" "$(grep -c "^${BT}mermaid" $A) >= 3"
for H in Overview Threads "Data ownership" "Lock order" "Process state machine" "Request flows" "Time model"; do has "ARCHITECTURE section: $H" "^## $H" $A; done
for T in tick_thread scheduler_thread memory_thread io_buffer_thread interrupt_thread logger_thread dashboard_thread; do
  hasf "ARCHITECTURE mentions $T" "$T" $A
  if grep -rq "$T" src; then ok "$T exists in src"; else bad "$T exists in src"; fi
done
for L in rq_lock mem_lock disk_lock svc_lock g_state.lock; do hasf "lock order names $L" "$L" $A; done
for L in rq_lock mem_lock disk_lock svc_lock; do if grep -rq "$L" src; then ok "$L exists in src"; else bad "$L exists in src"; fi; done

echo "--- EXPERIMENTS"
E=$D/EXPERIMENTS.md
for S in "scripts/compare.sh" "scripts/pagecurve.sh" "scripts/diskcompare.sh" "scripts/gantt.sh" "scripts/deadlock_demo.sh" "pagesim" "disksim" "deadsim" "compare.md" "pagecurve.md" "belady.md" "disk.md" "deadlock.md"; do hasf "EXPERIMENTS covers $S" "$S" $E; done
for H in Reproducibility "Scheduler comparison" "Paging experiments" "Disk scheduling" "Deadlock lab" "Reading the results"; do has "EXPERIMENTS section: $H" "^## $H" $E; done
hasf "EXPERIMENTS explains runs are not bit-exact" "not bit-exact" $E

echo "--- VIVA"
V=$D/VIVA.md
NQ=$(grep -cE '^### Q[0-9]+\.' $V); NA=$(grep -cE '^\*\*A:\*\*' $V)
chk "VIVA has >= 40 questions" "$NQ >= 40"
chk "every question has exactly one answer" "$NQ == $NA"
chk "VIVA has >= 7 sections" "$(grep -cE '^## ' $V) >= 7"
for S in "FIFO 15 faults" "LRU 12 faults" "OPT 9 faults" "9 faults with 3 frames" "10 faults with 4 frames" "1 3 0 2 4" "640" "236" "331" "382" "609"; do hasf "VIVA states: $S" "$S" $V; done

echo "--- LIMITATIONS"
L=$D/LIMITATIONS.md
for H in "Not implemented" "Modelling simplifications" "Known issues"; do has "LIMITATIONS section: $H" "^## $H" $L; done
for K in "Peterson" "readers-writers" "file system" "slab" "buddy" "segmentation" "system call" "Android" "fork" "swapping" "fragmentation" "not bit-exact" "standalone" "deadsim"; do
  if grep -qiF -- "$K" $L; then ok "LIMITATIONS mentions: $K"; else bad "LIMITATIONS mentions: $K"; fi
done
for I in INT_PAGE_FAULT INT_SUBMIT_COMPLETE; do
  if grep -rq "interrupt_raise($I" src; then ok "$I is raised in code"
  elif grep -q "$I" $L && grep -qi "never raised" $L; then ok "LIMITATIONS discloses that $I is never raised"
  else bad "$I is never raised in the code but LIMITATIONS does not say so"; fi
done

echo "--- PROJECT_STATE"
PS=$D/PROJECT_STATE.md
for G in gate.sh gate23.sh gate24.sh gate31.sh gate32.sh gate41a.sh gate41b.sh gate51.sh gate61.sh gate_all.sh; do hasf "PROJECT_STATE lists $G" "$G" $PS; done
MAXT=$(grep -oE '\bT[0-9]+\b' tests/run_tests.sh | tr -d T | sort -n | tail -1)
hasf "PROJECT_STATE mentions T$MAXT (highest test)" "T$MAXT" $PS
MAXI=$(grep -oE 'I[0-9]+' src/invariants.c | tr -d I | sort -n | tail -1)
hasf "PROJECT_STATE mentions I$MAXI (highest invariant)" "I$MAXI" $PS
MISS=""; for F in $(grep -oE '"--[a-z0-9-]+"' src/config.c | tr -d '"' | sort -u); do grep -qF -- "$F" $PS || MISS="$MISS $F"; done
if [ -z "$MISS" ]; then ok "PROJECT_STATE documents every CLI option"; else bad "PROJECT_STATE is missing options:$MISS"; fi

echo "--- DEMO: every command is executed"
DM=$D/DEMO.md
for H in "Before the demo" "Demo script" "Backup plan"; do has "DEMO section: $H" "^## $H" $DM; done
hasf "DEMO says 12 minutes" "12 minutes" $DM
awk -v bt="$BT" '$0 ~ "^" bt "console" {f=1; next} $0 ~ "^" bt {f=0} f && /^\$ / {sub(/^\$ /, ""); print}' "$DM" > /tmp/g61_cmds.txt
NC=$(wc -l < /tmp/g61_cmds.txt)
chk "DEMO has >= 12 runnable commands" "$NC >= 12"
before=$(git -C "$ROOT" status --porcelain)
i=0
while IFS= read -r c; do
  i=$((i+1))
  if echo "$c" | grep -q "make clean"; then bad "demo cmd $i must not run make clean: $c"; continue; fi
  case "$c" in
    "./exam_os "*) if echo "$c" | grep -q -- '--headless' && echo "$c" | grep -q -- '--tick-ms'; then :; else bad "demo cmd $i needs --headless and --tick-ms: $c"; continue; fi;;
    "make "*|"bash scripts/"*|"./pagesim "*|"./disksim "*|"./deadsim "*|"cat "*|"head "*|"tail "*|"grep "*|"ls "*) ;;
    *) bad "demo cmd $i is not an allowed command: $c"; continue;;
  esac
  case "$c" in
    "bash scripts/compare.sh"*|"bash scripts/pagecurve.sh"*|"bash scripts/diskcompare.sh"*|"bash scripts/deadlock_demo.sh"*)
      if echo "$c" | grep -q -- '--out /tmp/'; then :; else bad "demo cmd $i must write to --out /tmp/...: $c"; continue; fi;;
  esac
  if timeout 300 bash -c "$c" </dev/null >/tmp/g61_cmd_$i.txt 2>&1; then ok "demo cmd $i runs: $c"; else bad "demo cmd $i failed: $c"; tail -3 /tmp/g61_cmd_$i.txt; fi
done < /tmp/g61_cmds.txt
after=$(git -C "$ROOT" status --porcelain)
if [ "$before" = "$after" ]; then ok "demo commands left tracked files unchanged"; else bad "demo commands modified tracked files"; fi

echo; if [ $fail -eq 0 ]; then echo "GATE 6.1 PASSED"; else echo "GATE 6.1 FAILED"; fi
exit $fail