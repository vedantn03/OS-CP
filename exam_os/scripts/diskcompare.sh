#!/usr/bin/env bash
set -u

SEEDS="1 2 3"
OUT="../docs/results"
EXTRA=()

while [ "$#" -gt 0 ]; do
    case "$1" in
        --seeds) [ "$#" -ge 2 ] || { echo "missing value for --seeds" >&2; exit 2; }; SEEDS=$2; shift 2 ;;
        --out) [ "$#" -ge 2 ] || { echo "missing value for --out" >&2; exit 2; }; OUT=$2; shift 2 ;;
        --) shift; EXTRA=("$@"); break ;;
        *) EXTRA+=("$1"); shift ;;
    esac
done

cd "$(dirname "$0")/.." || exit 1
[ -x ./exam_os ] || make || exit 1
[ -x ./disksim ] || make disksim || exit 1
mkdir -p "$OUT" || exit 1

CSV="$OUT/disk.csv"
MD="$OUT/disk.md"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/exam_os_diskcompare.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
printf '%s\n' "algo,seed,requests,batches,movement,avg_seek" > "$CSV"
failed=0

for seed in $SEEDS; do
    trace="$TMP/trace_$seed.txt"
    ./exam_os --headless --strict --tick-ms 10 --workload INTERACTIVE \
        --students 12 --duration 300 --algo RR --frames 16 --write-pct 40 \
        --disk FIFO --disk-trace --seed "$seed" "${EXTRA[@]}" \
        >"$TMP/run_$seed.log" 2>&1
    rc=$?
    if [ "$rc" -ne 0 ] || [ ! -f output/disktrace.txt ]; then
        printf 'RUN FAILED %s\n' "$seed" >&2
        failed=1
        continue
    fi
    cp output/disktrace.txt "$trace"
    header=$(head -1 "$trace")
    cylinders=$(printf '%s\n' "$header" | sed -n 's/.*cylinders=\([0-9][0-9]*\).*/\1/p')
    start=$(printf '%s\n' "$header" | sed -n 's/.*start=\([0-9][0-9]*\).*/\1/p')
    if [ -z "$cylinders" ] || [ -z "$start" ]; then
        printf 'RUN FAILED %s invalid trace header\n' "$seed" >&2
        failed=1
        continue
    fi
    for algo in FIFO LIFO SSTF SCAN CSCAN; do
        head=$start
        dir=up
        requests=0
        batches=0
        movement=0
        while IFS= read -r line || [ -n "$line" ]; do
            case "$line" in
                ""|\#*) continue ;;
            esac
            count=$(printf '%s\n' "$line" | awk '{print NF}')
            requests=$((requests + count))
            batches=$((batches + 1))
            result=$(./disksim --algo "$algo" --head "$head" --dir "$dir" \
                --cylinders "$cylinders" --string "$line" 2>"$TMP/error")
            if [ "$?" -ne 0 ]; then
                printf 'RUN FAILED %s %s\n' "$algo" "$seed" >&2
                failed=1
                continue
            fi
            movement=$((movement + $(printf '%s\n' "$result" |
                sed -n 's/^movement: //p')))
            head=$(printf '%s\n' "$result" | sed -n 's/^final_head: //p')
            dir=$(printf '%s\n' "$result" | sed -n 's/^final_dir: //p')
        done < "$trace"
        avg=$(awk -v m="$movement" -v r="$requests" 'BEGIN {
            if (r > 0) printf "%.2f", m / r; else printf "0.00"
        }')
        printf '%s,%s,%d,%d,%d,%s\n' "$algo" "$seed" "$requests" \
            "$batches" "$movement" "$avg" >> "$CSV"
    done
done

{
    echo "# Disk scheduling comparison"
    printf 'Seeds: %s; workload INTERACTIVE, 12 students, 300 ticks, tick 10 ms\n' "$SEEDS"
    echo
    echo "| Algorithm | Requests | Batches | Head Movement | Avg Seek |"
    echo "|---|---:|---:|---:|---:|"
    awk -F, '
        NR == 1 { next }
        { count[$1]++; for (i = 3; i <= 6; i++) sum[$1, i] += $i }
        END {
            split("FIFO LIFO SSTF SCAN CSCAN", names, /[[:space:]]+/)
            for (i = 1; i <= 5; i++)
                printf "| %s | %.1f | %.1f | %.1f | %.1f |\n",
                    names[i], sum[names[i],3]/count[names[i]],
                    sum[names[i],4]/count[names[i]],
                    sum[names[i],5]/count[names[i]],
                    sum[names[i],6]/count[names[i]]
        }
    ' "$CSV"
    echo
    echo "All five policies are replayed on the SAME request stream."
    echo "FIFO ignores position, SSTF minimizes each seek but can starve far requests,"
    echo "SCAN and C-SCAN bound the wait, and C-SCAN gives more uniform waiting time."
} > "$MD"
cat "$MD"
exit "$failed"
