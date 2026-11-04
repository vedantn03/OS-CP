# Live demonstration plan

## Before the demo

This is a 12 minutes walkthrough of a userspace exam-server simulation and its standalone teaching tools. The simulated resources are GRADER (grading worker slots), DBCONN (database connections), and QBANK (question-bank read licences), alongside CPU, frames, buffering, and disk service. It is not a kernel and it does not control real processes, memory, disks, or interrupts.

Use Ubuntu or WSL with `build-essential` and `libncurses-dev` installed. Start in the repository, then change to `exam_os`; the commands below assume that directory is the current directory.

Make the terminal at least 80 columns by 31 rows for the dashboard. Build the simulator and tools before presenting. Open this document and the links to [the architecture](ARCHITECTURE.md), [the experiment notes](EXPERIMENTS.md), and [saved results](results/). A zero-byte `assets/dashboard.png` is not a usable screenshot.

The scripted terminal commands below are bounded headless runs or offline tools. Each line can be run from `exam_os`. The dashboard command is shown separately as an optional interactive action and is not part of the automated command list.

## Demo script

### 0:00–1:00 — Introduction

Say: “The model represents student sessions competing for CPU, frames, I/O buffering, and simulated disk service. The scheduler, memory manager, and supporting services run as POSIX threads; all resources and events are in userspace.”

Run the build and a short headless example:

```console
$ make all
$ ./exam_os --headless --tick-ms 10 --students 3 --duration 20 --seed 7
```

### 1:00–3:00 — Scheduling comparison and Gantt trace

Say: “A seed controls workload generation, but wall-clock thread interleavings keep the live simulation from being bit-exact. The comparison script averages a small selected seed set. A trace records dispatch intervals so the Gantt script can render them.”

Run a small two-seed comparison and then a short traced run. The commands save and restore the tracked example trace so this demo leaves no repository changes:

```console
$ bash scripts/compare.sh --seeds "1 2" --algos "FCFS RR" --out /tmp/compare
$ head -1 output/trace.csv > /tmp/demo-trace-before.csv
$ ./exam_os --headless --tick-ms 10 --trace --workload INTERACTIVE --students 8 --duration 80 --algo RR --seed 1
$ bash scripts/gantt.sh output/trace.csv --width 80
$ cat /tmp/demo-trace-before.csv > output/trace.csv
```

### 3:00–5:00 — Paging and memory-allocation labs

Say: “The standalone page simulator replays the same fixed reference string. FIFO, LRU, and OPT can therefore be compared exactly. OPT is offline because it uses future references.”

Run the textbook string and a small page curve:

```console
$ make pagesim
$ ./pagesim --algo FIFO --frames 3 --string "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1"
$ ./pagesim --algo LRU --frames 3 --string "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1"
$ ./pagesim --algo OPT --frames 3 --string "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1"
$ ./pagesim --algo FIFO --frames 3 --string "1 2 3 4 1 2 5 1 2 3 4 5"
$ ./pagesim --algo FIFO --frames 4 --string "1 2 3 4 1 2 5 1 2 3 4 5"
$ bash scripts/pagecurve.sh --seeds "1 2" --algos "FIFO LRU" --frames "8 16" --out /tmp/pagecurve
```

Expected textbook counts are FIFO 15 faults, LRU 12 faults, and OPT 9 faults for the first string with three frames. For the second string FIFO produces 9 faults with 3 frames and 10 faults with 4 frames, demonstrating Belady’s anomaly.

The standalone `memlab` examples are separate from the live page-frame model. They demonstrate contiguous-fit choice and external fragmentation, plus buddy and slab allocation overhead; runnable commands are in [the experiments guide](EXPERIMENTS.md#memory-allocation-lab).

### 5:00–6:30 — Disk scheduling

Say: “The offline disk tool replays one fixed queue. Movement includes endpoint travel for SCAN and the return jump for C-SCAN. The live comparison script records a batch stream and replays it under each policy.”

Run a deterministic example and a small live comparison:

```console
$ make disksim
$ ./disksim --algo FIFO --head 53 --cylinders 200 --string "98 183 37 122 14 124 65 67"
$ ./disksim --algo SSTF --head 53 --cylinders 200 --string "98 183 37 122 14 124 65 67"
$ bash scripts/diskcompare.sh --seeds "1 2" --out /tmp/disk
```

For the textbook queue, expected movements are FIFO 640, LIFO 609, SSTF 236, SCAN up-first 331, and C-SCAN up 382.

### 6:30–10:30 — Deadlock and synchronization labs

Say: “`deadsim` is a standalone lab using exam-server resources. It demonstrates Banker’s safety and request algorithms, detection, abort-based recovery, and wait-for-graph cycles; these mechanisms are not in the live threaded simulation.”

Run the textbook safety and request, inspect a detection case, and create a report outside the repository’s tracked results:

```console
$ make deadsim
$ ./deadsim safe --file scenarios/banker_textbook.txt
$ ./deadsim request --file scenarios/banker_textbook.txt --pid 1 --req "1 0 2"
$ ./deadsim detect --file scenarios/detect_textbook_deadlock.txt
$ ./deadsim recover --file scenarios/detect_textbook_deadlock.txt --policy min-alloc
$ ./deadsim wfg --nodes 3 --edges "0>1 1>2 2>0"
$ bash scripts/deadlock_demo.sh --out /tmp/deadlock
```

The textbook safe sequence is `1 3 0 2 4`; the P1 request is granted. Detection identifies processes 1, 2, 3, and 4 as deadlocked in the supplied deadlock state, and min-alloc aborts process 1. The script also compares recovery policies, textbook detection states, exam scenarios, and a wait-for chain.

The four necessary conditions are mutual exclusion, hold and wait, no preemption, and circular wait. Banker’s avoidance prevents unsafe states. Detection followed by process abortion breaks a cycle after the fact. `deadsim` does not implement the other prevention strategies; `synclab` separately demonstrates resource ordering, which attacks only circular wait.

The standalone synchronization lab is separate from the live simulator. Build it with `make synclab` and show Peterson mutual exclusion with `./synclab counter --mode peterson --threads 2 --iters 200000`, fair readers-writers locking with `./synclab rw --policy fair --readers 4 --writers 2 --ops 100 --hold-us 100`, and resource ordering with `./synclab phil --mode ordered --n 5 --meals 30 --eat-us 100`.

### 10:30–12:00 — Wrap-up

Say: “The offline tools provide fixed-input algorithm evidence, while live results depend on thread timing. The results folder has saved reports, and the limitations page separates simulated mechanisms from operating-system features that are out of scope.”

The standalone `fslab` demonstrates directory entries, free-space tracking, and three file-block allocation layouts; its runnable examples are in [the experiments guide](EXPERIMENTS.md#file-allocation-lab).

Show the generated report from the quick comparison and the repository’s deadlock example:

```console
$ head -20 /tmp/compare/compare.md
$ head -20 /tmp/deadlock/deadlock.md
$ cat ../docs/results/deadlock.md
```

Optional interactive dashboard, not run by the documentation gate:

```console
$! ./exam_os --demo
```

## Backup plan

If a live run fails, use the checked-in [scheduler comparison](results/compare.md), [page curves](results/pagecurve.md), [Belady example](results/belady.md), [disk comparison](results/disk.md), and [deadlock walkthrough](results/deadlock.md). Explain which inputs are fixed and which reports aggregate variable threaded runs.

If asked about a feature that is absent, say so plainly and point to [the syllabus map](SYLLABUS_MAP.md) or [limitations](LIMITATIONS.md). Examples include fork/exec, a persistent file system in the live simulator, system calls, swapping, Android internals, and deadlock handling in the live simulator.
