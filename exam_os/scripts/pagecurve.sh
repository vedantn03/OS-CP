#!/usr/bin/env bash
set -u

FRAMES="8 16 24 32 48 64"
ALGOS="FIFO LRU CLOCK"
SEEDS="1 2 3"
OUT="../docs/results"
EXTRA=()

while [ "$#" -gt 0 ]; do
    case "$1" in
        --frames) [ "$#" -ge 2 ] || exit 2; FRAMES=$2; shift 2 ;;
        --algos) [ "$#" -ge 2 ] || exit 2; ALGOS=$2; shift 2 ;;
        --seeds) [ "$#" -ge 2 ] || exit 2; SEEDS=$2; shift 2 ;;
        --out) [ "$#" -ge 2 ] || exit 2; OUT=$2; shift 2 ;;
        --) shift; EXTRA=("$@"); break ;;
        *) EXTRA+=("$1"); shift ;;
    esac
done

cd "$(dirname "$0")/.." || exit 1
[ -x ./exam_os ] || make || exit 1
mkdir -p "$OUT" || exit 1
CSV="$OUT/pagecurve.csv"
MD="$OUT/pagecurve.md"
TMP=$(mktemp -d "${TMPDIR:-/tmp}/exam_os_pagecurve.XXXXXX")
trap 'rm -rf "$TMP"' EXIT
printf '%s\n' "algo,frames,seed,faults,hits,fault_rate,evictions" > "$CSV"
failed=0

extract() {
    sed -n "s/.*$1 *: *\([0-9][0-9.]*\).*/\1/p" "$2" | head -1
}

for algo in $ALGOS; do
    for frame in $FRAMES; do
        for seed in $SEEDS; do
            ./exam_os --headless --strict --tick-ms 10 --workload INTERACTIVE \
                --students 12 --duration 300 --algo RR --page "$algo" \
                --frames "$frame" --seed "$seed" "${EXTRA[@]}" \
                >"$TMP/${algo}_${frame}_${seed}.log" 2>&1
            rc=$?
            summary="$TMP/${algo}_${frame}_${seed}.summary"
            cp output/summary.txt "$summary" 2>/dev/null || :
            if [ "$rc" -ne 0 ]; then
                printf 'RUN FAILED %s %s %s\n' "$algo" "$frame" "$seed" >&2
                failed=1
            fi
            faults=$(extract "Page Faults" "$summary")
            hits=$(extract "Page Hits" "$summary")
            evictions=$(extract "Evictions" "$summary")
            rate=$(awk -v f="${faults:-0}" -v h="${hits:-0}" '
                BEGIN { total = f + h; if (total > 0) printf "%.4f", f / total; else printf "0.0000" }')
            printf '%s,%s,%s,%s,%s,%s,%s\n' "$algo" "$frame" "$seed" \
                "${faults:-0}" "${hits:-0}" "$rate" "${evictions:-0}" >> "$CSV"
        done
    done
done

{
    echo "# Page-fault curve"
    echo
    printf '| Frames |'
    for algo in $ALGOS; do printf ' %s |' "$algo"; done
    echo
    printf '%s' '|---:|'
    for algo in $ALGOS; do printf '%s' '---:|'; done
    echo
    awk -F, -v algos="$ALGOS" '
        NR == 1 { next }
        { sum[$1,$2] += $6; count[$1,$2]++ }
        END {
            n = split(algos, names, /[[:space:]]+/)
            for (f = 1; f <= 100000; f++) {
                present = 0
                for (i = 1; i <= n; i++)
                    if (count[names[i], f]) present = 1
                if (!present) continue
                printf "| %d |", f
                for (i = 1; i <= n; i++)
                    printf " %.1f%% |", 100 * sum[names[i], f] / count[names[i], f]
                printf "\n"
            }
        }
    ' "$CSV"
    echo
    echo "Thrashing occurs when the fault rate rises sharply because available frames fall below the combined working set."
} > "$MD"
exit "$failed"
