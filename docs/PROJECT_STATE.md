# Project state (phases 1.5–2.2)

This repository includes the phase 1.5–2.2 scope:

- Memory subsystem synchronization fixes for ThreadSanitizer cleanliness.
- Scope-discipline contributor guidance in `.github/copilot-instructions.md`.
- Configurable simulation tick (`TIME_TICK_MS` / `--tick-ms`) with tick-driven scheduling.
- CPU busy/idle metrics with invariant checks `I7` and `I8`.
- Workload modes:
  - `LEGACY` (I/O thread random submissions)
  - `INTERACTIVE` (CPU bursts, `WAITING`, wakeups, scheduler-owned submissions)
- Process lifecycle additions for bursts, waits, wake deadlines, and scheduler-owned submissions.
- Invariant `I9` for wakeup/submission consistency.
- Test suite coverage `T1` through `T21` in `exam_os/tests/run_tests.sh`.

Phase 2.3 work is intentionally not started here.
