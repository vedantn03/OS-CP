# Exam OS: an operating-systems lab

## What this is

Exam OS is a C userspace simulation of an exam server. Student sessions compete for simulated CPU time, page frames, submission-buffer space, and disk service. It demonstrates operating-systems concepts through a threaded model and separate deterministic algorithm tools.

It is not a kernel. It does not control real memory, processes, disks, or hardware interrupts. The live program runs as a POSIX process using threads, mutexes, semaphores, and ncurses.

The live simulator includes multiple CPU scheduling policies, process states, demand paging, FIFO/LRU/CLOCK replacement, a bounded I/O buffer, simulated disk scheduling, timeout and overload events, logging, and invariant checks. Separate tools cover offline page replacement, disk scheduling, deadlock algorithms, synchronization, memory allocation, and file allocation.

## Quick start

On Ubuntu or WSL, install the build tools and ncurses development package:

```console
sudo apt update
sudo apt install build-essential libncurses-dev
```

Build from the repository root:

```console
cd exam_os && make
```

Run the interactive simulator in a terminal at least 80 columns by 31 rows:

```console
./exam_os
```

Run headless with a 10 millisecond simulated tick:

```console
./exam_os --headless --tick-ms 10 --students 12 --duration 100 --seed 1
```

Run the regression suite:

```console
make test
```

The program reads `config.conf` when that file is present, then applies command-line settings. Run it from `exam_os` so the configuration and relative output paths resolve as expected.

Standalone tool examples:

```console
make pagesim disksim deadsim
./pagesim --algo FIFO --frames 3 --string "1 2 3 1 2 4"
./disksim --algo SSTF --head 20 --cylinders 200 --string "10 90 30"
./deadsim safe --file scenarios/banker_textbook.txt
make synclab
./synclab counter --mode mutex --threads 4 --iters 200000
make memlab
./memlab contiguous --policy first --holes "100 500 200 300 600" --requests "212 417 112 426"
make fslab
./fslab --blocks 10 --method indexed --files "alpha:3,beta:4" --free alpha
```

## What you can demonstrate

| Program or script | Demonstration |
|---|---|
| `exam_os` | Live exam-session scheduling, paging, buffering, timeouts, disk service, and summary reporting. |
| `pagesim` | Offline FIFO, LRU, CLOCK, and OPT reference-string simulation. |
| `disksim` | Offline FIFO, LIFO, SSTF, SCAN, and C-SCAN movement and request ordering. |
| `deadsim` | Standalone Banker safety and requests, detection, abort recovery, and wait-for graph cycles. |
| `synclab` | Standalone counter synchronization, readers-writers policies, and dining philosophers. |
| `memlab` | Standalone contiguous-fit, buddy, and slab allocation with fragmentation metrics. |
| `fslab` | Standalone directory, free-space bitmap, and contiguous, linked, and indexed file allocation. |
| `scripts/compare.sh` | Average live scheduler metrics across selected seeds. |
| `scripts/pagecurve.sh` | Compare live page-fault rates across frame counts and policies. |
| `scripts/diskcompare.sh` | Replay recorded disk requests under five policies. |
| `scripts/gantt.sh` | Render a scheduler trace as a text chart. |
| `scripts/deadlock_demo.sh` | Generate a Markdown walkthrough of deadlock lab scenarios. |

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Syllabus map](docs/SYLLABUS_MAP.md)
- [Experiments](docs/EXPERIMENTS.md)
- [Demo plan](docs/DEMO.md)
- [Viva questions and answers](docs/VIVA.md)
- [Limitations](docs/LIMITATIONS.md)
- [Current project state](docs/PROJECT_STATE.md)
- [Saved results](docs/results/)

## Testing

Run `make test` inside `exam_os` for the numbered regression checks. Acceptance gates are in `exam_os/tests`, one file per line:

- `exam_os/tests/gate.sh`
- `exam_os/tests/gate23.sh`
- `exam_os/tests/gate24.sh`
- `exam_os/tests/gate31.sh`
- `exam_os/tests/gate32.sh`
- `exam_os/tests/gate41a.sh`
- `exam_os/tests/gate41b.sh`
- `exam_os/tests/gate51.sh`
- `exam_os/tests/gate61.sh`
- `exam_os/tests/gate_all.sh`

In this checkout `gate_all.sh` is empty and does not run the other gates. Run individual gates to check their specific requirements. Gates may rebuild executables or generate output files.

The offline page and disk tools accept fixed reference strings or request queues, so they are useful for exact algorithm walkthroughs. Live comparisons use generated workloads and should be interpreted as measurements of the displayed settings rather than universal results.

The deadlock lab uses text scenario files under `exam_os/scenarios`. It is intentionally separate from the live simulator’s process scheduling and memory-management threads.

## Origin and credit

The original project is credited to Priyanka Singh, and its upstream README identifies the MIT License. This course extension adds scheduling policies and interactive sessions, a tick-driven paging and thrashing model, disk scheduling and request replay, offline paging and disk tools, the standalone deadlock lab, and regression and acceptance checks. This checkout does not include a separate license file.

The extension is intended for learning and demonstration. The implementation represents mechanisms and metrics with model state and host threads; it does not reproduce kernel enforcement, hardware timing, persistent file-system behavior, or Android services.
