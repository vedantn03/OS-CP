# Scheduler comparison
Seeds: 1 2 3 4 5; runs per algorithm: 5; workload INTERACTIVE, 12 students, 300 ticks, tick 10 ms

| Algorithm | Avg Wait | Avg Turnaround | Avg Response | Completed | Timeouts | Ctx Switches | Preemptions | CPU % | Throughput |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| FCFS | 118.0 | 166.8 | 17.1 | 12.0 | 0.0 | 57.0 | 0.0 | 94.9 | 5.4 |
| SJF | 91.2 | 139.4 | 29.3 | 12.0 | 0.0 | 55.4 | 0.0 | 94.2 | 5.5 |
| SRTF | 91.8 | 138.4 | 29.0 | 12.0 | 0.0 | 59.6 | 45.4 | 96.7 | 5.7 |
| RR | 116.5 | 164.6 | 16.0 | 12.0 | 0.0 | 66.0 | 0.0 | 96.9 | 5.5 |
| PRIORITY | 91.1 | 139.4 | 29.3 | 12.0 | 0.0 | 56.4 | 0.0 | 94.2 | 5.5 |
| PRIO | 94.8 | 144.3 | 19.7 | 12.0 | 0.0 | 87.6 | 25.2 | 92.4 | 5.3 |
| MLFQ | 114.8 | 162.5 | 15.9 | 12.0 | 0.0 | 65.0 | 0.2 | 96.4 | 5.5 |

Lower is better for wait, turnaround, response, timeouts, and context switches.
Higher is better for completed exams, CPU percentage, and throughput.
Results are averages because the threaded simulation is not bit-exact repeatable across runs even with the same seed.
