# Project State

## Current status

Exam OS is a userspace POSIX-threaded simulation with configurable scheduling,
memory paging, I/O buffering, interrupts, invariant checks, and legacy or
interactive workloads.

## Changelog

- 4.1a: disk_sched module and disksim

## Disk scheduling model

The pure disk scheduling module supports FIFO, LIFO, SSTF, SCAN, and C-SCAN.
Requests retain arrival-order indices, and equal-cylinder requests are served
by arrival order. Movement includes every head move, including SCAN travel to
the disk endpoint and the C-SCAN return jump; the reported final head is the
last request cylinder, not an endpoint or jump position.

Build and run the standalone tool with:

    make disksim
    ./disksim --algo SCAN --head 53 --cylinders 200 \
        --string "98 183 37 122 14 124 65 67" --dir up

The tool accepts `--string` or `--file` input and prints the service order,
movement, final head, and final direction. Usage errors (including unknown
algorithms, invalid geometry, bad directions, missing input, and unknown
options) exit 2. Empty input, unreadable files, malformed tokens, and
out-of-range requests exit 1.

## Experiments

Run `make compare` or `bash exam_os/scripts/compare.sh` to generate
`docs/results/compare.csv` and `compare.md`. Run `make gantt` to generate an
ASCII chart from a traced RR run, or invoke `bash exam_os/scripts/gantt.sh`
with a trace file and optional `--width`.

The comparison table reports averages across seeds: lower is better for wait,
turnaround, response, timeouts, and switches; higher is better for completed
exams, CPU utilization, and throughput. Results are averages because the
threaded simulation is not bit-exact repeatable across runs.

- 2026-11-02: Completed memory race protection, configurable tick timing,
  interactive workload WAITING transitions, test restoration, and sanitizer
  validation for phases 1.5–2.2.
- 2026-11-03: Added deterministic per-thread RNG streams and shared simulation
  clock sleep/notification infrastructure.
- 2026-11-03: Made scheduler CPU accounting tick-driven with utilization,
  invariants, and optional dispatch tracing.
- 2026-11-03: Added configurable interactive CPU demand and completion
  submissions while preserving the legacy workload model.
- 2026-11-03: Added interactive CPU burst/WAITING cycles, wakeups, and burst
  submissions with timeout-safe sleeper handling.
- 2026-11-03: Added pluggable FCFS, SJF, and SRTF scheduling with a unified
  ready queue, preemption accounting, and configurable context-switch cost;
  PRIORITY retains shortest-remaining-time at dispatch boundaries (legacy name PRIORITY).
- 2026-11-03: Added static priority scheduling with aging and a multilevel
  feedback queue with configurable levels, preemption, demotion, and boosts.
- 2026-11-03: 2.4: compare harness and gantt.
- 2026-11-03: Added locality-based paging, CLOCK replacement, dirty-page
  tracking, eviction/thrash counters, and optional page-reference tracing.
- 2026-11-03: Added offline FIFO/LRU/CLOCK/OPT page simulation, Belady
  anomaly evidence, page-fault curves, and thrash admission control.
