# Viva questions and answers

## Design and project questions

### Q1. Is this a real operating system?
**A:** No. It is a userspace simulation that uses C, POSIX threads, semaphores, and ncurses. `exam_os/src/main.c::main` initializes the model and starts its threads; it cannot control real processes or devices.

### Q2. Why use one thread per subsystem?
**A:** The design gives scheduling, memory, I/O, interrupts, logging, and display separate loops so synchronization between services can be demonstrated. The threads are started from `exam_os/src/main.c::main`, and their loops are in files such as `exam_os/src/scheduler.c::scheduler_thread`.

### Q3. How does the simulated clock work?
**A:** `exam_os/src/main.c::tick_thread` increments `current_tick` under the state mutex and broadcasts `tick_cv`. The scheduler waits for that notification in `exam_os/src/scheduler.c::wait_for_tick`, using a timed condition wait as a fallback.

### Q4. Why are runs not bit-exact, and how are experiments made fairer?
**A:** Wall-clock wakeups and thread interleavings vary, even when the random seed is fixed. `scripts/compare.sh` averages over seeds, while `scripts/diskcompare.sh` records one request stream and replays it under every policy. Fixed-input standalone tools such as `./pagesim` avoid that source of variation.

### Q5. What does `--strict` prove?
**A:** It makes the process return a failure status when invariant checks fail; it does not prove the model correct in every possible state. `exam_os/src/invariants.c::invariants_run` checks accounting relationships such as completion counts, buffer bounds, CPU ticks, frame limits, and disk request outcomes.

### Q6. How do the tests and acceptance gates work?
**A:** `tests/run_tests.sh` builds the application and exercises configuration, scheduling, paging, disk, and standalone-tool behavior from T1 through T40. The gate scripts add source checks and textbook examples; `tests/gate61.sh` checks documentation and runs its listed demo commands. The present `tests/gate_all.sh` is empty and does not orchestrate the gates.

### Q7. What is not implemented, and why?
**A:** This project focuses on selected operating-system concepts that fit a deterministic or bounded classroom simulation. A real file system, system calls, fork/exec, swapping, Android internals, and deadlock handling inside the live simulation remain out of scope. The standalone `synclab` demonstrates Peterson with `./synclab counter --mode peterson --threads 2 --iters 200000`, readers-writers with `./synclab rw --policy fair --readers 4 --writers 2 --ops 100 --hold-us 100`, and dining philosophers with `./synclab phil --mode ordered --n 5 --meals 30 --eat-us 100`. `exam_os/tools/deadsim.c::main` provides resource-allocation deadlock algorithms as a separate lab.

## Unit 1: Operating Systems Overview

### Q8. Which kernel type does the design resemble?
**A:** Its separate scheduler, memory, I/O, and interrupt services resemble the organization of a modular system at a high level. That is only an analogy: `exam_os/src/main.c::main` starts userspace threads and does not implement a kernel boundary or loadable kernel modules.

### Q9. What can a shell script demonstrate here?
**A:** Shell scripts automate builds, runs, comparisons, and report generation. For example, `scripts/compare.sh` runs scheduler workloads and aggregates results, but it does not implement a shell or kernel service.

## Unit 2: Process Concepts and CPU Scheduling

### Q10. What process states exist and what transitions are legal?
**A:** The states are NEW, READY, RUNNING, WAITING, and TERMINATED. `exam_os/src/process.c::process_set_state` permits NEW to READY; READY to RUNNING or TERMINATED; RUNNING to READY, WAITING, or TERMINATED; and WAITING to READY or TERMINATED.

### Q11. What fields are stored in the PCB?
**A:** The PCB stores identity and state, arrival and finish ticks, remaining CPU demand, CPU and waiting time, scheduling priority and MLFQ level, burst and wake information, deadline and timeout flags, and submission counters. Its type is declared in `exam_os/include/shared.h`, and `exam_os/src/scheduler.c::scheduler_add_process` initializes scheduler-related fields.

### Q12. Which scheduling criteria are measured?
**A:** The report tracks waiting, turnaround, response, completion, timeouts, dispatches, context switches, preemptions, CPU utilization, and throughput. `exam_os/src/scheduler.c::dispatch_one` updates first-run, waiting, dispatch, and switch values; `exam_os/src/logger.c::logger_write_report` writes the summary.

### Q13. What is FCFS and what is the convoy effect?
**A:** FCFS selects the earliest queued process because `exam_os/src/scheduler.c::rq_select` leaves the first eligible entry selected for that policy. A long CPU burst can delay shorter jobs behind it, which is the convoy effect. The observed impact depends on workload and timing.

### Q14. How do SJF and SRTF differ, and can either starve a process?
**A:** SJF selects a ready process with the shortest predicted remaining work at dispatch. SRTF also checks for a shorter remaining job during execution and can preempt; that logic is in `exam_os/src/scheduler.c::dispatch_one`. A long job can wait behind repeated short arrivals, so starvation is possible in principle.

### Q15. How does Round Robin use a quantum?
**A:** The scheduler caps one dispatch slice at `time_quantum` for quantum-sliced policies in `exam_os/src/scheduler.c::dispatch_one`. When work remains, the process returns to READY. An optional switch cost consumes overhead ticks between different process IDs.

### Q16. How does priority aging work?
**A:** The PRIO policy computes an effective priority by adding ready-wait intervals to the base priority, capped at the configured maximum. `exam_os/src/scheduler.c::effective_priority` implements that calculation. Setting the aging interval to zero disables the increase.

### Q17. What are the MLFQ rules in this implementation?
**A:** MLFQ selects the best queue level, uses a quantum scaled by level, demotes a process that consumes its full quantum, preempts when a higher queue becomes ready, and periodically boosts nonterminated processes. These rules are implemented by `exam_os/src/scheduler.c::rq_select`, `exam_os/src/scheduler.c::dispatch_one`, and `exam_os/src/scheduler.c::apply_mlfq_boost`.

### Q18. Why is one algorithm called PRIORITY?
**A:** `PRIORITY` is a compatibility name retained for the legacy workload. In `exam_os/src/scheduler.c::rq_select`, it selects by remaining work at dispatch boundaries; `PRIO` is the static-priority policy with optional preemption and aging.

### Q19. What is the difference between a dispatch and a context switch in the metrics?
**A:** A dispatch is each selection of a process to run, while a context switch is counted when a different PID follows a prior dispatched PID. `exam_os/src/scheduler.c::dispatch_one` increments these separately, so multiple dispatches can occur without counting every one as a switch.

### Q20. Why is WAITING useful?
**A:** WAITING represents an interactive student session that is paused between CPU bursts. `exam_os/src/scheduler.c::dispatch_one` sets a wake tick, and `exam_os/src/scheduler.c::wake_sleepers` returns eligible sessions to READY. It lets the model show blocking and wakeups rather than keeping every student continuously runnable.

## Unit 3: Process Synchronization and Deadlock

### Q21. What is the difference between a mutex and a semaphore here?
**A:** A mutex protects shared data structures, such as the ready queue and shared state. POSIX semaphores count available I/O buffer slots and records; `exam_os/src/io_buffer.c::io_buffer_init` initializes the buffer synchronization objects and `exam_os/src/io_buffer.c::io_buffer_submit` attempts to reserve a slot.

### Q22. How does the bounded buffer handle full capacity?
**A:** The I/O buffer is a bounded ring with a counting semaphore. `exam_os/src/io_buffer.c::io_buffer_submit` uses a nonblocking slot attempt and drops a submission when no slot is available; flush thresholds and periodic flushing are handled by `exam_os/src/io_buffer.c::io_buffer_thread`.

### Q23. What is the lock order and how does it reduce lock-cycle risk?
**A:** The scheduler nests `rq_lock` before `g_state.lock`; memory operations nest `mem_lock` before `g_state.lock`, and dirty-page enqueue can nest `mem_lock`, `disk_lock`, then `g_state.lock`. `exam_os/src/scheduler.c::dispatch_one` and `exam_os/src/memory.c::load_page` show these chains. The documented short sleep in `load_page` occurs while holding the memory lock, so it can delay peers.

### Q24. How does Banker’s safety algorithm work?
**A:** It computes Need as Max minus Allocation, starts Work at Available, then repeatedly finishes the lowest-index process whose Need fits Work and adds its allocation. `exam_os/tools/deadsim.c::safe_sequence` implements the deterministic scan. The textbook state produces sequence `1 3 0 2 4`.

### Q25. What does the Banker's request algorithm check?
**A:** It first rejects a request larger than the process’s remaining Need, then one larger than Available. Otherwise it tentatively grants the request and runs the safety check; unsafe tentative states are denied. `exam_os/tools/deadsim.c::main` handles the request command and calls `exam_os/tools/deadsim.c::safe_sequence`.

### Q26. How does deadlock detection differ from avoidance?
**A:** Avoidance checks a hypothetical allocation before granting it, while detection checks whether current requests can be satisfied to completion. The former is modeled by `exam_os/tools/deadsim.c::safe_sequence`; the latter is `exam_os/tools/deadsim.c::detect`. Both are standalone lab operations.

### Q27. What recovery policies can deadsim use?
**A:** It can abort the currently deadlocked process with the smallest allocation total, largest total, lowest index, or highest index. `exam_os/tools/deadsim.c::main` returns the victim’s allocation to Available, clears its allocation and request, then runs detection again.

### Q28. What are the four necessary conditions for deadlock?
**A:** They are mutual exclusion, hold and wait, no preemption, and circular wait. `exam_os/tools/deadsim.c::detect` identifies an unresolved wait state; the standalone recovery command breaks a cycle by aborting a process and releasing its resources. Banker avoidance prevents unsafe states before a deadlock develops. In `synclab`, ordered forks attack only circular wait; demonstrate that with `./synclab phil --mode ordered --n 5 --meals 30 --eat-us 100`.

### Q29. Why is deadsim separate from the live simulation?
**A:** The tool operates on a text state file and fixed arrays, without connecting to the simulator’s process table or resource locks. `exam_os/tools/deadsim.c::read_state` parses the state independently, and its `main` dispatches the selected lab command. The live simulator has no resource-allocation deadlock handler.

### Q30. What is Peterson’s solution, and is it implemented?
**A:** Peterson’s two-process algorithm uses intent flags and a turn variable to enforce mutual exclusion under its assumptions. The standalone synchronization lab implements it with `exam_os/tools/synclab.c::peterson_enter` and `exam_os/tools/synclab.c::peterson_exit`; run `./synclab counter --mode peterson --threads 2 --iters 200000`. It is not integrated into the live simulation.

## Unit 4: Memory Management

### Q31. How are page tables and frames represented?
**A:** The memory manager stores per-process page mappings and a fixed frame pool with ownership, page number, load/use timing, reference state, and dirty state. `exam_os/src/memory.c::memory_init` initializes this storage and `exam_os/src/memory.c::memory_access_rw` resolves accesses.

### Q32. What happens on a page fault?
**A:** The running process’s access checks its mapping; a miss obtains a free frame or chooses a victim, updates mappings, and records a fault. `exam_os/src/memory.c::load_page` performs the load and eviction path, while `exam_os/src/memory.c::memory_thread` supplies references for the running process.

### Q33. How do FIFO, LRU, and Clock choose victims?
**A:** FIFO chooses the oldest loaded frame, LRU the least recently used frame, and Clock scans reference bits to find a second-chance victim. The selector functions are `exam_os/src/memory.c::evict_fifo`, `exam_os/src/memory.c::evict_lru`, and `exam_os/src/memory.c::evict_clock`.

### Q34. Why is Optimal page replacement an offline policy?
**A:** Optimal replaces the page whose next use is farthest in the future, so it needs the future reference string. For the textbook string `7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1` with three frames, FIFO 15 faults, LRU 12 faults, OPT 9 faults. `exam_os/tools/pagesim.c::run_simulation` can inspect that fixed string; the live `memory_thread` only sees references as they occur and does not implement OPT.

### Q35. What is Belady’s anomaly?
**A:** It is the counterintuitive possibility that FIFO incurs more faults with more frames. For `1 2 3 4 1 2 5 1 2 3 4 5`, FIFO has 9 faults with 3 frames and 10 faults with 4 frames. `exam_os/tools/pagesim.c::run_simulation` reproduces those deterministic counts.

### Q36. What does thrashing detection and control do?
**A:** The manager tracks recent fault activity and can defer new arrivals when the configured threshold is exceeded. `exam_os/src/memory.c::thrash_update` updates the signal, and `exam_os/src/scheduler.c::admit_arrivals` honors the pause. This admission control is not swapping.

### Q37. What locality model generates references?
**A:** The synthetic reference generator selects from a working set most of the time and can shift phase after a configured interval. `exam_os/src/memory.c::memory_thread` generates accesses only for the running process using the configured page count, working set, and locality percentage.

### Q38. What does a dirty bit mean here?
**A:** A dirty frame has been written, so replacing it records a writeback request. `exam_os/src/memory.c::memory_access_rw` marks writes, and `exam_os/src/memory.c::load_page` sends a dirty victim to the simulated disk queue before reuse.

### Q39. How does paging differ from segmentation?
**A:** Paging uses fixed-size pages and frames, while segmentation describes variable-size logical regions. This code implements the page/frame model in `exam_os/src/memory.c::memory_access_rw`; it does not implement segment tables or segment allocation.

## Unit 5: File and I/O Management

### Q40. How does I/O buffering and backpressure work?
**A:** Student submissions enter a bounded ring; full capacity causes the nonblocking submit operation to drop a new record. `exam_os/src/io_buffer.c::io_buffer_submit` implements that rule, while `exam_os/src/io_buffer.c::flush_buffer` transfers records to disk service in batches.

### Q41. What are the five disk scheduling algorithms?
**A:** The offline disk module supports FIFO, LIFO, SSTF, SCAN, and C-SCAN. For head 53, 200 cylinders, and queue `98 183 37 122 14 124 65 67`, movements are FIFO 640, LIFO 609, SSTF 236, SCAN (up first) 331, and C-SCAN 382. `exam_os/tools/disksim.c::main` invokes the shared calculation in `exam_os/src/disk_sched.c::disk_sched_run`.

### Q42. Why can SSTF starve distant requests?
**A:** SSTF repeatedly picks the nearest pending cylinder, so a far request can lose each choice when nearby requests keep arriving. `exam_os/src/disk_sched.c::disk_sched_run` chooses the closest request for the SSTF policy; the standalone example makes the rule visible on a fixed queue.

### Q43. How do SCAN and C-SCAN differ?
**A:** SCAN travels in one direction serving requests, then reverses; C-SCAN returns to the opposite end and serves in one direction on each sweep. `exam_os/src/disk_sched.c::disk_sched_run` implements both endpoint and direction behavior.

### Q44. Why replay one recorded request stream for all policies?
**A:** Otherwise each policy could receive a different workload, making movement comparisons confounded by request variation. `scripts/diskcompare.sh` records request batches and invokes the offline disk simulator for each algorithm using the same batch.

### Q45. Which file-system parts are missing?
**A:** The project does not model file objects, directories, persistent metadata, file allocation methods, or free-space management. The simulated disk queue in `exam_os/src/disk.c::disk_enqueue` models requests and cylinder positions, not a file system.

## Unit 6: Operating System Kernel Internals and Android OS

### Q46. What does the interrupt vector table do in this project?
**A:** It maps interrupt kinds to handler functions and dispatches queued events. `exam_os/src/interrupt.c::ivt_register` registers handlers and `exam_os/src/interrupt.c::dispatch` invokes them. Overload and exam-timeout events are raised by the live code; page-fault and submit-complete events currently are not.

### Q47. Which interrupt handlers are actually raised?
**A:** `exam_os/src/interrupt.c::check_overload` raises overload notifications, and `exam_os/src/interrupt.c::check_timeouts` raises exam-timeout notifications. The page-fault and submit-complete handlers are registered, but their corresponding event types are never raised by `exam_os/src/interrupt.c::interrupt_raise` call sites.

### Q48. What is user space versus kernel space, and can this project demonstrate it?
**A:** Real systems separate unprivileged application execution from privileged kernel work, with controlled transitions between them. This project cannot demonstrate that boundary because all simulator threads execute as one userspace program; `exam_os/src/main.c::main` starts pthreads rather than entering a kernel.

### Q49. What are Android HAL, ART, and AOSP?
**A:** They are Android-specific architecture concepts: hardware abstraction, the Android runtime, and the Android open-source platform. They are outside this project’s implementation; `exam_os/src/main.c::main` starts a POSIX-thread simulation and contains no Android subsystem.

### Q50. Why is this project useful if it is simplified?
**A:** Its small models make scheduling, paging, buffering, and algorithm tradeoffs inspectable without claiming to reproduce a production operating system. `exam_os/src/invariants.c::invariants_run` checks selected accounting relationships, and the standalone tools make fixed algorithm examples reproducible.

## Review prompts

### Q51. Where does the tick duration come from?
**A:** `config_load_defaults` sets the default tick length and `config.conf` can override it. The command-line option is parsed in `exam_os/src/config.c::config_parse_args`, and the configuration is checked in `exam_os/src/config.c::config_validate`.

### Q52. What happens to a dirty page on replacement?
**A:** The memory manager increments dirty writeback accounting and maps the victim to a simulated disk cylinder. `exam_os/src/memory.c::load_page` calls the disk queue interface in `exam_os/src/disk.c::disk_enqueue`; it does not write a host swap file.

### Q53. How are live results reported?
**A:** The logger gathers state and writes the summary, system log, and submission outputs. `exam_os/src/logger.c::logger_write_report` computes averages from terminated processes and prints counters such as dispatches, switches, faults, and disk movement.

### Q54. Does a fixed seed guarantee identical live results?
**A:** No, because a seed determines generated random choices but not operating-system thread scheduling. `exam_os/src/rng.c::rng_init_thread` creates separate streams, while wait and event timing remain wall-clock dependent. The comparison scripts average multiple seeds.

### Q55. How is a disk cylinder chosen for a submission?
**A:** The live model maps the process ID and question ID into the configured cylinder range using a fixed arithmetic formula. `exam_os/src/disk.c::disk_cyl_for_submission` implements this synthetic mapping; it is not a physical disk address.

### Q56. Does the simulator implement a file system because it writes reports?
**A:** No. The logger opens host text files for output, but there are no modeled directories, file metadata, allocation strategies, or free-space structures. `exam_os/src/logger.c::logger_write_report` writes reports through ordinary C file operations.
