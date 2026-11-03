# Exam OS contribution instructions

- Preserve the userspace POSIX-threaded OS simulation scope.
- Keep subsystem state synchronized with the existing mutexes and semaphores.
- Prefer small, testable C helpers over duplicated timing, parsing, or random-number logic.
- Validate configuration values before starting worker threads and report invalid input clearly.
- Keep generated binaries and runtime output out of source control.
- Run `make clean && make` and the test suite under `exam_os/tests` before submitting changes.
- NEVER call log_event() while holding g_state.lock (log_event takes g_state.lock itself and would deadlock). Copy what you need, unlock, then log.
- Process state changes go through process_set_state() / process_terminate() (src/process.c) once they exist. Never assign PCB.state directly.
## Scope discipline
- Do exactly what the prompt lists. If you find another bug, report it in the final report; do not fix it unless the prompt says so.
- Touch only the files the prompt allows. If a requirement seems to need another file, stop and report instead.
- Do not run sanitizers unless the prompt lists them.
- Finish with a short report: files changed, commands run with results, deviations from the spec.
- After the task, append one entry to docs/PROJECT_STATE.md (Changelog section).
- tests/gate.sh is hand-written and read-only for you. Never edit it. Never report a prompt as done unless "bash tests/gate.sh <stage>" prints PASSED; paste its full output in your report.
- Use the ACTUAL names in the code (e.g. src/clock.c, rng_uniform). If a prompt names something different, adapt and report it; do not create duplicate modules.
- New tests take the next free T number. Never reuse or renumber an existing test.
## Phase 1.5-2.2 scope
- Keep LEGACY workload behavior as the default; INTERACTIVE may add bounded
  I/O waits through the existing process state machine.
- Keep tick-based behavior driven by the configured tick duration; do not use
  wall-clock shortcuts for process accounting.
- Preserve `tests/run_tests.sh`, `.gitignore`, and `exam_os/output/.gitkeep`.
- Memory table and frame access must use `mem_lock` before `g_state.lock`.