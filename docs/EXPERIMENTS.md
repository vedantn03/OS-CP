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

## Synchronization lab

Build the independent C11 and pthreads lab from `exam_os`:

```console
make synclab
```

Counter modes share one volatile counter. `none` is the intentionally unsafe baseline, `mutex` locks each increment with a pthread mutex, `sem` uses a binary POSIX semaphore, and `peterson` uses Peterson's two-process algorithm:

```console
./synclab counter --mode none --threads 4 --iters 1000000
./synclab counter --mode mutex --threads 4 --iters 200000
./synclab counter --mode sem --threads 4 --iters 200000
./synclab counter --mode peterson --threads 2 --iters 200000
```

Counter output keys: `mode` names the synchronization mode; `threads` and `iters` are the inputs; `expected` is threads multiplied by iterations; `final` is the observed counter; `correct` says whether final equals expected; `lost_updates` is expected minus final.

The readers-writers lab supports `none` (unsafe control), `reader` (readers-preference), `writer` (writers-preference), and `fair` (a turnstile shared by readers and writers):

```console
./synclab rw --policy none --readers 4 --writers 2 --ops 100 --hold-us 100
./synclab rw --policy reader --readers 4 --writers 2 --ops 100 --hold-us 100
./synclab rw --policy writer --readers 4 --writers 2 --ops 100 --hold-us 100
./synclab rw --policy fair --readers 4 --writers 2 --ops 100 --hold-us 100
```

Readers-writers output keys: `policy` is the selected policy; `readers` and `writers` are thread counts; `ops` is each thread's operation count; `reads_done` and `writes_done` are completed operations; `torn_reads` counts reads whose two values differed; `violations` counts observed overlapping reader/writer or writer/writer critical sections; `max_readers` and `max_writers` are the maximum simultaneous counts observed. The three synchronized policies should report zero torn reads and violations.

Dining philosophers can demonstrate a deterministic timeout-detected deadlock with `naive`, resource hierarchy with `ordered`, and an N-1 permit waiter with `waiter`:

```console
./synclab phil --mode naive --n 5 --timeout-ms 150
./synclab phil --mode ordered --n 5 --meals 30 --eat-us 100
./synclab phil --mode waiter --n 5 --meals 30 --eat-us 100
```

Philosopher output keys: `mode` is the strategy; `philosophers` is N; `meals` is the requested meal count per philosopher; `deadlock` is yes when a timed fork wait got stuck; `stuck` counts those philosophers; `meals_eaten` is the total completed meals; `violations` counts attempts to own an already-owned fork. The naive mode intentionally eats no meals; ordered and waiter modes should finish every meal without violations.

The unsynchronized counter and the `none` readers-writers control may show lost updates, torn reads, or violations only probabilistically on a multicore machine. A run that happens not to expose a race does not make the unsafe control synchronized. The lab is standalone and does not change the live simulator's locking.

## Memory allocation lab

Build the standalone allocator examples from `exam_os`:

```console
make memlab
```

Contiguous allocation treats the supplied sizes as separate free holes and consumes a selected hole from its beginning. A request may fail from external fragmentation when total free space is sufficient but no individual hole is large enough. Run all three placement policies on the same requests:

```console
./memlab contiguous --policy first --holes "100 500 200 300 600" --requests "212 417 112 426"
./memlab contiguous --policy best --holes "100 500 200 300 600" --requests "212 417 112 426"
./memlab contiguous --policy worst --holes "100 500 200 300 600" --requests "212 417 112 426"
```

Contiguous output keys: `mode` names the allocator; `policy` is first, best, or worst fit; `requests` is the number of requests; `allocated` and `failed` count outcomes; `total_free` and `largest_free` report remaining space; `external_fragmentation` is yes when a failed request could fit in total free space but not in any single hole; `fragmented_free` is total free space excluding the largest hole.

For the textbook requests, first fit places 212 KB in the 500 KB hole, 417 KB in the 600 KB hole, and 112 KB in the 288 KB remainder; 426 KB fails. Best fit places the requests in 300, 500, 200, and 600 KB holes, respectively, and all four succeed. Worst fit uses 600, 500, and the 388 KB remainder for the first three requests; 426 KB fails. First and worst fit both end with 959 KB free, a largest hole of 300 KB, and 659 KB fragmented free; best fit ends with 533 KB free, a largest hole of 174 KB, and 359 KB fragmented free.

The buddy allocator requires power-of-two total memory and minimum block size, splits larger blocks to satisfy each request, and rounds allocations up to the next suitable block:

```console
./memlab buddy --total 1024 --min-block 32 --requests "100 240 64 256"
```

With these inputs the blocks round to 128, 256, 64, and 256 KB. Output keys: `mode`, `total`, and `min_block` identify the setup; `requests`, `allocated`, and `failed` count outcomes; `allocated_bytes` is reserved block space; `requested_bytes` is useful payload; `internal_fragmentation` is their difference; `total_free` and `largest_free` describe remaining blocks. The current CLI has no free operation, so buddy coalescing cannot be demonstrated or verified through it.

The slab allocator selects the smallest supplied size class that can hold each object and adds slabs as cache slots fill:

```console
./memlab slab --page-size 4096 --classes "16 32 64 128 256" --objects "12 24 40 100 200 12"
```

Slab output keys: `mode`, `page_size`, and `classes` identify the cache setup; `objects`, `allocated`, and `failed` count inputs and outcomes; `slabs` counts pages assigned to caches; `requested_bytes` is useful payload; `allocated_bytes` is the size-class space occupied by objects; `internal_fragmentation` is class space minus payload; `slab_waste` is all page space committed to slabs but not occupied by object slots. The current CLI has no free operation or slot identifiers, so reuse and duplicate-live-slot behavior cannot be demonstrated or verified through it. These allocator models are standalone and do not allocate the live simulator's page frames.

## File system lab

Build the standalone directory and block-allocation model:

```console
make fslab
```

`fslab` creates a directory of named files, marks their blocks in a free-space bitmap, and supports contiguous, linked, and indexed layouts. Indexed allocation reserves one extra block per file for its index. The optional `--free` list releases allocated file blocks and removes those entries from the directory:

```console
./fslab --blocks 16 --method contiguous --files "alpha:3,beta:4,gamma:5"
./fslab --blocks 10 --method linked --files "alpha:3,beta:4"
./fslab --blocks 10 --method indexed --files "alpha:3,beta:4"
./fslab --blocks 10 --method indexed --files "alpha:3,beta:4" --free alpha
```

Output keys: `mode` identifies this lab; `method` is the allocation layout; `blocks` is total disk blocks; `files_requested`, `files_allocated`, `files_failed`, and `files_freed` count outcomes; `directory` lists active names, sizes, and first or index blocks; `layout` shows contiguous block lists, linked chains, or indexed data-block lists; `free_blocks` counts available blocks; `bitmap` has one character per block (`1` allocated, `0` free). For the two-file examples above, contiguous and linked allocate seven of ten blocks and leave 3 free; indexed allocates the seven data blocks plus two index blocks and leaves 1 free. Freeing `alpha` in the indexed example releases its three data blocks and index block, leaving 5 free. A data file of size N reduces free space by N blocks for contiguous and linked methods, and N+1 for indexed allocation. Freeing the file restores those blocks. The current CLI cannot allocate more files after freeing one, so it cannot model a file request against extents fragmented by an earlier free. Duplicate names, missing names in `--free`, and capacity failures return nonzero status. This is an in-memory teaching model, not a persistent file system or part of the live simulator.

## Reading the results

Live result files describe a particular configuration and seed set; they are not guaranteed values for another machine or run. Read each report’s workload, policy, and seed description before comparing its rows. The live experiments produce CSV and Markdown output beneath the selected directory.

Offline tools use fixed input strings, queues, or state files, so their algorithm outputs are reproducible for identical inputs and binaries. Their saved reports are linked here: [scheduler comparison](results/compare.md), [page curve](results/pagecurve.md), [Belady example](results/belady.md), [disk comparison](results/disk.md), and [deadlock lab](results/deadlock.md).

For a demonstration, write generated outputs to a temporary directory so the checked-in evidence remains unchanged. Use the scripts’ `--out /tmp/...` option where available; `gantt.sh` prints its chart to the terminal.
