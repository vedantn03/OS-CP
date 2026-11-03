# Belady anomaly

| Frames | FIFO | LRU | OPT |
|---:|---:|---:|---:|
| 1 | 12 | 12 | 12 |
| 2 | 12 | 12 | 9 |
| 3 | 9 | 10 | 7 |
| 4 | 10 | 8 | 6 |
| 5 | 5 | 5 | 5 |
| 6 | 5 | 5 | 5 |
| 7 | 5 | 5 | 5 |

FIFO demonstrates Belady’s anomaly: faults rise from 9 with 3 frames to 10 with 4 frames, while LRU and OPT do not worsen.
