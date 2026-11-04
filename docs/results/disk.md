# Disk scheduling comparison
Seeds: 1 2 3; workload INTERACTIVE, 12 students, 300 ticks, tick 10 ms

| Algorithm | Requests | Batches | Head Movement | Avg Seek |
|---|---:|---:|---:|---:|
| FIFO | 146.3 | 11.0 | 6217.0 | 42.6 |
| LIFO | 146.3 | 11.0 | 6246.3 | 42.8 |
| SSTF | 146.3 | 11.0 | 1973.7 | 13.5 |
| SCAN | 146.3 | 11.0 | 2109.7 | 14.5 |
| CSCAN | 146.3 | 11.0 | 3799.7 | 26.1 |

All five policies are replayed on the SAME request stream.
FIFO ignores position, SSTF minimizes each seek but can starve far requests,
SCAN and C-SCAN bound the wait, and C-SCAN gives more uniform waiting time.
