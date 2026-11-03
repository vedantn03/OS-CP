#!/usr/bin/env bash
set -u

SEEDS="1 2 3 4 5"
ALGOS="FCFS SJF SRTF RR PRIORITY PRIO MLFQ"
OUT="../docs/results"
EXTRA=()

while [ "$#" -gt 0 ]; do
    case "$1" in
        --seeds) [ "$#" -ge 2 ] || { echo "missing value for --seeds" >&2; exit 2; }; SEEDS=$2; shift 2 ;;
        --algos) [ "$#" -ge 2 ] || { echo "missing value for --algos" >&2; exit 2; }; ALGOS=$2; shift 2 ;;
        --out) [ "$#" -ge 2 ] || { echo "missing value for --out" >&2; exit 2; }; OUT=$2; shift 2 ;;
        --) shift; EXTRA=("$@"); break ;;
        *) EXTRA+=("$1"); shift ;;
    esac
done

cd "$(dirname "$0")/.." || exit 1
[ -x ./exam_os ] || make || exit 1
mkdir -p "$OUT" || exit 1

CSV="$OUT/compare.csv"
MD="$OUT/compare.md"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/exam_os_compare.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
printf '%s\n' "algo,seed,avg_wait,avg_turnaround,avg_response,completed,timeouts,ctx_switches,preemptions,cpu_util,throughput" > "$CSV"
failed=0

extract() {
    sed -n "s/.*$1 *: *\([0-9][0-9.]*\).*/\1/p" "$2" | head -1
}

for algo in $ALGOS; do
    for seed in $SEEDS; do
        log="$TMP/${algo}_${seed}.log"
        ./exam_os --headless --strict --tick-ms 10 --workload INTERACTIVE \
            --students 12 --duration 300 --algo "$algo" --seed "$seed" \
            "${EXTRA[@]}" >"$log" 2>&1
        rc=$?
        summary="$TMP/${algo}_${seed}.summary"
        cp output/summary.txt "$summary" 2>/dev/null || : 
        if [ "$rc" -ne 0 ]; then
            printf 'RUN FAILED %s %s\n' "$algo" "$seed" >&2
            failed=1
        fi
        printf '%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n' \
            "$algo" "$seed" \
            "$(extract 'Avg Waiting Time' "$summary")" \
            "$(extract 'Avg Turnaround' "$summary")" \
            "$(extract 'Avg Response' "$summary")" \
            "$(extract 'Completed Exams' "$summary")" \
            "$(extract 'Timeouts Fired' "$summary")" \
            "$(extract 'Context Switches' "$summary")" \
            "$(extract 'Preemptions' "$summary")" \
            "$(extract 'CPU Utilization' "$summary")" \
            "$(extract 'Throughput (per 100 ticks)' "$summary")" >> "$CSV"
    done
done

{
    echo "# Scheduler comparison"
    printf 'Seeds: %s; runs per algorithm: %s; workload INTERACTIVE, 12 students, 300 ticks, tick 10 ms\n' \
        "$SEEDS" "$(printf '%s\n' $SEEDS | awk 'NF { n++ } END { print n+0 }')"
    echo
    echo "| Algorithm | Avg Wait | Avg Turnaround | Avg Response | Completed | Timeouts | Ctx Switches | Preemptions | CPU % | Throughput |"
    echo "|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|"
    awk -F, '
        NR == 1 { next }
        {
            n[$1]++
            for (i = 3; i <= 11; i++) sum[$1, i] += $i
        }
        END {
            order = "'"$ALGOS"'"
            count = split(order, names, /[[:space:]]+/)
            for (j = 1; j <= count; j++) {
                a = names[j]
                printf "| %s | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f | %.1f |\n",
                    a, sum[a,3]/n[a], sum[a,4]/n[a], sum[a,5]/n[a],
                    sum[a,6]/n[a], sum[a,7]/n[a], sum[a,8]/n[a],
                    sum[a,9]/n[a], sum[a,10]/n[a], sum[a,11]/n[a]
            }
        }
    ' "$CSV"
    echo
    echo "Lower is better for wait, turnaround, response, timeouts, and context switches."
    echo "Higher is better for completed exams, CPU percentage, and throughput."
    echo "Results are averages because the threaded simulation is not bit-exact repeatable across runs even with the same seed."
} > "$MD"
cat "$MD"
exit "$failed"
