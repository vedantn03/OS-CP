# Project State

## Current status

Exam OS is a userspace POSIX-threaded simulation with configurable scheduling,
memory paging, I/O buffering, interrupts, invariant checks, and legacy or
interactive workloads.

## Changelog

- 4.1a: disk_sched module and disksim
- 4.1b: live disk, trace replay, diskcompare
- 5.1: deadsim, scenarios and demo

## Disk integration model

Submissions and dirty-page evictions become disk requests. Submission cylinders
use `(pid * 131 + question_id * 17) % disk_cylinders`; page writebacks use a
swap-region mapping based on the frame slot and page. Requests are collected
into bounded batches and serviced by the configured FIFO, LIFO, SSTF, SCAN, or
C-SCAN policy. The disk head starts at `disk_start`, initially moves up, and
retains its head and direction between service batches. Optional tracing writes
each batch in arrival order, allowing `scripts/diskcompare.sh` to replay the
same stream under all five policies.

Disk configuration includes `DISK_ALGO`/`--disk`, `DISK_CYLINDERS`/`--cylinders`,
`DISK_START`/`--disk-start`, `DISK_BATCH`/`--disk-batch`, and
`DISK_TRACE`/`--disk-trace`. Cylinder count is 10..10000, the start must be
within that range, batch size is 1..1000, and trace is 0 or 1.

The lock-order chains are `mem_lock -> disk_lock -> g_state.lock` for dirty
writeback enqueue and `svc_lock -> disk_lock -> g_state.lock` for disk service.
Disk service never sleeps or performs blocking work while holding the queue
lock.

## Deadlock lab

`deadsim` is a standalone deadlock lab that models exam-server resources. It is
not part of the live threaded simulation. Each process index is zero-based;
limits are 1..32 processes and 1..16 resources. State files use whitespace
separated tokens, `#` comments, and upper-case keywords. A file starts with
`PROCESSES n`, `RESOURCES m`, and `AVAILABLE a1 ... am`, followed by optional
`MAX`, `ALLOC`, and `REQUEST` matrix sections with exactly `n` rows and `m`
non-negative integer values. An optional `NAMES` line is accepted and ignored.
Duplicate matrix sections, malformed rows, missing required headers, and
allocation values larger than the matching maximum in safety or request
commands are invalid input.
Scenario names label the three exam-server resources GRADER (grading worker
slots), DBCONN (database connections), and QBANK (question-bank read licences).

Build with `make deadsim`. Commands are `safe --file F`,
`request --file F --pid P --req "r1 ... rm"`, `detect --file F`,
`recover --file F [--policy min-alloc|max-alloc|lowest-index|highest-index]`,
and `wfg --nodes N --edges "a>b ..."`. Recovery defaults to `min-alloc`.
Outputs use the keys `state` and `sequence` for safety; `pid`, `request`,
`result`, `reason`, and `sequence` for requests; `deadlock`, `finish_order`,
and `deadlocked` for detection; `deadlock`, `aborted`, and `final` for
recovery; and `deadlock` and `on_cycle` for wait-for graphs.

Exit code 0 means a valid evaluation, including denied requests or unsafe and
deadlocked states. Invalid files, missing required state sections, malformed
requests, and out-of-range process or edge indices exit 1. Usage errors,
unknown commands or policies, and unknown options exit 2. Safety and detection
restart their scan at process 0 after each completion, making their sequences
deterministic. Recovery chooses among currently deadlocked processes and
re-runs detection after each abortion. Wait-for graph output lists every node
that can reach itself through one or more edges.

Run `bash scripts/deadlock_demo.sh` from `exam_os` to build and run the lab and
write `../docs/results/deadlock.md`; use `--out DIR` to select another output
directory. The demo includes textbook states, exam scenarios, all recovery
policies, and cycle examples.

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
