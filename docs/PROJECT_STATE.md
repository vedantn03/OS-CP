# Project State

## Current status

Exam OS is a userspace POSIX-threaded simulation with configurable scheduling,
memory paging, I/O buffering, interrupts, invariant checks, and legacy or
interactive workloads.

## Changelog

- 2026-11-02: Completed memory race protection, configurable tick timing,
  interactive workload WAITING transitions, test restoration, and sanitizer
  validation for phases 1.5–2.2.
- 2026-11-03: Added deterministic per-thread RNG streams and shared simulation
  clock sleep/notification infrastructure.
- 2026-11-03: Made scheduler CPU accounting tick-driven with utilization,
  invariants, and optional dispatch tracing.
- 2026-11-03: Added configurable interactive CPU demand and completion
  submissions while preserving the legacy workload model.
- 2026-11-03: Added interactive CPU burst/WAITING cycles, wakeups, and burst
  submissions with timeout-safe sleeper handling.
