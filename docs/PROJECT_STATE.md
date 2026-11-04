# Project state

## Purpose

Exam OS is a userspace C simulation of student sessions competing for CPU time, memory frames, I/O buffering, and simulated disk service. It uses POSIX threads, semaphores, and ncurses; it is not a kernel and does not control real hardware. Standalone programs under `exam_os/tools` demonstrate offline page replacement, disk scheduling, deadlock algorithms, synchronization, memory allocation, and file allocation.

## Modules

Selected source modules, headers, standalone tools, scenarios, scripts, and test entry points:

- `exam_os/src/clock.c` — shared sleep and tick timing helpers.
- `exam_os/src/config.c` — defaults, config-file parsing, command-line parsing, validation, and configuration display.
- `exam_os/src/disk_sched.c` — pure FIFO, LIFO, SSTF, SCAN, and C-SCAN ordering and movement calculations.
- `exam_os/src/disk.c` — live disk request queue, cylinder mapping, and service accounting.
- `exam_os/src/interrupt.c` — interrupt vector table, queue, handlers, and timeout/overload checks.
- `exam_os/src/invariants.c` — end-of-run invariant checks and report output.
- `exam_os/src/io_buffer.c` — bounded submission buffer, backpressure, flushing, and submission storm helper.
- `exam_os/src/logger.c` — asynchronous event queue, output files, and final summary report.
- `exam_os/src/main.c` — initialization, tick loop, thread startup/join, shutdown, and top-level orchestration.
- `exam_os/src/memory.c` — page tables, frame pool, live references, replacement, dirty writeback, and thrashing signal.
- `exam_os/src/process.c` — process state transition validation and termination.
- `exam_os/src/rng.c` — per-thread random number generators.
- `exam_os/src/scheduler.c` — ready queue, arrivals, wakeups, scheduling policies, CPU accounting, and trace generation.
- `exam_os/include/clock.h` — clock helper declarations.
- `exam_os/include/config.h` — configuration fields, limits, and option declarations.
- `exam_os/include/disk_sched.h` — disk scheduling types and API.
- `exam_os/include/disk.h` — live disk service API.
- `exam_os/include/interrupt.h` — interrupt kinds and registration/dispatch API.
- `exam_os/include/invariants.h` — invariant check entry point.
- `exam_os/include/io_buffer.h` — bounded I/O buffer API.
- `exam_os/include/logger.h` — logger API.
- `exam_os/include/memory.h` — memory manager API.
- `exam_os/include/process.h` — process state helper API.
- `exam_os/include/rng.h` — RNG API.
- `exam_os/include/scheduler.h` — scheduler API.
- `exam_os/include/shared.h` — shared state and PCB data structures.
- `exam_os/scenarios/banker_textbook.txt` — textbook Banker state for safety and request examples.
- `exam_os/scenarios/detect_textbook_ok.txt` — textbook detection state with no deadlock.
- `exam_os/scenarios/detect_textbook_deadlock.txt` — textbook detection state with deadlocked sessions.
- `exam_os/scenarios/exam_safe.txt` — safe exam-server resource allocation state.
- `exam_os/scenarios/exam_unsafe.txt` — unsafe but not yet deadlocked exam-server allocation state.
- `exam_os/scenarios/exam_deadlock.txt` — exam-server detection and recovery scenario.
- `exam_os/tools/deadsim.c` — standalone Banker's safety/request, detection, recovery, and wait-for graph tool.
- `exam_os/tools/fslab.c` — standalone directory, free-space bitmap, and file block allocation lab.
- `exam_os/tools/memlab.c` — standalone contiguous-fit, buddy, and slab allocator demonstrations.
- `exam_os/tools/disksim.c` — standalone disk scheduling command-line tool.
- `exam_os/tools/pagesim.c` — standalone FIFO, LRU, CLOCK, and OPT page reference tool.
- `exam_os/tools/synclab.c` — standalone counter, readers-writers, and dining-philosophers synchronization lab.
- `exam_os/scripts/baseline.sh` — baseline workload runner and report writer.
- `exam_os/scripts/compare.sh` — multi-seed scheduler comparison runner.
- `exam_os/scripts/deadlock_demo.sh` — deadlock lab walkthrough generator.
- `exam_os/scripts/diskcompare.sh` — live disk request capture and policy replay comparison.
- `exam_os/scripts/gantt.sh` — ASCII chart generator for scheduler trace CSV.
- `exam_os/scripts/pagecurve.sh` — multi-seed live page-fault/frame comparison.
- `exam_os/tests/run_tests.sh` — numbered regression suite T1 through T60.
- `exam_os/tests/gate61.sh` — documentation gate and demonstration command runner.
- `exam_os/tests/gate71.sh` — standalone synchronization lab and documentation gate.
- `exam_os/tests/gate_all.sh` — currently an empty placeholder.

## Data ownership

The process table and global counters live in `SystemState` and are protected by `g_state.lock`. The ready queue is private to `scheduler.c` and protected by `rq_lock`. The frame pool and page tables are owned by `memory.c` and protected by `mem_lock`. The bounded I/O ring and its slot/item semaphores are owned by `io_buffer.c`. The live disk request queue is protected by `disk_lock`; service head and direction are serialized by `svc_lock`. The interrupt ring uses `int_q_lock`, and its vector table uses `ivt_lock`. The log ring is owned by `logger.c` and protected by `q_lock`, with `q_ready` signaling available log records.

## Locking rules and lock order

The scheduler takes `rq_lock` before `g_state.lock` when selecting or changing ready-queue membership. Memory paths take `mem_lock` before `g_state.lock`; dirty eviction may enqueue a disk request while holding `mem_lock`, producing `mem_lock -> disk_lock -> g_state.lock`. Disk service takes `svc_lock` and briefly takes `disk_lock` to detach a batch; it releases `disk_lock` before later state accounting. I/O-buffer code can take its buffer mutex before updating shared state. Full interrupt and log queues update drop counters under their queue lock and then `g_state.lock`.

`exam_os/src/memory.c::load_page` performs a short sleep while `mem_lock` is held. `exam_os/src/disk.c::disk_service` separates queue detachment from service accounting. `exam_os/src/logger.c::logger_thread` removes a message under `q_lock` and writes after releasing it. The code should be read by these actual paths; do not assume every operation is nonblocking.

## Config options

Command-line defaults below are the values loaded by `config_load_defaults` before `config.conf` is read. The checked-in `config.conf` changes the workload to `LEGACY`; CLI options are parsed after the file and override it. Numeric validation is performed by `config_validate`; boolean switches without a value enable their feature.

| Key | Default | Validation or accepted values |
|---|---|---|
| `--help` | off | Prints option help and exits successfully. |
| `--students N` | 50 | 1..200. |
| `--frames N` | 64 | 1..256. |
| `--quantum N` | 5 | Positive integer. |
| `--duration N` | 100 | Positive integer ticks. |
| `--limit N` | 0 | 0..100000 ticks. |
| `--arrival-batch N` | 5 | At least 1. |
| `--arrival-interval N` | 10 | At least 1 tick. |
| `--buffer N` | 256 | 1..4096. |
| `--algo NAME` | PRIORITY | RR, ROUND_ROBIN, PRIORITY, FCFS, SJF, SRTF, PRIO, or MLFQ. |
| `--switch-cost N` | 0 | 0..50 ticks. |
| `--priority-levels N` | 3 | 1..9. |
| `--aging N` | 25 | Non-negative ticks; zero disables aging. |
| `--preempt N` | 1 | 0 or 1. |
| `--mlfq-levels N` | 3 | 2..6. |
| `--mlfq-boost N` | 100 | Non-negative ticks; zero disables boosts. |
| `--page NAME` | LRU | LRU, FIFO, or CLOCK. |
| `--demo` | off | Flag enables demo mode. |
| `--headless` | off | Flag disables the interactive ncurses display. |
| `--strict` | off | Flag requests nonzero exit when invariants fail. |
| `--tick-ms N` | 100 | 1..60000 milliseconds. |
| `--trace` | off | Flag writes scheduler trace. |
| `--workload NAME` | INTERACTIVE | LEGACY or INTERACTIVE. Config file default is LEGACY. |
| `--seed N` | 0 | Parsed as an unsigned integer; zero selects time-based seeding. |
| `--demand-min N` | 10 | At least 1 and no greater than demand max. |
| `--demand-max N` | 25 | At least demand min. |
| `--burst-min N` | 2 | At least 1 and no greater than burst max. |
| `--burst-max N` | 6 | At least burst min. |
| `--think-min N` | 4 | At least 1 and no greater than think max. |
| `--think-max N` | 12 | At least think min. |
| `--pages N` | 16 | 4..64 pages per process. |
| `--ws N` | 4 | 1..pages per process. |
| `--locality N` | 90 | 0..100 percent. |
| `--phase N` | 40 | At least 1 tick. |
| `--write-pct N` | 30 | 0..100 percent. |
| `--thrash-high N` | 45 | 1..100 percent. |
| `--page-trace` | off | Flag enables page reference trace. |
| `--thrash-control N` | 0 | 0 or 1. |
| `--thrash-pause N` | 40 | 1..1000 ticks. |
| `--disk NAME` | FIFO | FIFO, LIFO, SSTF, SCAN, or CSCAN. |
| `--cylinders N` | 200 | 10..10000. |
| `--disk-start N` | 0 | 0 through cylinders minus one. |
| `--disk-batch N` | 20 | 1..1000. |
| `--disk-trace` | off | Flag enables disk request trace. |

The parser rejects unknown or incomplete command-line arguments. Some integer values are converted with `atoi` before range checking, so malformed numeric text is not consistently rejected as a syntax error; use integer values in commands.

## Tests

The test runner names the checks T1 through T67; T8 is its final cleanup check and runs after T67. T43 and T44 repeat the same Peterson counter case. There are no unused T numbers between T1 and T67; T8's placement at the end is intentional.

- T1 builds the application.
- T2 checks that the executable exists.
- T3 checks that a short headless run completes.
- T4 rejects a zero-student configuration.
- T5 runs a seeded simulation.
- T6 checks expected output reports exist.
- T7 checks summary and log content.
- T8 checks that test cleanup completes.
- T9 checks timeouts, partial submission, and one-time termination logging.
- T10 checks RR and PRIORITY dispatch and switch accounting.
- T11 checks configurable arrival batches and intervals.
- T12 checks invalid student configuration again.
- T13 runs several strict-mode configurations and checks invariant output.
- T14 compares submission records with the total count.
- T15 checks LEGACY and INTERACTIVE workloads under strict mode.
- T16 checks tick-driven RR and PRIORITY runs.
- T17 checks interactive RR and PRIORITY completion and invariants.
- T18 checks a strict LEGACY workload.
- T19 checks interactive block and wake counts.
- T20 checks interactive session timeouts.
- T21 checks FCFS, SJF, and SRTF workloads.
- T22 checks context-switch overhead accounting.
- T23 checks PRIO and MLFQ runs.
- T24 checks non-preemptive PRIO mode.
- T25 checks PRIO with aging disabled.
- T26 checks MLFQ with boosts disabled.
- T27 rejects an unknown scheduling policy.
- T28 checks CLOCK paging.
- T29 checks FIFO paging and eviction.
- T30 checks read-only paging has no dirty writeback.
- T31 checks page trace count equals faults plus hits.
- T32 rejects an unknown page policy.
- T33 rejects an invalid working-set size.
- T34 checks textbook pagesim FIFO, LRU, and OPT fault totals.
- T35 checks the textbook FIFO Belady anomaly counts.
- T36 checks that thrash control defers arrivals.
- T37 checks that disabled thrash control does not defer arrivals.
- T38 checks disksim movement values and invalid inputs.
- T39 checks each live disk policy and disk tracing.
- T40 checks deadsim safety, requests, detection, recovery, graph cycles, and usage errors.
- T41 checks synclab mutex counter mutual exclusion.
- T42 checks synclab semaphore counter mutual exclusion.
- T43 and T44 repeat the synclab Peterson counter check.
- T45 checks fair synclab readers-writers counts and critical-section safety.
- T46 checks ordered synclab philosophers finish all meals.
- T47 checks the naive synclab philosophers demonstrate deadlock.
- T48 checks invalid Peterson thread-count handling in synclab.
- T49 checks first-fit external fragmentation in memlab.
- T50 checks best-fit allocation results in memlab.
- T51 checks worst-fit external fragmentation in memlab.
- T52 checks buddy block rounding and internal fragmentation.
- T53 checks slab size-class selection and slab counts.
- T54 rejects a buddy heap whose size is not a power of two.
- T55 checks contiguous fslab directory and bitmap output.
- T56 checks linked fslab block chains.
- T57 checks indexed fslab index blocks and data layout.
- T58 checks freeing a file updates the directory and bitmap.
- T59 checks allocation failure when the volume lacks enough blocks.
- T60 rejects an unknown fslab allocation method.
- T61 checks first-fit textbook total and largest free holes.
- T62 checks best-fit textbook allocation and fragmentation totals.
- T63 checks worst-fit textbook allocation and fragmentation totals.
- T64 checks buddy block rounding for the textbook requests.
- T65 checks fslab free-space counts and complete restoration for all methods.
- T66 rejects duplicate fslab file names.
- T67 rejects freeing a missing fslab file name.

## Invariants

`exam_os/src/invariants.c::invariants_run` checks these end-of-run conditions:

- I1 memory audit passes.
- I2 total submissions equal flushed plus buffered records.
- I3 completed-process count equals terminated-process count.
- I4 successful finishes plus timeouts equal completed-process count.
- I5 terminated-process timing values are nonnegative and ordered.
- I6 buffered record count stays within configured capacity.
- I7 busy, idle, and overhead ticks do not exceed the final tick.
- I8 process CPU tick totals equal aggregate busy ticks.
- I9 wakeups do not exceed block events.
- I10 process levels and priorities stay within configured ranges.
- I11 evictions do not exceed faults, dirty writebacks do not exceed evictions, and frame use stays within capacity.
- I12 served plus dropped disk requests equal total requests and movement is nonnegative.

## Gates

- `exam_os/tests/gate.sh` — original acceptance gate; inspect the file for its current checks.
- `exam_os/tests/gate23.sh` — phase 2.3 acceptance gate.
- `exam_os/tests/gate24.sh` — phase 2.4 acceptance gate.
- `exam_os/tests/gate31.sh` — phase 3.1 acceptance gate.
- `exam_os/tests/gate32.sh` — phase 3.2 acceptance gate.
- `exam_os/tests/gate41a.sh` — disk scheduler offline-tool acceptance gate.
- `exam_os/tests/gate41b.sh` — live disk integration acceptance gate.
- `exam_os/tests/gate51.sh` — standalone deadlock lab acceptance gate.
- `exam_os/tests/gate61.sh` — documentation truth and demo command gate.
- `exam_os/tests/gate71.sh` — synchronization lab source, behavior, race, and documentation checks.
- `exam_os/tests/gate_all.sh` — currently empty; it does not run the other gates.

The gate scripts should be run from the `exam_os` directory. Some gates require generated executables and a clean working tree; read each script’s checks before interpreting its result.

## Known issues

`INT_PAGE_FAULT` and `INT_SUBMIT_COMPLETE` handlers are registered but never raised by the current interrupt call sites. `exam_os/src/memory.c::load_page` sleeps briefly while holding `mem_lock`. The simulation depends on wall-clock thread scheduling and is not bit-exact for identical seeds. `gate_all.sh` is an empty placeholder.

## Changelog

- 4.1a: disk_sched module and disksim.
- 4.1b: live disk, trace replay, diskcompare.
- 5.1: deadsim, scenarios and demo.
- 6.1: documentation set.
- 7.1: synclab.
- 7.2: memlab.
- 7.3: fslab.
- 7.2/7.3: memlab and fslab.
- 2026-11-02: Completed memory race protection, configurable tick timing, interactive workload WAITING transitions, test restoration, and sanitizer validation for phases 1.5–2.2.
- 2026-11-03: Added deterministic per-thread RNG streams and shared simulation clock sleep/notification infrastructure.
- 2026-11-03: Made scheduler CPU accounting tick-driven with utilization, invariants, and optional dispatch tracing.
- 2026-11-03: Added configurable interactive CPU demand and completion submissions while preserving the legacy workload model.
- 2026-11-03: Added interactive CPU burst/WAITING cycles, wakeups, and burst submissions with timeout-safe sleeper handling.
- 2026-11-03: Added pluggable FCFS, SJF, and SRTF scheduling with a unified ready queue, preemption accounting, and configurable context-switch cost; PRIORITY retains shortest-remaining-time at dispatch boundaries.
- 2026-11-03: Added static priority scheduling with aging and a multilevel feedback queue with configurable levels, preemption, demotion, and boosts.
- 2026-11-03: Added the comparison harness and Gantt trace renderer.
- 2026-11-03: Added locality-based paging, CLOCK replacement, dirty-page tracking, eviction and thrash counters, and optional page-reference tracing.
- 2026-11-03: Added offline FIFO, LRU, CLOCK, and OPT page simulation, Belady anomaly evidence, page-fault curves, and thrash admission control.
