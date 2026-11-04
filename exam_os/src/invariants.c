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
    int overhead_ticks;
    int final_tick;
    int block_events;
    int wakeups;
    int process_levels_ok = 1;
    int evictions;
    int dirty_writebacks;
    int frames_used;
    int page_faults;
    long disk_movement;
    int disk_requests;
    int disk_served;
    int disk_dropped;

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
    overhead_ticks = g_state.overhead_ticks;
    final_tick = g_state.current_tick;
    block_events = g_state.block_events;
    wakeups = g_state.wakeups;
    evictions = g_state.evictions;
    dirty_writebacks = g_state.dirty_writebacks;
    frames_used = g_state.frames_used;
    page_faults = g_state.page_faults;
    disk_movement = g_state.disk_movement;
    disk_requests = g_state.disk_requests;
    disk_served = g_state.disk_served;
    disk_dropped = g_state.disk_dropped;
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

    snprintf(msg, sizeof(msg), "busy=%d idle=%d overhead=%d final=%d",
             busy_ticks, idle_ticks, overhead_ticks, final_tick);
    report_check("I7", busy_ticks + idle_ticks + overhead_ticks <= final_tick, msg);

    int cpu_sum = 0;
    for (int i = 0; i < process_count; i++)
        cpu_sum += processes[i].cpu_ticks;
    snprintf(msg, sizeof(msg), "cpu_sum=%d busy=%d", cpu_sum, busy_ticks);
    report_check("I8", cpu_sum == busy_ticks, msg);

    snprintf(msg, sizeof(msg), "wakeups=%d blocked=%d", wakeups, block_events);
    report_check("I9", wakeups <= block_events, msg);

    for (int i = 0; i < process_count; i++) {
        if (processes[i].level < 0 || processes[i].level >= g_config.mlfq_levels ||
            processes[i].priority < 1 || processes[i].priority > g_config.priority_levels) {
            process_levels_ok = 0;
            snprintf(msg, sizeof(msg), "PID %d level=%d priority=%d",
                     processes[i].pid, processes[i].level, processes[i].priority);
            break;
        }
    }
    if (process_levels_ok) msg[0] = '\0';
    report_check("I10", process_levels_ok, msg);

    snprintf(msg, sizeof(msg), "evictions=%d faults=%d dirty=%d frames=%d configured=%d",
             evictions, page_faults, dirty_writebacks, frames_used,
             g_config.memory_frames);
    report_check("I11", evictions <= page_faults &&
                 dirty_writebacks <= evictions &&
                 frames_used <= g_config.memory_frames, msg);

    snprintf(msg, sizeof(msg), "served=%d dropped=%d requests=%d movement=%ld",
             disk_served, disk_dropped, disk_requests, disk_movement);
    report_check("I12", disk_served + disk_dropped == disk_requests &&
                 disk_movement >= 0, msg);

    if (invariant_file) fclose(invariant_file);
    invariant_file = NULL;
    return failures;
}
