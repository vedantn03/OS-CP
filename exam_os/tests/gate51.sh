#!/usr/bin/env bash
# Gate for 5.1: deadsim (Banker's, detection, recovery, wait-for graph). Hand-written; Copilot must not edit.
cd "$(dirname "$0")/.." || exit 1
fail=0
ok()    { echo "PASS  $1"; }
bad()   { echo "FAIL  $1"; fail=1; }
has()   { d=$1; p=$2; shift 2; if grep -q -- "$p" "$@" 2>/dev/null; then ok "$d"; else bad "$d"; fi; }
hasnt() { d=$1; p=$2; shift 2; if grep -q -- "$p" "$@" 2>/dev/null; then bad "$d"; else ok "$d"; fi; }
eq()    { d=$1; got=$2; want=$3; if [ "$got" = "$want" ]; then ok "$d"; else bad "$d [got '$got', want '$want']"; fi; }
chk()   { d=$1; shift; if awk "BEGIN{exit !($*)}" 2>/dev/null; then ok "$d"; else bad "$d [$*]"; fi; }
rc()    { "$@" >/dev/null 2>&1; echo $?; }
field() { sed -n "s/^$1: *//p" | head -1; }
ds()    { ./deadsim "$@" 2>/dev/null; }

if bash tests/gate41a.sh >/tmp/g51_prev.txt 2>&1; then ok "regression: gate 4.1a"; else bad "regression: gate 4.1a"; tail -15 /tmp/g51_prev.txt; fi
make clean >/dev/null 2>&1
make >/tmp/g51_make.txt 2>&1 || { echo "FAIL  build"; tail -20 /tmp/g51_make.txt; exit 1; }
make deadsim >/tmp/g51_ds.txt 2>&1 || { echo "FAIL  deadsim build"; tail -20 /tmp/g51_ds.txt; exit 1; }
if grep -qi warning /tmp/g51_make.txt /tmp/g51_ds.txt; then bad "build: warnings"; else ok "build: no warnings"; fi

echo "--- source checks"
[ -s tools/deadsim.c ] && ok "tools/deadsim.c exists" || bad "tools/deadsim.c exists"
has   "Makefile builds deadsim"  "deadsim" Makefile
has   "deadsim git-ignored"      "deadsim" ../.gitignore
hasnt "deadsim.c is thread-free" "pthread" tools/deadsim.c
if [ -z "$(git status --porcelain -- src include 2>/dev/null)" ]; then ok "simulator sources untouched"; else bad "simulator sources modified"; fi

W=/tmp/g51; mkdir -p $W
cat > $W/bank.txt <<'EOF'
PROCESSES 5
RESOURCES 3
AVAILABLE 3 3 2
MAX
7 5 3
3 2 2
9 0 2
2 2 2
4 3 3
ALLOC
0 1 0
2 0 0
3 0 2
2 1 1
0 0 2
EOF
sed 's/^AVAILABLE .*/AVAILABLE 0 0 0/' $W/bank.txt > $W/unsafe.txt
cat > $W/after.txt <<'EOF'
PROCESSES 5
RESOURCES 3
AVAILABLE 2 3 0
MAX
7 5 3
3 2 2
9 0 2
2 2 2
4 3 3
ALLOC
0 1 0
3 0 2
3 0 2
2 1 1
0 0 2
EOF
cat > $W/det.txt <<'EOF'
PROCESSES 5
RESOURCES 3
AVAILABLE 0 0 0
ALLOC
0 1 0
2 0 0
3 0 3
2 1 1
0 0 2
REQUEST
0 0 0
2 0 2
0 0 0
1 0 0
0 0 2
EOF
cat > $W/dead.txt <<'EOF'
PROCESSES 5
RESOURCES 3
AVAILABLE 0 0 0
ALLOC
0 1 0
2 0 0
3 0 3
2 1 1
0 0 2
REQUEST
0 0 0
2 0 2
0 0 1
1 0 0
0 0 2
EOF

echo "--- Banker's safety (textbook, deterministic lowest-index rule)"
eq "safe state"            "$(ds safe --file $W/bank.txt | field state)" safe
eq "safe sequence"         "$(ds safe --file $W/bank.txt | field sequence)" "1 3 0 2 4"
eq "unsafe state"          "$(ds safe --file $W/unsafe.txt | field state)" unsafe
eq "unsafe sequence none"  "$(ds safe --file $W/unsafe.txt | field sequence)" none

echo "--- Banker's resource-request algorithm"
O=$(ds request --file $W/bank.txt --pid 1 --req "1 0 2")
eq "P1 (1 0 2): granted"   "$(echo "$O" | field result)" granted
eq "P1 (1 0 2): reason ok" "$(echo "$O" | field reason)" ok
eq "P1 (1 0 2): sequence"  "$(echo "$O" | field sequence)" "1 3 0 2 4"
O=$(ds request --file $W/after.txt --pid 4 --req "3 3 0")
eq "P4 (3 3 0): denied"    "$(echo "$O" | field result)" denied
eq "P4 (3 3 0): exceeds_available" "$(echo "$O" | field reason)" exceeds_available
O=$(ds request --file $W/after.txt --pid 0 --req "0 2 0")
eq "P0 (0 2 0): denied"    "$(echo "$O" | field result)" denied
eq "P0 (0 2 0): unsafe"    "$(echo "$O" | field reason)" unsafe
O=$(ds request --file $W/bank.txt --pid 1 --req "5 5 5")
eq "P1 (5 5 5): exceeds_need" "$(echo "$O" | field reason)" exceeds_need

echo "--- deadlock detection (textbook)"
O=$(ds detect --file $W/det.txt)
eq "no deadlock"           "$(echo "$O" | field deadlock)" no
eq "finish order"          "$(echo "$O" | field finish_order)" "0 2 1 3 4"
eq "deadlocked none"       "$(echo "$O" | field deadlocked)" none
O=$(ds detect --file $W/dead.txt)
eq "deadlock found"        "$(echo "$O" | field deadlock)" yes
eq "deadlocked set"        "$(echo "$O" | field deadlocked)" "1 2 3 4"
eq "finish order (P0 only)" "$(echo "$O" | field finish_order)" "0"

echo "--- recovery policies"
for PL in "min-alloc:1" "max-alloc:2" "lowest-index:1" "highest-index:4"; do
  P=${PL%%:*}; E=${PL##*:}
  O=$(ds recover --file $W/dead.txt --policy $P)
  eq "$P aborts $E"        "$(echo "$O" | field aborted)" "$E"
  eq "$P final state"      "$(echo "$O" | field final)" no_deadlock
done
eq "recover on healthy state aborts none" "$(ds recover --file $W/det.txt | field aborted)" none

echo "--- wait-for graph"
O=$(ds wfg --nodes 4 --edges "0>1 1>2 2>0 3>0")
eq "cycle 0-1-2 found"     "$(echo "$O" | field deadlock)" yes
eq "on_cycle"              "$(echo "$O" | field on_cycle)" "0 1 2"
O=$(ds wfg --nodes 3 --edges "0>1 1>2")
eq "chain: no deadlock"    "$(echo "$O" | field deadlock)" no
eq "chain: on_cycle none"  "$(echo "$O" | field on_cycle)" none
eq "self loop"             "$(ds wfg --nodes 3 --edges "1>1" | field on_cycle)" "1"
eq "empty edges"           "$(ds wfg --nodes 3 --edges "" | field deadlock)" no

PARSE='
$1=="PROCESSES"{P=$2; next} $1=="RESOURCES"{R=$2; next}
$1=="AVAILABLE"{for(j=1;j<=R;j++) av[j]=$(j+1); next}
$1=="MAX"{sec="m"; r=0; next} $1=="ALLOC"{sec="a"; r=0; next} $1=="REQUEST"{sec="q"; r=0; next}
{ if(sec=="m"){for(j=1;j<=R;j++) mx[r,j]=$j; r++} else if(sec=="a"){for(j=1;j<=R;j++) al[r,j]=$j; r++} else if(sec=="q"){for(j=1;j<=R;j++) rq[r,j]=$j; r++} }
'
verdict() { awk "$PARSE"'
END{ for(j=1;j<=R;j++) w[j]=av[j]; for(i=0;i<P;i++) d[i]=0
 do { p=0; for(i=P-1;i>=0;i--) if(!d[i]){ g=1; for(j=1;j<=R;j++) if(mx[i,j]-al[i,j]>w[j]) g=0; if(g){ for(j=1;j<=R;j++) w[j]+=al[i,j]; d[i]=1; p=1 } } } while(p)
 s="safe"; for(i=0;i<P;i++) if(!d[i]) s="unsafe"; print s }' "$1"; }
dverdict() { awk "$PARSE"'
END{ for(j=1;j<=R;j++) w[j]=av[j]
 for(i=0;i<P;i++){ z=1; for(j=1;j<=R;j++) if(al[i,j]>0) z=0; d[i]=z }
 do { p=0; for(i=P-1;i>=0;i--) if(!d[i]){ g=1; for(j=1;j<=R;j++) if(rq[i,j]>w[j]) g=0; if(g){ for(j=1;j<=R;j++) w[j]+=al[i,j]; d[i]=1; p=1 } } } while(p)
 s=""; for(i=0;i<P;i++) if(!d[i]) s=s (s==""?"":" ") i; print (s==""?"none":s) }' "$1"; }
seqvalid() { awk -v seq="$2" "$PARSE"'
END{ n=split(seq,q," "); v=(n==P); for(j=1;j<=R;j++) w[j]=av[j]
 for(k=1;k<=n&&v;k++){ i=q[k]+0; if(seen[i]++){v=0;break}; for(j=1;j<=R;j++) if(mx[i,j]-al[i,j]>w[j]) v=0; if(v) for(j=1;j<=R;j++) w[j]+=al[i,j] }
 print (v?"valid":"invalid") }' "$1"; }
gen() { awk -v seed="$2" -v mode="$3" 'BEGIN{ srand(seed); P=4; R=3
 print "PROCESSES " P; print "RESOURCES " R
 l="AVAILABLE"; for(j=0;j<R;j++) l=l " " int(rand()*4); print l
 for(i=0;i<P;i++) for(j=0;j<R;j++){ a[i,j]=int(rand()*(mode=="d"?3:4)); m[i,j]=a[i,j]+int(rand()*4); q[i,j]=int(rand()*4) }
 if(mode=="s"){ print "MAX"; for(i=0;i<P;i++){ l=""; for(j=0;j<R;j++) l=l (j?" ":"") m[i,j]; print l } }
 print "ALLOC"; for(i=0;i<P;i++){ l=""; for(j=0;j<R;j++) l=l (j?" ":"") a[i,j]; print l }
 if(mode=="d"){ print "REQUEST"; for(i=0;i<P;i++){ l=""; for(j=0;j<R;j++) l=l (j?" ":"") q[i,j]; print l } } }' > "$1"; }

echo "--- cross-check against an independent awk implementation"
MIS=0; NS=0; NU=0
for i in $(seq 1 80); do
  gen $W/s$i.txt $i s
  V=$(verdict $W/s$i.txt); O=$(ds safe --file $W/s$i.txt); S=$(echo "$O" | field state)
  [ "$S" = "$V" ] || { MIS=$((MIS+1)); echo "  safety mismatch case $i: deadsim=$S awk=$V"; }
  if [ "$V" = safe ]; then NS=$((NS+1)); SQ=$(echo "$O" | field sequence)
    [ "$(seqvalid $W/s$i.txt "$SQ")" = valid ] || { MIS=$((MIS+1)); echo "  invalid sequence case $i: $SQ"; }
  else NU=$((NU+1)); fi
done
chk "80 random safety cases: 0 mismatches" "$MIS == 0"
chk "random safety cases include safe and unsafe" "$NS >= 1 && $NU >= 1"
echo "INFO  safety cases: safe=$NS unsafe=$NU"
MIS=0; ND=0; NN=0
for i in $(seq 1 80); do
  gen $W/d$i.txt $((i+1000)) d
  V=$(dverdict $W/d$i.txt); O=$(ds detect --file $W/d$i.txt); S=$(echo "$O" | field deadlocked)
  [ "$S" = "$V" ] || { MIS=$((MIS+1)); echo "  detect mismatch case $i: deadsim='$S' awk='$V'"; }
  if [ "$V" = none ]; then NN=$((NN+1)); else ND=$((ND+1)); fi
done
chk "80 random detection cases: 0 mismatches" "$MIS == 0"
chk "random detection cases include both outcomes" "$ND >= 1 && $NN >= 1"
echo "INFO  detection cases: deadlocked=$ND clean=$NN"

echo "--- scenario files"
for F in banker_textbook detect_textbook_ok detect_textbook_deadlock exam_safe exam_unsafe exam_deadlock; do
  [ -s scenarios/$F.txt ] && ok "scenario $F exists" || bad "scenario $F exists"
done
eq "banker_textbook sequence" "$(ds safe --file scenarios/banker_textbook.txt | field sequence)" "1 3 0 2 4"
eq "detect_textbook_ok"       "$(ds detect --file scenarios/detect_textbook_ok.txt | field deadlock)" no
eq "detect_textbook_deadlock" "$(ds detect --file scenarios/detect_textbook_deadlock.txt | field deadlocked)" "1 2 3 4"
eq "exam_safe is safe"        "$(ds safe --file scenarios/exam_safe.txt | field state)" safe
eq "exam_unsafe is unsafe"    "$(ds safe --file scenarios/exam_unsafe.txt | field state)" unsafe
eq "exam_deadlock detected"   "$(ds detect --file scenarios/exam_deadlock.txt | field deadlock)" yes
ND=$(ds detect --file scenarios/exam_deadlock.txt | field deadlocked | wc -w)
chk "exam_deadlock: >= 3 sessions deadlocked" "$ND >= 3"
O=$(ds recover --file scenarios/exam_deadlock.txt --policy min-alloc)
[ -n "$(echo "$O" | field aborted)" ] && [ "$(echo "$O" | field aborted)" != none ] && ok "exam_deadlock: recovery aborts a session" || bad "exam_deadlock: recovery aborts a session"
eq "exam_deadlock: recovered" "$(echo "$O" | field final)" no_deadlock

echo "--- demo script"
rm -rf /tmp/g51dd
if bash scripts/deadlock_demo.sh --out /tmp/g51dd >/tmp/g51_dd.log 2>&1; then ok "deadlock_demo exit 0"; else bad "deadlock_demo exit 0"; tail -10 /tmp/g51_dd.log; fi
for H in "## Banker" "## Deadlock detection" "## Recovery" "## Wait-for graph"; do has "demo section: $H" "^$H" /tmp/g51dd/deadlock.md; done
has "demo shows textbook sequence" "1 3 0 2 4" /tmp/g51dd/deadlock.md

echo "--- error handling"
printf 'PROCESSES 1\nRESOURCES 1\nAVAILABLE 1\nMAX\n1\nALLOC\n2\n' > $W/bad.txt
eq "unknown command -> 2"      "$(rc ./deadsim bogus)" 2
eq "missing --file -> 2"       "$(rc ./deadsim safe)" 2
eq "missing file -> 1"         "$(rc ./deadsim safe --file /nonexistent)" 1
eq "alloc > max -> 1"          "$(rc ./deadsim safe --file $W/bad.txt)" 1
eq "pid out of range -> 1"     "$(rc ./deadsim request --file $W/bank.txt --pid 9 --req "1 0 0")" 1
eq "wrong request length -> 1" "$(rc ./deadsim request --file $W/bank.txt --pid 1 --req "1 0")" 1
eq "detect without REQUEST -> 1" "$(rc ./deadsim detect --file $W/bank.txt)" 1
eq "bad policy -> 2"           "$(rc ./deadsim recover --file $W/dead.txt --policy bogus)" 2
eq "wfg edge out of range -> 1" "$(rc ./deadsim wfg --nodes 2 --edges "0>5")" 1
eq "wfg nodes 0 -> 2"          "$(rc ./deadsim wfg --nodes 0 --edges "")" 2

echo; if [ $fail -eq 0 ]; then echo "GATE 5.1 PASSED"; else echo "GATE 5.1 FAILED"; fi
exit $fail