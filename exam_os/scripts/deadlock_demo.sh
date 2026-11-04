#!/usr/bin/env bash
set -eu
OUT="../docs/results"
while [ "$#" -gt 0 ]; do
    case "$1" in
        --out) [ "$#" -ge 2 ] || { echo "missing value for --out" >&2; exit 2; }; OUT=$2; shift 2 ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
cd "$(dirname "$0")/.." || exit 1
[ -x ./deadsim ] || make deadsim
mkdir -p "$OUT"
OUT_FILE="$OUT/deadlock.md"
TMP=$(mktemp -d "${TMPDIR:-/tmp}"/deadsim_demo.XXXXXX)
trap 'rm -rf "$TMP"' EXIT INT TERM
cat > "$TMP/post_grant.txt" <<'EOF'
# Student sessions; resources are GRADER, DBCONN and QBANK.
PROCESSES 5
RESOURCES 3
AVAILABLE 2 3 0
NAMES GRADER DBCONN QBANK
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
{
    echo '# Deadlock lab demo'; echo
    echo 'This report uses a standalone lab that models exam-server resources; it is not part of the live threaded simulation.'; echo
    echo 'The four necessary conditions for deadlock are mutual exclusion, hold and wait, no preemption, and circular wait. Banker avoidance prevents unsafe states before grant. Detection followed by process abortion breaks circular wait after the fact.'; echo
    echo "## Banker's algorithm (safety and requests)"; echo
    echo 'The safety algorithm applies the deterministic lowest-index rule to the textbook state and prints its safe sequence. The textbook request by P1 for (1 0 2) is granted, after which the written tentative state is used for two denial examples.'; echo
    cmd='./deadsim safe --file scenarios/banker_textbook.txt'
    echo "Command: $cmd"; printf '\140\140\140text\n'; ./deadsim safe --file scenarios/banker_textbook.txt; printf '\140\140\140\n'
    echo
    cmd='./deadsim request --file scenarios/banker_textbook.txt --pid 1 --req "1 0 2"'
    echo "Command: $cmd"; printf '\140\140\140text\n'; ./deadsim request --file scenarios/banker_textbook.txt --pid 1 --req "1 0 2"; printf '\140\140\140\n'
    echo
    cmd="./deadsim request --file $TMP/post_grant.txt --pid 4 --req \"3 3 0\""
    echo "Command: $cmd"; printf '\140\140\140text\n'; ./deadsim request --file "$TMP/post_grant.txt" --pid 4 --req "3 3 0"; printf '\140\140\140\n'
    echo
    cmd="./deadsim request --file $TMP/post_grant.txt --pid 0 --req \"0 2 0\""
    echo "Command: $cmd"; printf '\140\140\140text\n'; ./deadsim request --file "$TMP/post_grant.txt" --pid 0 --req "0 2 0"; printf '\140\140\140\n'
    echo
    cmd='./deadsim safe --file scenarios/exam_unsafe.txt'
    echo "Command: $cmd"; printf '\140\140\140text\n'; ./deadsim safe --file scenarios/exam_unsafe.txt; printf '\140\140\140\n'
    echo
    echo 'The textbook state has deterministic sequence 1 3 0 2 4; P1 is granted while P4 and P0 are denied for different reasons.'
    echo 'The exam state is unsafe. The P1 grant is written to a temporary state file and removed when the demo exits.'
    echo
    echo '## Deadlock detection'; echo
    echo 'Detection starts with currently available resources and repeatedly completes the lowest-index eligible session. Processes holding no resources begin finished.'; echo
    for name in detect_textbook_ok detect_textbook_deadlock exam_deadlock; do
        cmd="./deadsim detect --file scenarios/$name.txt"
        echo "Command: $cmd"; printf '\140\140\140text\n'; ./deadsim detect --file "scenarios/$name.txt"; printf '\140\140\140\n'
    done
    echo 'The textbook examples demonstrate both outcomes, while the exam case leaves multiple sessions unfinished.'
    echo 'Processes with zero allocation begin finished and are omitted from finish_order.'
    echo
    echo '## Recovery (victim selection policies)'; echo
    echo 'Recovery aborts one currently deadlocked session at a time and releases its allocation. Victim selection changes the abort order; each policy is demonstrated on the textbook deadlock state.'; echo
    for policy in min-alloc max-alloc lowest-index highest-index; do
        if [ "$policy" = min-alloc ]; then cmd='./deadsim recover --file scenarios/detect_textbook_deadlock.txt'; else cmd="./deadsim recover --file scenarios/detect_textbook_deadlock.txt --policy $policy"; fi
        echo "Command: $cmd"; printf '\140\140\140text\n'
        if [ "$policy" = min-alloc ]; then ./deadsim recover --file scenarios/detect_textbook_deadlock.txt; else ./deadsim recover --file scenarios/detect_textbook_deadlock.txt --policy "$policy"; fi
        printf '\140\140\140\n'
    done
    echo 'The selected victim is aborted and its allocation is returned before detection runs again.'
    echo 'The initial deadlock result stays visible even when recovery reaches a no-deadlock final state.'
    echo
    echo '## Wait-for graph'; echo
    echo 'A wait-for graph reports every node that can reach itself through one or more edges. A chain has no cycle; the directed triangle has a cycle.'; echo
    cmd='./deadsim wfg --nodes 3 --edges "0>1 1>2 2>0"'
    echo "Command: $cmd"; printf '\140\140\140text\n'; ./deadsim wfg --nodes 3 --edges "0>1 1>2 2>0"; printf '\140\140\140\n'
    echo
    cmd='./deadsim wfg --nodes 3 --edges "0>1 1>2"'
    echo "Command: $cmd"; printf '\140\140\140text\n'; ./deadsim wfg --nodes 3 --edges "0>1 1>2"; printf '\140\140\140\n'
    echo
    echo 'A node is listed only if a nonempty directed path returns to that same node.'
    echo 'The triangle lists all three nodes; the chain lists none.'
} > "$OUT_FILE"
cat "$OUT_FILE"
