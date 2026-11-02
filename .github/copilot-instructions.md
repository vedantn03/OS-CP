# Exam OS contribution instructions

- Preserve the userspace POSIX-threaded OS simulation scope.
- Keep subsystem state synchronized with the existing mutexes and semaphores.
- Prefer small, testable C helpers over duplicated timing, parsing, or random-number logic.
- Validate configuration values before starting worker threads and report invalid input clearly.
- Keep generated binaries and runtime output out of source control.
- Run `make clean && make` and the test suite under `exam_os/tests` before submitting changes.
- NEVER call log_event() while holding g_state.lock (log_event takes g_state.lock itself and would deadlock). Copy what you need, unlock, then log.
- Process state changes go through process_set_state() / process_terminate() (src/process.c) once they exist. Never assign PCB.state directly.
