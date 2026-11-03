#include <stdio.h>
#include <string.h>
#include "invariants.h"
#include "memory.h"
#include "shared.h"

static FILE *invariant_file;
static int failures;

static void report_check(const char *id, int passed, const char *details) {
    char line[512];
    snprintf(line, sizeof(line), "%s %s%s%s",
             passed ? "PASS" : "FAIL", id,
             details[0] ? " " : "", details);
    puts(line);
    if (invariant_file) fprintf(invariant_file, "%s\n", line);
    if (!passed) failures++;
}

int invariants_run(void) {
    char msg[256];
    int total_submissions;
    int flushed_records;
    int completed_processes;
    int finished_ok;
    int timeouts_fired;
    int process_count;
    PCB processes[MAX_STUDENTS];
    int io_count;
    int busy_ticks;
    int idle_ticks;
    int final_tick;

    invariant_file = fopen("output/invariants.txt", "w");
    failures = 0;

    msg[0] = '\0';
    report_check("I1", memory_audit(msg, sizeof(msg)) == 0, msg);

    pthread_mutex_lock(&g_io_buffer.lock);
    io_count = g_io_buffer.count;
    pthread_mutex_unlock(&g_io_buffer.lock);

    pthread_mutex_lock(&g_state.lock);
    total_submissions = g_state.total_submissions;
    flushed_records = g_state.flushed_records;
    completed_processes = g_state.completed_processes;
    finished_ok = g_state.finished_ok;
    timeouts_fired = g_state.timeouts_fired;
    process_count = g_state.process_count;
    busy_ticks = g_state.busy_ticks;
    idle_ticks = g_state.idle_ticks;
    final_tick = g_state.current_tick;
    memcpy(processes, g_state.processes, sizeof(processes));
    pthread_mutex_unlock(&g_state.lock);

    snprintf(msg, sizeof(msg), "total=%d flushed=%d buffered=%d",
             total_submissions, flushed_records, io_count);
    report_check("I2", total_submissions == flushed_records + io_count, msg);

    int terminated_count = 0;
    for (int i = 0; i < process_count; i++)
        if (processes[i].state == TERMINATED) terminated_count++;
    snprintf(msg, sizeof(msg), "completed=%d terminated=%d",
             completed_processes, terminated_count);
    report_check("I3", completed_processes == terminated_count, msg);

    snprintf(msg, sizeof(msg), "finished=%d timeouts=%d completed=%d",
             finished_ok, timeouts_fired, completed_processes);
    report_check("I4", finished_ok + timeouts_fired == completed_processes, msg);

    int timing_ok = 1;
    for (int i = 0; i < process_count; i++) {
        PCB *p = &processes[i];
        if (p->state == TERMINATED &&
            (p->finish_tick < p->arrival_tick ||
             p->waiting_time < 0 || p->turnaround_time < 0)) {
            timing_ok = 0;
            snprintf(msg, sizeof(msg), "PID %d finish=%d arrival=%d waiting=%d turnaround=%d",
                     p->pid, p->finish_tick, p->arrival_tick,
                     p->waiting_time, p->turnaround_time);
            break;
        }
    }
    if (timing_ok) msg[0] = '\0';
    report_check("I5", timing_ok, msg);

    snprintf(msg, sizeof(msg), "buffer=%d capacity=%d", io_count, g_io_buffer.capacity);
    report_check("I6", io_count >= 0 && io_count <= g_config.buffer_capacity, msg);

    snprintf(msg, sizeof(msg), "busy=%d idle=%d final=%d",
             busy_ticks, idle_ticks, final_tick);
    report_check("I7", busy_ticks + idle_ticks <= final_tick, msg);

    int cpu_sum = 0;
    for (int i = 0; i < process_count; i++)
        cpu_sum += processes[i].cpu_ticks;
    snprintf(msg, sizeof(msg), "cpu_sum=%d busy=%d", cpu_sum, busy_ticks);
    report_check("I8", cpu_sum == busy_ticks, msg);

    if (invariant_file) fclose(invariant_file);
    invariant_file = NULL;
    return failures;
}
