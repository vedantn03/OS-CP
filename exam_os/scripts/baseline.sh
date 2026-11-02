#!/bin/sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
cd "$ROOT"
mkdir -p ../docs
make >/dev/null

run_case() {
    name=$1
    shift
    output=$(./exam_os --headless --students 10 --frames 8 --duration 20 --seed 42 "$@" 2>&1)
    summary=$(cat output/summary.txt)
    completed=$(printf '%s\n' "$summary" | awk -F: '/Completed Exams/ {gsub(/[^0-9]/, "", $2); print $2}')
    faults=$(printf '%s\n' "$summary" | awk -F: '/Page Faults/ {gsub(/[^0-9]/, "", $2); print $2}')
    dropped=$(printf '%s\n' "$summary" | awk -F: '/Dropped/ {gsub(/[^0-9]/, "", $2); print $2}')
    printf '| %s | %s | %s | %s |\n' "$name" "${completed:-0}" "${faults:-0}" "${dropped:-0}"
}

{
    printf '# Exam OS baseline\n\n'
    printf 'Generated: %s\n\n' "$(date -u '+%Y-%m-%dT%H:%M:%SZ')"
    printf '| Scenario | Completed exams | Page faults | Dropped submissions |\n'
    printf '|---|---:|---:|---:|\n'
    run_case 'Priority + LRU' --algo PRIORITY --page LRU
    run_case 'Round Robin + FIFO' --algo RR --page FIFO
} | tee ../docs/baseline.md

make clean >/dev/null
