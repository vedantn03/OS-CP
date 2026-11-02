#include <stdio.h>
#include "process.h"
#include "memory.h"
#include "logger.h"
#include "clock.h"

int process_slot(int pid) {
    return (pid >= 1 && pid <= MAX_STUDENTS) ? pid - 1 : -1;
}

int process_set_state(PCB *p, ProcessState to) {
    int legal = (p->state == NEW && to == READY) ||
                (p->state == READY && (to == RUNNING || to == TERMINATED)) ||
                (p->state == RUNNING &&
                 (to == READY || to == WAITING || to == TERMINATED)) ||
                (p->state == WAITING && (to == READY || to == TERMINATED));
    if (!legal) return -1;
    p->state = to;
    return 0;
}

int process_terminate(int pid, int timed_out) {
    int slot = process_slot(pid);
    if (slot < 0) return 0;

    pthread_mutex_lock(&g_state.lock);
    PCB *p = &g_state.processes[slot];
    if (p->state == TERMINATED ||
        process_set_state(p, TERMINATED) != 0) {
        pthread_mutex_unlock(&g_state.lock);
        return 0;
    }

    p->finish_tick = g_state.current_tick;
    p->turnaround_time = p->finish_tick - p->arrival_tick;
    p->timed_out = timed_out ? 1 : 0;
    g_state.completed_processes++;
    if (!timed_out) g_state.finished_ok++;
    pthread_mutex_unlock(&g_state.lock);

    memory_free_process(pid);

    char msg[128];
    snprintf(msg, sizeof(msg), "PID %d terminated (%s)", pid,
             timed_out ? "timeout" : "completed");
    log_event("INFO", "SCHEDULER", msg);
    return 1;
}
