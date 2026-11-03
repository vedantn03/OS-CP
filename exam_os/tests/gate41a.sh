#!/usr/bin/env bash
# Gate for 4.1a: pure disk-scheduling module + disksim tool. Hand-written; Copilot must not edit.
cd "$(dirname "$0")/.." || exit 1
fail=0
ok()    { echo "PASS  $1"; }
bad()   { echo "FAIL  $1"; fail=1; }
chk()   { d=$1; shift; if awk "BEGIN{exit !($*)}" 2>/dev/null; then ok "$d"; else bad "$d [$*]"; fi; }
has()   { d=$1; p=$2; shift 2; if grep -q -- "$p" "$@" 2>/dev/null; then ok "$d"; else bad "$d"; fi; }
hasnt() { d=$1; p=$2; shift 2; if grep -q -- "$p" "$@" 2>/dev/null; then bad "$d"; else ok "$d"; fi; }
rc()    { "$@" >/dev/null 2>&1; echo $?; }
if make test >/tmp/g41a_test.txt 2>&1; then ok "regression: make test"; else bad "regression: make test"; tail -8 /tmp/g41a_test.txt; fi
make clean >/dev/null 2>&1
make >/tmp/g41a_make.txt 2>&1 || { echo "FAIL  build"; tail -20 /tmp/g41a_make.txt; exit 1; }
make disksim >/tmp/g41a_ds.txt 2>&1 || { echo "FAIL  disksim build"; tail -20 /tmp/g41a_ds.txt; exit 1; }
if grep -qi warning /tmp/g41a_make.txt /tmp/g41a_ds.txt; then bad "build: warnings"; else ok "build: no warnings"; fi
out()   { ./disksim --algo "$1" --head "$2" --cylinders "$3" --string "$4" --dir "${5:-up}" 2>/dev/null; }
field() { sed -n "s/^$1: *//p" | head -1; }
mvt()   { out "$@" | field movement; }

echo "--- source checks"
has   "disk_sched_run defined"   "disk_sched_run" src/disk_sched.c
has   "disk_algo_parse defined"  "disk_algo_parse" src/disk_sched.c
has   "DISK_CSCAN in header"     "DISK_CSCAN" include/disk_sched.h
hasnt "disk_sched.c is thread-free" "pthread" src/disk_sched.c
has   "Makefile builds disksim"  "disksim" Makefile
has   "disksim git-ignored"      "disksim" ../.gitignore

echo "--- textbook example: head 53, 200 cylinders, queue 98 183 37 122 14 124 65 67"
Q="98 183 37 122 14 124 65 67"
chk "FIFO = 640"            "$(mvt FIFO 53 200 "$Q") == 640"
chk "LIFO = 609"            "$(mvt LIFO 53 200 "$Q") == 609"
chk "SSTF = 236"            "$(mvt SSTF 53 200 "$Q") == 236"
chk "SCAN (up first) = 331" "$(mvt SCAN 53 200 "$Q") == 331"
chk "SCAN (down first) = 236" "$(mvt SCAN 53 200 "$Q" down) == 236"
chk "C-SCAN (up) = 382"     "$(mvt CSCAN 53 200 "$Q") == 382"
[ "$(out SSTF 53 200 "$Q" | field order)" = "65 67 37 14 98 122 124 183" ] && ok "SSTF order" || bad "SSTF order"
[ "$(out FIFO 53 200 "$Q" | field order)" = "98 183 37 122 14 124 65 67" ] && ok "FIFO order" || bad "FIFO order"
[ "$(out LIFO 53 200 "$Q" | field order)" = "67 65 124 14 122 37 183 98" ] && ok "LIFO order" || bad "LIFO order"
[ "$(out SCAN 53 200 "$Q" | field order)" = "65 67 98 122 124 183 37 14" ] && ok "SCAN order" || bad "SCAN order"
[ "$(out CSCAN 53 200 "$Q" | field order)" = "65 67 98 122 124 183 14 37" ] && ok "C-SCAN order" || bad "C-SCAN order"
chk "requests line = 8"     "$(out FIFO 53 200 "$Q" | field requests) == 8"
chk "SCAN final head 14"    "$(out SCAN 53 200 "$Q" | field final_head) == 14"
[ "$(out SCAN 53 200 "$Q" | field final_dir)" = "down" ] && ok "SCAN final dir down" || bad "SCAN final dir down"
chk "C-SCAN final head 37"  "$(out CSCAN 53 200 "$Q" | field final_head) == 37"
[ "$(out CSCAN 53 200 "$Q" | field final_dir)" = "up" ] && ok "C-SCAN final dir up" || bad "C-SCAN final dir up"
chk "SSTF final head 183"   "$(out SSTF 53 200 "$Q" | field final_head) == 183"
[ "$(out FIFO 53 200 "$Q" | field final_dir)" = "up" ] && ok "FIFO keeps dir" || bad "FIFO keeps dir"

echo "--- edge cases"
chk "all requests at head = 0"      "$(mvt FIFO 5 200 "5 5 5") == 0"
chk "single request"                "$(mvt FIFO 10 200 "50") == 40"
chk "SCAN: all requests behind"     "$(mvt SCAN 150 200 "10 20") == 238"
chk "C-SCAN: all requests behind"   "$(mvt CSCAN 150 200 "10 20") == 268"
[ "$(out SSTF 50 200 "60 40" | field order)" = "60 40" ] && ok "SSTF tie -> earlier arrival" || bad "SSTF tie -> earlier arrival"
chk "comma-separated input"         "$(mvt FIFO 53 200 "98,183,37,122,14,124,65,67") == 640"
printf '%s\n' $Q >/tmp/g41a_q.txt
chk "--file input" "$(./disksim --algo FIFO --head 53 --cylinders 200 --file /tmp/g41a_q.txt 2>/dev/null | field movement) == 640"

echo "--- 60 random requests: seek-optimizing policies beat FIFO"
R=$(awk 'BEGIN{srand(7); for(i=0;i<60;i++) printf "%d ", int(rand()*200)}')
F=$(mvt FIFO 100 200 "$R"); S=$(mvt SSTF 100 200 "$R"); K=$(mvt SCAN 100 200 "$R"); C=$(mvt CSCAN 100 200 "$R")
chk "SSTF < FIFO"   "${S:-999999} < ${F:-0}"
chk "SCAN < FIFO"   "${K:-999999} < ${F:-0}"
chk "C-SCAN < FIFO" "${C:-999999} < ${F:-0}"
echo "INFO  60 random requests: FIFO=$F SSTF=$S SCAN=$K CSCAN=$C"

echo "--- error handling"
chk "bad algo -> exit 2"       "$(rc ./disksim --algo BOGUS --head 0 --cylinders 200 --string "1 2") == 2"
chk "cylinders 0 -> exit 2"    "$(rc ./disksim --algo FIFO --head 0 --cylinders 0 --string "1") == 2"
chk "head out of range -> 2"   "$(rc ./disksim --algo FIFO --head 500 --cylinders 200 --string "1") == 2"
chk "bad --dir -> exit 2"      "$(rc ./disksim --algo FIFO --head 0 --cylinders 200 --string "1" --dir sideways) == 2"
chk "no input -> exit 2"       "$(rc ./disksim --algo FIFO --head 0 --cylinders 200) == 2"
chk "request out of range -> 1" "$(rc ./disksim --algo FIFO --head 0 --cylinders 200 --string "999") == 1"
chk "empty request list -> 1"  "$(rc ./disksim --algo FIFO --head 0 --cylinders 200 --string "") == 1"

echo; if [ $fail -eq 0 ]; then echo "GATE 4.1a PASSED"; else echo "GATE 4.1a FAILED"; fi
exit $fail