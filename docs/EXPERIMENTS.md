# Experiments

## Reproducibility

The live threaded simulation is not bit-exact because wall-clock wakeups and thread interleavings affect when concurrently produced events are observed. Fixed seeds make generated random streams repeatable, but they do not fix operating-system scheduling.

The live comparison scripts compensate by averaging over seeds. The disk experiment also replays one recorded request stream under every disk policy so each policy receives the same requests. Standalone `pagesim`, `disksim`, and `deadsim` use fixed inputs and deterministic algorithms.

Run commands from `exam_os`. The scripts accept `--out DIR` to choose where generated reports go. The examples below use `/tmp` for scratch output and link to checked-in reports without copying run-specific measurements into this guide.

## Scheduler comparison

`scripts/compare.sh` runs the live simulator for each chosen scheduling algorithm and seed. It measures waiting, turnaround, response, completion, timeout, context-switch, preemption, utilization, and throughput metrics, then averages them by policy. Its default output is `../docs/results/compare.csv` and `compare.md`; this small example writes to `/tmp/compare`:

```console
bash scripts/compare.sh --seeds "1 2" --algos "FCFS RR" --out /tmp/compare
```

The saved example is [docs/results/compare.md](results/compare.md). `scripts/gantt.sh` renders half-open intervals from a scheduler trace as a terminal chart. Generate a short trace and render it with:

```console
./exam_os --headless --tick-ms 10 --trace --workload INTERACTIVE --students 8 --duration 80 --algo RR --seed 1
bash scripts/gantt.sh output/trace.csv --width 80
```

The Gantt script writes to standard output. The repository includes saved examples such as [the RR chart](results/gantt_RR.txt).

## Paging experiments

`scripts/pagecurve.sh` measures live page faults and hits across frame counts, page policies, and seeds, then averages the fault rate. Its default outputs are `../docs/results/pagecurve.csv` and `pagecurve.md`; use a short sweep for a quick run:

```console
bash scripts/pagecurve.sh --seeds "1 2" --algos "FIFO LRU" --frames "8 16" --out /tmp/pagecurve
```

The saved curve is [docs/results/pagecurve.md](results/pagecurve.md). The offline `pagesim` program replays a fixed reference string and reports per-reference frame contents and fault totals. Build and run it with:

```console
make pagesim
./pagesim --algo FIFO --frames 3 --string "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1"
```

`pagesim` supports FIFO, LRU, CLOCK, and OPT, including file input and frame sweeps. OPT is offline because it uses future references. The fixed Belady example is documented in [docs/results/belady.md](results/belady.md).

## Disk scheduling

The offline `disksim` program measures total movement, final head and direction, and service order for one request queue. For example:

```console
make disksim
./disksim --algo SSTF --head 53 --cylinders 200 --string "98 183 37 122 14 124 65 67"
```

Supported policies are FIFO, LIFO, SSTF, SCAN, and C-SCAN. `scripts/diskcompare.sh` captures live disk request batches once and replays every captured batch under all five policies. It measures request and batch counts, movement, and average seek. Its default output files are `../docs/results/disk.csv` and `disk.md`; this run writes them under `/tmp/disk`:

```console
bash scripts/diskcompare.sh --seeds "1 2" --out /tmp/disk
```

The saved comparison is [docs/results/disk.md](results/disk.md).

## Deadlock lab

The `deadsim` program is a standalone lab using exam-server resource scenarios; it is not connected to the live process table. Its commands demonstrate Banker’s safety and request algorithms, detection, recovery by process abortion, and wait-for-graph cycle detection. Build it and run a textbook safety check with:

```console
make deadsim
./deadsim safe --file scenarios/banker_textbook.txt
```

The full lab walkthrough runs textbook and exam states, compares recovery policies, and shows graph examples. It writes `deadlock.md` into the selected directory, defaulting to `../docs/results`:

```console
bash scripts/deadlock_demo.sh --out /tmp/deadlock
```

The checked-in report is [docs/results/deadlock.md](results/deadlock.md).

## Reading the results

Live result files describe a particular configuration and seed set; they are not guaranteed values for another machine or run. Read each report’s workload, policy, and seed description before comparing its rows. The live experiments produce CSV and Markdown output beneath the selected directory.

Offline tools use fixed input strings, queues, or state files, so their algorithm outputs are reproducible for identical inputs and binaries. Their saved reports are linked here: [scheduler comparison](results/compare.md), [page curve](results/pagecurve.md), [Belady example](results/belady.md), [disk comparison](results/disk.md), and [deadlock lab](results/deadlock.md).

For a demonstration, write generated outputs to a temporary directory so the checked-in evidence remains unchanged. Use the scripts’ `--out /tmp/...` option where available; `gantt.sh` prints its chart to the terminal.
