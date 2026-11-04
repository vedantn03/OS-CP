# Syllabus map

This map distinguishes live simulator behavior from standalone tools and topics outside the project scope. Evidence paths point to code, commands, or saved result documents.

Legend: IMPLEMENTED (in the live simulator and tested), LAB (a standalone tool in exam_os/tools, tested), PARTIAL, NOT IMPLEMENTED, EXCLUDED (deliberately out of scope).

## Unit 1: Operating Systems Overview

| Topic | Status | Where | Demo / evidence |
|---|---|---|---|
| Shell programming | PARTIAL | exam_os/scripts/compare.sh provides shell automation, not a shell language implementation. | bash scripts/compare.sh --seeds "1 2" --algos "FCFS RR" --out /tmp/compare |
| Kernel structures (monolithic, microkernel, hybrid, modular) | EXCLUDED | This is a userspace simulation, not a kernel architecture implementation. | No kernel architecture demo |

## Unit 2: Process Concepts and CPU Scheduling

| Topic | Status | Where | Demo / evidence |
|---|---|---|---|
| Process states | IMPLEMENTED | exam_os/src/process.c::process_set_state | make test T3, T13, T15 |
| Process control block | IMPLEMENTED | exam_os/include/shared.h PCB and exam_os/src/scheduler.c::scheduler_add_process | make test T13 |
| Process creation with fork/exec/wait | EXCLUDED | Sessions are PCB records created inside one process; POSIX child creation is outside scope. | No child-process demo |
| Threads (POSIX threads) | IMPLEMENTED | exam_os/src/main.c::main starts subsystem threads. | make test |
| Scheduling criteria and metrics | IMPLEMENTED | exam_os/src/scheduler.c::update_cpu_utilization and exam_os/src/logger.c::logger_write_report | docs/results/compare.md |
| Preemptive and non-preemptive scheduling | IMPLEMENTED | exam_os/src/scheduler.c::dispatch_one supports preemption paths and run-to-block dispatch. | make test T21, T24 |
| FCFS | IMPLEMENTED | exam_os/src/scheduler.c::rq_select preserves ready order for FCFS. | bash scripts/compare.sh --seeds "1 2" --algos "FCFS" --out /tmp/compare |
| SJF | IMPLEMENTED | exam_os/src/scheduler.c::rq_select selects shortest remaining work for SJF. | bash scripts/compare.sh --seeds "1 2" --algos "SJF" --out /tmp/compare |
| Round Robin | IMPLEMENTED | exam_os/src/scheduler.c::dispatch_one applies the configured quantum. | make test T10, T16 |
| Priority scheduling with aging | IMPLEMENTED | exam_os/src/scheduler.c::effective_priority raises queued priority with wait time. | make test T23, T25 |
| Multilevel Feedback Queue | IMPLEMENTED | exam_os/src/scheduler.c::apply_mlfq_boost and exam_os/src/scheduler.c::rq_select | make test T23, T26 |

## Unit 3: Process Synchronization and Deadlock

| Topic | Status | Where | Demo / evidence |
|---|---|---|---|
| Critical section and mutual exclusion (mutexes) | IMPLEMENTED | exam_os/src/main.c::state_init initializes shared-state synchronization. | make test |
| Semaphores | IMPLEMENTED | exam_os/src/io_buffer.c::io_buffer_init initializes empty and filled counts. | make test T3, T39 |
| Producer-consumer (bounded buffer) | IMPLEMENTED | exam_os/src/io_buffer.c::io_buffer_submit and exam_os/src/io_buffer.c::flush_buffer | make test T39 |
| Peterson's solution | NOT IMPLEMENTED | No two-process software mutual-exclusion algorithm is present in the source. | No implementation |
| Readers-writers and dining philosophers | NOT IMPLEMENTED | The repository contains no readers-writers or dining-philosophers simulation. | No implementation |
| Deadlock prevention | NOT IMPLEMENTED | No live resource-allocation prevention policy is implemented. | No implementation |
| Deadlock avoidance (Banker's algorithm) | LAB | exam_os/tools/deadsim.c::safe_sequence | ./deadsim safe --file scenarios/banker_textbook.txt |
| Deadlock detection and recovery | LAB | exam_os/tools/deadsim.c::detect and exam_os/tools/deadsim.c::main | ./deadsim recover --file scenarios/detect_textbook_deadlock.txt --policy min-alloc |
| Deadlock handling inside the live simulation | NOT IMPLEMENTED | The live scheduler has no resource-allocation deadlock detector or recovery routine. | No live deadlock handler |

## Unit 4: Memory Management

| Topic | Status | Where | Demo / evidence |
|---|---|---|---|
| Paging and page tables | IMPLEMENTED | exam_os/src/memory.c::memory_init initializes per-session page tables and the frame pool. | make test T28 |
| Demand paging and page faults | IMPLEMENTED | exam_os/src/memory.c::memory_access_rw loads a page after an invalid entry. | make test T28, T31 |
| FIFO page replacement | IMPLEMENTED | exam_os/src/memory.c::evict_fifo | make test T29 |
| LRU page replacement | IMPLEMENTED | exam_os/src/memory.c::evict_lru | make test T13 |
| Clock (second chance) page replacement | IMPLEMENTED | exam_os/src/memory.c::evict_clock | make test T28 |
| Optimal page replacement | LAB | exam_os/tools/pagesim.c::run_simulation implements offline OPT using future references. | ./pagesim --algo OPT --frames 3 --string "7 0 1 2 0 3 0 4 2 3 0 3 2 1 2 0 1 7 0 1" |
| Belady's anomaly | LAB | exam_os/tools/pagesim.c::run_simulation compares offline FIFO frame counts. | docs/results/belady.md |
| Thrashing detection and control | IMPLEMENTED | exam_os/src/memory.c::thrash_update and exam_os/src/scheduler.c::admit_arrivals | make test T36, T37 |
| Swapping | NOT IMPLEMENTED | Arrival admission can pause during thrashing; that deferral is not swapping pages to backing storage. | make test T36, T37 |
| Contiguous allocation (first, best, worst fit) and fragmentation | NOT IMPLEMENTED | The live memory model uses page frames and has no contiguous-fit allocator. | No implementation |
| Segmentation | NOT IMPLEMENTED | No segment tables or segment-address translation appear in the memory module. | No implementation |
| Buddy and slab allocation | NOT IMPLEMENTED | The project does not implement kernel heap or buddy allocators. | No implementation |

## Unit 5: File and I/O Management

| Topic | Status | Where | Demo / evidence |
|---|---|---|---|
| I/O buffering | IMPLEMENTED | exam_os/src/io_buffer.c::io_buffer_submit and exam_os/src/io_buffer.c::flush_buffer | make test T39 |
| Disk scheduling (FIFO, LIFO, SSTF, SCAN, C-SCAN) | IMPLEMENTED | exam_os/src/disk_sched.c::disk_sched_run is used by live disk service. | docs/results/disk.md |
| File concept, directories and allocation methods | NOT IMPLEMENTED | Output files are ordinary host files; no simulated file namespace or allocation policy exists. | No implementation |
| File system implementation and free-space management | NOT IMPLEMENTED | There is no simulated filesystem or free-space manager in this project. | No implementation |

## Unit 6: Operating System Kernel Internals and Android OS

| Topic | Status | Where | Demo / evidence |
|---|---|---|---|
| Linux kernel architecture and boot process | EXCLUDED | The project is a userspace program and does not implement Linux startup or kernel subsystems. | No kernel boot demo |
| System call interface | NOT IMPLEMENTED | The program calls host POSIX and C library APIs; it does not implement a system-call boundary. | No system-call implementation |
| Interrupt vector table and handlers | IMPLEMENTED | exam_os/src/interrupt.c::interrupt_init, exam_os/src/interrupt.c::dispatch, and exam_os/src/interrupt.c::handle_exam_timeout | make test T20 |
| Android architecture (HAL, ART, AOSP) | EXCLUDED | Android runtime and platform components are outside this exam-server simulation. | No Android platform demo |
