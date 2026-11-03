#!/usr/bin/env bash
set -u

# Trace intervals are half-open: [tick_start,tick_end).
TRACE="output/trace.csv"
WIDTH=100
if [ "$#" -gt 0 ] && [[ "$1" != --* ]]; then
    TRACE=$1
    shift
fi
if [ "$#" -gt 0 ] && [ "$1" = "--width" ]; then
    [ "$#" -ge 2 ] || { echo "missing value for --width" >&2; exit 2; }
    WIDTH=$2
fi
case "$WIDTH" in
    ''|*[!0-9]*) echo "invalid width" >&2; exit 2 ;;
esac
[ "$WIDTH" -ge 1 ] || { echo "invalid width" >&2; exit 2; }
[ "$WIDTH" -le 140 ] || WIDTH=140
[ -s "$TRACE" ] || { echo "missing or empty trace file: $TRACE" >&2; exit 1; }

awk -F, -v width="$WIDTH" '
NR == 1 { next }
NF >= 4 {
    start = $1 + 0
    end = $2 + 0
    pid = $3 + 0
    reason = $4
    if (end > max) max = end
    if (pid > 0) pids[pid] = 1
    rows[++row_count] = pid "," start "," end "," reason
}
END {
    if (max < 1) max = 1
    scale = int((max + width - 1) / width)
    if (scale < 1) scale = 1
    columns = int((max + scale - 1) / scale)
    if (columns > width) columns = width
    for (r = 1; r <= row_count; r++) {
        split(rows[r], fields, ",")
        pid = fields[1] + 0
        start = fields[2] + 0
        end = fields[3] + 0
        reason = fields[4]
        for (t = start; t < end; t++) {
            col = int(t / scale)
            if (col < columns) used[pid, col] = 1
            if (pid == 0 && reason == "SWITCH" && col < columns)
                overhead[col] = 1
        }
    }
    printf "Gantt chart (1 column = %d ticks)\n", scale
    printf "     "
    for (c = 0; c < columns; c++) {
        if (c % 10 == 0) printf "%-10d", c * scale
    }
    printf "\n"
    for (pid = 1; pid <= 999; pid++) if (pids[pid]) {
        printf "P%02d |", pid
        for (c = 0; c < columns; c++) printf "%s", used[pid, c] ? "#" : " "
        printf "|\n"
    }
    printf "CPU |"
    for (c = 0; c < columns; c++) {
        ch = "."
        for (pid = 1; pid <= 999; pid++) if (used[pid, c]) ch = "#"
        if (ch == "." && overhead[c]) ch = "s"
        printf "%s", ch
    }
    printf "|\n"
    printf "legend: # process running, s switch overhead, . idle; %d ticks per column.\n", scale
}
' "$TRACE"
