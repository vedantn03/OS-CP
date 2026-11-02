# 🖥️ Exam OS Simulation

> A real-time operating system simulation built in C that models how an exam server manages resources under heavy load — featuring CPU scheduling, memory paging, I/O buffering, interrupt handling, and a live ncurses terminal dashboard.

---

## 📸 Live Dashboard

![Exam OS Dashboard](assets/dashboard.png)

> *Real-time terminal dashboard showing CPU utilization, memory paging, I/O buffer depth, interrupt events, and active process list — all updating live.*

---

## 🎯 Motivation

Online exam platforms routinely crash during peak usage — when hundreds of students log in simultaneously, submit answers at once, or hit their exam deadline at the same time. This project simulates exactly those failure conditions and demonstrates how OS-level resource management solves them.

| Real-World Problem | OS Concept Applied |
|---|---|
| CPU starvation under heavy load | Priority Scheduling + Round Robin |
| Memory overflow with many sessions | Demand Paging with LRU / FIFO |
| Submission loss during I/O spikes | Circular Buffer + Producer-Consumer |
| Exam timeouts not enforced reliably | Hardware Timer Interrupts (SIGALRM) |
| No visibility into system behavior | Structured Logging + Live Dashboard |

---

## ✨ Features

### ⚙️ CPU Scheduler
- Two switchable algorithms: **Priority Scheduling** and **Round Robin**
- Min-heap ready queue — processes closest to deadline get CPU first
- Tracks context switches, turnaround time, and CPU utilization live
- Processes trickle in over time simulating students joining the exam

### 🧠 Memory Paging
- Per-process page tables with physical frame pool
- Two page replacement algorithms: **LRU** (default) and **FIFO**
- Dirty page tracking — modified pages written to I/O buffer before eviction
- Page fault rate and hit ratio tracked and reported in final summary

### 📥 I/O Submission Buffer
- Circular buffer with **producer-consumer** model using POSIX semaphores
- Non-blocking producers — full buffer drops and counts submissions rather than freezing
- Flushes to disk when **80% full** or every 15 ticks (write-back policy)
- **Demo mode** triggers a submission storm at tick 30 — watch the buffer spike

### ⚡ Interrupt Handler
- Full **Interrupt Vector Table** with 4 registered handlers
- `INT_0 EXAM_TIMEOUT` — saves partial submission, frees memory, terminates process
- `INT_1 OVERLOAD` — detects buffer at 95%, applies back-pressure
- `INT_2 PAGE_FAULT` — centrally logs all page fault events
- `INT_3 SUBMIT_COMPLETE` — acknowledges successful flush

### 📊 Logger & Statistics
- Thread-safe async log queue — callers never block waiting for disk
- Writes `output/system_log.txt` in real time throughout simulation
- Generates `output/summary.txt` with final performance report on exit
- Live event feed in dashboard showing last 3 system events color-coded by severity

### 🖥️ ncurses Live Dashboard
- Refreshes every 500ms with 6 panels: CPU, Memory, I/O, Interrupts, Processes, Events
- Progress bars turn **red** at critical thresholds (memory >85%, buffer >80%)
- Currently running process highlighted in green in process list
- Press `q` at any time to end simulation cleanly

### 🔧 Fully Configurable
- Edit `config.conf` to change simulation parameters without recompiling
- CLI args override config file at runtime

---

## 🚀 Getting Started

### Prerequisites
```bash
# Ubuntu / Debian / GitHub Codespaces
sudo apt-get install libncurses5-dev libncursesw5-dev -y
```

### Build
```bash
git clone https://github.com/Priyanka-Singh27/exam-os-simulation.git
cd exam-os-simulation/exam_os
make
```

### Run
```bash
# Normal run with config.conf settings
./exam_os

# Headless run — no ncurses dashboard (useful for CI and servers)
./exam_os --headless

# Demo mode — triggers submission storm at tick 30
./exam_os --demo

# Custom parameters
./exam_os --students 100 --algo PRIORITY --frames 64 --duration 120

# Round Robin instead of Priority
./exam_os --algo RR --demo
```

---

## ⚙️ Configuration

Edit `config.conf` before running, or override with CLI flags:

| Parameter | Config Key | CLI Flag | Default |
|---|---|---|---|
| Number of students | `NUM_STUDENTS` | `--students N` | 50 |
| Memory frames | `MEMORY_FRAMES` | `--frames N` | 64 |
| Time quantum (ticks) | `TIME_QUANTUM` | `--quantum N` | 5 |
| Exam duration (ticks) | `EXAM_DURATION` | `--duration N` | 100 |
| I/O buffer capacity | `BUFFER_CAPACITY` | `--buffer N` | 256 |
| Scheduling algorithm | `SCHEDULING_ALGO` | `--algo PRIORITY\|RR` | PRIORITY |
| Page replacement | `PAGE_REPLACE` | `--page LRU\|FIFO` | LRU |
| Demo mode | — | `--demo` | off |
| Headless mode | — | `--headless` | off |

---

## 📁 Project Structure
```
exam_os/
├── Makefile
├── config.conf
├── include/
│   ├── shared.h        ← SystemState, PCB, all shared types
│   ├── config.h
│   ├── logger.h
│   ├── scheduler.h
│   ├── memory.h
│   ├── io_buffer.h
│   ├── interrupt.h
│   └── dashboard.h
├── src/
│   ├── main.c          ← entry point, thread spawning, simulation loop
│   ├── config.c        ← config file + CLI arg parser
│   ├── logger.c        ← async log queue + report generator
│   ├── scheduler.c     ← CPU scheduling (Priority + Round Robin)
│   ├── memory.c        ← paging (LRU + FIFO page replacement)
│   ├── io_buffer.c     ← circular buffer + submission flusher
│   ├── interrupt.c     ← IVT + interrupt dispatcher
│   └── dashboard.c     ← ncurses live dashboard
└── output/
    ├── system_log.txt  ← generated at runtime
    ├── submissions.txt ← generated at runtime
    └── summary.txt     ← generated at runtime
```

---

## 📈 Sample Results

Run `./exam_os --algo PRIORITY --demo` vs `./exam_os --algo RR --demo` and compare:

| Metric | Priority Scheduling | Round Robin |
|---|---|---|
| Avg Context Switches | Lower | Higher |
| Timeouts Fired | Fewer (urgent first) | More |
| Page Hit Rate | ~70–80% (LRU) | ~60–70% (FIFO) |
| Dropped Submissions | 0 (normal load) | 0–3 (storm) |

> These results demonstrate why Priority Scheduling outperforms Round Robin for deadline-driven workloads like online exams.

---

## 🧵 Architecture

All subsystems run as independent POSIX threads communicating through a shared `SystemState` struct protected by a mutex. No subsystem blocks another — the logger uses an async queue, the I/O buffer uses semaphores, and interrupts are dispatched asynchronously.
```
main.c
  ├── tick_thread        — central simulation clock
  ├── scheduler_thread   — CPU scheduling decisions
  ├── memory_thread      — page access simulation
  ├── io_buffer_thread   — submission flusher
  ├── interrupt_thread   — IVT dispatcher + timeout monitor
  ├── logger_thread      — async disk writer
  └── dashboard_thread   — ncurses renderer (500ms refresh)
        |
        └── all read from → SystemState (mutex-protected)
```

---

## 🛠️ Tech Stack

- **Language:** C (core) + C++ STL compatible
- **Concurrency:** POSIX pthreads
- **IPC:** Shared memory + POSIX semaphores
- **Signals:** POSIX signals (SIGALRM via software timer)
- **UI:** ncurses terminal dashboard
- **Build:** GNU Make

---

## 📄 Output Files

After simulation ends, three files are generated in `output/`:

- **`system_log.txt`** — timestamped log of every event from all subsystems
- **`submissions.txt`** — every exam submission with PID, question, answer, and partial flag
- **`summary.txt`** — formatted final report with all performance metrics

---

## 👩‍💻 Author

**Priyanka Singh**

---

## 📜 License

MIT License — free to use, modify, and reference for academic purposes.
EOF