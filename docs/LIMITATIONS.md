# Limitations

## Not implemented

The simulator does not implement Peterson’s solution, readers-writers synchronization, a file system, slab allocation, buddy allocation, segmentation, a system call interface, Android architecture, fork/exec, swapping, or contiguous-allocation fragmentation. Deadlock handling exists only in the standalone `deadsim` tool; it is not part of the live simulation. The project does not model kernel/user address spaces or booting a kernel.

## Modelling simplifications

This is a userspace simulation built with C, POSIX threads, semaphores, and ncurses. It does not control real memory, processes, disks, or interrupts. Disk seek time is represented by synthetic cylinder movement; request cylinders are mapped from submission and page identifiers. Only the running process generates page references, using a configurable synthetic locality model.

Workloads are generated from random streams. Even when the same seed is used, runs are “not bit-exact” repeatable because thread scheduling and wall-clock wakeups affect event ordering. Comparison scripts average over seeds; disk comparisons replay one recorded request stream under every policy. CPU service is measured in simulated ticks, not native processor time. `PRIORITY` is a legacy algorithm name that uses shortest-remaining-time selection at dispatch boundaries.

## Known issues

The interrupt vector table registers `INT_PAGE_FAULT` and `INT_SUBMIT_COMPLETE` handlers, but both interrupt types are never raised by calls to `exam_os/src/interrupt.c::interrupt_raise`. The handlers exist in `exam_os/src/interrupt.c::handle_page_fault` and `exam_os/src/interrupt.c::handle_submit_complete`, but those paths do not currently receive events from the live simulator.

`exam_os/src/memory.c::load_page` has a short sleep while holding the memory mutex. This can delay concurrent memory accesses. The standalone page and disk tools use fixed inputs and do not model concurrent device execution.

The root `gate_all.sh` file is currently an empty placeholder in `exam_os/tests`; it does not run the acceptance gates. Run the individual gate scripts when available. Saved live-simulation result files document particular configurations and are not guarantees for another machine or run.

The configuration parser converts most numeric command-line values before validating their ranges. For reliable input, provide integer strings and keep values within the documented limits in [the project-state reference](PROJECT_STATE.md).

The live disk queue is bounded; requests can be dropped when it is full. The I/O submission ring also drops an attempted record when it has no free slot. These are model policies visible in `exam_os/src/disk.c::disk_enqueue` and `exam_os/src/io_buffer.c::io_buffer_submit`.

The dashboard requires a suitable terminal and is not available in headless mode. The tool outputs and saved reports are text artifacts, not a graphical device display.

The offline page tool supports OPT because it receives the complete reference string. The live pager receives references incrementally and supports only FIFO, LRU, and CLOCK. The two interfaces therefore demonstrate different policy sets.

The model uses a fixed maximum process table and frame pool. It does not dynamically allocate kernel objects, implement isolation between students, or enforce permissions on memory references.

The disk movement metric is total cylinder distance calculated by the selected policy. It does not model rotational latency, transfer rate, controller caches, or physical device completion time.

The interrupt queue and logging queue are finite. Their queue-full paths update drop counters; they do not provide a lossless event journal under arbitrary overload.

The terminal dashboard is an observational display of shared model state. Its snapshots do not provide a synchronized view of real operating-system activity.

The random workload is synthetic rather than a trace of actual students or applications. The model does not claim that its configured demand, burst, locality, or think-time distributions match a production exam service.

The algorithms illustrate policies on bounded input and state. Their results should not be treated as benchmarks of an operating system or a physical storage device.

The demo and acceptance scripts expect a Unix-like shell and the tools listed in their commands. A native Windows PowerShell environment without GNU Make, a compatible C toolchain, pthreads, and ncurses cannot run the same build flow directly.

The legacy and interactive workloads model different behaviors. Comparisons should hold the workload type and other configuration settings fixed when examining a scheduling policy.

The dashboard and text reports expose only the counters included in the program. They do not provide production telemetry, process isolation, or a complete operating-system resource audit.

The scheduler trace has a fixed in-memory row capacity. When that capacity is exceeded, the trace writer reports dropped rows rather than producing a complete unlimited history.

The demonstration scenarios focus on textbook-sized examples and configured exam-server resources. They do not claim to represent every possible resource-allocation state.

No user program or simulated student can execute arbitrary instructions inside this model. The session state and reference stream are generated by the simulator itself.
